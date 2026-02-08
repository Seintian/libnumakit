#ifndef NKIT_RING_BUFFER_H
#define NKIT_RING_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdalign.h>

// Cache line size (64 bytes on x86/ARM, 128 to be safe against prefetchers)
#define NKIT_CACHE_LINE 128

/**
 * @brief Lock-Free Ring Buffer (SPSC).
 * optimized to prevent False Sharing between Producer and Consumer.
 */
typedef struct {
    // -------------------------------------------------------------------------
    // Producer Cache Line (Head)
    // -------------------------------------------------------------------------
    alignas(NKIT_CACHE_LINE) atomic_size_t head; 

    // Padding to ensure 'tail' is on a different cache line
    char pad1[NKIT_CACHE_LINE - sizeof(atomic_size_t)];

    // -------------------------------------------------------------------------
    // Consumer Cache Line (Tail)
    // -------------------------------------------------------------------------
    alignas(NKIT_CACHE_LINE) atomic_size_t tail;

    // Padding to ensure read-only fields are separated
    char pad2[NKIT_CACHE_LINE - sizeof(atomic_size_t)];

    // -------------------------------------------------------------------------
    // Read-Only Fields (Shared)
    // -------------------------------------------------------------------------
    size_t capacity;                // Number of items the ring can hold
    size_t mask;                    // capacity - 1 (for fast bitwise modulo)
    void** data;                    // Array of pointers to items
    struct nkit_arena_s* _arena;    // Arena used to allocate the ring buffer

} nkit_ring_t;

/**
 * @brief Create a Ring Buffer pinned to a specific NUMA node.
 * Uses Hugepages via nkit_arena.
 * @param node_id The NUMA node where memory should physically reside.
 * @param capacity Number of items (must be power of 2).
 * @return Pointer to new ring, or NULL on failure.
 */
nkit_ring_t* nkit_ring_create(int node_id, size_t capacity);

/**
 * @brief Destroy the ring and release Hugepages.
 */
void nkit_ring_free(nkit_ring_t* ring);

/**
 * @brief Push an item (Producer only).
 * @return true if successful, false if full.
 */
static inline bool nkit_ring_push(nkit_ring_t* ring, void* item) {
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    // Check if full: (head + 1) % cap == tail
    // We use bitwise mask because capacity is power of 2
    if (((head + 1) & ring->mask) == (tail & ring->mask)) {
        return false; // Full
    }

    // Write data
    ring->data[head & ring->mask] = item;

    // Commit the write (Publish head)
    atomic_store_explicit(&ring->head, head + 1, memory_order_release);
    return true;
}

/**
 * @brief Pop an item (Consumer only).
 * @return true if successful, false if empty.
 */
static inline bool nkit_ring_pop(nkit_ring_t* ring, void** item) {
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);

    // Check if empty
    if ((head & ring->mask) == (tail & ring->mask)) {
        return false; // Empty
    }

    // Read data
    *item = ring->data[tail & ring->mask];

    // Commit the read (Update tail)
    atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // NKIT_RING_BUFFER_H
