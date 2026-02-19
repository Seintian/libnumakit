#define _GNU_SOURCE

#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <numakit/numakit.h>
#include <numakit/sched.h>

// Internal wrapper for tasks
typedef struct {
    void (*func)(void *);
    void *arg;
} nkit_task_t;

// Forward declaration needed for the back-pointer
struct nkit_pool_s;

typedef struct {
    int node_id;
    nkit_ring_t *queue;
    pthread_t *threads;
    int num_threads;
    int *steal_order;                // Array of other node IDs sorted by distance
    struct nkit_pool_s *global_pool;
} nkit_node_pool_t;

struct nkit_pool_s {
    int num_nodes;
    nkit_node_pool_t *node_pools;
    volatile int stop;
};

// Helper: Round up to next power of 2 for fast ring buffer bitwise operations
static uint32_t _next_power_of_2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

// Worker thread routine
static void *_nkit_worker(void *arg) {
    nkit_node_pool_t *my_pool = (nkit_node_pool_t *)arg;
    struct nkit_pool_s *global_pool =
        my_pool->global_pool; // Safely access global pool

    // Pin worker to its designated node
    nkit_pin_thread_to_node(my_pool->node_id);

    while (!global_pool->stop) {
        void *task_ptr = NULL;

        // 1. Try Local Queue First (Fast Path)
        if (nkit_ring_pop(my_pool->queue, &task_ptr)) {
            nkit_task_t *task = (nkit_task_t *)task_ptr;
            task->func(task->arg);
            free(task);
            continue;
        }

        // 2. Local is empty. Try Stealing (Hierarchical: closest nodes first)
        int stole = 0;
        for (int i = 0; i < global_pool->num_nodes - 1; i++) {
            int target_node = my_pool->steal_order[i];
            if (nkit_ring_pop(global_pool->node_pools[target_node].queue,
                                &task_ptr)) {
                nkit_task_t *task = (nkit_task_t *)task_ptr;
                task->func(task->arg);
                free(task);
                stole = 1;
                break; // Process one task, then re-check local queue
            }
        }

        // 3. Completely idle
        if (!stole) {
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #else
            sched_yield();
        #endif
        }
    }
    return NULL;
}

// Compare function for qsort to sort stealing order by NUMA distance
static int _my_node_for_sort = 0;
static int _cmp_distance(const void *a, const void *b) {
    int node_a = *(const int *)a;
    int node_b = *(const int *)b;
    int dist_a = numa_distance(_my_node_for_sort, node_a);
    int dist_b = numa_distance(_my_node_for_sort, node_b);
    return dist_a - dist_b;
}

nkit_pool_t *nkit_pool_create(void) {
    if (numa_available() < 0)
        return NULL;

    nkit_pool_t *pool = malloc(sizeof(nkit_pool_t));
    if (!pool)
        return NULL;

    pool->stop = 0;
    pool->num_nodes = numa_max_node() + 1;
    pool->node_pools = calloc(pool->num_nodes, sizeof(nkit_node_pool_t));

    int total_cpus = numa_num_configured_cpus();
    int cpus_per_node = total_cpus / pool->num_nodes;
    if (cpus_per_node == 0)
        cpus_per_node = 1;

    // Dynamically scale queue capacity
    uint32_t ring_capacity = _next_power_of_2(cpus_per_node * 1024);
    if (ring_capacity < 1024)
        ring_capacity = 1024;

    for (int i = 0; i < pool->num_nodes; i++) {
        nkit_node_pool_t *np = &pool->node_pools[i];
        np->node_id = i;
        np->queue = nkit_ring_create(i, ring_capacity);
        np->num_threads = cpus_per_node;
        np->threads = malloc(sizeof(pthread_t) * np->num_threads);
        np->global_pool = pool; // <-- Set the back-pointer cleanly

        // Compute Steal Order based on hardware distances
        np->steal_order = malloc(sizeof(int) * (pool->num_nodes - 1));
        int idx = 0;
        for (int j = 0; j < pool->num_nodes; j++) {
        if (i != j)
            np->steal_order[idx++] = j;
        }
        _my_node_for_sort = i;
        qsort(np->steal_order, pool->num_nodes - 1, sizeof(int), _cmp_distance);

        for (int t = 0; t < np->num_threads; t++) {
            pthread_create(&np->threads[t], NULL, _nkit_worker, np);
        }
    }
    return pool;
}

int nkit_pool_submit_to_node(nkit_pool_t *pool, int target_node,
                             void (*func)(void *), void *arg) {
    if (target_node < 0 || target_node >= pool->num_nodes)
        target_node = 0;

    nkit_task_t *task = malloc(sizeof(nkit_task_t));
    if (!task)
        return -1;
    task->func = func;
    task->arg = arg;

    while (!nkit_ring_push(pool->node_pools[target_node].queue, task)) {
    #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
    #else
        sched_yield();
    #endif
    }
    return 0;
}

int nkit_pool_submit_local(nkit_pool_t *pool, void (*func)(void *),
                           void *data_ptr) {
    int node_id = 0;

    void *pages[1] = {data_ptr};
    int status[1] = {-1};
    if (move_pages(0, 1, pages, NULL, status, 0) == 0 && status[0] >= 0) {
        node_id = status[0];
    }

    return nkit_pool_submit_to_node(pool, node_id, func, data_ptr);
}

void nkit_pool_destroy(nkit_pool_t *pool) {
    if (!pool)
        return;

    pool->stop = 1;
    for (int i = 0; i < pool->num_nodes; i++) {
        nkit_node_pool_t *np = &pool->node_pools[i];
        for (int t = 0; t < np->num_threads; t++) {
            pthread_join(np->threads[t], NULL);
        }
        nkit_ring_free(np->queue);
        free(np->threads);
        free(np->steal_order);
    }
    free(pool->node_pools);
    free(pool);
}
