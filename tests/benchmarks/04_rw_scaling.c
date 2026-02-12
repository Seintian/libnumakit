#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <numakit/numakit.h>

#include "benchmarks.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define RUN_TIME_SEC 1
#define READ_RATIO 90  // 90% Reads, 10% Writes

static int g_num_threads = 8;
static volatile int g_running = 1;
static pthread_barrier_t g_barrier;

// The Shared Data
static volatile long g_shared_counter = 0;

// -----------------------------------------------------------------------------
// Baseline: Pthread RW Lock
// -----------------------------------------------------------------------------

static pthread_rwlock_t g_std_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void* std_worker(void* arg) {
    long ops = 0;
    unsigned int seed = (uintptr_t) arg; // thread-local seed

    pthread_barrier_wait(&g_barrier);

    while (g_running) {
        int r = rand_r(&seed) % 100;

        if (r < READ_RATIO) {
            // READ
            pthread_rwlock_rdlock(&g_std_rwlock);
            volatile long val = g_shared_counter; // Force read
            (void)val;
            pthread_rwlock_unlock(&g_std_rwlock);
        } else {
            // WRITE
            pthread_rwlock_wrlock(&g_std_rwlock);
            g_shared_counter++;
            pthread_rwlock_unlock(&g_std_rwlock);
        }
        ops++;
    }
    return (void*)ops;
}

// -----------------------------------------------------------------------------
// LibNumaKit: Reader-Writer Spinlock
// -----------------------------------------------------------------------------
static nkit_rws_lock_t g_nkit_rwlock;

static void* nkit_worker(void* arg) {
    long ops = 0;
    unsigned int seed = (uintptr_t) arg;

    pthread_barrier_wait(&g_barrier);

    while (g_running) {
        int r = rand_r(&seed) % 100;

        if (r < READ_RATIO) {
            // READ
            nkit_rws_read_lock(&g_nkit_rwlock);
            volatile long val = g_shared_counter;
            (void)val;
            nkit_rws_read_unlock(&g_nkit_rwlock);
        } else {
            // WRITE
            nkit_rws_write_lock(&g_nkit_rwlock);
            g_shared_counter++;
            nkit_rws_write_unlock(&g_nkit_rwlock);
        }
        ops++;
    }
    return (void*)ops;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

int bench_04_rw_scaling(int argc, char** argv) {
    if (nkit_init() != 0) return 1;

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    g_num_threads = (num_cores > 1) ? num_cores : 4;
    if (argc > 2) g_num_threads = atoi(argv[2]);

    printf("=========================================================\n");
    printf(" BENCHMARK: Reader/Writer Scaling (90%% Read / 10%% Write)\n");
    printf(" Threads:   %d\n", g_num_threads);
    printf(" Duration:  %d seconds per test\n", RUN_TIME_SEC);
    printf("=========================================================\n");

    pthread_t* threads = malloc(sizeof(pthread_t) * g_num_threads);
    pthread_barrier_init(&g_barrier, NULL, g_num_threads + 1); // +1 for main thread

    // -------------------------------------------------
    // 1. Baseline: Pthread RWLock
    // -------------------------------------------------
    printf("[Baseline] pthread_rwlock (Kernel)...\n");
    g_running = 1;
    g_shared_counter = 0;

    for (long i = 0; i < g_num_threads; i++) {
        pthread_create(&threads[i], NULL, std_worker, (void*)i);
    }

    // Start!
    pthread_barrier_wait(&g_barrier);
    sleep(RUN_TIME_SEC);
    g_running = 0;

    long total_std_ops = 0;
    for (int i = 0; i < g_num_threads; i++) {
        void* ret;
        pthread_join(threads[i], &ret);
        total_std_ops += (long)ret;
    }

    printf("  -> Total Ops: %ld\n", total_std_ops);
    printf("  -> Throughput: %.2f M/ops\n", (double)total_std_ops / RUN_TIME_SEC / 1000000.0);

    // -------------------------------------------------
    // 2. LibNumaKit: RW Spinlock
    // -------------------------------------------------

    printf("\n[LibNumaKit] RWS Spinlock (User-Space)...\n");
    nkit_rws_init(&g_nkit_rwlock);
    g_running = 1;
    g_shared_counter = 0;

    // Re-init barrier
    pthread_barrier_destroy(&g_barrier);
    pthread_barrier_init(&g_barrier, NULL, g_num_threads + 1);

    for (long i = 0; i < g_num_threads; i++) {
        pthread_create(&threads[i], NULL, nkit_worker, (void*)i);
    }

    pthread_barrier_wait(&g_barrier);
    sleep(RUN_TIME_SEC);
    g_running = 0;

    long total_nkit_ops = 0;
    for (int i = 0; i < g_num_threads; i++) {
        void* ret;
        pthread_join(threads[i], &ret);
        total_nkit_ops += (long)ret;
    }

    printf("  -> Total Ops: %ld\n", total_nkit_ops);
    printf("  -> Throughput: %.2f M/ops\n", (double) total_nkit_ops / RUN_TIME_SEC / 1000000.0);

    printf("\n---------------------------------------------------------\n");
    double speedup = (double) total_nkit_ops / total_std_ops;
    printf(" SPEEDUP: %.2fx\n", speedup);
    printf("---------------------------------------------------------\n");

    free(threads);
    pthread_barrier_destroy(&g_barrier);
    return 0;
}
