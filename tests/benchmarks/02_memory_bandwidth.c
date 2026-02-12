#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <numakit/numakit.h>
#include <numa.h>

#include "benchmarks.h"

// 64 MB Payload
#define DATA_SIZE (64 * 1024 * 1024)
#define ITERATIONS 10

// -----------------------------------------------------------------------------
// Configuration & Globals
// -----------------------------------------------------------------------------

static int g_node_src = 0;
static int g_node_dst = 0;

static pthread_barrier_t g_barrier_start;
static pthread_barrier_t g_barrier_done;

// -----------------------------------------------------------------------------
// Helper: Check Transparent Huge Pages (THP)
// -----------------------------------------------------------------------------

static int is_thp_enabled(void) {
    FILE* f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
    if (!f) return 0; // Assume disabled or non-Linux

    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        // format is usually "[always] madvise never" or "always [madvise] never"
        if (strstr(buf, "[always]")) {
            fclose(f);
            return 1; // THP is active globally
        }
    }
    fclose(f);
    return 0;
}

// -----------------------------------------------------------------------------
// Baseline: Standard Malloc
// -----------------------------------------------------------------------------

static volatile uint8_t* g_std_buffer;

static void* std_writer_thread(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_src);
    pthread_barrier_wait(&g_barrier_start);

    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t* ptr = (uint64_t*)g_std_buffer;
        size_t count = DATA_SIZE / sizeof(uint64_t);
        uint64_t pattern = 0xAAAAAAAAAAAAAAAA;

        for (size_t j = 0; j < count; j++) {
            ptr[j] = pattern;
        }
        pthread_barrier_wait(&g_barrier_done); 
        pthread_barrier_wait(&g_barrier_done); 
    }
    return NULL;
}

static void* std_reader_thread(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_dst);
    pthread_barrier_wait(&g_barrier_start);
    long long sum = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        pthread_barrier_wait(&g_barrier_done); 
        uint64_t* ptr = (uint64_t*)g_std_buffer;
        size_t count = DATA_SIZE / sizeof(uint64_t);
        for (size_t j = 0; j < count; j++) {
            sum += ptr[j];
        }
        pthread_barrier_wait(&g_barrier_done);
    }
    return (void*) sum;
}

// -----------------------------------------------------------------------------
// LibNumaKit: Arena (Hugepages)
// -----------------------------------------------------------------------------

static nkit_arena_t* g_arena;
static volatile uint8_t* g_nkit_buffer;

static void* nkit_writer_thread(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_src);
    pthread_barrier_wait(&g_barrier_start);

    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t* ptr = (uint64_t*)g_nkit_buffer;
        size_t count = DATA_SIZE / sizeof(uint64_t);
        uint64_t pattern = 0xBBBBBBBBBBBBBBBB;

        for (size_t j = 0; j < count; j++) {
            ptr[j] = pattern;
        }
        pthread_barrier_wait(&g_barrier_done);
        pthread_barrier_wait(&g_barrier_done);
    }
    return NULL;
}

