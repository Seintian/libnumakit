#include <numakit/structs/ring_buffer.h>
#include <numakit/memory.h>
#include <stddef.h>
#include <stdatomic.h>

nkit_ring_t* nkit_ring_create(int node_id, size_t capacity) {
    // 1. Validate power of 2
    if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
        return NULL;
    }

    // 2. Calculate sizes
    // Note: nkit_cell_t is usually 16 bytes (8 seq + 8 ptr)
    size_t struct_sz = sizeof(nkit_ring_t);
    size_t cells_sz  = sizeof(nkit_cell_t) * capacity;
    size_t total_sz  = struct_sz + cells_sz;

    // 3. Create Arena & Allocate
    nkit_arena_t* arena = nkit_arena_create(node_id, total_sz);
    if (!arena) return NULL;

    void* block = nkit_arena_alloc(arena, total_sz);
    if (!block) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // 4. Init Struct
    nkit_ring_t* ring = (nkit_ring_t*)block;
    ring->cells = (nkit_cell_t*)((char*)block + struct_sz);
    ring->capacity = capacity;
    ring->mask = capacity - 1;
    ring->_arena = (struct nkit_arena_s*)arena;

    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);

    // 5. CRITICAL: Initialize Sequence Numbers
    // Slot 0 gets seq 0, Slot 1 gets seq 1...
    // This allows the first 'push' (head=0) to succeed on slot 0.
    for (size_t i = 0; i < capacity; i++) {
        atomic_init(&ring->cells[i].sequence, i);
    }

    return ring;
}

void nkit_ring_free(nkit_ring_t* ring) {
    if (ring && ring->_arena) {
        // Destroying the arena frees all memory (struct + data)
        // We cast to nkit_arena_t* because header uses forward declaration
        nkit_arena_destroy((nkit_arena_t*) ring->_arena);
    }
}
