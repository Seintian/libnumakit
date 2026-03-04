#include <numakit/structs/hash_table.h>
#include <numakit/memory.h>
#include <numakit/sync.h>

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

// =============================================================================
// Internal Definitions
// =============================================================================

/**
 * @brief A single bucket in the hash table.
 * hash == 0 is the "empty" sentinel. FNV-1a never produces 0
 * after our bit-mix, so this is safe.
 */
typedef struct {
    uint64_t hash;
    const void* key;
    size_t key_len;
    void* value;
} nkit_bucket_t;

struct nkit_hash_s {
    nkit_bucket_t* buckets;     // Arena-allocated bucket array
    size_t capacity;            // Always a power of 2
    size_t mask;                // capacity - 1 (fast modulo)
    size_t count;               // Live entry count
    nkit_arena_t* _arena;       // NUMA-pinned backing memory
    nkit_mcs_lock_t lock;       // Thread-safe access
};

// Maximum load factor: 75% (3/4)
#define NKIT_HASH_MAX_LOAD_NUM   3
#define NKIT_HASH_MAX_LOAD_DEN   4
#define NKIT_HASH_MIN_CAPACITY  16

// =============================================================================
// FNV-1a Hash
// =============================================================================

static inline uint64_t _nkit_fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;            // FNV prime
    }

    // Bit-mix: ensure hash is never 0 (our empty sentinel)
    hash |= (hash == 0);
    return hash;
}

// =============================================================================
// Helpers
// =============================================================================

static inline size_t _next_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

/**
 * @brief Distance from ideal slot (probe distance / DIB).
 */
static inline size_t _probe_distance(uint64_t hash, size_t slot, size_t mask) {
    return (slot - (size_t)(hash & mask)) & mask;
}

