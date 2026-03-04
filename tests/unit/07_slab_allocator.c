#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <numakit/numakit.h>
#include "unit.h"

// ============================================================================
// Test 1: Create / Destroy (Smoke Test)
// ============================================================================
static void test_slab_create_destroy(void) {
    nkit_slab_t *slab = nkit_slab_create(0, 128, 64);
    assert(slab != NULL);
    assert(nkit_slab_capacity(slab) == 64);
    assert(nkit_slab_available(slab) == 64);
    nkit_slab_destroy(slab);
    printf("  [Check] Create/Destroy: OK\n");
}

// ============================================================================
// Test 2: Alloc / Free (Single Object Round-Trip)
// ============================================================================
static void test_slab_alloc_free(void) {
    nkit_slab_t *slab = nkit_slab_create(0, 64, 16);
    assert(slab != NULL);

    void *obj = nkit_slab_alloc(slab);
    assert(obj != NULL);

    // Write to the memory to make sure it is valid
    memset(obj, 0xAB, 64);

    nkit_slab_free(slab, obj);
    nkit_slab_destroy(slab);
    printf("  [Check] Alloc/Free: OK\n");
}

// ============================================================================
// Test 3: Exhaust and Recover
// ============================================================================
static void test_slab_exhaust(void) {
    size_t cap = 32;
    nkit_slab_t *slab = nkit_slab_create(0, 128, cap);
    assert(slab != NULL);

    // Allocate everything
    void *ptrs[32];
    for (size_t i = 0; i < cap; i++) {
        ptrs[i] = nkit_slab_alloc(slab);
        assert(ptrs[i] != NULL);
    }

    // Next allocation must fail (exhausted)
    assert(nkit_slab_alloc(slab) == NULL);

    // Free one, then re-alloc must succeed
    nkit_slab_free(slab, ptrs[0]);
    void *recycled = nkit_slab_alloc(slab);
    assert(recycled != NULL);

    // Free everything
    for (size_t i = 1; i < cap; i++) {
        nkit_slab_free(slab, ptrs[i]);
    }
    nkit_slab_free(slab, recycled);

    nkit_slab_destroy(slab);
    printf("  [Check] Exhaust/Recover: OK\n");
}

// ============================================================================
// Test 4: Uniqueness (No Duplicate Pointers)
// ============================================================================
static void test_slab_uniqueness(void) {
    size_t cap = 64;
    nkit_slab_t *slab = nkit_slab_create(0, 256, cap);
    assert(slab != NULL);

    void *ptrs[64];
    for (size_t i = 0; i < cap; i++) {
        ptrs[i] = nkit_slab_alloc(slab);
        assert(ptrs[i] != NULL);
    }

    // Verify no two pointers are the same
    for (size_t i = 0; i < cap; i++) {
        for (size_t j = i + 1; j < cap; j++) {
            assert(ptrs[i] != ptrs[j]);
        }
    }

    for (size_t i = 0; i < cap; i++) {
        nkit_slab_free(slab, ptrs[i]);
    }
    nkit_slab_destroy(slab);
    printf("  [Check] Uniqueness: OK\n");
}

// ============================================================================
// Test 5: Alignment (All Pointers 64-byte Aligned)
// ============================================================================
static void test_slab_alignment(void) {
    size_t cap = 32;
    nkit_slab_t *slab = nkit_slab_create(0, 17, cap); // Odd size
    assert(slab != NULL);

    for (size_t i = 0; i < cap; i++) {
        void *p = nkit_slab_alloc(slab);
        assert(p != NULL);
        assert(((uintptr_t)p & 63) == 0); // Must be 64-byte aligned
        nkit_slab_free(slab, p);
    }

    nkit_slab_destroy(slab);
    printf("  [Check] Alignment: OK\n");
}

