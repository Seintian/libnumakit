#ifndef NKIT_SCHED_H
#define NKIT_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread affinity policies.
 */
typedef enum {
    NKIT_POLICY_BIND_NODE,  // Pin thread to all CPUs in a specific NUMA node
    NKIT_POLICY_STRICT_CPU, // Pin thread to a specific logical CPU core
} nkit_policy_type_e;

typedef struct {
    nkit_policy_type_e type;
    union {
        int node_id;
        int cpu_id;
    };
} nkit_thread_policy_t;

// Function pointer for thread routines
typedef void (*nkit_thread_func_t)(void* arg);

/**
 * @brief Launch a managed thread with a specific NUMA policy.
 * * @param policy The placement policy (e.g., Node 0).
 * @param func The function to run.
 * @param arg Argument passed to func.
 * @return 0 on success, -1 on failure.
 */
int nkit_thread_launch(nkit_thread_policy_t policy, nkit_thread_func_t func, void* arg);

/**
 * @brief Bind the *current* calling thread to a node.
 * Useful if you use your own thread pool (OpenMP/pthreads) but want nkit binding.
 */
int nkit_bind_current_thread(int node_id);

/**
 * @brief Wait for all nkit-launched threads to finish.
 * (Simple barrier for example programs).
 */
void nkit_thread_join_all(void);

#ifdef __cplusplus
}
#endif

#endif // NKIT_SCHED_H
