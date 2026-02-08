#include "../internal.h"

#include <numakit/sched.h>
#include <numakit/sync.h>
#include <numakit/structs/ring_buffer.h>

#include <stddef.h>

/**
 * @brief Send a message (pointer) to a specific NUMA node.
 * Lock-Free MPSC.
 * @return 0 on success
 * @return -1 on invalid node
 * @return -2 on buffer full (congestion)
 */
int nkit_send(int target_node, void* data) {
    if (target_node < 0 || target_node >= g_nkit_ctx.num_nodes) return -1;

    // Direct access: No MCS lock needed (MPSC safe)
    nkit_ring_t* ring = g_nkit_ctx.mailboxes[target_node]->ring;

    // Returns false if ring is full
    return nkit_ring_push(ring, data) ? 0 : -2;
}

size_t nkit_process_local(void (*handler)(void*), size_t limit) {
    int current_node = nkit_current_node();

    // Safety check: invalid node
    if (current_node < 0 || current_node >= g_nkit_ctx.num_nodes) {
        return 0;
    }

    nkit_ring_t* ring = g_nkit_ctx.mailboxes[current_node]->ring;
    size_t processed = 0;
    void* data = NULL;

    // Loop until empty OR limit reached
    while ((limit == 0 || processed < limit) && nkit_ring_pop(ring, &data)) {
        if (handler) {
            handler(data);
        }
        processed++;
    }

    return processed;
}