static void* nkit_reader_thread(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_node_dst);
    pthread_barrier_wait(&g_barrier_start);
    long long sum = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        pthread_barrier_wait(&g_barrier_done);
        uint64_t* ptr = (uint64_t*)g_nkit_buffer;
        size_t count = DATA_SIZE / sizeof(uint64_t);
        for (size_t j = 0; j < count; j++) {
            sum += ptr[j];
        }
        pthread_barrier_wait(&g_barrier_done);
    }
    return (void*)sum;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int bench_02_bandwidth(__attribute__((unused)) int argc, __attribute__((unused)) char** argv) {
    if (nkit_init() != 0) return 1;

    int max_node = numa_max_node();
    int is_uma = (max_node == 0);
    int thp_on = is_thp_enabled();

    g_node_src = 0;
    g_node_dst = (is_uma) ? 0 : 1;

    printf("=========================================================\n");
    printf(" BENCHMARK: Memory Bandwidth (Hugepages vs 4K Pages)\n");
    printf(" Nodes:     %d -> %d\n", g_node_src, g_node_dst);
    printf(" Data Size: %d MB per Iteration\n", DATA_SIZE / 1024 / 1024);
    printf(" Total:     %ld GB Transferred\n", (long)DATA_SIZE * ITERATIONS / 1024 / 1024 / 1024);

    // -------------------------------------------------------------------------
    // SYSTEM ANALYSIS & WARNINGS
    // -------------------------------------------------------------------------
    if (is_uma) {
        printf("\n [INFO] System is UMA (Single Socket).\n");
        printf("        Traffic does not cross QPI/UPI interconnects.\n");
        printf("        Speedup will be limited to TLB efficiency only.\n");
    } else {
        printf("\n [INFO] System is NUMA (Multi-Socket).\n");
        printf("        Traffic crosses interconnect. Explicit pinning is critical.\n");
    }

    if (thp_on) {
        printf(" [WARN] Transparent Huge Pages (THP) is '[always]'.\n");
        printf("        The OS might automatically upgrade 'malloc' to hugepages.\n");
        printf("        This will hide the benefit of manual Arena allocation.\n");
        printf("        Run: 'echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled'\n");
        printf("        to see the true cost of standard 4KB pages.\n");
    }
    printf("=========================================================\n");

    pthread_t t1, t2;
    pthread_barrier_init(&g_barrier_start, NULL, 2);
    pthread_barrier_init(&g_barrier_done, NULL, 2);

    // 1. Baseline
    printf("[Baseline] Standard malloc...\n");
    g_std_buffer = malloc(DATA_SIZE);
    if (!g_std_buffer) { printf("Malloc failed\n"); return 1; }

    nkit_bind_thread(g_node_src);
    memset((void*)g_std_buffer, 0, DATA_SIZE); // Fault in pages

    double start = get_time();
    pthread_create(&t1, NULL, std_writer_thread, NULL);
    pthread_create(&t2, NULL, std_reader_thread, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    double end = get_time();

    double bytes_moved = (double)DATA_SIZE * ITERATIONS;
    double gb_s_std = (bytes_moved / (1024*1024*1024)) / (end - start);

    printf("  -> Time: %.4f s\n", end - start);
    printf("  -> BW:   %.2f GB/s\n", gb_s_std);

    free((void*)g_std_buffer);


    // 2. LibNumaKit
    printf("\n[LibNumaKit] Arena Allocator (2MB Hugepages)...\n");

    g_arena = nkit_arena_create(g_node_src, DATA_SIZE + 1024);
    if (!g_arena) { 
        printf("Arena creation failed (Check Hugepages)\n"); 
        return 1; 
    }
    g_nkit_buffer = nkit_arena_alloc(g_arena, DATA_SIZE);

    start = get_time();
    pthread_create(&t1, NULL, nkit_writer_thread, NULL);
    pthread_create(&t2, NULL, nkit_reader_thread, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    end = get_time();

    double gb_s_nkit = (bytes_moved / (1024*1024*1024)) / (end - start);

    printf("  -> Time: %.4f s\n", end - start);
    printf("  -> BW:   %.2f GB/s\n", gb_s_nkit);

    printf("\n---------------------------------------------------------\n");
    double diff = (gb_s_nkit / gb_s_std - 1.0) * 100.0;
    printf(" SPEEDUP: %+.2f%%\n", diff);

    if (diff < 1.0 && is_uma) {
        printf(" (NOTE: Zero speedup is expected on UMA + Sequential Access)\n");
    }
    printf("---------------------------------------------------------\n");

    nkit_arena_destroy(g_arena);
    pthread_barrier_destroy(&g_barrier_start);
    pthread_barrier_destroy(&g_barrier_done);
    nkit_teardown();

    return 0;
}
