#include <stdio.h>
#include <pthread.h>
#include <assert.h>

#include <numakit/numakit.h>
#include <numakit/sync.h>
#include "unit.h"

#define NUM_THREADS 4
#define INCREMENTS_PER_THREAD 10000

static nkit_mcs_lock_t global_lock;
static int shared_counter = 0;

static void* worker_thread(void* arg) {
    (void)arg; // unused
    
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        nkit_mcs_node_t node; // Allocated on the stack for cache locality
        nkit_mcs_lock(&global_lock, &node);
        shared_counter++;
        nkit_mcs_unlock(&global_lock, &node);
    }
    return NULL;
}

int test_14_mcs_lock(void) {
    printf("[UNIT] MCS Lock Test Started...\n");

    nkit_mcs_init(&global_lock);
    shared_counter = 0;

    pthread_t threads[NUM_THREADS];

    // Spawn threads
    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, worker_thread, NULL);
        assert(ret == 0);
    }

    // Join threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("  [Check] Expected counter: %d, Actual: %d\n", NUM_THREADS * INCREMENTS_PER_THREAD, shared_counter);
    assert(shared_counter == NUM_THREADS * INCREMENTS_PER_THREAD);

    printf("[UNIT] MCS Lock Test Passed\n");
    return 0;
}
