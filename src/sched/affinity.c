#define _GNU_SOURCE

#include "../internal.h"
#include <numakit/numakit.h>
#include <numakit/sched.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

// Mandatory Dependencies
#include <hwloc.h>
#include <numa.h>

// Internal struct to pass data to the thread
typedef struct {
    nkit_thread_policy_t policy;
    nkit_thread_func_t user_func;
    void *user_arg;
} nkit_trampoline_args_t;

// Helper: Apply policy using hwloc
static int _apply_policy(nkit_thread_policy_t policy) {
    if (!g_nkit_ctx.topo)
        return -1;

    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    int ret = -1;

    if (policy.type == NKIT_POLICY_BIND_NODE) {
        // Convert Node ID to CPU Bitmap
        hwloc_obj_t node = hwloc_get_obj_by_type(
            g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE, policy.node_id);
        if (node && node->cpuset) {
        hwloc_bitmap_copy(cpuset, node->cpuset);
        ret = 0;
        }
    } else if (policy.type == NKIT_POLICY_STRICT_CPU) {
        hwloc_bitmap_only(cpuset, policy.cpu_id);
        ret = 0;
    }

    if (ret == 0) {
        // Bind current thread to this bitmap
        if (hwloc_set_cpubind(g_nkit_ctx.topo, cpuset,
                            HWLOC_CPUBIND_THREAD | HWLOC_CPUBIND_STRICT) != 0) {
        ret = -1;
        }
    }

    hwloc_bitmap_free(cpuset);
    return ret;
}

// The Trampoline: Sets affinity -> Runs User Code -> Decrements Counter
static void *_nkit_thread_wrapper(void *arg) {
    nkit_trampoline_args_t *args = (nkit_trampoline_args_t *)arg;

    // 1. Set Affinity
    _apply_policy(args->policy);

    // 2. Run User Function
    args->user_func(args->user_arg);

    // 3. Cleanup
    free(args);
    atomic_fetch_sub(&g_nkit_ctx.active_threads, 1);

    return NULL;
}

int nkit_thread_launch(nkit_thread_policy_t policy, nkit_thread_func_t func,
                       void *arg) {
    nkit_trampoline_args_t *args = malloc(sizeof(nkit_trampoline_args_t));
    if (!args)
        return -1;

    args->policy = policy;
    args->user_func = func;
    args->user_arg = arg;

    atomic_fetch_add(&g_nkit_ctx.active_threads, 1);

    pthread_t th;
    if (pthread_create(&th, NULL, _nkit_thread_wrapper, args) != 0) {
        free(args);
        atomic_fetch_sub(&g_nkit_ctx.active_threads, 1);
        return -1;
    }

    pthread_detach(th); // We don't return the handle, we track via counter
    return 0;
}

int nkit_bind_thread(int node_id) {
    nkit_thread_policy_t pol = {.type = NKIT_POLICY_BIND_NODE,
                                .node_id = node_id};
    return _apply_policy(pol);
}

int nkit_unbind_thread(void) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    // Enable all CPUs
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < num_cpus; i++) {
        CPU_SET(i, &cpuset);
    }

    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

void nkit_thread_join_all(void) {
    // Simple spin-wait for demo purposes.
    // In production, use condition variables or semaphore.
    while (atomic_load(&g_nkit_ctx.active_threads) > 0) {
        usleep(1000); // Sleep 1ms
    }
}

int nkit_current_node(void) {
    unsigned int cpu, node;
    // SYS_getcpu fills both CPU and Node. We pass NULL for tcache.
    if (syscall(SYS_getcpu, &cpu, &node, NULL) == 0) {
        return (int) node;
    }
    return -1;
}

int nkit_current_cpu(void) {
    unsigned int cpu, node;
    if (syscall(SYS_getcpu, &cpu, &node, NULL) == 0) {
        return (int) cpu;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// IMPLEMENTATION: Native Pinning (Direct Backend)
// -----------------------------------------------------------------------------

int nkit_pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (ret != 0) {
        errno = ret;
        return -1;
    }
    return 0;
}

int nkit_pin_thread_to_node(int node_id) {
    // 1. Validation using libnuma
    if (numa_available() < 0 || node_id > numa_max_node()) {
        errno = EINVAL;
        return -1;
    }

    // 2. Allocate bitmask from libnuma
    struct bitmask *mask = numa_allocate_cpumask();
    if (numa_node_to_cpus(node_id, mask) != 0) {
        numa_free_cpumask(mask);
        return -1;
    }

    // 3. Convert to pthread cpu_set_t
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    int num_cpus = numa_num_configured_cpus();
    for (int i = 0; i < num_cpus; i++) {
        if (numa_bitmask_isbitset(mask, i)) {
        CPU_SET(i, &cpuset);
        }
    }

    // 4. Apply Affinity
    pthread_t current_thread = pthread_self();
    int ret = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    numa_free_cpumask(mask);

    if (ret != 0) {
        errno = ret;
        return -1;
    }
    return 0;
}

int nkit_get_current_core(void) {
    // Standard glibc wrapper for SYS_getcpu
    return sched_getcpu();
}

int nkit_get_current_node(void) {
    int cpu = sched_getcpu();
    if (cpu < 0)
        return -1;

    // Use libnuma to look up which node owns this CPU
    if (numa_available() >= 0) {
        return numa_node_of_cpu(cpu);
    }
    return 0; // Default if NUMA is technically available but returned error
}
