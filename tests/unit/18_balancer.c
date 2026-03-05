#include <stdio.h>
#include <assert.h>

#include <numakit/numakit.h>
#include <numakit/sched.h>
#include "unit.h"

int test_18_balancer(void) {
    printf("[UNIT] Auto-Balancer Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    nkit_balancer_set_threshold(50.0);

    int ret = nkit_balancer_start();
    if (ret != 0) {
        printf("  [Skip] Hardware profiling not supported in this environment.\n");
    } else {
        // Do some work to generate instructions
        volatile int dummy = 0;
        for (int i = 0; i < 100000; i++) {
            dummy += i;
        }

        nkit_advice_e advice = nkit_balancer_check();
        // Just verify it doesn't crash. Advice might vary depending on env.
        printf("  [Check] Balancer advice: %d\n", advice);
        assert(advice == NKIT_ADVISE_STAY || advice == NKIT_ADVISE_MIGRATE || advice == NKIT_ADVISE_ERROR);
    }

    nkit_teardown();
    printf("[UNIT] Auto-Balancer Test Passed\n");
    return 0;
}
