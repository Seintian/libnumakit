#include <numakit/structs/skip_list.h>
#include <numakit/memory.h>
#include <numakit/sync.h>
#include <numakit/topology.h>

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <numa.h>

// =============================================================================
// Internal Definitions
// =============================================================================

typedef struct nkit_skip_node_s {
    const void* key;
    size_t key_len;
    void* value;
    uint32_t level;
    struct nkit_skip_node_s* next[]; // Flexible array member for forward pointers
} nkit_skip_node_t;

typedef struct {
    nkit_skip_node_t* head;
    uint32_t max_level;
    uint32_t current_level;
    size_t count;
    int node_id;
    nkit_mcs_lock_t lock;
    unsigned int seed; // Per-shard seed for rand_r
} nkit_skip_shard_t;

struct nkit_skip_s {
    uint32_t max_level;
    uint32_t num_shards;
    nkit_skip_shard_t* shards;
};

// =============================================================================
// FNV-1a Hash (Redefined for locality, should be moved to internal.h later)
// =============================================================================

static inline uint64_t _nkit_fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// =============================================================================
// Helpers
// =============================================================================

static uint32_t _random_level(nkit_skip_shard_t* shard) {
    uint32_t level = 1;
    while (rand_r(&shard->seed) < (RAND_MAX / 2) && level < shard->max_level) {
        level++;
    }
    return level;
}

static nkit_skip_node_t* _create_node(int node_id, const void* key, size_t key_len, void* value, uint32_t level) {
    size_t sz = sizeof(nkit_skip_node_t) + (sizeof(nkit_skip_node_t*) * level);
    nkit_skip_node_t* node = (nkit_skip_node_t*)numa_alloc_onnode(sz, node_id);
    if (!node) return NULL;

    node->key = key;
    node->key_len = key_len;
    node->value = value;
    node->level = level;
    for (uint32_t i = 0; i < level; i++) {
        node->next[i] = NULL;
    }
    return node;
}

static void _free_node(nkit_skip_node_t* node) {
    if (!node) return;
    size_t sz = sizeof(nkit_skip_node_t) + (sizeof(nkit_skip_node_t*) * node->level);
    numa_free(node, sz);
}

