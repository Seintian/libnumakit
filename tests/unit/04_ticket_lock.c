#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#define _GNU_SOURCE

#include <numakit/numakit.h>
#include <numakit/sync.h>
#include "unit.h"

#define NUM_THREADS 4
#define INCREMENTS_PER_THREAD 100000

static nkit_ticket_lock_t global_lock;
static int shared_counter = 0;

static void* worker_thread(void* arg) {
    (void)arg; // unused
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        nkit_ticket_lock(&global_lock);
        shared_counter++;
        nkit_ticket_unlock(&global_lock);
    }
    return NULL;
}

int test_04_ticket_lock(void) {
    printf("[UNIT] Ticket Lock API Test Started...\n");

    nkit_ticket_init(&global_lock);
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

    printf("[UNIT] Ticket Lock API Test Passed\n");
    return 0;
}
