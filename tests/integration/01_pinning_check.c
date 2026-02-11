#define _GNU_SOURCE

#include <assert.h>
#include <numa.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include <numakit/numakit.h>
#include "integration.h"

// Helper: Count how many CPUs are allowed in the current thread's mask
int count_allowed_cpus(void) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    // Get the actual OS affinity mask
    if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        return -1;
    }

    int count = 0;
    // Iterate up to CPU_SETSIZE (usually 1024)
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &cpuset))
        count++;
    }
    return count;
}

int integration_01_pinning_check(void) {
    printf("[INTEGRATION] Pinning Verification Started...\n");

    if (nkit_init() != 0)
        return 1;

    // -------------------------------------------------------------------------
    // Case 1: Pin to Core (Strict)
    // -------------------------------------------------------------------------
    printf("  -> Testing Strict Core Pinning (Core 0)...\n");
    if (nkit_pin_thread_to_core(0) == 0) {
        int allowed = count_allowed_cpus();
        printf("     Allowed CPUs: %d (Expected: 1)\n", allowed);

        if (allowed != 1) {
        fprintf(stderr, "FAILURE: Pinned to core 0 but allowed on %d CPUs\n",
                allowed);
        return 1;
        }

        // Verify we are actually on Core 0
        if (nkit_get_current_core() != 0) {
        fprintf(stderr, "FAILURE: OS says we are running on Core %d\n",
                nkit_get_current_core());
        return 1;
        }
    } else {
        printf("     [Skipped] Could not pin to Core 0.\n");
    }

    // -------------------------------------------------------------------------
    // Case 2: Pin to Node (Loose)
    // -------------------------------------------------------------------------
    printf("  -> Testing Node Pinning (Node 0)...\n");
    if (nkit_pin_thread_to_node(0) == 0) {
        // Calculate expected CPU count for Node 0 using libnuma directly
        struct bitmask *node_cpus = numa_allocate_cpumask();
        numa_node_to_cpus(0, node_cpus);
        unsigned int expected = numa_bitmask_weight(node_cpus);

        int allowed = count_allowed_cpus();
        printf("     Allowed CPUs: %d (Expected: %u)\n", allowed, expected);

        if (allowed != (int)expected) {
        fprintf(stderr,
                "FAILURE: Node 0 has %u CPUs, but affinity mask allows %d\n",
                expected, allowed);
        numa_free_cpumask(node_cpus);
        return 1;
        }
        numa_free_cpumask(node_cpus);
    } else {
        printf("     [Skipped] Could not pin to Node 0.\n");
    }

    nkit_teardown();
    printf("[INTEGRATION] Pinning Verification Passed.\n");
    return 0;
}
