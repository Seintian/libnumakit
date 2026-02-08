#ifndef NKIT_SYNC_H
#define NKIT_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdint.h>

/**
 * @brief MCS Lock Node.
 * Allocating this on the *stack* of the waiting thread 
 * ensures the spinning happens on local cache lines.
 */
typedef struct nkit_mcs_node_s {
    struct nkit_mcs_node_s* next;
    atomic_int locked;
} nkit_mcs_node_t;

/**
 * @brief The MCS Lock (Global).
 * Uses a tail pointer to build the queue of waiters.
 */
typedef struct {
    _Atomic(nkit_mcs_node_t*) tail;
} nkit_mcs_lock_t;

/**
 * @brief Initialize the lock.
 */
void nkit_mcs_init(nkit_mcs_lock_t* lock);

/**
 * @brief Acquire the lock.
 * @param lock The global lock object.
 * @param node A thread-local node (usually allocated on stack).
 */
void nkit_mcs_lock(nkit_mcs_lock_t* lock, nkit_mcs_node_t* node);

/**
 * @brief Release the lock.
 */
void nkit_mcs_unlock(nkit_mcs_lock_t* lock, nkit_mcs_node_t* node);

/**
 * @brief Reader-Writer Spinlock (Writer Preferred).
 * optimized for high-contention read scenarios.
 * * Layout:
 * - Bit 0: Writer Active (Exclusive)
 * - Bit 1: Writer Waiting (Blocks new readers)
 * - Bits 2-31: Reader Count
 */
typedef struct {
    _Atomic(uint32_t) state;
} nkit_rws_lock_t;

/**
 * @brief Initialize the RW lock.
 */
void nkit_rws_init(nkit_rws_lock_t* lock);

/**
 * @brief Acquire shared read access.
 * Spins if a writer is active or waiting.
 */
void nkit_rws_read_lock(nkit_rws_lock_t* lock);

/**
 * @brief Release shared read access.
 */
void nkit_rws_read_unlock(nkit_rws_lock_t* lock);

/**
 * @brief Acquire exclusive write access.
 */
void nkit_rws_write_lock(nkit_rws_lock_t* lock);

/**
 * @brief Release exclusive write access.
 */
void nkit_rws_write_unlock(nkit_rws_lock_t* lock);

#ifdef __cplusplus
}
#endif

#endif // NKIT_SYNC_H
