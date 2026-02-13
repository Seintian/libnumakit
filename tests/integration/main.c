#include <stdio.h>
#include <string.h>
#include "integration.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <integration_name>\n", argv[0]);
        printf("Available integrations:\n");
        printf("  00_topology_check   - Test topology check (00)\n");
        printf("  01_pinning_check    - Test pinning check (01)\n");
        printf("  02_cross_node_messaging - Test cross-node messaging (02)\n");
        printf("  all                 - Run all integrations sequentially\n");
        return 1;
    }

    if (strcmp(argv[1], "00_topology_check") == 0) {
        return integration_00_topology_check();
    } else if (strcmp(argv[1], "01_pinning_check") == 0) {
        return integration_01_pinning_check();
    } else if (strcmp(argv[1], "02_cross_node_messaging") == 0) {
        return integration_02_cross_node_messaging();
    } else if (strcmp(argv[1], "all") == 0) {
        printf(">>> RUNNING INTEGRATION 00: TOPOLOGY CHECK <<<\n");
        integration_00_topology_check();

        printf("\n\n>>> RUNNING INTEGRATION 01: PINNING CHECK <<<\n");
        integration_01_pinning_check();

        printf("\n\n>>> RUNNING INTEGRATION 02: CROSS-NODE MESSAGING <<<\n");
        integration_02_cross_node_messaging();
        return 0;
    }

    printf("Unknown integration: %s\n", argv[1]);
    return 1;
}
