#include <stdio.h>
#include <string.h>
#include "unit.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <unit_name>\n", argv[0]);
        printf("Available units:\n");
        printf("  00_sanity_check   - Test basic functionality (00)\n");
        printf("  01_affinity       - Test thread affinity (01)\n");
        printf("  02_task_pool      - Test task pool (02)\n");
        printf("  03_memory_migrate - Test memory migration (03)\n");
        printf("  04_ticket_lock    - Test ticket lock primitive (04)\n");
        printf("  05_pcounter       - Test partitioned counter (05)\n");
        printf("  06_hash_table     - Test hash table (06)\n");
        printf("  07_slab_allocator - Test slab allocator (07)\n");
        printf("  08_auto_balancer  - Test auto-balancer (08)\n");
        printf("  09_hugepage_coal  - Test hugepage coalescing (09)\n");
        printf("  all               - Run all units sequentially\n");
        return 1;
    }

    if (strcmp(argv[1], "00_sanity_check") == 0) {
        return test_00_sanity_check();
    } else if (strcmp(argv[1], "01_affinity") == 0) {
        return test_01_affinity();
    } else if (strcmp(argv[1], "02_task_pool") == 0) {
        return test_02_task_pool();
    } else if (strcmp(argv[1], "03_memory_migrate") == 0) {
        return test_03_memory_migrate();
    } else if (strcmp(argv[1], "04_ticket_lock") == 0) {
        return test_04_ticket_lock();
    } else if (strcmp(argv[1], "05_pcounter") == 0) {
        return test_05_pcounter();
    } else if (strcmp(argv[1], "06_hash_table") == 0) {
        return test_06_hash_table();
    } else if (strcmp(argv[1], "07_slab_allocator") == 0) {
        return test_07_slab_allocator();
    } else if (strcmp(argv[1], "08_auto_balancer") == 0) {
        return test_08_auto_balancer();
    } else if (strcmp(argv[1], "09_hugepage_coal") == 0) {
        return test_09_hugepage_coalesce();
    } else if (strcmp(argv[1], "all") == 0) {
        printf(">>> RUNNING UNIT 00: SANITY CHECK <<<\n");
        test_00_sanity_check();

        printf("\n\n>>> RUNNING UNIT 01: AFFINITY <<<\n");
        test_01_affinity();

        printf("\n\n>>> RUNNING UNIT 02: TASK POOL <<<\n");
        test_02_task_pool();

        printf("\n\n>>> RUNNING UNIT 03: MEMORY MIGRATE <<<\n");
        test_03_memory_migrate();

        printf("\n\n>>> RUNNING UNIT 04: TICKET LOCK <<<\n");
        test_04_ticket_lock();

        printf("\n\n>>> RUNNING UNIT 05: PARTITIONED COUNTER <<<\n");
        test_05_pcounter();

        printf("\n\n>>> RUNNING UNIT 06: HASH TABLE <<<\n");
        test_06_hash_table();

        printf("\n\n>>> RUNNING UNIT 07: SLAB ALLOCATOR <<<\n");
        test_07_slab_allocator();

        printf("\n\n>>> RUNNING UNIT 08: AUTO-BALANCER <<<\n");
        test_08_auto_balancer();

        printf("\n\n>>> RUNNING UNIT 09: HUGEPAGE COALESCING <<<\n");
        test_09_hugepage_coalesce();
        return 0;
    }

    printf("Unknown unit: %s\n", argv[1]);
    return 1;
}
