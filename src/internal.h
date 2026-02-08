#ifndef _NKIT_INTERNAL_H
#define _NKIT_INTERNAL_H

#include <hwloc.h>
#include <stdatomic.h>
#include <stdbool.h>

/**
 * @brief Global internal state of the library.
 * Hidden from the public API to allow ABI changes.
 */
typedef struct {
    hwloc_topology_t topo;      // The hwloc topology handle
    int num_nodes;              // Total NUMA nodes detected
    int num_pus;                // Total Processing Units (Threads)
    atomic_bool initialized;    // Thread-safe initialization flag
    atomic_int active_threads;  // Track running threads
} nkit_context_t;

// Defined in init.c
extern nkit_context_t g_nkit_ctx;

// Internal Helper: Detects default hugepage size from /proc/meminfo
size_t _nkit_get_hugepage_size(void);

// Internal Helper: Get the hwloc object for a specific node ID
hwloc_obj_t _nkit_get_hwloc_node(int node_id);

#endif // _NKIT_INTERNAL_H
