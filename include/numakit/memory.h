#ifndef NKIT_MEMORY_H
#define NKIT_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque handle for a NUMA-aware memory arena.
 */
typedef struct nkit_arena_s nkit_arena_t;

/**
 * @brief Create a new memory arena bound to a specific NUMA node.
 * @param node_id The NUMA node to bind memory to (e.g., 0, 1).
 * @param size Total size of the arena in bytes.
 * @return nkit_arena_t* Handle to the arena, or NULL on failure.
 */
nkit_arena_t* nkit_arena_create(int node_id, size_t size);

/**
 * @brief Allocate memory from the arena.
 * This is a fast, lock-free bump-pointer allocation. 
 * It is NOT thread-safe by default (wrap it in a lock if sharing).
 * @param arena The arena handle.
 * @param size Bytes to allocate.
 * @return void* Pointer to the allocated memory, or NULL if arena is full.
 */
void* nkit_arena_alloc(nkit_arena_t* arena, size_t size);

/**
 * @brief Reset the arena (freeing all objects at once).
 * Does not return memory to the OS, just resets the pointer.
 */
void nkit_arena_reset(nkit_arena_t* arena);

/**
 * @brief Destroy the arena and return memory to the OS.
 */
void nkit_arena_destroy(nkit_arena_t* arena);

#ifdef __cplusplus
}
#endif

#endif // NKIT_MEMORY_H