// ============================================================================
// Test 6: Write-After-Alloc Stress (Memory Validity)
// ============================================================================
static void test_slab_write_stress(void) {
    size_t cap = 128;
    nkit_slab_t *slab = nkit_slab_create(0, 512, cap);
    assert(slab != NULL);

    // Allocate all, write unique patterns, verify
    void *ptrs[128];
    for (size_t i = 0; i < cap; i++) {
        ptrs[i] = nkit_slab_alloc(slab);
        assert(ptrs[i] != NULL);
        memset(ptrs[i], (int)(i & 0xFF), 512);
    }

    // Verify patterns are intact (no overlap)
    for (size_t i = 0; i < cap; i++) {
        unsigned char *buf = (unsigned char *)ptrs[i];
        unsigned char expected = (unsigned char)(i & 0xFF);
        for (size_t b = 0; b < 512; b++) {
            assert(buf[b] == expected);
        }
    }

    for (size_t i = 0; i < cap; i++) {
        nkit_slab_free(slab, ptrs[i]);
    }
    nkit_slab_destroy(slab);
    printf("  [Check] Write Stress: OK\n");
}

// ============================================================================
// Test 7: Multi-Threaded Alloc/Free Contention
// ============================================================================
#define MT_THREADS 4
#define MT_ALLOCS  64

typedef struct {
    nkit_slab_t *slab;
    int          id;
} mt_ctx_t;

static void *mt_alloc_free_worker(void *arg) {
    mt_ctx_t *ctx = (mt_ctx_t *)arg;
    void *ptrs[MT_ALLOCS];

    // Allocate
    for (int i = 0; i < MT_ALLOCS; i++) {
        ptrs[i] = nkit_slab_alloc(ctx->slab);
        // May be NULL under contention; that's fine
        if (ptrs[i]) {
            memset(ptrs[i], ctx->id, 64);
        }
    }

    // Free
    for (int i = 0; i < MT_ALLOCS; i++) {
        if (ptrs[i]) {
            nkit_slab_free(ctx->slab, ptrs[i]);
        }
    }
    return NULL;
}

static void test_slab_multithread(void) {
    // capacity must hold worst-case: all threads allocating simultaneously
    size_t cap = 256; // MT_THREADS * MT_ALLOCS = 256
    nkit_slab_t *slab = nkit_slab_create(0, 64, cap);
    assert(slab != NULL);

    pthread_t threads[MT_THREADS];
    mt_ctx_t ctxs[MT_THREADS];

    for (int i = 0; i < MT_THREADS; i++) {
        ctxs[i].slab = slab;
        ctxs[i].id   = i;
        int ret = pthread_create(&threads[i], NULL, mt_alloc_free_worker, &ctxs[i]);
        assert(ret == 0);
    }

    for (int i = 0; i < MT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // After all threads finished, all objects should have been returned
    assert(nkit_slab_available(slab) == cap);

    nkit_slab_destroy(slab);
    printf("  [Check] Multi-thread Contention: OK\n");
}

// ============================================================================
// Test 8: NULL Safety
// ============================================================================
static void test_slab_null_safety(void) {
    // All functions must handle NULL gracefully
    assert(nkit_slab_alloc(NULL) == NULL);
    nkit_slab_free(NULL, NULL);
    nkit_slab_destroy(NULL);
    assert(nkit_slab_available(NULL) == 0);
    assert(nkit_slab_capacity(NULL) == 0);

    // Invalid params
    assert(nkit_slab_create(0, 0, 16) == NULL);   // obj_size = 0
    assert(nkit_slab_create(0, 64, 3) == NULL);   // not power of 2
    assert(nkit_slab_create(0, 64, 1) == NULL);   // < 2

    printf("  [Check] NULL Safety: OK\n");
}

// ============================================================================
// Entry Point
// ============================================================================
int test_07_slab_allocator(void) {
    printf("[UNIT] Slab Allocator Test Started...\n");

    test_slab_create_destroy();
    test_slab_alloc_free();
    test_slab_exhaust();
    test_slab_uniqueness();
    test_slab_alignment();
    test_slab_write_stress();
    test_slab_multithread();
    test_slab_null_safety();

    printf("[UNIT] Slab Allocator Test Passed\n");
    return 0;
}
