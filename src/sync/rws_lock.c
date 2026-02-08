#include <numakit/sync.h>
#include <stdatomic.h>
#include <stdint.h>

// Constants for bit manipulation
#define RWS_WRITER_ACTIVE  (1U << 0)
#define RWS_WRITER_WAITING (1U << 1)
#define RWS_READER_INCR    (1U << 2)
#define RWS_READER_MASK    (~(RWS_WRITER_ACTIVE | RWS_WRITER_WAITING))

// Helper for CPU relaxation
#if defined(__x86_64__) || defined(_M_X64)
    #define CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__)
    #define CPU_RELAX() __asm__ __volatile__("yield")
#else
    #define CPU_RELAX() do { } while (0)
#endif

void nkit_rws_init(nkit_rws_lock_t* lock) {
    atomic_init(&lock->state, 0);
}

void nkit_rws_read_lock(nkit_rws_lock_t* lock) {
    while (1) {
        uint32_t state = atomic_load_explicit(&lock->state, memory_order_relaxed);

        // 1. If writer is active OR waiting, we spin.
        // (This preference prevents writer starvation)
        if (state & (RWS_WRITER_ACTIVE | RWS_WRITER_WAITING)) {
            CPU_RELAX();
            continue;
        }

        // 2. Try to increment reader count
        // We expect 'state' to match our load. If it changed, we loop again.
        if (atomic_compare_exchange_weak_explicit(&lock->state, &state,
                                                  state + RWS_READER_INCR,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
            return; // Acquired
        }
    }
}

void nkit_rws_read_unlock(nkit_rws_lock_t* lock) {
    atomic_fetch_sub_explicit(&lock->state, RWS_READER_INCR, memory_order_release);
}

void nkit_rws_write_lock(nkit_rws_lock_t* lock) {
    // 1. Announce intent (set WAITING bit)
    // This stops NEW readers from entering.
    atomic_fetch_or_explicit(&lock->state, RWS_WRITER_WAITING, memory_order_relaxed);

    // 2. Wait for current readers to drain and acquire ACTIVE bit
    while (1) {
        uint32_t state = atomic_load_explicit(&lock->state, memory_order_relaxed);

        // We can only grab the lock if NO readers are active
        // and NO other writer is active.
        if ((state & RWS_READER_MASK) == 0 && !(state & RWS_WRITER_ACTIVE)) {
            // Try to set ACTIVE and clear WAITING
            if (atomic_compare_exchange_weak_explicit(&lock->state, &state,
                                                      (state & ~RWS_WRITER_WAITING) | RWS_WRITER_ACTIVE,
                                                      memory_order_acquire,
                                                      memory_order_relaxed)) {
                return; // Acquired
            }
        }
        CPU_RELAX();
    }
}

void nkit_rws_write_unlock(nkit_rws_lock_t* lock) {
    // Clear the ACTIVE bit
    atomic_fetch_and_explicit(&lock->state, ~RWS_WRITER_ACTIVE, memory_order_release);
}
