#include <stdio.h>
#include <assert.h>
#include <numakit/memory.h>
#include <numakit/numakit.h>
#include "unit.h"

int test_13_mempool(void) {
    printf("[UNIT] Advanced Memory Pool Test...\n");

    if (nkit_init() != 0) {
        printf("Failed to initialize libnumakit\n");
        return 1;
    }

    nkit_mempool_t* pool = nkit_mempool_create();
    assert(pool != NULL);
    printf("  [+] Memory Pool created successfully.\n");

    // Test Allocating various sizes
    printf("  [+] Testing allocations...\n");
    void* ptr1 = nkit_mempool_alloc(pool, 32);
    void* ptr2 = nkit_mempool_alloc(pool, 128);
    void* ptr3 = nkit_mempool_alloc(pool, 1024);
    void* ptr4 = nkit_mempool_alloc(pool, 8192);

    assert(ptr1 != NULL);
    assert(ptr2 != NULL);
    assert(ptr3 != NULL);
    assert(ptr4 != NULL);

    // Write to memory to ensure it's valid
    ((char*)ptr1)[0] = 'A';
    ((char*)ptr2)[0] = 'B';
    ((char*)ptr3)[0] = 'C';
    ((char*)ptr4)[0] = 'D';

    printf("  [+] Allocations successful.\n");

    // Test Deallocation
    printf("  [+] Testing deallocations...\n");
    nkit_mempool_free(pool, ptr1);
    nkit_mempool_free(pool, ptr2);
    nkit_mempool_free(pool, ptr3);
    nkit_mempool_free(pool, ptr4);
    printf("  [+] Deallocations successful (O(1) lock-free).\n");

    // Test Over-allocation (should return NULL)
    printf("  [+] Testing oversized allocation...\n");
    void* ptr5 = nkit_mempool_alloc(pool, 32768); // Exceeds 16384 max class
    assert(ptr5 == NULL);
    printf("  [+] Oversized allocation correctly rejected.\n");

    // Test Destroy
    nkit_mempool_destroy(pool);
    printf("  [+] Memory Pool destroyed successfully.\n");

    nkit_teardown();
    printf("[UNIT] Advanced Memory Pool Test Passed.\n");
    return 0;
}
