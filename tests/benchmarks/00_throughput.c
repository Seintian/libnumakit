#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <numakit/numakit.h>
#include <numa.h>

#include "benchmarks.h"

#define NUM_MSGS (1 * 1000 * 1000)
#define BATCH_SIZE 64

// Configuration
static int g_producer_node = 0;
static int g_consumer_node = 0;

// -----------------------------------------------------------------------------
// Baseline: Standard Mutex + Condition Variable
// -----------------------------------------------------------------------------

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    void* buffer[1024];
    int    count;
    int    head;
    int    tail;
    int    done;
} std_queue_t;

static std_queue_t g_std_queue;

static void std_push(std_queue_t* q, void* item) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 1024) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    q->buffer[q->head] = item;
    q->head = (q->head + 1) % 1024;
    q->count++;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

static void* std_pop(std_queue_t* q) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->done) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    if (q->count == 0 && q->done) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }
    void* item = q->buffer[q->tail];
    q->tail = (q->tail + 1) % 1024;
    q->count--;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return item;
}

static void* baseline_producer(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_producer_node);

    for (size_t i = 0; i < NUM_MSGS; i++) {
        std_push(&g_std_queue, (void*)(uintptr_t)i);
    }

    pthread_mutex_lock(&g_std_queue.lock);
    g_std_queue.done = 1;
    pthread_cond_broadcast(&g_std_queue.cond);
    pthread_mutex_unlock(&g_std_queue.lock);
    return NULL;
}

static void* baseline_consumer(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_consumer_node);

    size_t received = 0;
    while (1) {
        void* item = std_pop(&g_std_queue);
        if (item == NULL && g_std_queue.done && g_std_queue.count == 0) break;
        if (item != NULL) received++;
    }
    (void)received;
    return NULL;
}

// -----------------------------------------------------------------------------
// LibNumaKit: Lock-Free Ring Buffer
// -----------------------------------------------------------------------------

static nkit_ring_t* g_nkit_ring;

static void* nkit_producer(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_producer_node);

    for (size_t i = 0; i < NUM_MSGS; i++) {
        while (!nkit_ring_push(g_nkit_ring, (void*)(uintptr_t)i)) {
            __builtin_ia32_pause();
        }
    }
    return NULL;
}

static void* nkit_consumer(__attribute__((unused)) void* arg) {
    nkit_bind_thread(g_consumer_node);

    size_t received = 0;
    void* data;

    while (received < NUM_MSGS) {
        if (nkit_ring_pop(g_nkit_ring, &data)) {
            received++;
        } else {
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

int bench_00_throughput(__attribute__((unused)) int argc, __attribute__((unused)) char** argv) {
    if (nkit_init() != 0) {
        printf("Failed to init numakit\n");
        return 1;
    }

    // ADAPTIVE NODE DETECTION
    int max_node = numa_max_node();
    g_producer_node = 0;
    g_consumer_node = (max_node > 0) ? 1 : 0;

    printf("=========================================================\n");
    if (g_consumer_node != g_producer_node) {
        printf(" BENCHMARK: Cross-Node Throughput (Node %d -> Node %d)\n", g_producer_node, g_consumer_node);
    } else {
        printf(" BENCHMARK: Single-Node Throughput (Node %d -> Node %d)\n", g_producer_node, g_consumer_node);
        printf(" (Running on single-socket machine: measuring thread-safety overhead only)\n");
    }
    printf(" Messages: %d Million\n", NUM_MSGS / 1000000);
    printf("=========================================================\n");

    pthread_t p, c;

    // 1. Run Baseline
    printf("[Baseline] Standard Mutex + CondVar...\n");
    
    pthread_mutex_init(&g_std_queue.lock, NULL);
    pthread_cond_init(&g_std_queue.cond, NULL);
    g_std_queue.head = 0;
    g_std_queue.tail = 0;
    g_std_queue.count = 0;
    g_std_queue.done = 0;

    double start = get_time();
    pthread_create(&p, NULL, baseline_producer, NULL);
    pthread_create(&c, NULL, baseline_consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    double end = get_time();
    
    double baseline_ops = NUM_MSGS / (end - start);
    printf("  -> Time: %.4f s\n", end - start);
    printf("  -> Ops:  %.2f M/sec\n", baseline_ops / 1e6);

    // 2. Run Numakit
    printf("\n[LibNumaKit] Lock-Free Ring (SPSC)...\n");

    // Create ring on the CONSUMER node (always best practice)
    g_nkit_ring = nkit_ring_create(g_consumer_node, 4096); 
    if (!g_nkit_ring) {
        printf("Failed to create ring. (Check Hugepage settings?)\n");
        return 1;
    }

    start = get_time();
    pthread_create(&p, NULL, nkit_producer, NULL);
    pthread_create(&c, NULL, nkit_consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    end = get_time();

    double nkit_ops = NUM_MSGS / (end - start);
    printf("  -> Time: %.4f s\n", end - start);
    printf("  -> Ops:  %.2f M/sec\n", nkit_ops / 1e6);

    printf("\n---------------------------------------------------------\n");
    printf(" SPEEDUP: %.2fx\n", nkit_ops / baseline_ops);
    printf("---------------------------------------------------------\n");

    nkit_ring_free(g_nkit_ring);
    nkit_teardown();
    return 0;
}
