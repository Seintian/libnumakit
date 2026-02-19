#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include <numakit/numakit.h>

#include "unit.h"

static atomic_int g_task_counter = 0;

static void sample_task(void* arg) {
    int val = (int)(intptr_t)arg;
    atomic_fetch_add(&g_task_counter, val);
}

int test_02_task_pool(void) {
    printf("[UNIT] Task Pool Test Started...\n");

    // CRITICAL FIX: Initialize library first
    if (nkit_init() != 0) {
        printf("  [Error] Failed to initialize libnumakit\n");
        return 1;
    }

    nkit_pool_t* pool = nkit_pool_create();
    if (!pool) {
        printf("  [Warning] Failed to create pool (Hugepages missing?). Skipping.\n");
        nkit_teardown();
        return 0; 
    }

    int num_tasks = 100;
    atomic_store(&g_task_counter, 0);

    for (int i = 0; i < num_tasks; i++) {
        int ret = nkit_pool_submit_to_node(pool, 0, sample_task, (void*)(intptr_t)1);
        assert(ret == 0);
    }

    int timeouts = 0;
    while (atomic_load(&g_task_counter) < num_tasks) {
        usleep(1000); 
        if (++timeouts > 5000) { 
            printf("  [Error] Tasks did not complete in time!\n");
            assert(0);
        }
    }

    assert(atomic_load(&g_task_counter) == num_tasks);
    printf("  -> Executed %d tasks successfully.\n", num_tasks);

    nkit_pool_destroy(pool);
    nkit_teardown(); // Clean up
    printf("[UNIT] Task Pool Test Passed.\n");
    return 0;
}