#include <numakit/structs/ring_buffer.h>
#include <numakit/memory.h>
#include <stddef.h>
#include <stdatomic.h>

nkit_ring_t* nkit_ring_create(int node_id, size_t capacity) {
    // 1. Validate power of 2
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        return NULL;
    }

    // 2. Calculate total size needed
    // We need space for the Control Struct + The Array of Pointers
    size_t struct_sz = sizeof(nkit_ring_t);
    size_t data_sz   = sizeof(void*) * capacity;
    size_t total_sz  = struct_sz + data_sz;

    // 3. Create a dedicated Arena on the target Node
    // This ensures BOTH the atomic counters and the data array 
    // reside on the fast local memory of the target node.
    nkit_arena_t* arena = nkit_arena_create(node_id, total_sz);
    if (!arena) {
        return NULL;
    }

    // 4. Allocate from the arena
    // Since it's a fresh arena, this will be at the start of the Hugepage
    void* block = nkit_arena_alloc(arena, total_sz);
    if (!block) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // 5. Initialize the struct
    nkit_ring_t* ring = (nkit_ring_t*)block;

    // The data array starts immediately after the struct
    ring->data = (void**) ((char*)block + struct_sz);

    ring->capacity = capacity;
    ring->mask = capacity - 1;
    ring->_arena = (struct nkit_arena_s*) arena; // Store opaque handle

    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);

    return ring;
}

void nkit_ring_free(nkit_ring_t* ring) {
    if (ring && ring->_arena) {
        // Destroying the arena frees all memory (struct + data)
        // We cast to nkit_arena_t* because header uses forward declaration
        nkit_arena_destroy((nkit_arena_t*) ring->_arena);
    }
}
