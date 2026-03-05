#ifndef NKIT_TOPOLOGY_H
#define NKIT_TOPOLOGY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Get the total number of NUMA nodes in the system.
 * 
 * If the system is not NUMA or kernel NUMA support is disabled,
 * this returns 1.
 * 
 * @return Total number of NUMA nodes (>= 1).
 */
int nkit_topo_num_nodes(void);

/**
 * @brief Get the total number of Processing Units (PUs / Logical CPUs) in the system.
 * 
 * @return Total number of logical CPUs.
 */
int nkit_topo_num_cpus(void);

/**
 * @brief Get the number of Processing Units (logical CPUs) on a specific NUMA node.
 * 
 * @param node_id The NUMA node ID to query.
 * @return Number of CPUs on this node, or -1 if the node_id is invalid.
 */
int nkit_topo_cpus_on_node(int node_id);

/**
 * @brief Get the relative latency distance between two NUMA nodes.
 * 
 * This relies on system hardware providing ACPI SLIT tables or equivalent.
 * Typically, local access (node A to node A) is 10.
 * Single inter-processor links might be 20 or 21.
 * 
 * @param from_node Source NUMA node ID.
 * @param to_node Destination NUMA node ID.
 * @return The latency weight, or -1 if the distance is unknown or nodes are invalid.
 */
int nkit_topo_distance(int from_node, int to_node);

/**
 * @brief Get the memory capacity and currently available memory of a NUMA node.
 * 
 * @param node_id The NUMA node ID to query.
 * @param total_bytes Pointer to store the total memory in bytes (may be NULL).
 * @param free_bytes Pointer to store the free/available memory in bytes (may be NULL).
 *                   Note: Free memory is a snapshot in time and can change instantly.
 * @return 0 on success, -1 if the node ID is invalid.
 */
int nkit_topo_node_memory(int node_id, size_t *total_bytes, size_t *free_bytes);

/**
 * @brief Query if proper NUMA support (libnuma) is available and active.
 * 
 * If this returns 0, the library will fall back to using standard
 * memory allocators and default CPU scheduling, ignoring placement requests.
 * 
 * @return 1 if true NUMA is supported, 0 otherwise.
 */
int nkit_topo_is_numa(void);

#ifdef __cplusplus
}
#endif

#endif // NKIT_TOPOLOGY_H
