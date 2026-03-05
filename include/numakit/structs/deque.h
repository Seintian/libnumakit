/**
 * @file deque.h
 * @brief NUMA-aware lock-free work-stealing deque (Chase-Lev algorithm).
 */

#ifndef NKIT_DEQUE_H
#define NKIT_DEQUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a NUMA-aware work-stealing deque.
 */
typedef struct nkit_deque_s nkit_deque_t;

/**
 * @brief Create a new NUMA-aware work-stealing deque.
 * 
 * @param node_id The NUMA node to allocate the deque and its internal buffer on.
 * @param initial_capacity The initial capacity of the deque (must be a power of 2).
 * @return nkit_deque_t* Pointer to the created deque, or NULL on failure.
 */
nkit_deque_t* nkit_deque_create(int node_id, uint32_t initial_capacity);

/**
 * @brief Destroy a deque and free all associated memory.
 * 
 * @param dq Pointer to the deque to destroy.
 */
void nkit_deque_destroy(nkit_deque_t* dq);

/**
 * @brief Push a data pointer onto the bottom of the deque.
 * 
 * This function should only be called by the thread that "owns" the deque.
 * 
 * @param dq The deque.
 * @param data The data pointer to push.
 * @return bool True if the push was successful, false if the deque is full (and resize failed).
 */
bool nkit_deque_push(nkit_deque_t* dq, void* data);

/**
 * @brief Pop a data pointer from the bottom of the deque.
 * 
 * This function should only be called by the thread that "owns" the deque.
 * It operates in LIFO order (last-in, first-out).
 * 
 * @param dq The deque.
 * @param data Pointer to store the popped data pointer.
 * @return bool True if a data pointer was popped, false if the deque is empty.
 */
bool nkit_deque_pop(nkit_deque_t* dq, void** data);

/**
 * @brief Steal a data pointer from the top of the deque.
 * 
 * This function can be called by any thread (thieves).
 * It operates in FIFO order (first-in, first-out).
 * 
 * @param dq The deque.
 * @param data Pointer to store the stolen data pointer.
 * @return bool True if a data pointer was stolen, false if the deque is empty.
 */
bool nkit_deque_steal(nkit_deque_t* dq, void** data);

/**
 * @brief Get the current number of elements in the deque (approximate).
 * 
 * @param dq The deque.
 * @return uint32_t Approximate count of elements.
 */
uint32_t nkit_deque_size(nkit_deque_t* dq);

#ifdef __cplusplus
}
#endif

#endif // NKIT_DEQUE_H
