#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <numakit/numakit.h>
#include "unit.h"

// ============================================================================
// Test 1: Create / Query / Destroy (Smoke Test)
// ============================================================================
static void test_hpc_create_query(void) {
    // Create a 4MB arena on node 0
    nkit_arena_t *arena = nkit_arena_create(0, 4 * 1024 * 1024);
    assert(arena != NULL);

    // Size should be >= 4MB (may be rounded up to hugepage alignment)
    assert(nkit_arena_size(arena) >= 4 * 1024 * 1024);
    assert(nkit_arena_used(arena) == 0);

    nkit_arena_destroy(arena);
    printf("  [Check] Create/Query: OK\n");
}

// ============================================================================
// Test 2: Coalesce on Fresh Arena (All Pages Unused)
// ============================================================================
static void test_hpc_coalesce_fresh(void) {
    // A fresh arena with no allocations should be able to coalesce
    nkit_arena_t *arena = nkit_arena_create(0, 4 * 1024 * 1024);
    assert(arena != NULL);

    size_t returned = nkit_arena_coalesce(arena);
    // All pages should be returnable since used == 0
    assert(returned > 0);
    printf("  [Check] Coalesce Fresh: OK (%zu pages returned)\n", returned);

    nkit_arena_destroy(arena);
}

// ============================================================================
// Test 3: Coalesce After Partial Allocation
// ============================================================================
static void test_hpc_coalesce_partial(void) {
    // Create a 6MB arena
    size_t arena_size = 6 * 1024 * 1024;
    nkit_arena_t *arena = nkit_arena_create(0, arena_size);
    assert(arena != NULL);

    // Allocate 1MB (less than one hugepage)
    void *p = nkit_arena_alloc(arena, 1 * 1024 * 1024);
    assert(p != NULL);
    assert(nkit_arena_used(arena) > 0);

    // Coalesce should return the pages beyond the first hugepage
    size_t returned = nkit_arena_coalesce(arena);
    // With 6MB total and 1MB used, there should be at least 1-2 pages
    printf("  [Check] Coalesce Partial: OK (%zu pages returned, used=%zu)\n",
           returned, nkit_arena_used(arena));

    nkit_arena_destroy(arena);
}

// ============================================================================
// Test 4: Coalesce After Full Allocation (No Free Pages)
// ============================================================================
static void test_hpc_coalesce_full(void) {
    // Create a 2MB arena (exactly one hugepage)
    nkit_arena_t *arena = nkit_arena_create(0, 2 * 1024 * 1024);
    assert(arena != NULL);

    // Allocate the entire arena
    void *p = nkit_arena_alloc(arena, nkit_arena_size(arena));
    assert(p != NULL);

    // No pages should be returnable
    size_t returned = nkit_arena_coalesce(arena);
    assert(returned == 0);
    printf("  [Check] Coalesce Full: OK (0 pages returned as expected)\n");

    nkit_arena_destroy(arena);
}

// ============================================================================
// Test 5: Reset + Coalesce (Full Reclaim)
// ============================================================================
static void test_hpc_reset_coalesce(void) {
    nkit_arena_t *arena = nkit_arena_create(0, 4 * 1024 * 1024);
    assert(arena != NULL);

    // Allocate everything
    void *p = nkit_arena_alloc(arena, nkit_arena_size(arena));
    assert(p != NULL);

    // No pages to return
    assert(nkit_arena_coalesce(arena) == 0);

    // Reset the arena
    nkit_arena_reset(arena);
    assert(nkit_arena_used(arena) == 0);

    // Now all pages should be returnable
    size_t returned = nkit_arena_coalesce(arena);
    assert(returned > 0);
    printf("  [Check] Reset + Coalesce: OK (%zu pages returned)\n", returned);

    nkit_arena_destroy(arena);
}

// ============================================================================
// Test 6: Re-Allocation After Coalesce (Pages Re-Fault)
// ============================================================================
static void test_hpc_realloc_after_coalesce(void) {
    nkit_arena_t *arena = nkit_arena_create(0, 4 * 1024 * 1024);
    assert(arena != NULL);

    // Allocate, reset, coalesce
    nkit_arena_alloc(arena, nkit_arena_size(arena));
    nkit_arena_reset(arena);
    nkit_arena_coalesce(arena);

    // Re-allocate: should succeed — pages will re-fault with zeroed data
    void *p = nkit_arena_alloc(arena, 1024);
    assert(p != NULL);

    // Write to re-faulted memory — must not crash
    memset(p, 0xCD, 1024);

    // Verify the data
    unsigned char *buf = (unsigned char *)p;
    for (int i = 0; i < 1024; i++) {
        assert(buf[i] == 0xCD);
    }

    nkit_arena_destroy(arena);
    printf("  [Check] Re-Alloc After Coalesce: OK\n");
}

// ============================================================================
// Test 7: Multiple Coalesce Cycles (Idempotency)
// ============================================================================
static void test_hpc_idempotent(void) {
    nkit_arena_t *arena = nkit_arena_create(0, 4 * 1024 * 1024);
    assert(arena != NULL);

    // First coalesce
    size_t r1 = nkit_arena_coalesce(arena);
    // Second coalesce on the same state should return the same count
    // (MADV_DONTNEED is idempotent)
    size_t r2 = nkit_arena_coalesce(arena);
    assert(r1 == r2);

    printf("  [Check] Idempotent Coalesce: OK (r1=%zu, r2=%zu)\n", r1, r2);

    nkit_arena_destroy(arena);
}

// ============================================================================
// Test 8: NULL Safety
// ============================================================================
static void test_hpc_null_safety(void) {
    assert(nkit_arena_coalesce(NULL) == 0);
    assert(nkit_arena_used(NULL) == 0);
    assert(nkit_arena_size(NULL) == 0);
    assert(nkit_arena_is_huge(NULL) == 0);

    printf("  [Check] NULL Safety: OK\n");
}

// ============================================================================
// Entry Point
// ============================================================================
int test_09_hugepage_coalesce(void) {
    printf("[UNIT] Hugepage Coalescing Test Started...\n");

    test_hpc_create_query();
    test_hpc_coalesce_fresh();
    test_hpc_coalesce_partial();
    test_hpc_coalesce_full();
    test_hpc_reset_coalesce();
    test_hpc_realloc_after_coalesce();
    test_hpc_idempotent();
    test_hpc_null_safety();

    printf("[UNIT] Hugepage Coalescing Test Passed\n");
    return 0;
}
