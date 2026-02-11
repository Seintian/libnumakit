#ifndef NKIT_SCHED_H
#define NKIT_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

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
typedef void (*nkit_thread_func_t)(void *arg);

/**
 * @brief Balancer Advice Codes.
 */
typedef enum {
    NKIT_ADVISE_STAY,    // Doing fine, keep running here.
    NKIT_ADVISE_MIGRATE, // High cache misses detected. Consider moving.
    NKIT_ADVISE_ERROR    // Profiling failed (e.g., no hardware support).
} nkit_advice_e;

/**
 * @brief Start profiling the current thread.
 * Opens hardware counters for Cache Misses and Instructions.
 */
int nkit_balancer_start(void);

/**
 * @brief Stop profiling and analyze performance.
 * @return An advice code based on the "Cache Miss Ratio".
 */
nkit_advice_e nkit_balancer_check(void);

/**
 * @brief Set the specific cache miss threshold for the balancer.
 * @param mpki Misses Per Kilo-Instruction (e.g., 50.0).
 * If current MPKI > threshold, balancer advises migration.
 */
void nkit_balancer_set_threshold(double mpki);

/**
 * @brief Launch a managed thread with a specific NUMA policy.
 * @param policy The placement policy (e.g., Node 0).
 * @param func The function to run.
 * @param arg Argument passed to func.
 * @return 0 on success, -1 on failure.
 */
int nkit_thread_launch(nkit_thread_policy_t policy, nkit_thread_func_t func,
                       void *arg);

/**
 * @brief Bind the calling thread to a specific NUMA node (hwloc backend).
 * @param node_id The NUMA node ID (0..N-1).
 * @return 0 on success, -1 on failure.
 */
int nkit_bind_thread(int node_id);

/**
 * @brief Unbind the thread (allow it to run on any CPU).
 */
int nkit_unbind_thread(void);

/**
 * @brief Wait for all nkit-launched threads to finish.
 * (Simple barrier for example programs).
 */
void nkit_thread_join_all(void);

/**
 * @brief Get the ID of the NUMA node the current thread is running on.
 * Uses the getcpu syscall for low-overhead checks.
 * @return Node ID (>= 0) on success, -1 on failure.
 */
int nkit_current_node(void);

/**
 * @brief Get the ID of the CPU core the current thread is running on.
 * @return CPU ID (>= 0) on success, -1 on failure.
 */
int nkit_current_cpu(void);

/**
 * @brief Send a data pointer to a target NUMA node.
 * Thread-safe (Multi-Producer).
 */
int nkit_send(int target_node, void *data);

/**
 * @brief Process pending messages for the CURRENT node.
 * Lock-Free Consumer.
 * @param handler Function to call for each message.
 * @param limit Maximum number of messages to process (0 = unlimited,
 * dangerous!).
 * @return Number of messages actually processed.
 */
size_t nkit_process_local(void (*handler)(void *), size_t limit);

// -----------------------------------------------------------------------------
// Direct Pinning API (Native Backend)
// -----------------------------------------------------------------------------

/**
 * @brief Bind the calling thread to a specific logical core.
 * Uses pthread_setaffinity_np directly (lower overhead than policy launcher).
 * @param core_id The logical core ID (OS index).
 * @return 0 on success, -1 on error.
 */
int nkit_pin_thread_to_core(int core_id);

/**
 * @brief Bind the calling thread to all CPUs in a NUMA node.
 * Uses libnuma bitmasks directly.
 * @param node_id The NUMA node ID.
 * @return 0 on success, -1 on error.
 */
int nkit_pin_thread_to_node(int node_id);

/**
 * @brief Get the current core ID (Alias to sched_getcpu).
 */
int nkit_get_current_core(void);

/**
 * @brief Get the current NUMA node ID (Native lookup).
 */
int nkit_get_current_node(void);

#ifdef __cplusplus
}
#endif

#endif // NKIT_SCHED_H
