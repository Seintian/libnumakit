#include <stdio.h>
#include <assert.h>

#include <numakit/numakit.h>
#include <numakit/structs/ring_buffer.h>
#include "unit.h"

int test_16_ring_buffer(void) {
    printf("[UNIT] Ring Buffer Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    // Must be power of 2
    size_t capacity = 1024;
    nkit_ring_t* ring = nkit_ring_create(0, capacity);
    assert(ring != NULL);

    int val1 = 42;
    int val2 = 84;
    int val3 = 126;

    // Test push
    assert(nkit_ring_push(ring, &val1));
    assert(nkit_ring_push(ring, &val2));
    assert(nkit_ring_push(ring, &val3));

    // Fill the buffer to verify capacity
    int fill_val = 0;
    for (size_t i = 3; i < capacity; i++) {
        assert(nkit_ring_push(ring, &fill_val));
    }
    
    // Push one more, should fail (buffer full)
    assert(!nkit_ring_push(ring, &fill_val));

    // Test pop
    void* out = NULL;
    assert(nkit_ring_pop(ring, &out));
    assert(out == &val1);

    assert(nkit_ring_pop(ring, &out));
    assert(out == &val2);

    assert(nkit_ring_pop(ring, &out));
    assert(out == &val3);

    nkit_ring_free(ring);
    nkit_teardown();

    printf("[UNIT] Ring Buffer Test Passed\n");
    return 0;
}
