#define _GNU_SOURCE
#include <assert.h>
#include <numa.h>
#include <numakit/numakit.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "integration.h"

static atomic_int g_executed_node = -1;
static atomic_int g_task_done = 0;

static void verify_node_task(void *arg) {
    (void)arg;
    int current_node = nkit_get_current_node();
    atomic_store(&g_executed_node, current_node);
    atomic_store(&g_task_done, 1);
}

int integration_03_pool_routing(void) {
    printf("[INTEGRATION] Pool Routing & Data Locality Check...\n");

    int num_nodes = numa_max_node() + 1;
    if (num_nodes < 2) {
        printf("  -> System has only 1 NUMA node. Skipping routing validation.\n");
        return 0;
    }

    nkit_pool_t *pool = nkit_pool_create();
    assert(pool != NULL);

    // ---------------------------------------------------------
    // Test 1: Explicit Routing
    // ---------------------------------------------------------
    int target_node = 1;
    printf("  -> Submitting explicit task to Node %d...\n", target_node);

    atomic_store(&g_task_done, 0);
    nkit_pool_submit_to_node(pool, target_node, verify_node_task, NULL);

    while (!atomic_load(&g_task_done)) {
        usleep(100);
    }

    int executed_on = atomic_load(&g_executed_node);
    printf("     Task executed on Node %d. (Expected: %d)\n", executed_on,
            target_node);
    assert(executed_on == target_node);

    // ---------------------------------------------------------
    // Test 2: Auto-Locality Routing
    // ---------------------------------------------------------
    printf("  -> Submitting local-aware task (Data allocated on Node 1)...\n");

    // Allocate memory specifically on Node 1
    void *node1_data = numa_alloc_onnode(4096, 1);
    assert(node1_data != NULL);
    ((char *)node1_data)[0] = 'X'; // Touch to fault

    atomic_store(&g_task_done, 0);
    atomic_store(&g_executed_node, -1);

    // The pool should auto-detect that 'node1_data' is on Node 1, and route there
    nkit_pool_submit_local(pool, verify_node_task, node1_data);

    while (!atomic_load(&g_task_done)) {
        usleep(100);
    }

    executed_on = atomic_load(&g_executed_node);
    printf("     Auto-routed task executed on Node %d. (Expected: 1)\n",
            executed_on);
    assert(executed_on == 1);

    numa_free(node1_data, 4096);
    nkit_pool_destroy(pool);

    printf("[INTEGRATION] Pool Routing Verification Passed.\n");
    return 0;
}
