#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <numakit/numakit.h>
#include <numa.h>

#include "benchmarks.h"

#define NUM_ROUNDTRIPS 50000

// Configuration
static int g_node_a = 0;
static int g_node_b = 0;

// -----------------------------------------------------------------------------
// Baseline: Standard Mutex + CondVar (Ping-Pong)
// -----------------------------------------------------------------------------

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    long data;     // Payload
    int has_data;
} std_mailbox_t;

static std_mailbox_t g_std_a_to_b;
static std_mailbox_t g_std_b_to_a;

static void std_send(std_mailbox_t* mb, long val) {
    pthread_mutex_lock(&mb->lock);
    while (mb->has_data) {
        pthread_cond_wait(&mb->cond, &mb->lock);
    }
    mb->data = val;
    mb->has_data = 1;
    pthread_cond_signal(&mb->cond);
    pthread_mutex_unlock(&mb->lock);
}

static long std_recv(std_mailbox_t* mb) {
    pthread_mutex_lock(&mb->lock);
    while (!mb->has_data) {
        pthread_cond_wait(&mb->cond, &mb->lock);
    }
    long val = mb->data;
    mb->has_data = 0;
    pthread_cond_signal(&mb->cond);
    pthread_mutex_unlock(&mb->lock);
    return val;
}

static void* std_thread_a(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_a);
    long checksum = 0;

    for (long i = 0; i < NUM_ROUNDTRIPS; i++) {
        // Ping
        std_send(&g_std_a_to_b, i);

        // Wait for Pong
        long reply = std_recv(&g_std_b_to_a);

        // VALIDATION:
        if (reply != i) {
            fprintf(stderr, "FATAL: Baseline mismatch! Sent %ld, got %ld\n", i, reply);
            exit(1);
        }
        checksum += reply;
    }
    return (void*)checksum;
}

static void* std_thread_b(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_b);

    for (long i = 0; i < NUM_ROUNDTRIPS; i++) {
        long val = std_recv(&g_std_a_to_b);
        std_send(&g_std_b_to_a, val);
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// LibNumaKit: Lock-Free Ring Buffer (Ping-Pong)
// -----------------------------------------------------------------------------

static nkit_ring_t* g_ring_a_to_b;
static nkit_ring_t* g_ring_b_to_a;

static void* nkit_thread_a(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_a);
    long checksum = 0;
    void* data;

    for (long i = 0; i < NUM_ROUNDTRIPS; i++) {
        // 1. Send Ping (Cast long directly to pointer)
        while (!nkit_ring_push(g_ring_a_to_b, (void*)i)) {
            __builtin_ia32_pause();
        }

        // 2. Wait for Pong
        while (!nkit_ring_pop(g_ring_b_to_a, &data)) {
            __builtin_ia32_pause();
        }

        // VALIDATION:
        long reply = (long)data;
        if (reply != i) {
            fprintf(stderr, "FATAL: NumaKit mismatch! Sent %ld, got %ld\n", i, reply);
            exit(1);
        }
        checksum += reply;
    }
    return (void*)checksum;
}

static void* nkit_thread_b(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_b);
    void* data;

    for (long i = 0; i < NUM_ROUNDTRIPS; i++) {
        // 1. Wait for Ping
        while (!nkit_ring_pop(g_ring_a_to_b, &data)) {
            __builtin_ia32_pause();
        }

        // 2. Send Pong (echo data back)
        while (!nkit_ring_push(g_ring_b_to_a, data)) {
            __builtin_ia32_pause();
        }
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

int bench_01_latency(__attribute__((unused)) int argc, __attribute__((unused)) char** argv) {
    if (nkit_init() != 0) return 1;

    int max_node = numa_max_node();
    g_node_a = 0;
    g_node_b = (max_node > 0) ? 1 : 0;

    printf("=========================================================\n");
    printf(" BENCHMARK: Round-Trip Latency (Hardened Verification)\n");
    printf(" Nodes:    %d <-> %d\n", g_node_a, g_node_b);
    printf(" Messages: %d Million\n", NUM_ROUNDTRIPS / 1000000);
    printf("=========================================================\n");

    pthread_t t1, t2;
    void *res1;

    // 1. Baseline
    printf("[Baseline] Mutex + CondVar...\n");

    pthread_mutex_init(&g_std_a_to_b.lock, NULL);
    pthread_cond_init(&g_std_a_to_b.cond, NULL);
    g_std_a_to_b.has_data = 0;

    pthread_mutex_init(&g_std_b_to_a.lock, NULL);
    pthread_cond_init(&g_std_b_to_a.cond, NULL);
    g_std_b_to_a.has_data = 0;

    double start = get_time();
    pthread_create(&t1, NULL, std_thread_a, NULL);
    pthread_create(&t2, NULL, std_thread_b, NULL);

    pthread_join(t1, &res1);
    pthread_join(t2, NULL);
    double end = get_time();

    long base_check = (long)res1;
    printf("  -> Checksum: %ld (Expected: %ld) %s\n", base_check, (long)NUM_ROUNDTRIPS*(NUM_ROUNDTRIPS-1)/2, 
           (base_check == (long)NUM_ROUNDTRIPS*(NUM_ROUNDTRIPS-1)/2) ? "OK" : "FAIL");

    double time_std = (end - start);
    double latency_std = (time_std / NUM_ROUNDTRIPS) * 1e9;
    printf("  -> Avg RTT:  %.0f ns\n", latency_std);


    // 2. LibNumaKit
    printf("\n[LibNumaKit] Lock-Free Ring...\n");

    g_ring_a_to_b = nkit_ring_create(g_node_b, 4096);
    g_ring_b_to_a = nkit_ring_create(g_node_a, 4096);

    if (!g_ring_a_to_b || !g_ring_b_to_a) {
        printf("Failed to create rings\n");
        return 1;
    }

    start = get_time();
    pthread_create(&t1, NULL, nkit_thread_a, NULL);
    pthread_create(&t2, NULL, nkit_thread_b, NULL);

    pthread_join(t1, &res1);
    pthread_join(t2, NULL);
    end = get_time();

    long nkit_check = (long)res1;
    printf("  -> Checksum: %ld (Expected: %ld) %s\n", nkit_check, (long)NUM_ROUNDTRIPS*(NUM_ROUNDTRIPS-1)/2, 
           (nkit_check == (long)NUM_ROUNDTRIPS*(NUM_ROUNDTRIPS-1)/2) ? "OK" : "FAIL");

    double time_nkit = (end - start);
    double latency_nkit = (time_nkit / NUM_ROUNDTRIPS) * 1e9;
    printf("  -> Avg RTT:  %.0f ns\n", latency_nkit);

    printf("\n---------------------------------------------------------\n");
    printf(" LATENCY REDUCTION: %.1fx Lower\n", latency_std / latency_nkit);
    printf("---------------------------------------------------------\n");

    nkit_teardown();
    return 0;
}
