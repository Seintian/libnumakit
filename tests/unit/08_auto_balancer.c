#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdatomic.h>

#include <numakit/numakit.h>
#include "unit.h"

// ============================================================================
// Test 1: Create / Destroy (Smoke Test)
// ============================================================================
static void test_ab_create_destroy(void) {
    nkit_auto_balancer_t *ab = nkit_auto_balancer_create(100, 50.0, NULL);
    // May be NULL if perf counters aren't available (VMs, containers)
    // but the create itself should not crash.
    if (ab) {
        assert(nkit_auto_balancer_migrations(ab) == 0);
        nkit_auto_balancer_destroy(ab);
    }
    printf("  [Check] Create/Destroy: OK\n");
}

// ============================================================================
// Test 2: Register / Unregister
// ============================================================================
static void test_ab_register_unregister(void) {
    nkit_auto_balancer_t *ab = nkit_auto_balancer_create(200, 50.0, NULL);
    if (!ab) {
        printf("  [Check] Register/Unregister: SKIP (no perf support)\n");
        return;
    }

    // Register this thread
    int ret = nkit_auto_balancer_register(ab, pthread_self(), 0);
    assert(ret == 0);

    // Unregister
    ret = nkit_auto_balancer_unregister(ab, pthread_self());
    assert(ret == 0);

    // Double unregister should fail
    ret = nkit_auto_balancer_unregister(ab, pthread_self());
    assert(ret == -1);

    nkit_auto_balancer_destroy(ab);
    printf("  [Check] Register/Unregister: OK\n");
}

// ============================================================================
// Test 3: Register Multiple Threads
// ============================================================================
static atomic_int worker_running;

static void *dummy_worker(void *arg) {
    (void)arg;
    // Spin until told to stop
    while (atomic_load(&worker_running)) {
        volatile int x = 0;
        for (int i = 0; i < 1000; i++) x += i;
        (void)x;
    }
    return NULL;
}

static void test_ab_multi_register(void) {
    nkit_auto_balancer_t *ab = nkit_auto_balancer_create(100, 50.0, NULL);
    if (!ab) {
        printf("  [Check] Multi-Register: SKIP (no perf support)\n");
        return;
    }

    atomic_store(&worker_running, 1);

    #define NUM_WORKERS 4
    pthread_t workers[NUM_WORKERS];

    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&workers[i], NULL, dummy_worker, NULL);
        int ret = nkit_auto_balancer_register(ab, workers[i], 0);
        assert(ret == 0);
    }

    // Let the balancer run for a short time
    usleep(300000); // 300ms

    // Unregister all
    for (int i = 0; i < NUM_WORKERS; i++) {
        int ret = nkit_auto_balancer_unregister(ab, workers[i]);
        assert(ret == 0);
    }

    atomic_store(&worker_running, 0);
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }

    nkit_auto_balancer_destroy(ab);
    printf("  [Check] Multi-Register: OK\n");
    #undef NUM_WORKERS
}

// ============================================================================
// Test 4: Callback Invocation
// ============================================================================
static atomic_int cb_called;

static void test_callback(pthread_t tid, int from_node, int to_node, double mpki) {
    (void)tid;
    (void)from_node;
    (void)to_node;
    (void)mpki;
    atomic_fetch_add(&cb_called, 1);
}

static void test_ab_callback(void) {
    atomic_store(&cb_called, 0);

    nkit_auto_balancer_t *ab = nkit_auto_balancer_create(50, 50.0, test_callback);
    if (!ab) {
        printf("  [Check] Callback: SKIP (no perf support)\n");
        return;
    }

    // Register a thread and let it run briefly
    nkit_auto_balancer_register(ab, pthread_self(), 0);
    usleep(200000); // 200ms
    nkit_auto_balancer_unregister(ab, pthread_self());

    nkit_auto_balancer_destroy(ab);

    // We don't assert that the callback was called because
    // migration depends on actual MPKI thresholds.
    // This test just verifies no crash with a callback installed.
    printf("  [Check] Callback: OK (invocations=%d)\n", atomic_load(&cb_called));
}

// ============================================================================
// Test 5: NULL Safety
// ============================================================================
static void test_ab_null_safety(void) {
    assert(nkit_auto_balancer_migrations(NULL) == 0);
    nkit_auto_balancer_destroy(NULL);

    // Invalid params
    assert(nkit_auto_balancer_create(0, 50.0, NULL) == NULL);    // 0 interval
    assert(nkit_auto_balancer_create(100, -1.0, NULL) == NULL);  // negative threshold
    assert(nkit_auto_balancer_create(100, 0.0, NULL) == NULL);   // zero threshold

    assert(nkit_auto_balancer_register(NULL, pthread_self(), 0) == -1);
    assert(nkit_auto_balancer_unregister(NULL, pthread_self()) == -1);

    printf("  [Check] NULL Safety: OK\n");
}

// ============================================================================
// Test 6: Registry Full
// ============================================================================
static void test_ab_registry_full(void) {
    nkit_auto_balancer_t *ab = nkit_auto_balancer_create(500, 50.0, NULL);
    if (!ab) {
        printf("  [Check] Registry Full: SKIP (no perf support)\n");
        return;
    }

    // Fill all 64 slots (AB_MAX_THREADS)
    for (int i = 0; i < 64; i++) {
        // Use i+1 as fake pthread_t values (just for registration; we won't
        // actually use these threads)
        int ret = nkit_auto_balancer_register(ab, (pthread_t)(uintptr_t)(i + 1), 0);
        assert(ret == 0);
    }

    // 65th registration should fail
    int ret = nkit_auto_balancer_register(ab, (pthread_t)(uintptr_t)999, 0);
    assert(ret == -1);

    nkit_auto_balancer_destroy(ab);
    printf("  [Check] Registry Full: OK\n");
}

// ============================================================================
// Entry Point
// ============================================================================
int test_08_auto_balancer(void) {
    printf("[UNIT] Auto-Balancer Test Started...\n");

    test_ab_create_destroy();
    test_ab_register_unregister();
    test_ab_multi_register();
    test_ab_callback();
    test_ab_null_safety();
    test_ab_registry_full();

    printf("[UNIT] Auto-Balancer Test Passed\n");
    return 0;
}
