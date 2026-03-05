#include <stdio.h>
#include <numakit/numakit.h>

int main() {
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    int num_nodes = nkit_topo_num_nodes();
    printf("Topology Query Example\n");
    printf("======================\n");
    printf("Total NUMA nodes: %d\n", num_nodes);

    for (int i = 0; i < num_nodes; i++) {
        size_t total_mem = 0, free_mem = 0;
        if (nkit_topo_node_memory(i, &total_mem, &free_mem) == 0) {
            printf("\nNode %d:\n", i);
            printf("  CPUs: %d\n", nkit_topo_cpus_on_node(i));
            printf("  Total Memory: %zu MB\n", total_mem / (1024 * 1024));
            printf("  Free Memory:  %zu MB\n", free_mem / (1024 * 1024));
        }
    }

    printf("\nDistance Matrix:\n    ");
    for (int j = 0; j < num_nodes; j++) {
        printf("N%-2d ", j);
    }
    printf("\n");
    for (int i = 0; i < num_nodes; i++) {
        printf("N%-2d ", i);
        for (int j = 0; j < num_nodes; j++) {
            printf("%3d ", nkit_topo_distance(i, j));
        }
        printf("\n");
    }

    nkit_teardown();
    return 0;
}