static bool _keys_equal(const void* a, size_t a_len, const void* b, size_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

// =============================================================================
// Public API
// =============================================================================

nkit_skip_t* nkit_skip_create(uint32_t max_level) {
    if (numa_available() < 0) return NULL;

    int num_nodes = numa_max_node() + 1;
    nkit_skip_t* sl = (nkit_skip_t*)malloc(sizeof(nkit_skip_t));
    if (!sl) return NULL;

    sl->max_level = max_level;
    sl->num_shards = num_nodes;
    sl->shards = (nkit_skip_shard_t*)calloc(num_nodes, sizeof(nkit_skip_shard_t));
    if (!sl->shards) {
        free(sl);
        return NULL;
    }

    for (int i = 0; i < num_nodes; i++) {
        nkit_skip_shard_t* shard = &sl->shards[i];
        shard->node_id = i;
        shard->max_level = max_level;
        shard->current_level = 1;
        shard->count = 0;
        shard->seed = (unsigned int)i + 42; // Basic seed
        nkit_mcs_init(&shard->lock);

        // Header node for each shard
        shard->head = _create_node(i, NULL, 0, NULL, max_level);
        if (!shard->head) {
            // Rollback (simplified)
            for (int k = 0; k < i; k++) {
                _free_node(sl->shards[k].head);
            }
            free(sl->shards);
            free(sl);
            return NULL;
        }
    }

    return sl;
}

void nkit_skip_destroy(nkit_skip_t* sl) {
    if (!sl) return;

    for (uint32_t i = 0; i < sl->num_shards; i++) {
        nkit_skip_shard_t* shard = &sl->shards[i];
        nkit_skip_node_t* curr = shard->head;
        while (curr) {
            nkit_skip_node_t* next = curr->next[0];
            _free_node(curr);
            curr = next;
        }
    }
    free(sl->shards);
    free(sl);
}

int nkit_skip_put(nkit_skip_t* sl, const void* key, size_t key_len, void* value) {
    if (!sl || !key || key_len == 0) return -1;

    uint64_t hash = _nkit_fnv1a(key, key_len);
    uint32_t shard_idx = (uint32_t)(hash % sl->num_shards);
    nkit_skip_shard_t* shard = &sl->shards[shard_idx];

    nkit_mcs_node_t mcs_node;
    nkit_mcs_lock(&shard->lock, &mcs_node);

    nkit_skip_node_t* update[shard->max_level];
    nkit_skip_node_t* curr = shard->head;

    for (int i = (int)shard->max_level - 1; i >= 0; i--) {
        while (curr->next[i] != NULL && 
               (curr->next[i]->key_len < key_len || 
                (curr->next[i]->key_len == key_len && memcmp(curr->next[i]->key, key, key_len) < 0))) {
            curr = curr->next[i];
        }
        update[i] = curr;
    }

    curr = curr->next[0];

    if (curr != NULL && _keys_equal(curr->key, curr->key_len, key, key_len)) {
        curr->value = value;
        nkit_mcs_unlock(&shard->lock, &mcs_node);
        return 0;
    }

    uint32_t level = _random_level(shard);
    if (level > shard->current_level) {
        shard->current_level = level;
    }

    nkit_skip_node_t* new_node = _create_node(shard->node_id, key, key_len, value, level);
    if (!new_node) {
        nkit_mcs_unlock(&shard->lock, &mcs_node);
        return -1;
    }

    for (uint32_t i = 0; i < level; i++) {
        new_node->next[i] = update[i]->next[i];
        update[i]->next[i] = new_node;
    }

    shard->count++;
    nkit_mcs_unlock(&shard->lock, &mcs_node);
    return 0;
}

void* nkit_skip_get(nkit_skip_t* sl, const void* key, size_t key_len) {
    if (!sl || !key || key_len == 0) return NULL;

    uint64_t hash = _nkit_fnv1a(key, key_len);
    uint32_t shard_idx = (uint32_t)(hash % sl->num_shards);
    nkit_skip_shard_t* shard = &sl->shards[shard_idx];

    // Shared read access would be better, but we only have MCS locks right now.
    // Given the roadmap, RWS locks exist in include/numakit/sync.h
    // I will use MCS for consistency with hash_table.c for now.
    nkit_mcs_node_t mcs_node;
    nkit_mcs_lock(&shard->lock, &mcs_node);

    nkit_skip_node_t* curr = shard->head;
    for (int i = (int)shard->max_level - 1; i >= 0; i--) {
        while (curr->next[i] != NULL && 
               (curr->next[i]->key_len < key_len || 
                (curr->next[i]->key_len == key_len && memcmp(curr->next[i]->key, key, key_len) < 0))) {
            curr = curr->next[i];
        }
    }

    curr = curr->next[0];

    void* result = NULL;
    if (curr != NULL && _keys_equal(curr->key, curr->key_len, key, key_len)) {
        result = curr->value;
    }

    nkit_mcs_unlock(&shard->lock, &mcs_node);
    return result;
}

int nkit_skip_remove(nkit_skip_t* sl, const void* key, size_t key_len) {
    if (!sl || !key || key_len == 0) return -1;

    uint64_t hash = _nkit_fnv1a(key, key_len);
    uint32_t shard_idx = (uint32_t)(hash % sl->num_shards);
    nkit_skip_shard_t* shard = &sl->shards[shard_idx];

    nkit_mcs_node_t mcs_node;
    nkit_mcs_lock(&shard->lock, &mcs_node);

    nkit_skip_node_t* update[shard->max_level];
    nkit_skip_node_t* curr = shard->head;

    for (int i = (int)shard->max_level - 1; i >= 0; i--) {
        while (curr->next[i] != NULL && 
               (curr->next[i]->key_len < key_len || 
                (curr->next[i]->key_len == key_len && memcmp(curr->next[i]->key, key, key_len) < 0))) {
            curr = curr->next[i];
        }
        update[i] = curr;
    }

    curr = curr->next[0];

    if (curr != NULL && _keys_equal(curr->key, curr->key_len, key, key_len)) {
        for (uint32_t i = 0; i < curr->level; i++) {
            if (update[i]->next[i] != curr) break;
            update[i]->next[i] = curr->next[i];
        }
        _free_node(curr);
        shard->count--;
        nkit_mcs_unlock(&shard->lock, &mcs_node);
        return 0;
    }

    nkit_mcs_unlock(&shard->lock, &mcs_node);
    return -1;
}

size_t nkit_skip_count(const nkit_skip_t* sl) {
    if (!sl) return 0;
    size_t total = 0;
    for (uint32_t i = 0; i < sl->num_shards; i++) {
        // Read without lock (approximate but avoids stall)
        total += sl->shards[i].count;
    }
    return total;
}
