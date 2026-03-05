#include <numakit/topology.h>
#include "../internal.h"
#include <hwloc.h>
#include <numa.h>

int nkit_topo_num_nodes(void) {
    if (!g_nkit_ctx.initialized) return -1;
    return g_nkit_ctx.num_nodes;
}

int nkit_topo_num_cpus(void) {
    if (!g_nkit_ctx.initialized) return -1;
    return g_nkit_ctx.num_pus;
}

int nkit_topo_cpus_on_node(int node_id) {
    if (!g_nkit_ctx.initialized) return -1;
    
    hwloc_obj_t node_obj = _nkit_get_hwloc_node(node_id);
    if (!node_obj) {
        return -1;
    }

    // Return the number of PUs (logical processors) inside this node
    // Using hwloc_get_nbobjs_inside_cpuset_by_type
    return hwloc_get_nbobjs_inside_cpuset_by_type(
        g_nkit_ctx.topo, node_obj->cpuset, HWLOC_OBJ_PU
    );
}

int nkit_topo_distance(int from_node, int to_node) {
    if (!g_nkit_ctx.initialized) return -1;

    hwloc_obj_t obj_from = _nkit_get_hwloc_node(from_node);
    hwloc_obj_t obj_to   = _nkit_get_hwloc_node(to_node);

    if (!obj_from || !obj_to) {
        return -1;
    }

    // Get latency from libnuma (if hwloc doesn't readily provide distance matrices without complex queries)
    if (g_nkit_ctx.numa_supported) {
        int dist = numa_distance(from_node, to_node);
        if (dist > 0) {
            return dist;
        }
    }
    // Fallback or UMA system
    return (from_node == to_node) ? 10 : 20; 
}

int nkit_topo_node_memory(int node_id, size_t *total_bytes, size_t *free_bytes) {
    if (!g_nkit_ctx.initialized) return -1;

    hwloc_obj_t node_obj = _nkit_get_hwloc_node(node_id);
    if (!node_obj) {
        return -1;
    }

    if (total_bytes) {
        *total_bytes = node_obj->attr->numanode.local_memory;
    }

    if (free_bytes) {
        if (g_nkit_ctx.numa_supported) {
            long long free_size = 0;
            numa_node_size64(node_id, &free_size);
            *free_bytes = (size_t)free_size;
        } else {
            // No accurate free memory reporting per-node on non-NUMA via hwloc effortlessly,
            // fallback to total memory as a best guess for UMA
            *free_bytes = node_obj->attr->numanode.local_memory;
        }
    }

    return 0;
}

int nkit_topo_is_numa(void) {
    if (!g_nkit_ctx.initialized) return 0;
    return g_nkit_ctx.numa_supported ? 1 : 0;
}
