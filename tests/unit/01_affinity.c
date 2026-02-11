#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <numakit/numakit.h>
#include "unit.h"

void test_getters(void) {
    int cpu = nkit_get_current_core();
    int node = nkit_get_current_node();

    printf("  [Info] Current: CPU %d, Node %d\n", cpu, node);

    // Sanity checks
    assert(cpu >= 0);
    assert(node >= 0);
}

void test_core_pinning(void) {
    // Pin to CPU 0 (guaranteed to exist on any linux system)
    printf("  [Action] Pinning to Core 0...\n");

    if (nkit_pin_thread_to_core(0) == 0) {
        int cpu = nkit_get_current_core();
        printf("  [Check] Current Core: %d\n", cpu);
        assert(cpu == 0);
    } else {
        printf("  [Warning] Pinning to Core 0 failed (Container restrictions?)\n");
    }
}

void test_node_pinning(void) {
    // Pin to Node 0 (guaranteed to exist)
    printf("  [Action] Pinning to Node 0...\n");

    if (nkit_pin_thread_to_node(0) == 0) {
        int node = nkit_get_current_node();
        printf("  [Check] Current Node: %d\n", node);

        // If we are pinned to Node 0, we must be running on Node 0
        assert(node == 0);
    } else {
        printf("  [Error] Pinning to Node 0 failed\n");
        // Failure to pin to Node 0 is usually fatal for this library tests
        assert(0 && "Node 0 pinning failed");
    }
}

int test_01_affinity(void) {
    printf("[UNIT] Affinity API Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    test_getters();
    test_core_pinning();
    test_node_pinning();

    nkit_teardown();
    printf("[UNIT] Affinity API Test Passed\n");
    return 0;
}
