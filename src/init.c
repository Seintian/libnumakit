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
    if (g_nkit_ctx.initialized) return 0;

    // 1. Check Kernel NUMA Support
    // If this fails, we are on a standard single-socket machine (or WSL1).
    if (numa_available() < 0) {
        g_nkit_ctx.numa_supported = false;
        // We do NOT return -1 here. We want the library to still work 
        // as a standard thread/memory library, just without pinning.
    } else {
        g_nkit_ctx.numa_supported = true;
    }

    // 2. Thread-safe "Run Once" check
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_nkit_ctx.initialized, &expected, true)) {
        // Already initialized. Return success (idempotent).
        return 0;
    }

    // 3. Initialize hwloc topology
    if (hwloc_topology_init(&g_nkit_ctx.topo) != 0) {
        atomic_store(&g_nkit_ctx.initialized, false);
        return -1;
    }

    // 4. Configure hwloc (Load all OS devices, filter I/O if needed)
    hwloc_topology_set_flags(g_nkit_ctx.topo, HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM);

    // 5. Load topology (Expensive operation)
    if (hwloc_topology_load(g_nkit_ctx.topo) != 0) {
        hwloc_topology_destroy(g_nkit_ctx.topo);
        atomic_store(&g_nkit_ctx.initialized, false);
        return -1;
    }

    // 6. Initialize Mailboxes (One per Node)
    g_nkit_ctx.mailboxes = calloc(g_nkit_ctx.num_nodes, sizeof(nkit_mailbox_t*));

    if (g_nkit_ctx.mailboxes) {
        for (int i = 0; i < g_nkit_ctx.num_nodes; i++) {
            if (g_nkit_ctx.numa_supported) {
                // NUMA path: Pin memory to specific node
                g_nkit_ctx.mailboxes[i] = numa_alloc_onnode(sizeof(nkit_mailbox_t), i);
            } else {
                // UMA/Fallback path: Standard malloc (OS decides placement)
                g_nkit_ctx.mailboxes[i] = malloc(sizeof(nkit_mailbox_t));
            }

            if (g_nkit_ctx.mailboxes[i]) {
                // Initialize Ring (already hugepage backed on Node i)
                g_nkit_ctx.mailboxes[i]->ring = nkit_ring_create(i, 4096);
            }
        }
    }

    // 7. Set Defaults
    g_nkit_ctx.balancer_threshold_mpki = DEFAULT_MPKI; // Default: 5% miss rate is "bad"

    // 8. Cache key metrics (to avoid querying hwloc repeatedly)
    g_nkit_ctx.num_nodes = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE);
    g_nkit_ctx.num_pus   = hwloc_get_nbobjs_by_type(g_nkit_ctx.topo, HWLOC_OBJ_PU);

    // 9. Fallback for non-NUMA systems (Unified Memory)
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

                if (g_nkit_ctx.numa_supported) {
                    numa_free(g_nkit_ctx.mailboxes[i], sizeof(nkit_mailbox_t));
                } else {
                    free(g_nkit_ctx.mailboxes[i]);
                }
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
