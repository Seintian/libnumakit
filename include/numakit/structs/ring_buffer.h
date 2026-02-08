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
#include <sched.h>

// Cache line size (64 bytes on x86/ARM, 128 to be safe against prefetchers)
#define NKIT_CACHE_LINE 128

/**
 * @brief A single slot in the ring buffer.
 * Contains a sequence number for concurrency control.
 */
typedef struct {
    atomic_size_t sequence;
    void* data;
} nkit_cell_t;

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
    nkit_cell_t* cells;             // Array of cells
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
 * @brief Lock-Free Push (Multi-Producer Safe).
 * @return true if successful, false if full.
 */
static inline bool nkit_ring_push(nkit_ring_t* ring, void* item) {
    nkit_cell_t* cell;
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);

    for (;;) {
        // 1. Locate the cell for the current head
        cell = &ring->cells[head & ring->mask];

        // 2. Load the sequence number of this cell
        size_t seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);

        intptr_t diff = (intptr_t)seq - (intptr_t)head;

        // Case A: The cell is ready for writing (seq == head)
        if (diff == 0) {
            // Try to move the head forward (Claim the slot)
            if (atomic_compare_exchange_weak_explicit(&ring->head, &head, head + 1, 
                                                      memory_order_relaxed, memory_order_relaxed)) {
                // SUCCESS: We claimed the slot.
                cell->data = item;

                // Commit: Increment sequence to tell Consumer "Data is ready"
                // Seq becomes (head + 1)
                atomic_store_explicit(&cell->sequence, head + 1, memory_order_release);
                return true;
            }
            // If CAS failed, 'head' was updated by another thread. Loop again.
        }
        // Case B: The cell is not ready yet (seq < head) -> Buffer Full
        else if (diff < 0) {
            return false;
        }
        // Case C: The cell sequence is weird (seq > head) -> Stale head?
        else {
            head = atomic_load_explicit(&ring->head, memory_order_relaxed);
        }
    }
}

/**
 * @brief Lock-Free Pop (Multi-Consumer Safe).
 * Note: Even if we only have 1 consumer, this logic is safe and correct.
 */
static inline bool nkit_ring_pop(nkit_ring_t* ring, void** item) {
    nkit_cell_t* cell;
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);

    for (;;) {
        cell = &ring->cells[tail & ring->mask];
        size_t seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);

        // Check if data is ready. For a slot at 'tail', ready means seq == tail + 1
        intptr_t diff = (intptr_t)seq - (intptr_t)(tail + 1);

        if (diff == 0) {
            // Try to move tail forward
            if (atomic_compare_exchange_weak_explicit(&ring->tail, &tail, tail + 1, 
                                                      memory_order_relaxed, memory_order_relaxed)) {
                // SUCCESS: We claimed the item
                *item = cell->data;

                // Commit: Increment sequence to tell Producer "Slot is empty"
                // Seq becomes (tail + capacity)
                atomic_store_explicit(&cell->sequence, tail + ring->mask + 1, memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false; // Empty
        } else {
            tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif // NKIT_RING_BUFFER_H
