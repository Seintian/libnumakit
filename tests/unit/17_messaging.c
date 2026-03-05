#include <stdio.h>
#include <assert.h>

#include <numakit/numakit.h>
#include <numakit/sched.h>
#include "unit.h"

static int processed_messages = 0;

static void message_handler(void* arg) {
    int* val = (int*)arg;
    assert(*val == 42 || *val == 84);
    processed_messages++;
}

int test_17_messaging(void) {
    printf("[UNIT] Messaging Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    int current_node = nkit_current_node();
    if (current_node < 0) current_node = 0; // fallback if hardware detection is odd

    int msg1 = 42;
    int msg2 = 84;

    // Send two messages to our own node
    int ret = nkit_send(current_node, &msg1);
    assert(ret == 0);
    
    ret = nkit_send(current_node, &msg2);
    assert(ret == 0);

    // Process local messages
    size_t processed = nkit_process_local(message_handler, 0); // 0 = unlimited
    printf("  [Check] Processed messages: %zu\n", processed);
    assert(processed == 2);
    assert(processed_messages == 2);

    nkit_teardown();
    printf("[UNIT] Messaging Test Passed\n");
    return 0;
}
