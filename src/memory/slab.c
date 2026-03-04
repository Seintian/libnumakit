#define _GNU_SOURCE

#include <numakit/memory.h>
#include <numakit/structs/ring_buffer.h>
#include <stdlib.h>
#include <string.h>

// Cache-line alignment for object slots
#define SLAB_ALIGN 64

/**
 * @brief Internal slab structure.
 *
 * The slab carves a contiguous memory arena into fixed-size chunks and
 * manages a lock-free ring buffer as a free-list.  Both the arena and
 * the ring are pinned to the same NUMA node, guaranteeing that
 * allocations never touch remote memory.
 */
struct nkit_slab_s {
    nkit_arena_t *arena;       // Backing memory arena (hugepage-backed)
    nkit_ring_t  *freelist;    // Lock-free MPMC ring used as the free-list
    void         *base;        // Start of the object memory region
    size_t        obj_size;    // Aligned object size (>= user-requested size)
    size_t        capacity;    // Total number of object slots
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

nkit_slab_t *nkit_slab_create(int node_id, size_t obj_size, size_t capacity) {
    // 1. Validate: capacity must be a power-of-2 >= 2
    if (capacity < 2 || (capacity & (capacity - 1)) != 0) return NULL;
    if (obj_size == 0) return NULL;

    // 2. Align object size up to cache-line boundary
    size_t aligned = (obj_size + SLAB_ALIGN - 1) & ~(size_t)(SLAB_ALIGN - 1);

    // 3. Create backing arena large enough for the slab struct + all objects
    size_t data_bytes  = aligned * capacity;
    size_t total_bytes = sizeof(struct nkit_slab_s) + data_bytes;

    nkit_arena_t *arena = nkit_arena_create(node_id, total_bytes);
    if (!arena) return NULL;

    // 4. Allocate the slab control structure from the arena
    struct nkit_slab_s *slab = nkit_arena_alloc(arena, sizeof(struct nkit_slab_s));
    if (!slab) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // 5. Allocate the contiguous object memory block from the arena
    void *base = nkit_arena_alloc(arena, data_bytes);
    if (!base) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // 6. Create the lock-free ring buffer (free-list) on the same node
    nkit_ring_t *ring = nkit_ring_create(node_id, capacity);
    if (!ring) {
        nkit_arena_destroy(arena);
        return NULL;
    }

    // 7. Populate: push every slot pointer into the free-list
    for (size_t i = 0; i < capacity; i++) {
        void *slot = (char *)base + (i * aligned);
        nkit_ring_push(ring, slot);
    }

    // 8. Fill out the slab descriptor
    slab->arena    = arena;
    slab->freelist = ring;
    slab->base     = base;
    slab->obj_size = aligned;
    slab->capacity = capacity;

    return slab;
}

void *nkit_slab_alloc(nkit_slab_t *slab) {
    if (!slab) return NULL;

    void *ptr = NULL;
    if (nkit_ring_pop(slab->freelist, &ptr)) {
        return ptr;
    }
    return NULL; // Slab exhausted
}

void nkit_slab_free(nkit_slab_t *slab, void *ptr) {
    if (!slab || !ptr) return;
    nkit_ring_push(slab->freelist, ptr);
}

void nkit_slab_destroy(nkit_slab_t *slab) {
    if (!slab) return;

    // Free the ring buffer first (it has its own arena)
    nkit_ring_t *ring = slab->freelist;
    nkit_arena_t *arena = slab->arena;

    if (ring) nkit_ring_free(ring);
    if (arena) nkit_arena_destroy(arena);
}

size_t nkit_slab_available(nkit_slab_t *slab) {
    if (!slab) return 0;

    // Available slots = capacity - (head - tail)
    // head and tail are atomic; this is an approximate snapshot.
    size_t head = atomic_load_explicit(&slab->freelist->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&slab->freelist->tail, memory_order_relaxed);

    return head >= tail ? (head - tail) : 0;
}

size_t nkit_slab_capacity(nkit_slab_t *slab) {
    if (!slab) return 0;
    return slab->capacity;
}
