#include <numakit/numakit.h>
#include "internal.h"
#include <stddef.h>
#include <hwloc.h>
#include <stdatomic.h>

// Global Context Definition
nkit_context_t g_nkit_ctx = {0};

int nkit_init(void) {
    // 1. Thread-safe "Run Once" check
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_nkit_ctx.initialized, &expected, true)) {
        // Already initialized. Return success (idempotent).
        return 0;
    }

    // 2. Initialize hwloc topology
    if (hwloc_topology_init(&g_nkit_ctx.topo) != 0) {
        atomic_store(&g_nkit_ctx.initialized, false);
        return -1;
    }

    // 3. Configure hwloc (Load all OS devices, filter I/O if needed)
    hwloc_topology_set_flags(g_nkit_ctx.topo, HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM);

    // 4. Load topology (Expensive operation)
    if (hwloc_topology_load(g_nkit_ctx.topo) != 0) {
        hwloc_topology_destroy(g_nkit_ctx.topo);
        atomic_store(&g_nkit_ctx.initialized, false);
        return -1;
    }

    // 5. Cache key metrics (to avoid querying hwloc repeatedly)
    g_nkit_ctx.num_nodes = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE);
    g_nkit_ctx.num_pus   = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_PU);

    // Fallback for non-NUMA systems (Unified Memory)
    if (g_nkit_ctx.num_nodes <= 0) {
        g_nkit_ctx.num_nodes = 1; 
    }

    return 0;
}

void nkit_teardown(void) {
    bool expected = true;
    if (atomic_compare_exchange_strong(&g_nkit_ctx.initialized, &expected, false)) {
        hwloc_topology_destroy(g_nkit_ctx.topo);
        g_nkit_ctx.topo = NULL;
    }
}
