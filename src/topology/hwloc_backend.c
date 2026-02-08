#include "../internal.h"
#include <stddef.h>

/**
 * @brief internal wrapper to fetch a NUMA node object from hwloc.
 * Used to avoid sprinkling HWLOC_OBJ_NUMANODE constants everywhere.
 */
hwloc_obj_t _nkit_get_hwloc_node(int node_id) {
    if (!g_nkit_ctx.topo) {
        return NULL;
    }

    // Safety check: Ensure node_id is within detected range
    if (node_id < 0 || node_id >= g_nkit_ctx.num_nodes) {
        return NULL;
    }

    return hwloc_get_obj_by_type(g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE, (unsigned)node_id);
}
