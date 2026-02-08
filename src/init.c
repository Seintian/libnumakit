#include <numakit/numakit.h>
#include "internal.h"

#include <numa.h>
#include <stddef.h>
#include <hwloc.h>
#include <stdatomic.h>

// Global Context Definition
nkit_context_t g_nkit_ctx = {0};

#define DEFAULT_MPKI 50.0

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

    // 5. Initialize Mailboxes (One per Node)
    g_nkit_ctx.mailboxes = calloc(g_nkit_ctx.num_nodes, sizeof(nkit_mailbox_t*));

    if (g_nkit_ctx.mailboxes) {
        for (int i = 0; i < g_nkit_ctx.num_nodes; i++) {
            // ALLOCATE ON NODE 'i'
            // This ensures the lock variable is local to the worker on that node.
            g_nkit_ctx.mailboxes[i] = numa_alloc_onnode(sizeof(nkit_mailbox_t), i);

            if (g_nkit_ctx.mailboxes[i]) {
                // Initialize Ring (already hugepage backed on Node i)
                g_nkit_ctx.mailboxes[i]->ring = nkit_ring_create(i, 4096);
            }
        }
    }

    // 6. Set Defaults
    g_nkit_ctx.balancer_threshold_mpki = DEFAULT_MPKI; // Default: 5% miss rate is "bad"

    // 7. Cache key metrics (to avoid querying hwloc repeatedly)
    g_nkit_ctx.num_nodes = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE);
    g_nkit_ctx.num_pus   = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_PU);

    // Fallback for non-NUMA systems (Unified Memory)
    if (g_nkit_ctx.num_nodes <= 0) {
        g_nkit_ctx.num_nodes = 1; 
    }

    return 0;
}

void nkit_teardown(void) {
    // Cleanup Mailboxes
    if (g_nkit_ctx.mailboxes) {
        for (int i = 0; i < g_nkit_ctx.num_nodes; i++) {
            if (g_nkit_ctx.mailboxes[i]) {
                nkit_ring_free(g_nkit_ctx.mailboxes[i]->ring);

                // Free the NUMA-local memory
                numa_free(g_nkit_ctx.mailboxes[i], sizeof(nkit_mailbox_t));
            }
        }
        free(g_nkit_ctx.mailboxes);
    }

    bool expected = true;
    if (atomic_compare_exchange_strong(&g_nkit_ctx.initialized, &expected, false)) {
        hwloc_topology_destroy(g_nkit_ctx.topo);
        g_nkit_ctx.topo = NULL;
    }
}
