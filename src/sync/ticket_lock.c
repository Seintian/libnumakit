#include <numakit/sync.h>
#include <stdatomic.h>
#include <stdint.h>



void nkit_ticket_init(nkit_ticket_lock_t* lock) {
    atomic_init(&lock->ticket, 0);
    atomic_init(&lock->serving, 0);
}

void nkit_ticket_lock(nkit_ticket_lock_t* lock) {
    // 1. Take a ticket atomically
    uint32_t my_ticket = atomic_fetch_add_explicit(&lock->ticket, 1,
                                                    memory_order_relaxed);

    // 2. Spin until our ticket is being served
    // Uses proportional backoff: threads further back in the queue
    // delay longer to reduce interconnect pressure.
    while (1) {
        uint32_t current = atomic_load_explicit(&lock->serving,
                                                 memory_order_acquire);
        if (current == my_ticket) {
            return; // Our turn
        }

        // Proportional backoff: distance determines how many pauses
        uint32_t distance = my_ticket - current;
        for (uint32_t i = 0; i < distance; i++) {
            nkit_cpu_pause();
        }
    }
}

void nkit_ticket_unlock(nkit_ticket_lock_t* lock) {
    // Signal the next waiter in FIFO order
    atomic_fetch_add_explicit(&lock->serving, 1, memory_order_release);
}
