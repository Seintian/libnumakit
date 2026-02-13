#define _GNU_SOURCE

#include <assert.h>
#include <numa.h>
#include <numakit/numakit.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Increase slightly to ensuring ring wrapping occurs (Capacity 1024 vs 2000
// msgs)
#define ITERS 2000
#define RING_CAP 1024

typedef struct {
    int node_id;
    nkit_ring_t *ring;
} thread_arg_t;

// -----------------------------------------------------------------------------
// Worker Routines
// -----------------------------------------------------------------------------

void *producer(void *arg) {
    thread_arg_t *args = (thread_arg_t *)arg;

    // Attempt to pin, but don't crash if restricted (e.g. single core VM)
    nkit_bind_thread(args->node_id);

    for (size_t i = 0; i < ITERS; i++) {
        // Spin until space is available
        while (!nkit_ring_push(args->ring, (void *)(uintptr_t)i)) {
    #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
    #else
        sched_yield();
    #endif
        }
    }
    return NULL;
}

void *consumer(void *arg) {
    thread_arg_t *args = (thread_arg_t *)arg;
    nkit_bind_thread(args->node_id);

    size_t received = 0;
    void *data;

    while (received < ITERS) {
        // Spin until data is available
        if (nkit_ring_pop(args->ring, &data)) {
        // CRITICAL: Integrity Check
        if ((size_t)data != received) {
            fprintf(stderr,
                    "\n     [FATAL] Data Corruption! Expected %zu, Got %zu\n",
                    received, (size_t)data);
            exit(1);
        }
        received++;
        } else {
    #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
    #else
        sched_yield();
    #endif
        }
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Test Runner
// -----------------------------------------------------------------------------

void run_direction_test(int src, int dst, const char *name) {
    printf("  -> Testing %s (Node %d -> Node %d)...\n", name, src, dst);

    // 1. Create Ring on Destination Node (Standard practice: Push to remote, Pop
    // from local)
    nkit_ring_t *ring = nkit_ring_create(dst, RING_CAP);
    if (!ring) {
        printf("     [Skipped] Failed to create ring on Node %d (Hugepages "
            "missing?)\n",
            dst);
        return;
    }

    thread_arg_t t_src = {.node_id = src, .ring = ring};
    thread_arg_t t_dst = {.node_id = dst, .ring = ring};

    pthread_t p, c;
    pthread_create(&p, NULL, producer, &t_src);
    pthread_create(&c, NULL, consumer, &t_dst);

    pthread_join(p, NULL);
    pthread_join(c, NULL);

    printf("     Messages: %d/%d (Integrity: OK)\n", ITERS, ITERS);

    nkit_ring_free(ring);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int integration_02_cross_node_messaging(void) {
    printf("[INTEGRATION] Cross-Node Messaging Check...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    int max_node = numa_max_node(); // e.g., returns 1 for 2 nodes
    int num_nodes = max_node + 1;

    printf("  -> Configuration: %d NUMA Node(s) Detected\n", num_nodes);

    int node_a = 0;
    int node_b = (num_nodes > 1) ? 1 : 0;

    // 1. Test Forward Direction
    run_direction_test(node_a, node_b, "Forward");

    // 2. Test Reverse Direction (Only if true NUMA)
    if (node_a != node_b) {
        run_direction_test(node_b, node_a, "Reverse");
    } else {
        printf("  -> Reverse test skipped (Single Node System)\n");
    }

    nkit_teardown();
    printf("[INTEGRATION] Messaging Verification Passed.\n");
    return 0;
}
