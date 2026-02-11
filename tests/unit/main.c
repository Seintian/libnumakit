#include <stdio.h>
#include <string.h>
#include "unit.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <unit_name>\n", argv[0]);
        printf("Available units:\n");
        printf("  00_sanity_check   - Test basic functionality (00)\n");
        printf("  01_affinity       - Test thread affinity (01)\n");
        printf("  all               - Run all units sequentially\n");
        return 1;
    }

    if (strcmp(argv[1], "00_sanity_check") == 0) {
        return test_00_sanity_check();
    } else if (strcmp(argv[1], "01_affinity") == 0) {
        return test_01_affinity();
    } else if (strcmp(argv[1], "all") == 0) {
        printf(">>> RUNNING UNIT 00: SANITY CHECK <<<\n");
        test_00_sanity_check();

        printf("\n\n>>> RUNNING UNIT 01: AFFINITY <<<\n");
        test_01_affinity();
        return 0;
    }

    printf("Unknown unit: %s\n", argv[1]);
    return 1;
}
