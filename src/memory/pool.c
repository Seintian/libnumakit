#define _GNU_SOURCE

#include <numakit/memory.h>
#include <numakit/topology.h>
#include <numakit/sched.h>
#include <numakit/numakit.h>
#include "../internal.h"
#include <stdlib.h>
#include <string.h>

#define NUM_SIZE_CLASSES 10
#define MAX_NODES 64 // Reasonable upper bound for topology

// Predefined size classes from 32B up to 16KB
static const size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
};

// Initial capacity per size class (number of objects)
// For simplicity, we use a fixed capacity here. In a production system,
// larger object sizes might have fewer slots to save memory.
#define INITIAL_CAPACITY_PER_SLAB 1024

// Default alignment for memory pool blocks (cache line)
#define MEMPOOL_ALIGN 64

/**
 * @brief Internal header for each allocation.
 *
 * Placed immediately before the user's data pointer.
 * It stores a back-pointer to the slab that originated the allocation,
 * enabling O(1) deallocation without complex page mapping.
 */
typedef struct {
    nkit_slab_t* owner_slab;
} nkit_mempool_header_t;

/**
 * @brief State for a single NUMA node.
 */
typedef struct {
    int logical_node_id;
    nkit_slab_t* slabs[NUM_SIZE_CLASSES];
} nkit_mempool_node_t;

/**
 * @brief The global memory pool structure.
 */
struct nkit_mempool_s {
    int num_nodes;
    nkit_mempool_node_t nodes[MAX_NODES];
};

nkit_mempool_t* nkit_mempool_create(void) {
    if (!g_nkit_ctx.initialized) {
        // Assume library is initialized, or we return NULL
        return NULL;
    }

    nkit_mempool_t* pool = malloc(sizeof(nkit_mempool_t));
    if (!pool) return NULL;

    memset(pool, 0, sizeof(nkit_mempool_t));
    pool->num_nodes = g_nkit_ctx.num_nodes;
    if (pool->num_nodes > MAX_NODES) {
        pool->num_nodes = MAX_NODES; // Bound check
    }

    for (int node = 0; node < pool->num_nodes; node++) {
        pool->nodes[node].logical_node_id = node;
        for (int sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            // Need enough capacity for the header + user size
            size_t total_obj_size = sizeof(nkit_mempool_header_t) + SIZE_CLASSES[sc];
            
            // Slabs align elements to 64 bytes by default, but let's ensure
            // the total size accommodates the user data correctly.
            pool->nodes[node].slabs[sc] = nkit_slab_create(node, total_obj_size, INITIAL_CAPACITY_PER_SLAB);
            if (!pool->nodes[node].slabs[sc]) {
                // Cleanup on partial failure
                nkit_mempool_destroy(pool);
                return NULL;
            }
        }
    }

    return pool;
}

void* nkit_mempool_alloc(nkit_mempool_t* pool, size_t size) {
    if (!pool || size == 0) return NULL;

    // Reject allocations larger than our biggest size class
    if (size > SIZE_CLASSES[NUM_SIZE_CLASSES - 1]) return NULL;

    int current_node = nkit_get_current_node();
    // Fallback to node 0 if nkit_get_current_node fails or system is UMA
    if (current_node < 0 || current_node >= pool->num_nodes) {
        current_node = 0;
    }

    // Find the appropriate size class
    int sc_idx = -1;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            sc_idx = i;
            break;
        }
    }

    if (sc_idx == -1) return NULL; // Should be handled by size check above

    // Attempt allocation from local node's slab
    nkit_slab_t* target_slab = pool->nodes[current_node].slabs[sc_idx];
    void* raw_block = nkit_slab_alloc(target_slab);

    // [FUTURE] If raw_block is NULL (slab exhausted), we could try borrowing
    // from a neighboring NUMA node or allocating a new larger slab.
    if (!raw_block) {
        // Fallback: Try other nodes (simple linear search for now)
        for(int n = 0; n < pool->num_nodes; n++) {
            if (n == current_node) continue;
             target_slab = pool->nodes[n].slabs[sc_idx];
             raw_block = nkit_slab_alloc(target_slab);
             if (raw_block) break;
        }
    }

    if (!raw_block) return NULL; // Completely exhausted across all nodes

    // Write header
    nkit_mempool_header_t* header = (nkit_mempool_header_t*)raw_block;
    header->owner_slab = target_slab;

    // Return pointer to user data (just after the header)
    return (void*)(header + 1);
}

void nkit_mempool_free(nkit_mempool_t* pool, void* ptr) {
    if (!pool || !ptr) return;

    // Pointer arithmetic to strictly get the header
    nkit_mempool_header_t* header = ((nkit_mempool_header_t*)ptr) - 1;

    // Give it back to the original slab
    if (header->owner_slab) {
         nkit_slab_free(header->owner_slab, (void*)header);
    }
}

void nkit_mempool_destroy(nkit_mempool_t* pool) {
    if (!pool) return;

    for (int node = 0; node < pool->num_nodes; node++) {
        for (int sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            if (pool->nodes[node].slabs[sc]) {
                nkit_slab_destroy(pool->nodes[node].slabs[sc]);
            }
        }
    }

    free(pool);
}
