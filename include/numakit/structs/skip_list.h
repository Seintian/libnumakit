/**
 * @file skip_list.h
 * @brief NUMA-aware sharded skip list.
 */

#ifndef NKIT_SKIP_LIST_H
#define NKIT_SKIP_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Opaque handle for a NUMA-aware sharded skip list.
 *
 * This structure shards entries across multiple NUMA nodes to minimize
 * cross-node synchronization and improve cache locality.
 */
typedef struct nkit_skip_s nkit_skip_t;

/**
 * @brief Create a new NUMA-aware sharded skip list.
 *
 * @param max_level Maximum height of the skip list (e.g., 16 or 32).
 * @return nkit_skip_t* Pointer to the created skip list, or NULL on failure.
 */
nkit_skip_t* nkit_skip_create(uint32_t max_level);

/**
 * @brief Destroy the skip list and free all associated memory.
 * 
 * @param sl Pointer to the skip list to destroy.
 */
void nkit_skip_destroy(nkit_skip_t* sl);

/**
 * @brief Insert or update a key-value pair in the skip list.
 *
 * The key is hashed to determine which NUMA node shard will store the entry.
 *
 * @param sl      The skip list.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @param value   The value to associate with this key.
 * @return 0 on success, -1 on failure.
 */
int nkit_skip_put(nkit_skip_t* sl, const void* key, size_t key_len, void* value);

/**
 * @brief Look up a value by key.
 *
 * @param sl      The skip list.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @return The associated value, or NULL if not found.
 */
void* nkit_skip_get(nkit_skip_t* sl, const void* key, size_t key_len);

/**
 * @brief Remove a key-value pair from the skip list.
 *
 * @param sl      The skip list.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @return 0 on success, -1 if the key was not found.
 */
int nkit_skip_remove(nkit_skip_t* sl, const void* key, size_t key_len);

/**
 * @brief Get the total number of entries across all shards.
 * 
 * @param sl The skip list.
 * @return size_t Total entry count.
 */
size_t nkit_skip_count(const nkit_skip_t* sl);

#ifdef __cplusplus
}
#endif

#endif // NKIT_SKIP_LIST_H
