#include <numakit/sync.h>
#include <numakit/sched.h>
#include "internal.h"

#include <numa.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Per-node counter slot, padded to a full cache line.
 * Prevents false sharing: each node's counter lives on its own cache line.
 */
typedef struct {
    alignas(64) _Atomic(int64_t) value;
} nkit_pcounter_slot_t;

/**
 * @brief Internal representation of a partitioned counter.
 */
struct nkit_pcounter_s {
    nkit_pcounter_slot_t** slots;   // Array of per-node slot pointers
    int num_nodes;                  // Snapshot of g_nkit_ctx.num_nodes at creation
};

nkit_pcounter_t* nkit_pcounter_create(void) {
    int num_nodes = g_nkit_ctx.num_nodes;
    if (num_nodes <= 0) num_nodes = 1;

    nkit_pcounter_t* counter = malloc(sizeof(nkit_pcounter_t));
    if (!counter) return NULL;

    counter->num_nodes = num_nodes;
    counter->slots = calloc(num_nodes, sizeof(nkit_pcounter_slot_t*));

    if (!counter->slots) {
        free(counter);
        return NULL;
    }

    for (int i = 0; i < num_nodes; i++) {
        if (g_nkit_ctx.numa_supported) {
            // Pin the slot to the node it belongs to
            counter->slots[i] = numa_alloc_onnode(sizeof(nkit_pcounter_slot_t), i);
        } else {
            // UMA fallback
            counter->slots[i] = aligned_alloc(64, sizeof(nkit_pcounter_slot_t));
        }

        if (!counter->slots[i]) {
            // Cleanup on partial failure
            for (int j = 0; j < i; j++) {
                if (g_nkit_ctx.numa_supported) {
                    numa_free(counter->slots[j], sizeof(nkit_pcounter_slot_t));
                } else {
                    free(counter->slots[j]);
                }
            }
            free(counter->slots);
            free(counter);
            return NULL;
        }

        atomic_init(&counter->slots[i]->value, 0);
    }

    return counter;
}

void nkit_pcounter_destroy(nkit_pcounter_t* counter) {
    if (!counter) return;

    if (counter->slots) {
        for (int i = 0; i < counter->num_nodes; i++) {
            if (counter->slots[i]) {
                if (g_nkit_ctx.numa_supported) {
                    numa_free(counter->slots[i], sizeof(nkit_pcounter_slot_t));
                } else {
                    free(counter->slots[i]);
                }
            }
        }
        free(counter->slots);
    }

    free(counter);
}

void nkit_pcounter_add(nkit_pcounter_t* counter, int64_t value) {
    // Determine which node the calling thread is on
    int node = nkit_current_node();
    if (node < 0 || node >= counter->num_nodes) {
        node = 0; // Fallback to slot 0 on error
    }

    // Relaxed: eventual consistency is fine for counters
    atomic_fetch_add_explicit(&counter->slots[node]->value, value,
                              memory_order_relaxed);
}

void nkit_pcounter_inc(nkit_pcounter_t* counter) {
    nkit_pcounter_add(counter, 1);
}

void nkit_pcounter_dec(nkit_pcounter_t* counter) {
    nkit_pcounter_add(counter, -1);
}

int64_t nkit_pcounter_read(nkit_pcounter_t* counter) {
    int64_t sum = 0;
    for (int i = 0; i < counter->num_nodes; i++) {
        sum += atomic_load_explicit(&counter->slots[i]->value,
                                    memory_order_acquire);
    }
    return sum;
}

void nkit_pcounter_reset(nkit_pcounter_t* counter) {
    for (int i = 0; i < counter->num_nodes; i++) {
        atomic_store_explicit(&counter->slots[i]->value, 0,
                              memory_order_release);
    }
}
