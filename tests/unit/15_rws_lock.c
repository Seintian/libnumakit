#include <stdio.h>
#include <pthread.h>
#include <assert.h>

#include <numakit/numakit.h>
#include <numakit/sync.h>
#include "unit.h"

#define NUM_READERS 4
#define NUM_WRITERS 2
#define ITERATIONS 10000

static nkit_rws_lock_t global_rwlock;
static int shared_data = 0;

static void* reader_thread(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        nkit_rws_read_lock(&global_rwlock);
        // We just read the data. In a real test we'd ensure no writer is active.
        int val = shared_data;
        (void)val;
        nkit_rws_read_unlock(&global_rwlock);
    }
    return NULL;
}

static void* writer_thread(void* arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        nkit_rws_write_lock(&global_rwlock);
        shared_data++;
        nkit_rws_write_unlock(&global_rwlock);
    }
    return NULL;
}

int test_15_rws_lock(void) {
    printf("[UNIT] RWS Lock Test Started...\n");

    nkit_rws_init(&global_rwlock);
    shared_data = 0;

    pthread_t readers[NUM_READERS];
    pthread_t writers[NUM_WRITERS];

    for (int i = 0; i < NUM_READERS; i++) {
        int ret = pthread_create(&readers[i], NULL, reader_thread, NULL);
        assert(ret == 0);
    }
    
    for (int i = 0; i < NUM_WRITERS; i++) {
        int ret = pthread_create(&writers[i], NULL, writer_thread, NULL);
        assert(ret == 0);
    }

    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(readers[i], NULL);
    }
    for (int i = 0; i < NUM_WRITERS; i++) {
        pthread_join(writers[i], NULL);
    }

    printf("  [Check] Writers finished, shared_data: %d (Expected: %d)\n", shared_data, NUM_WRITERS * ITERATIONS);
    assert(shared_data == NUM_WRITERS * ITERATIONS);

    printf("[UNIT] RWS Lock Test Passed\n");
    return 0;
}
