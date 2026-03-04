#ifndef NKIT_SCHED_H
#define NKIT_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <pthread.h>

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

// -----------------------------------------------------------------------------
// NUMA-Aware Task Pool
// -----------------------------------------------------------------------------

typedef struct nkit_pool_s nkit_pool_t;

/**
 * @brief Initialize a global NUMA-aware thread pool.
 * Spawns threads proportional to the hardware cores on each node.
 */
nkit_pool_t *nkit_pool_create(void);

/**
 * @brief Submit a task to run on the optimal node for the given data.
 * The pool looks up the physical memory location of 'data_ptr' and
 * queues the task on the thread local to that NUMA node.
 * * @param pool The task pool.
 * @param func The function to execute.
 * @param data_ptr Pointer to the data the task will process.
 */
int nkit_pool_submit_local(nkit_pool_t *pool, void (*func)(void *),
                           void *data_ptr);

/**
 * @brief Submit a task to a specific, explicit NUMA node.
 */
int nkit_pool_submit_to_node(nkit_pool_t *pool, int target_node,
                             void (*func)(void *), void *arg);

/**
 * @brief Gracefully shutdown the pool and wait for tasks to finish.
 */
void nkit_pool_destroy(nkit_pool_t *pool);

// -----------------------------------------------------------------------------
// Auto-Balancer (Background Cache-Miss Monitor)
// -----------------------------------------------------------------------------

/**
 * @brief Opaque handle for the auto-balancer.
 *
 * The auto-balancer runs a **background thread** that periodically
 * samples hardware performance counters (cache misses / instructions)
 * for every registered worker thread. When a thread's MPKI exceeds
 * the configured threshold, the balancer migrates it to the NUMA
 * node with the most available capacity.
 */
typedef struct nkit_auto_balancer_s nkit_auto_balancer_t;

/**
 * @brief Callback invoked when the auto-balancer migrates a thread.
 *
 * @param tid         The pthread_t of the migrated thread.
 * @param from_node   The NUMA node the thread was on.
 * @param to_node     The NUMA node the thread was moved to.
 * @param mpki        The measured Misses Per Kilo-Instruction.
 */
typedef void (*nkit_migration_cb_t)(pthread_t tid, int from_node,
                                     int to_node, double mpki);

/**
 * @brief Create and start the auto-balancer.
 *
 * @param interval_ms  Sampling interval in milliseconds (e.g. 500).
 * @param mpki_thresh  MPKI threshold above which migration is advised.
 * @param cb           Optional callback for migration events (may be NULL).
 * @return Handle to the balancer, or NULL on failure.
 */
nkit_auto_balancer_t *nkit_auto_balancer_create(unsigned interval_ms,
                                                 double mpki_thresh,
                                                 nkit_migration_cb_t cb);

/**
 * @brief Register a thread for automatic monitoring.
 *
 * @param ab   The auto-balancer handle.
 * @param tid  The pthread_t to monitor.
 * @param node Current NUMA node the thread is pinned to.
 * @return 0 on success, -1 if the registry is full.
 */
int nkit_auto_balancer_register(nkit_auto_balancer_t *ab, pthread_t tid,
                                 int node);

/**
 * @brief Unregister a thread from monitoring.
 *
 * @param ab   The auto-balancer handle.
 * @param tid  The pthread_t to stop monitoring.
 * @return 0 on success, -1 if not found.
 */
int nkit_auto_balancer_unregister(nkit_auto_balancer_t *ab, pthread_t tid);

/**
 * @brief Query the number of migrations performed so far.
 */
size_t nkit_auto_balancer_migrations(nkit_auto_balancer_t *ab);

/**
 * @brief Stop the background thread and destroy the balancer.
 */
void nkit_auto_balancer_destroy(nkit_auto_balancer_t *ab);

#ifdef __cplusplus
}
#endif

#endif // NKIT_SCHED_H
