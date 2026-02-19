#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <numa.h>
#include <numaif.h>
#include <numakit/numakit.h>

#include "integration.h"

static int get_physical_node(void* ptr) {
    int status[1] = {-1};
    void* pages[1] = { ptr };
    if (move_pages(0, 1, pages, NULL, status, 0) == 0) {
        return status[0];
    }
    return -1;
}

int integration_04_page_migration(void) {
    printf("[INTEGRATION] Dynamic Page Migration Check...\n");

    if (nkit_init() != 0) return 1;

    int num_nodes = numa_max_node() + 1;
    if (num_nodes < 2) {
        printf("  -> Single node system. Skipping migration test.\n");
        nkit_teardown();
        return 0;
    }

    size_t size = 2 * 1024 * 1024; // 2MB

    void* buffer = numa_alloc_onnode(size, 0);
    assert(buffer != NULL);

    for (size_t i = 0; i < size; i += 4096) { ((char*)buffer)[i] = 1; }

    int start_node = get_physical_node(buffer);
    printf("  -> Initial allocation resides on Node: %d\n", start_node);
    assert(start_node == 0);

    printf("  -> Force migrating 2MB to Node 1...\n");
    int ret = nkit_memory_migrate(buffer, size, 1);
    assert(ret == 0);

    int end_node = get_physical_node(buffer);
    printf("  -> After migration, memory resides on Node: %d\n", end_node);
    assert(end_node == 1);

    numa_free(buffer, size);
    nkit_teardown();

    printf("[INTEGRATION] Page Migration Passed.\n");
    return 0;
}
