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

/**
 * @brief Forceably migrate a block of memory to a new NUMA node.
 * Translates virtual addresses to pages and migrates them in the kernel.
 * @param ptr Start of the memory block.
 * @param size Size in bytes.
 * @param target_node The destination NUMA node.
 * @return 0 on success, -1 on failure.
 */
int nkit_memory_migrate(void *ptr, size_t size, int target_node);

// =============================================================================
// NUMA-Aware Slab Allocator
// =============================================================================

/**
 * @brief Opaque handle for a NUMA-aware slab allocator.
 *
 * Provides O(1) fixed-size object allocation and deallocation using
 * a lock-free ring buffer (nkit_ring) as the internal free-list.
 * All memory is backed by an nkit_arena pinned to a specific NUMA node.
 */
typedef struct nkit_slab_s nkit_slab_t;

/**
 * @brief Create a slab allocator for fixed-size objects on a NUMA node.
 *
 * Allocates a contiguous block of (object_size * capacity) bytes from
 * a new arena bound to the specified node, then populates the internal
 * free-list with pointers to each slot.
 *
 * @param node_id   NUMA node to bind memory to.
 * @param obj_size  Size of each object in bytes (will be aligned to 64B).
 * @param capacity  Maximum number of objects (must be a power of 2, >= 2).
 * @return Pointer to the slab, or NULL on failure.
 */
nkit_slab_t *nkit_slab_create(int node_id, size_t obj_size, size_t capacity);

/**
 * @brief Allocate a single object from the slab. O(1), lock-free.
 * @param slab The slab handle.
 * @return Pointer to an object-sized block, or NULL if the slab is exhausted.
 */
void *nkit_slab_alloc(nkit_slab_t *slab);

/**
 * @brief Return an object to the slab. O(1), lock-free.
 * @param slab The slab handle.
 * @param ptr  Pointer previously returned by nkit_slab_alloc.
 */
void nkit_slab_free(nkit_slab_t *slab, void *ptr);

/**
 * @brief Destroy the slab and release all backing memory.
 * @param slab The slab handle.
 */
void nkit_slab_destroy(nkit_slab_t *slab);

/**
 * @brief Query the number of objects currently available (not allocated).
 *
 * Note: In a concurrent setting this is an approximate snapshot.
 * @param slab The slab handle.
 * @return Number of free slots.
 */
size_t nkit_slab_available(nkit_slab_t *slab);

/**
 * @brief Query the total capacity of the slab.
 * @param slab The slab handle.
 * @return Total number of object slots.
 */
size_t nkit_slab_capacity(nkit_slab_t *slab);

// =============================================================================
// Hugepage Coalescing (Return Unused Pages to OS)
// =============================================================================

/**
 * @brief Scan the arena for unused hugepage-aligned regions and return
 *        them to the OS using madvise(MADV_DONTNEED).
 *
 * After allocations are made and then the arena is partially reset,
 * there may be entire 2MB hugepages that are completely unused.
 * This function identifies such pages and advises the kernel to
 * reclaim them, reducing the resident set size (RSS) without
 * unmapping the virtual address range.
 *
 * **Important**: The virtual mapping remains valid.  Future
 * nkit_arena_alloc() calls into coalesced regions will re-fault
 * fresh zeroed pages on demand.
 *
 * @param arena The arena to coalesce.
 * @return Number of hugepages returned to the OS, or 0 if none.
 */
size_t nkit_arena_coalesce(nkit_arena_t *arena);

/**
 * @brief Query how many bytes are currently allocated (in use) in the arena.
 * @param arena The arena handle.
 * @return Bytes currently in use.
 */
size_t nkit_arena_used(nkit_arena_t *arena);

/**
 * @brief Query the total capacity of the arena in bytes.
 * @param arena The arena handle.
 * @return Total arena size.
 */
size_t nkit_arena_size(nkit_arena_t *arena);

/**
 * @brief Query whether the arena is backed by hugepages.
 * @param arena The arena handle.
 * @return 1 if hugepage-backed, 0 if standard pages.
 */
int nkit_arena_is_huge(nkit_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif // NKIT_MEMORY_H
