#include "../internal.h"

#include <numakit/sched.h>
#include <numakit/sync.h>
#include <numakit/structs/ring_buffer.h>

#include <stddef.h>

/**
 * @brief Send a data pointer to a specific NUMA node.
 * Thread-Safe: Uses MCS Lock to protect the SPSC ring, making it MPSC.
 */
int nkit_send(int target_node, void* data) {
    if (target_node < 0 || target_node >= g_nkit_ctx.num_nodes) return -1;
    if (!g_nkit_ctx.mailboxes) return -1;

    // 1. Identify the target mailbox
    nkit_ring_t* ring = g_nkit_ctx.mailboxes[target_node].ring;
    nkit_mcs_lock_t* lock = &g_nkit_ctx.mailboxes[target_node].lock;

    // 2. Secure access using MCS Lock
    // This is crucial: standard spinlocks would collapse under contention here.
    // MCS ensures that waiting senders spin on their OWN stack, not the global lock.
    nkit_mcs_node_t my_node;
    nkit_mcs_lock(lock, &my_node);

    // 3. Critical Section: Push to Ring
    // Since we hold the lock, we are the only producer right now.
    bool success = nkit_ring_push(ring, data);

    // 4. Release Lock
    nkit_mcs_unlock(lock, &my_node);

    return success ? 0 : -1; // -1 if ring was full
}

/**
 * @brief Process pending messages for the CURRENT node.
 * Usually called by a worker thread pinned to this node.
 * @param handler Function to call for each message.
 * @return Number of messages processed.
 */
size_t nkit_process_local(void (*handler)(void*)) {
    // 1. Find out which node we are currently running on
    int current_node = nkit_current_node();

    if (current_node < 0 || current_node >= g_nkit_ctx.num_nodes) return 0;

    nkit_ring_t* ring = g_nkit_ctx.mailboxes[current_node].ring;
    nkit_mcs_lock_t* lock = &g_nkit_ctx.mailboxes[current_node].lock;

    size_t processed = 0;
    void* data = NULL;

    // 2. Drain the queue
    // We lock to prevent racing with Senders (Producers)
    // Note: If we had a true MPSC ring, we wouldn't need to lock the Consumer side.
    // But since we are reusing the SPSC ring, we must lock.
    nkit_mcs_node_t my_node;
    nkit_mcs_lock(lock, &my_node);

    // Pop loop
    while (nkit_ring_pop(ring, &data)) {
        // Optimization: Pop to a local buffer, unlock, then process.

        // For simplicity v1: Process inside lock (safe but serialized)
        // ideally we would pop to a stack array `void* batch[16]` then unlock.
        if (handler) {
            handler(data);
        }
        processed++;
    }

    nkit_mcs_unlock(lock, &my_node);
    return processed;
}
