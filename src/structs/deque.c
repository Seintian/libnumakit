#define _GNU_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <numa.h>
#include <errno.h>

#include <numakit/structs/deque.h>

/**
 * @brief Internal circular buffer for the deque.
 */
typedef struct {
    uint32_t size;
    uint32_t mask;
    _Atomic(void*) buffer[];
} nkit_deque_array_t;

/**
 * @brief Internal structure of the Chase-Lev deque.
 */
struct nkit_deque_s {
    int node_id;
    _Atomic(long) bottom;
    _Atomic(long) top;
    _Atomic(nkit_deque_array_t*) array;
    
    // Track retired arrays for cleanup (Chase-Lev doesn't reclaim immediately)
    nkit_deque_array_t** retired_arrays;
    uint32_t num_retired;
    uint32_t retired_capacity;
};

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

/**
 * @brief Allocate a new circular buffer on a specific NUMA node.
 */
static nkit_deque_array_t* _nkit_deque_array_alloc(int node_id, uint32_t size) {
    size_t total_size = sizeof(nkit_deque_array_t) + (sizeof(_Atomic(void*)) * size);
    nkit_deque_array_t* a = (nkit_deque_array_t*)numa_alloc_onnode(total_size, node_id);
    if (a) {
        a->size = size;
        a->mask = size - 1;
        for (uint32_t i = 0; i < size; i++) {
            atomic_init(&a->buffer[i], NULL);
        }
    }
    return a;
}

/**
 * @brief Free a circular buffer using numa_free.
 */
