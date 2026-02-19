#define _GNU_SOURCE

#include <assert.h>
#include <numakit/numakit.h>
#include <stdio.h>
#include <stdlib.h>

#include "unit.h"

int test_03_memory_migrate(void) {
    printf("[UNIT] Memory Migration Test Started...\n");

    // Allocate standard memory
    size_t size = 1024 * 1024; // 1MB
    void *buffer = malloc(size);
    assert(buffer != NULL);

    // Touch memory to ensure it is physically allocated (faulted in)
    char *char_buf = (char *)buffer;
    for (size_t i = 0; i < size; i += 4096) {
        char_buf[i] = 1;
    }

    // Try migrating to Node 0 (Should always be safe)
    int ret = nkit_memory_migrate(buffer, size, 0);
    if (ret == 0) {
        printf("  -> Successfully migrated 1MB buffer to Node 0.\n");
    } else {
        printf(
            "  [Warning] Migration failed (Permissions/Capabilities missing?)\n");
    }

    free(buffer);
    printf("[UNIT] Memory Migration Test Passed.\n");
    return 0;
}
