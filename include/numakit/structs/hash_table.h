#ifndef NKIT_HASH_TABLE_H
#define NKIT_HASH_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Opaque handle for a NUMA-aware hash table.
 *
 * An open-addressing hash table with Robin Hood probing, backed entirely
 * by a NUMA-pinned arena. All memory resides on a single NUMA node,
 * ensuring threads pinned to that node experience local-only access.
 *
 * Thread safety is provided via an internal MCS lock.
 */
typedef struct nkit_hash_s nkit_hash_t;

/**
 * @brief Create a new hash table pinned to a specific NUMA node.
 *
 * The backing memory (bucket array) is allocated from a hugepage-backed
 * arena bound to @p node_id. Capacity is rounded up to the next power of 2.
 *
 * @param node_id  The NUMA node where memory should physically reside.
 * @param capacity Minimum number of buckets (rounded up to power of 2, min 16).
 * @return Pointer to the hash table, or NULL on failure.
 */
nkit_hash_t* nkit_hash_create(int node_id, size_t capacity);

/**
 * @brief Destroy the hash table and release all backing memory.
 * @param ht The hash table to destroy (NULL is safe).
 */
void nkit_hash_destroy(nkit_hash_t* ht);

/**
 * @brief Insert or update a key-value pair.
 *
 * Uses Robin Hood probing for collision resolution. If the key already
 * exists, its value is overwritten.
 *
 * @note The key bytes are compared by value (memcmp), not by pointer.
 *       The caller is responsible for the lifetime of @p key and @p value.
 *
 * @param ht      The hash table.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @param value   The value to associate with this key.
 * @return 0 on success, -1 if the table is full (load > 75%).
 */
int nkit_hash_put(nkit_hash_t* ht, const void* key, size_t key_len,
                  void* value);

/**
 * @brief Look up a value by key.
 *
 * @param ht      The hash table.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @return The associated value, or NULL if not found.
 */
void* nkit_hash_get(nkit_hash_t* ht, const void* key, size_t key_len);

/**
 * @brief Remove a key-value pair.
 *
 * Uses backward-shift deletion (no tombstones) to maintain probe-chain
 * integrity.
 *
 * @param ht      The hash table.
 * @param key     Pointer to the key bytes.
 * @param key_len Length of the key in bytes.
 * @return 0 on success, -1 if the key was not found.
 */
int nkit_hash_remove(nkit_hash_t* ht, const void* key, size_t key_len);

/**
 * @brief Get the number of live entries in the hash table.
 * @param ht The hash table.
 * @return The current entry count.
 */
size_t nkit_hash_count(const nkit_hash_t* ht);

#ifdef __cplusplus
}
#endif

#endif // NKIT_HASH_TABLE_H