static void _nkit_deque_array_free(nkit_deque_array_t* a) {
    if (a) {
        size_t total_size = sizeof(nkit_deque_array_t) + (sizeof(_Atomic(void*)) * a->size);
        numa_free(a, total_size);
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

nkit_deque_t* nkit_deque_create(int node_id, uint32_t initial_capacity) {
    if (numa_available() < 0) return NULL;

    // Ensure capacity is a power of 2
    if (initial_capacity == 0 || (initial_capacity & (initial_capacity - 1)) != 0) {
        errno = EINVAL;
        return NULL;
    }

    nkit_deque_t* dq = (nkit_deque_t*)numa_alloc_onnode(sizeof(nkit_deque_t), node_id);
    if (!dq) return NULL;

    nkit_deque_array_t* a = _nkit_deque_array_alloc(node_id, initial_capacity);
    if (!a) {
        numa_free(dq, sizeof(nkit_deque_t));
        return NULL;
    }

    dq->node_id = node_id;
    atomic_init(&dq->bottom, 0);
    atomic_init(&dq->top, 0);
    atomic_init(&dq->array, a);

    dq->num_retired = 0;
    dq->retired_capacity = 8;
    dq->retired_arrays = (nkit_deque_array_t**)numa_alloc_onnode(
        sizeof(nkit_deque_array_t*) * dq->retired_capacity, node_id);

    return dq;
}

void nkit_deque_destroy(nkit_deque_t* dq) {
    if (!dq) return;
    nkit_deque_array_t* a = atomic_load_explicit(&dq->array, memory_order_relaxed);
    _nkit_deque_array_free(a);

    for (uint32_t i = 0; i < dq->num_retired; i++) {
        _nkit_deque_array_free(dq->retired_arrays[i]);
    }
    numa_free(dq->retired_arrays, sizeof(nkit_deque_array_t*) * dq->retired_capacity);
    numa_free(dq, sizeof(nkit_deque_t));
}

/**
 * @brief Resize the deque by doubling its capacity.
 */
static void _nkit_deque_resize(nkit_deque_t* dq) {
    long b = atomic_load_explicit(&dq->bottom, memory_order_relaxed);
    long t = atomic_load_explicit(&dq->top, memory_order_relaxed);
    nkit_deque_array_t* old_a = atomic_load_explicit(&dq->array, memory_order_relaxed);
    
    uint32_t new_size = old_a->size * 2;
    nkit_deque_array_t* new_a = _nkit_deque_array_alloc(dq->node_id, new_size);
    if (!new_a) return; // Silent failure, push will return false

    // Copy elements from old to new
    for (long i = t; i < b; i++) {
        void* item = atomic_load_explicit(&old_a->buffer[i & old_a->mask], memory_order_relaxed);
        atomic_store_explicit(&new_a->buffer[i & new_a->mask], item, memory_order_relaxed);
    }

    // Update current array
    atomic_store_explicit(&dq->array, new_a, memory_order_release);

    // Store old array for later cleanup
    if (dq->num_retired == dq->retired_capacity) {
        uint32_t new_retired_cap = dq->retired_capacity * 2;
        nkit_deque_array_t** new_retired = (nkit_deque_array_t**)numa_alloc_onnode(
            sizeof(nkit_deque_array_t*) * new_retired_cap, dq->node_id);
        if (new_retired) {
            memcpy(new_retired, dq->retired_arrays, sizeof(nkit_deque_array_t*) * dq->num_retired);
            numa_free(dq->retired_arrays, sizeof(nkit_deque_array_t*) * dq->retired_capacity);
            dq->retired_arrays = new_retired;
            dq->retired_capacity = new_retired_cap;
        } else {
            // If we can't grow retired_arrays, we have to free the old array now.
            // This is slightly unsafe for concurrent thieves but better than a leak.
            // In practice, retired_capacity starts at 8, so we'd need 8 resizes.
            _nkit_deque_array_free(old_a);
            return;
        }
    }
    dq->retired_arrays[dq->num_retired++] = old_a;
}

bool nkit_deque_push(nkit_deque_t* dq, void* data) {
    long b = atomic_load_explicit(&dq->bottom, memory_order_relaxed);
    long t = atomic_load_explicit(&dq->top, memory_order_acquire);
    nkit_deque_array_t* a = atomic_load_explicit(&dq->array, memory_order_relaxed);

    if (b - t >= (long)a->size) {
        _nkit_deque_resize(dq);
        a = atomic_load_explicit(&dq->array, memory_order_relaxed);
        if (b - t >= (long)a->size) return false; // Resize failed
    }

    atomic_store_explicit(&a->buffer[b & a->mask], data, memory_order_relaxed);
    atomic_store_explicit(&dq->bottom, b + 1, memory_order_release);
    return true;
}

bool nkit_deque_pop(nkit_deque_t* dq, void** data) {
    long b = atomic_load_explicit(&dq->bottom, memory_order_relaxed) - 1;
    nkit_deque_array_t* a = atomic_load_explicit(&dq->array, memory_order_relaxed);
    atomic_store_explicit(&dq->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);

    long t = atomic_load_explicit(&dq->top, memory_order_relaxed);
    bool result = true;

    if (t <= b) {
        *data = atomic_load_explicit(&a->buffer[b & a->mask], memory_order_relaxed);
        if (t == b) {
            // Last element: try to claim it against potential thieves
            if (!atomic_compare_exchange_strong_explicit(&dq->top, &t, t + 1, 
                                                       memory_order_seq_cst, 
                                                       memory_order_relaxed)) {
                result = false;
            }
            atomic_store_explicit(&dq->bottom, b + 1, memory_order_relaxed);
        }
    } else {
        // Empty
        result = false;
        atomic_store_explicit(&dq->bottom, b + 1, memory_order_relaxed);
    }

    return result;
}

bool nkit_deque_steal(nkit_deque_t* dq, void** data) {
    long t = atomic_load_explicit(&dq->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    long b = atomic_load_explicit(&dq->bottom, memory_order_acquire);

    if (t < b) {
        nkit_deque_array_t* a = atomic_load_explicit(&dq->array, memory_order_consume);
        *data = atomic_load_explicit(&a->buffer[t & a->mask], memory_order_relaxed);

        if (!atomic_compare_exchange_strong_explicit(&dq->top, &t, t + 1, 
                                                   memory_order_seq_cst, 
                                                   memory_order_relaxed)) {
            return false;
        }
        return true;
    }

    return false;
}

uint32_t nkit_deque_size(nkit_deque_t* dq) {
    long b = atomic_load_explicit(&dq->bottom, memory_order_relaxed);
    long t = atomic_load_explicit(&dq->top, memory_order_relaxed);
    return (b > t) ? (uint32_t)(b - t) : 0;
}
