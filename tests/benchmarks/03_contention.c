#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <numakit/numakit.h>

#include "benchmarks.h"

#define TOTAL_OPS 1000000 // 1 Million Total Increments

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static int g_num_threads = 4;
static volatile long g_shared_counter = 0;
static pthread_barrier_t g_barrier;

// -----------------------------------------------------------------------------
// Baseline: Pthread Mutex
// -----------------------------------------------------------------------------

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* std_worker(void* arg) {
    long ops_for_this_thread = (long)arg;

    // Start simultaneously to maximize contention
    pthread_barrier_wait(&g_barrier);

    for (long i = 0; i < ops_for_this_thread; i++) {
        pthread_mutex_lock(&g_mutex);
        g_shared_counter++;
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// LibNumaKit: MCS Lock (Queue-Based Spinlock)
// -----------------------------------------------------------------------------

static nkit_mcs_lock_t g_mcs_lock;

static void* nkit_worker(void* arg) {
    long ops_for_this_thread = (long)arg;

    // Each thread needs its own queue node on its stack
    nkit_mcs_node_t my_node;

    pthread_barrier_wait(&g_barrier);

    for (long i = 0; i < ops_for_this_thread; i++) {
        // Acquire
        nkit_mcs_lock(&g_mcs_lock, &my_node);

        // Critical Section
        g_shared_counter++;

        // Release
        nkit_mcs_unlock(&g_mcs_lock, &my_node);
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int bench_03_contention(int argc, char** argv) {
    if (nkit_init() != 0) return 1;

    // Detect CPU count to set reasonable thread defaults
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    // We want enough threads to cause pain, but not oversubscribe the OS
    g_num_threads = (num_cores > 1) ? num_cores : 2;
    if (g_num_threads > 16) g_num_threads = 16; // Cap at 16 for readability

    if (argc > 2) {
        g_num_threads = atoi(argv[2]);
    }

    printf("=========================================================\n");
    printf(" BENCHMARK: Lock Contention (Mutex vs MCS)\n");
    printf(" Threads:   %d\n", g_num_threads);
    printf(" Total Ops: %d Million Increments\n", TOTAL_OPS / 1000000);
    printf("=========================================================\n");

    pthread_t* threads = malloc(sizeof(pthread_t) * g_num_threads);
    pthread_barrier_init(&g_barrier, NULL, g_num_threads);

    // Calculate ops distribution (Handle Remainder!)
    long base_ops = TOTAL_OPS / g_num_threads;
    long remainder = TOTAL_OPS % g_num_threads;

    // -------------------------------------------------
    // 1. Baseline: Standard Mutex
    // -------------------------------------------------

    printf("[Baseline] pthread_mutex (Kernel Arbitrated)...\n");
    g_shared_counter = 0;

    double start = get_time();
    for (int i = 0; i < g_num_threads; i++) {
        long ops_for_this_thread = base_ops + (i < remainder ? 1 : 0);
        pthread_create(&threads[i], NULL, std_worker, (void*)ops_for_this_thread);
    }
    for (int i = 0; i < g_num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    double end = get_time();

    double time_std = end - start;
    printf("  -> Time:  %.4f s\n", time_std);
    printf("  -> Count: %ld (Expected: %d) %s\n", g_shared_counter, TOTAL_OPS,
           (g_shared_counter == TOTAL_OPS) ? "OK" : "FAIL");

    // -------------------------------------------------
    // 2. LibNumaKit: MCS Lock
    // -------------------------------------------------

    printf("\n[LibNumaKit] MCS Lock (Local-Spinning)...\n");
    g_shared_counter = 0;
    nkit_mcs_init(&g_mcs_lock);

    start = get_time();
    for (int i = 0; i < g_num_threads; i++) {
        long ops_for_this_thread = base_ops + (i < remainder ? 1 : 0);
        pthread_create(&threads[i], NULL, nkit_worker, (void*)ops_for_this_thread);
    }
    for (int i = 0; i < g_num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    end = get_time();

    double time_nkit = end - start;
    printf("  -> Time:  %.4f s\n", time_nkit);
    printf("  -> Count: %ld (Expected: %d) %s\n", g_shared_counter, TOTAL_OPS,
           (g_shared_counter == TOTAL_OPS) ? "OK" : "FAIL");

    printf("\n---------------------------------------------------------\n");
    double speedup = time_std / time_nkit;
    printf(" SPEEDUP: %.2fx Faster\n", speedup);
    if (speedup > 1.0) {
        printf(" (MCS avoids cache-line bouncing and kernel sleeps)\n");
    } else {
        printf(" (Note: On low core counts, Pthread mutex is highly optimized)\n");
    }
    printf("---------------------------------------------------------\n");

    free(threads);
    pthread_barrier_destroy(&g_barrier);
    nkit_teardown();
    return 0;
}
