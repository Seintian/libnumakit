#define _GNU_SOURCE

#include <numakit/sched.h>
#include <numakit/numakit.h>
#include "../internal.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

// Internal struct to pass data to the thread
typedef struct {
    nkit_thread_policy_t policy;
    nkit_thread_func_t user_func;
    void* user_arg;
} nkit_trampoline_args_t;

// Helper: Apply policy using hwloc
static int _apply_policy(nkit_thread_policy_t policy) {
    if (!g_nkit_ctx.topo) return -1;

    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    int ret = -1;

    if (policy.type == NKIT_POLICY_BIND_NODE) {
        // Convert Node ID to CPU Bitmap
        hwloc_obj_t node = hwloc_get_obj_by_type(g_nkit_ctx.topo, HWLOC_OBJ_NUMANODE, policy.node_id);
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
        if (hwloc_set_cpubind(g_nkit_ctx.topo, cpuset, HWLOC_CPUBIND_THREAD | HWLOC_CPUBIND_STRICT) != 0) {
            ret = -1;
        }
    }

    hwloc_bitmap_free(cpuset);
    return ret;
}

// The Trampoline: Sets affinity -> Runs User Code -> Decrements Counter
static void* _nkit_thread_wrapper(void* arg) {
    nkit_trampoline_args_t* args = (nkit_trampoline_args_t*) arg;

    // 1. Set Affinity
    _apply_policy(args->policy);

    // 2. Run User Function
    args->user_func(args->user_arg);

    // 3. Cleanup
    free(args);
    atomic_fetch_sub(&g_nkit_ctx.active_threads, 1);

    return NULL;
}

int nkit_thread_launch(nkit_thread_policy_t policy, nkit_thread_func_t func, void* arg) {
    nkit_trampoline_args_t* args = malloc(sizeof(nkit_trampoline_args_t));
    if (!args) return -1;

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

int nkit_bind_current_thread(int node_id) {
    nkit_thread_policy_t pol = { .type = NKIT_POLICY_BIND_NODE, .node_id = node_id };
    return _apply_policy(pol);
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