static inline int _keys_equal(const void* a, size_t a_len,
                              const void* b, size_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

// =============================================================================
// Public API
// =============================================================================

nkit_hash_t* nkit_hash_create(int node_id, size_t capacity) {
    // Round up to power of 2, enforce minimum
    if (capacity < NKIT_HASH_MIN_CAPACITY) capacity = NKIT_HASH_MIN_CAPACITY;
    capacity = _next_power_of_2(capacity);

    // Calculate total allocation size
    size_t struct_sz  = sizeof(nkit_hash_t);
    size_t buckets_sz = sizeof(nkit_bucket_t) * capacity;
    size_t total_sz   = struct_sz + buckets_sz;

    // Create a NUMA-pinned arena for the entire table
    nkit_arena_t* arena = nkit_arena_create(node_id, total_sz);
    if (!arena) return NULL;

    void* block = nkit_arena_alloc(arena, total_sz);
    if (!block) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // Zero the entire block (sets all bucket hashes to 0 = empty)
    memset(block, 0, total_sz);

    // Layout: [ nkit_hash_t | nkit_bucket_t[capacity] ]
    nkit_hash_t* ht   = (nkit_hash_t*)block;
    ht->buckets       = (nkit_bucket_t*)((char*)block + struct_sz);
    ht->capacity      = capacity;
    ht->mask          = capacity - 1;
    ht->count         = 0;
    ht->_arena        = arena;

    nkit_mcs_init(&ht->lock);

    return ht;
}

void nkit_hash_destroy(nkit_hash_t* ht) {
    if (!ht) return;
    // Destroying the arena releases all memory (struct + buckets)
    nkit_arena_destroy(ht->_arena);
}

int nkit_hash_put(nkit_hash_t* ht, const void* key, size_t key_len,
                  void* value) {
    if (!ht || !key || key_len == 0) return -1;

    nkit_mcs_node_t node;
    nkit_mcs_lock(&ht->lock, &node);

    // Check load factor BEFORE insert (allow overwrite even if full)
    uint64_t hash = _nkit_fnv1a(key, key_len);
    size_t slot = (size_t)(hash & ht->mask);

    // Robin Hood insertion
    const void* cur_key   = key;
    size_t cur_key_len    = key_len;
    void* cur_value       = value;
    uint64_t cur_hash     = hash;
    size_t dist           = 0;

    for (;;) {
        nkit_bucket_t* b = &ht->buckets[slot];

        // Empty slot: place the entry here
        if (b->hash == 0) {
            // Check load factor before adding a NEW entry
            if (ht->count * NKIT_HASH_MAX_LOAD_DEN >=
                ht->capacity * NKIT_HASH_MAX_LOAD_NUM) {
                nkit_mcs_unlock(&ht->lock, &node);
                return -1; // Table full
            }

            b->hash    = cur_hash;
            b->key     = cur_key;
            b->key_len = cur_key_len;
            b->value   = cur_value;
            ht->count++;

            nkit_mcs_unlock(&ht->lock, &node);
            return 0;
        }

        // Key exists: overwrite value
        if (b->hash == cur_hash &&
            _keys_equal(b->key, b->key_len, cur_key, cur_key_len)) {
            b->value = cur_value;

            nkit_mcs_unlock(&ht->lock, &node);
            return 0;
        }

        // Robin Hood: steal from the rich
        size_t existing_dist = _probe_distance(b->hash, slot, ht->mask);
        if (dist > existing_dist) {
            // Swap current entry with the resident
            uint64_t tmp_hash       = b->hash;
            const void* tmp_key     = b->key;
            size_t tmp_key_len      = b->key_len;
            void* tmp_value         = b->value;

            b->hash    = cur_hash;
            b->key     = cur_key;
            b->key_len = cur_key_len;
            b->value   = cur_value;

            cur_hash    = tmp_hash;
            cur_key     = tmp_key;
            cur_key_len = tmp_key_len;
            cur_value   = tmp_value;

            dist = existing_dist;
        }

        slot = (slot + 1) & ht->mask;
        dist++;
    }
}

void* nkit_hash_get(nkit_hash_t* ht, const void* key, size_t key_len) {
    if (!ht || !key || key_len == 0) return NULL;

    nkit_mcs_node_t node;
    nkit_mcs_lock(&ht->lock, &node);

    uint64_t hash = _nkit_fnv1a(key, key_len);
    size_t slot = (size_t)(hash & ht->mask);
    size_t dist = 0;

    for (;;) {
        nkit_bucket_t* b = &ht->buckets[slot];

        // Empty slot: key not found
        if (b->hash == 0) {
            nkit_mcs_unlock(&ht->lock, &node);
            return NULL;
        }

        // Robin Hood invariant: if probe distance of resident < ours,
        // the key cannot be further ahead
        if (_probe_distance(b->hash, slot, ht->mask) < dist) {
            nkit_mcs_unlock(&ht->lock, &node);
            return NULL;
        }

        // Check for match
        if (b->hash == hash &&
            _keys_equal(b->key, b->key_len, key, key_len)) {
            void* result = b->value;
            nkit_mcs_unlock(&ht->lock, &node);
            return result;
        }

        slot = (slot + 1) & ht->mask;
        dist++;
    }
}

int nkit_hash_remove(nkit_hash_t* ht, const void* key, size_t key_len) {
    if (!ht || !key || key_len == 0) return -1;

    nkit_mcs_node_t node;
    nkit_mcs_lock(&ht->lock, &node);

    uint64_t hash = _nkit_fnv1a(key, key_len);
    size_t slot = (size_t)(hash & ht->mask);
    size_t dist = 0;

    // 1. Find the entry
    for (;;) {
        nkit_bucket_t* b = &ht->buckets[slot];

        if (b->hash == 0) {
            nkit_mcs_unlock(&ht->lock, &node);
            return -1; // Not found
        }

        if (_probe_distance(b->hash, slot, ht->mask) < dist) {
            nkit_mcs_unlock(&ht->lock, &node);
            return -1; // Not found (Robin Hood invariant)
        }

        if (b->hash == hash &&
            _keys_equal(b->key, b->key_len, key, key_len)) {
            break; // Found at 'slot'
        }

        slot = (slot + 1) & ht->mask;
        dist++;
    }

    // 2. Backward-shift deletion (no tombstones)
    // Shift subsequent entries backward to fill the gap.
    size_t empty = slot;
    for (;;) {
        size_t next = (empty + 1) & ht->mask;
        nkit_bucket_t* nb = &ht->buckets[next];

        // Stop if the next slot is empty or at its ideal position
        if (nb->hash == 0 || _probe_distance(nb->hash, next, ht->mask) == 0) {
            break;
        }

        // Shift backward
        ht->buckets[empty] = *nb;
        empty = next;
    }

    // Clear the final empty slot
    memset(&ht->buckets[empty], 0, sizeof(nkit_bucket_t));
    ht->count--;

    nkit_mcs_unlock(&ht->lock, &node);
    return 0;
}

size_t nkit_hash_count(const nkit_hash_t* ht) {
    if (!ht) return 0;
    return ht->count;
}
