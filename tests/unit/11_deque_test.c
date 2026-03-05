#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <numakit/numakit.h>
#include <numakit/structs/deque.h>
#include "unit.h"

static void test_deque_basic(void) {
    printf("  [Check] Basic Push/Pop/Size\n");
    nkit_deque_t* dq = nkit_deque_create(0, 1024);
    assert(dq != NULL);
    assert(nkit_deque_size(dq) == 0);

    int val1 = 1, val2 = 2, val3 = 3;
    void* out = NULL;

    // Push 3 elements
    assert(nkit_deque_push(dq, &val1));
    assert(nkit_deque_push(dq, &val2));
    assert(nkit_deque_push(dq, &val3));
    assert(nkit_deque_size(dq) == 3);

    // Pop should be LIFO
    assert(nkit_deque_pop(dq, &out));
    assert(out == &val3);
    assert(nkit_deque_size(dq) == 2);

    assert(nkit_deque_pop(dq, &out));
    assert(out == &val2);
    assert(nkit_deque_size(dq) == 1);

    assert(nkit_deque_pop(dq, &out));
    assert(out == &val1);
    assert(nkit_deque_size(dq) == 0);

    // Pop from empty
    assert(!nkit_deque_pop(dq, &out));

    nkit_deque_destroy(dq);
}

static void test_deque_steal(void) {
    printf("  [Check] Steal (FIFO)\n");
    nkit_deque_t* dq = nkit_deque_create(0, 1024);
    
    int val1 = 1, val2 = 2, val3 = 3;
    void* out = NULL;

    assert(nkit_deque_push(dq, &val1));
    assert(nkit_deque_push(dq, &val2));
    assert(nkit_deque_push(dq, &val3));

    // Steal should be FIFO
    assert(nkit_deque_steal(dq, &out));
    assert(out == &val1);
    assert(nkit_deque_size(dq) == 2);

    assert(nkit_deque_steal(dq, &out));
    assert(out == &val2);
    assert(nkit_deque_size(dq) == 1);

    // Pop the last one
    assert(nkit_deque_pop(dq, &out));
    assert(out == &val3);
    assert(nkit_deque_size(dq) == 0);

    // Steal from empty
    assert(!nkit_deque_steal(dq, &out));

    nkit_deque_destroy(dq);
}

static void test_deque_race_simulation(void) {
    printf("  [Check] Pop/Steal Race Simulation\n");
    nkit_deque_t* dq = nkit_deque_create(0, 1024);
    
    int val = 42;
    void* out = NULL;

    assert(nkit_deque_push(dq, &val));
    assert(nkit_deque_size(dq) == 1);

    // In a real scenario, pop and steal might happen concurrently.
    // Here we just verify that they both can't win.
    // If we pop, steal should fail.
    assert(nkit_deque_pop(dq, &out));
    assert(out == &val);
    assert(!nkit_deque_steal(dq, &out));

    nkit_deque_destroy(dq);
}

static void test_deque_resize(void) {
    printf("  [Check] Dynamic Resizing\n");
    // Start with small capacity
    nkit_deque_t* dq = nkit_deque_create(0, 8);
    assert(dq != NULL);

    int values[100];
    for (int i = 0; i < 100; i++) {
        values[i] = i;
        assert(nkit_deque_push(dq, &values[i]));
    }
    assert(nkit_deque_size(dq) == 100);

    // Verify LIFO order
    for (int i = 99; i >= 0; i--) {
        void* out = NULL;
        assert(nkit_deque_pop(dq, &out));
        assert(*(int*)out == i);
    }
    assert(nkit_deque_size(dq) == 0);

    nkit_deque_destroy(dq);
}

int test_11_deque(void) {
    printf("[UNIT] Deque Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    test_deque_basic();
    test_deque_steal();
    test_deque_race_simulation();
    test_deque_resize();

    nkit_teardown();
    printf("[UNIT] Deque Test Passed\n");
    return 0;
}
