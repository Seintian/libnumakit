#include <stdio.h>
#include <numakit/numakit.h>

int main() {
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    if (nkit_topo_is_numa()) {
        printf("Hello, NUMA! System has %d active NUMA nodes.\n", nkit_topo_num_nodes());
    } else {
        printf("Hello, UMA! System is not NUMA-aware.\n");
    }

    printf("Total logical CPUs: %d\n", nkit_topo_num_cpus());

    nkit_teardown();
    return 0;
}
