#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <numakit/numakit.h>
#include "unit.h"

static void test_basic_counts(void) {
    int nodes = nkit_topo_num_nodes();
    int cpus = nkit_topo_num_cpus();

    assert(nodes >= 1);
    assert(cpus >= 1);
    
    printf("  [Check] Nodes: %d, CPUs: %d\n", nodes, cpus);
}

static void test_per_node_cpus(void) {
    int nodes = nkit_topo_num_nodes();
    int total_cpus = nkit_topo_num_cpus();
    int sum_cpus = 0;

    for (int i = 0; i < nodes; i++) {
        int node_cpus = nkit_topo_cpus_on_node(i);
        assert(node_cpus >= 0);
        sum_cpus += node_cpus;
    }

    assert(sum_cpus == total_cpus);
    printf("  [Check] Per-node CPU sum matches total: %d == %d\n", sum_cpus, total_cpus);
}

static void test_distances(void) {
    int nodes = nkit_topo_num_nodes();
    
    for (int i = 0; i < nodes; i++) {
        int self_dist = nkit_topo_distance(i, i);
        // Self distance is usually 10 on Linux systems
        assert(self_dist == 10);
    }

    if (nodes > 1) {
        int cross_dist = nkit_topo_distance(0, 1);
        assert(cross_dist >= 10);
        printf("  [Check] Cross-node distance (0->1): %d\n", cross_dist);
    }

    printf("  [Check] Distances: OK\n");
}

static void test_memory(void) {
    int nodes = nkit_topo_num_nodes();
    
    for (int i = 0; i < nodes; i++) {
        size_t total = 0;
        size_t free_mem = 0;
        int ret = nkit_topo_node_memory(i, &total, &free_mem);
        
        assert(ret == 0);
        assert(total > 0);
        assert(free_mem <= total);
        
        // Test with NULLs
        assert(nkit_topo_node_memory(i, NULL, NULL) == 0);
    }

    printf("  [Check] Memory stats: OK\n");
}

static void test_error_paths(void) {
    int nodes = nkit_topo_num_nodes();

    // Invalid node IDs
    assert(nkit_topo_cpus_on_node(-1) == -1);
    assert(nkit_topo_cpus_on_node(nodes) == -1);
    assert(nkit_topo_distance(-1, 0) == -1);
    assert(nkit_topo_distance(0, nodes) == -1);
    assert(nkit_topo_node_memory(-1, NULL, NULL) == -1);
    assert(nkit_topo_node_memory(nodes, NULL, NULL) == -1);

    printf("  [Check] Invalid input handling: OK\n");
}

int test_10_topology_query(void) {
    printf("[UNIT] Topology Query Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    test_basic_counts();
    test_per_node_cpus();
    test_distances();
    test_memory();
    test_error_paths();

    nkit_teardown();
    printf("[UNIT] Topology Query Test Passed\n");
    return 0;
}
