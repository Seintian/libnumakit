#include <numakit/sync.h>
#include <stddef.h>

void nkit_mcs_init(nkit_mcs_lock_t* lock) {
    atomic_init(&lock->tail, NULL);
}

void nkit_mcs_lock(nkit_mcs_lock_t* lock, nkit_mcs_node_t* node) {
    // 1. Initialize our node
    node->next = NULL;
    atomic_store(&node->locked, 1);

    // 2. Swap ourselves into the tail
    // "prev" is the node that was at the tail before us
    nkit_mcs_node_t* prev = atomic_exchange(&lock->tail, node);

    if (prev) {
        // 3. Someone was there. Link ourselves to them.
        prev->next = node;

        // 4. Spin on OUR local variable until 'prev' unlocks us
        while (atomic_load(&node->locked)) {
            // Hint to CPU that we are spinning (saves power/latency)
            #if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause(); 
            #elif defined(__aarch64__)
                __asm__ __volatile__("yield");
            #endif
        }
    }
}

void nkit_mcs_unlock(nkit_mcs_lock_t* lock, nkit_mcs_node_t* node) {
    // 1. Check if there is a successor
    if (!node->next) {
        // No obvious successor. Try to reset tail to NULL.
        nkit_mcs_node_t* expected = node;
        if (atomic_compare_exchange_strong(&lock->tail, &expected, NULL)) {
            // Success: Queue is now empty.
            return; 
        }

        // Failure: Someone is currently adding themselves (step 2 above).
        // We must wait for them to finish linking 'next'.
        while (!node->next) {
            #if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause(); 
            #elif defined(__aarch64__)
                __asm__ __volatile__("yield");
            #endif
        }
    }

    // 2. Notify the successor they can go
    atomic_store(&node->next->locked, 0);
}
