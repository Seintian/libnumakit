#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#define _GNU_SOURCE

#include <numakit/numakit.h>
#include <numakit/sync.h>
#include "unit.h"

#define NUM_THREADS 4
#define INCREMENTS_PER_THREAD 100000

static nkit_pcounter_t* global_counter;

static void* worker_thread(void* arg) {
    (void)arg;
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        nkit_pcounter_inc(global_counter);
    }
    return NULL;
}

int test_05_pcounter(void) {
    printf("[UNIT] Partitioned Counter API Test Started...\n");

    // 1. Create / Destroy sanity
    nkit_pcounter_t* tmp = nkit_pcounter_create();
    assert(tmp != NULL);
    nkit_pcounter_destroy(tmp);
    printf("  [Check] Create/Destroy: OK\n");

    // 2. Single-threaded increment
    global_counter = nkit_pcounter_create();
    assert(global_counter != NULL);

    for (int i = 0; i < 1000; i++) {
        nkit_pcounter_inc(global_counter);
    }
    int64_t val = nkit_pcounter_read(global_counter);
    printf("  [Check] Single-thread: Expected 1000, Got %ld\n", (long)val);
    assert(val == 1000);

    // 3. Decrement
    for (int i = 0; i < 500; i++) {
        nkit_pcounter_dec(global_counter);
    }
    val = nkit_pcounter_read(global_counter);
    printf("  [Check] After dec: Expected 500, Got %ld\n", (long)val);
    assert(val == 500);

    // 4. Reset
    nkit_pcounter_reset(global_counter);
    val = nkit_pcounter_read(global_counter);
    printf("  [Check] After reset: Expected 0, Got %ld\n", (long)val);
    assert(val == 0);

    nkit_pcounter_destroy(global_counter);

    // 5. Multi-threaded contention
    global_counter = nkit_pcounter_create();
    assert(global_counter != NULL);

    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, worker_thread, NULL);
        assert(ret == 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int64_t expected = (int64_t)NUM_THREADS * INCREMENTS_PER_THREAD;
    val = nkit_pcounter_read(global_counter);
    printf("  [Check] Multi-thread: Expected %ld, Actual %ld\n",
           (long)expected, (long)val);
    assert(val == expected);

    nkit_pcounter_destroy(global_counter);

    printf("[UNIT] Partitioned Counter API Test Passed\n");
    return 0;
}
