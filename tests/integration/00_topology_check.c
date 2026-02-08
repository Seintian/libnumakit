/**
 * @file 00_topology_check.c
 * @brief Minimal integration test for NUMA hardware/emulation.
 */

#include <stdio.h>
#include <numakit/numakit.h>

int main(void) {
    printf("[INTEGRATION] Topology check started...\n");

    // 1. Initialize Topology
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    printf("libnumakit initialized successfully.\n");

    // 2. Cleanup
    nkit_teardown();

    printf("[INTEGRATION] Topology check passed.\n");
    return 0;
}
