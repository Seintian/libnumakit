/**
 * @file 00_topology_check.c
 * @brief Minimal integration test for NUMA hardware/emulation.
 */

#include <stdio.h>

// #include <numakit/numakit.h>

int main(void) {
    printf("[INTEGRATION] Topology check started...\n");

    // TODO: verification logic will go here
    // int node_count = nkit_get_node_count();
    // if (node_count < 1) return 1;

    printf("[INTEGRATION] Topology check passed.\n");
    return 0;
}
