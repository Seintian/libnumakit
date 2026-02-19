#define _GNU_SOURCE

#include <stdlib.h>
#include <pthread.h>
#include <numa.h>
#include <numaif.h>
#include <unistd.h>
#include <sched.h>
#include <stdint.h>
#include <errno.h>

#include <numakit/sched.h>
#include <numakit/numakit.h>

// -----------------------------------------------------------------------------
// Internal Data Structures
// -----------------------------------------------------------------------------

// The task structure itself now knows where it came from.
typedef struct {
    void (*func)(void*);
    void* arg;
    nkit_ring_t* home_free_queue; // Where this struct must be returned after execution
} nkit_task_t;

struct nkit_pool_s;

typedef struct {
    int node_id;
    uint32_t capacity;           // Track capacity so we can numa_free properly
    nkit_ring_t* task_queue;     // Tasks waiting to be executed
    nkit_ring_t* free_queue;     // Pointers to unused nkit_task_t structs
    nkit_task_t* task_array;     // The physical memory for the task structs

    pthread_t* threads;
    int num_threads;
    int threads_started;
    int* steal_order; 
    struct nkit_pool_s* global_pool; 
} nkit_node_pool_t;

struct nkit_pool_s {
    int num_nodes;
    nkit_node_pool_t* node_pools;
    volatile int stop;
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Progressive Backoff Helper
static inline void nkit_backoff(int* spin_count) {
    if (*spin_count < 2000) {
        // Phase 1: Hot spin (Ultra-low latency, low power)
        #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
        #else
            __asm__ volatile ("yield" ::: "memory"); // ARM equivalent
        #endif
    } else if (*spin_count < 5000) {
        // Phase 2: Warm spin (Politely yield OS thread)
        sched_yield(); 
    } else {
        // Phase 3: Cold idle (Drop CPU usage to 0%)
        usleep(1000); // Sleep for 1 millisecond
    }
    (*spin_count)++;
}

// Round up to next power of 2 for fast ring buffer bitwise operations
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

// Compare function for qsort to sort stealing order by NUMA distance
static int _my_node_for_sort = 0;
static int _cmp_distance(const void* a, const void* b) {
    int node_a = *(const int*)a;
    int node_b = *(const int*)b;
    return numa_distance(_my_node_for_sort, node_a) - numa_distance(_my_node_for_sort, node_b);
}

// -----------------------------------------------------------------------------
// Worker Thread
// -----------------------------------------------------------------------------

static void* _nkit_worker(void* arg) {
    nkit_node_pool_t* my_pool = (nkit_node_pool_t*)arg;
    struct nkit_pool_s* global_pool = my_pool->global_pool; 

    nkit_pin_thread_to_node(my_pool->node_id);

    int idle_spins = 0;

    while (!global_pool->stop) {
        void* task_ptr = NULL;

        // 1. Try Local Queue First
        if (nkit_ring_pop(my_pool->task_queue, &task_ptr)) {
            idle_spins = 0;
            nkit_task_t* task = (nkit_task_t*)task_ptr;
            task->func(task->arg);

            while (!nkit_ring_push(task->home_free_queue, task)) {
                #if defined(__x86_64__) || defined(_M_X64)
                    __builtin_ia32_pause();
                #endif
            }
            continue;
        }

        // 2. Try Stealing
        int stole = 0;
        if (global_pool->num_nodes > 1 && my_pool->steal_order != NULL) {
            for (int i = 0; i < global_pool->num_nodes - 1; i++) {
                int target_node = my_pool->steal_order[i];
                if (nkit_ring_pop(global_pool->node_pools[target_node].task_queue, &task_ptr)) {
                    idle_spins = 0;
                    nkit_task_t* task = (nkit_task_t*)task_ptr;
                    task->func(task->arg);

                    while (!nkit_ring_push(task->home_free_queue, task)) {
                    #if defined(__x86_64__) || defined(_M_X64)
                        __builtin_ia32_pause();
                    #endif
                    }
                    stole = 1;
                    break;
                }
            }
        }

        // 3. Progressive Idle
        if (!stole) {
            nkit_backoff(&idle_spins);
        }
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

nkit_pool_t* nkit_pool_create(void) {
    if (numa_available() < 0) return NULL;

    nkit_pool_t* pool = malloc(sizeof(nkit_pool_t));
    if (!pool) return NULL;

    pool->stop = 0;
    pool->num_nodes = numa_max_node() + 1;
    pool->node_pools = calloc(pool->num_nodes, sizeof(nkit_node_pool_t));

    int total_cpus = numa_num_configured_cpus();
    int cpus_per_node = total_cpus / pool->num_nodes;
    if (cpus_per_node == 0) cpus_per_node = 1;

    // Dynamically scale queue capacity: Allocate 1024 slots per CPU core
    uint32_t ring_capacity = _next_power_of_2(cpus_per_node * 1024);
    if (ring_capacity < 1024) ring_capacity = 1024;

    // Phase 1: Allocate ALL memory and queues first to prevent race conditions
    for (int i = 0; i < pool->num_nodes; i++) {
        nkit_node_pool_t* np = &pool->node_pools[i];
        np->node_id = i;
        np->num_threads = cpus_per_node;
        np->threads_started = 0;
        np->global_pool = pool; 
        np->capacity = ring_capacity;

        np->task_queue = nkit_ring_create(i, ring_capacity);
        np->free_queue = nkit_ring_create(i, ring_capacity);

        // Allocate the physical task structs directly on this NUMA node
        np->task_array = numa_alloc_onnode(sizeof(nkit_task_t) * ring_capacity, i);

        if (!np->task_queue || !np->free_queue || !np->task_array) {
            // Memory allocation failed (e.g. out of hugepages)
            nkit_pool_destroy(pool);
            return NULL; 
        }

        // Populate the free queue with pointers to our pre-allocated array
        for (uint32_t j = 0; j < ring_capacity; j++) {
            np->task_array[j].home_free_queue = np->free_queue;
            nkit_ring_push(np->free_queue, &np->task_array[j]);
        }

        np->threads = malloc(sizeof(pthread_t) * np->num_threads);

        // Build the steal order
        if (pool->num_nodes > 1) {
            np->steal_order = malloc(sizeof(int) * (pool->num_nodes - 1));
            int idx = 0;
            for (int j = 0; j < pool->num_nodes; j++) {
                if (i != j) np->steal_order[idx++] = j;
            }
            _my_node_for_sort = i;
            qsort(np->steal_order, pool->num_nodes - 1, sizeof(int), _cmp_distance);
        } else {
            np->steal_order = NULL;
        }
    }

    // Phase 2: Start workers ONLY AFTER all queues are fully established
    for (int i = 0; i < pool->num_nodes; i++) {
        nkit_node_pool_t* np = &pool->node_pools[i];
        for (int t = 0; t < np->num_threads; t++) {
            if (pthread_create(&np->threads[t], NULL, _nkit_worker, np) == 0) {
                np->threads_started++;
            }
        }
    }
    return pool;
}

int nkit_pool_submit_to_node(nkit_pool_t* pool, int target_node, void (*func)(void*), void* arg) {
    if (target_node < 0 || target_node >= pool->num_nodes)
        target_node = 0;

    nkit_node_pool_t* np = &pool->node_pools[target_node];
    void* free_task_ptr = NULL;

    if (!nkit_ring_pop(np->free_queue, &free_task_ptr)) {
        errno = EAGAIN;
        return -1;
    }

    nkit_task_t* task = (nkit_task_t*)free_task_ptr;
    task->func = func;
    task->arg = arg;

    int submit_spins = 0;
    while (!nkit_ring_push(np->task_queue, task)) {
        nkit_backoff(&submit_spins);
    }
    return 0;
}

int nkit_pool_submit_local(nkit_pool_t* pool, void (*func)(void*), void* data_ptr) {
    int node_id = 0;

    // Auto-detect physical node where data_ptr resides
    void* pages[1] = { data_ptr };
    int status[1] = { -1 };
    if (move_pages(0, 1, pages, NULL, status, 0) == 0 && status[0] >= 0) {
        node_id = status[0];
    }

    return nkit_pool_submit_to_node(pool, node_id, func, data_ptr);
}

void nkit_pool_destroy(nkit_pool_t* pool) {
    if (!pool) return;

    pool->stop = 1;
    for (int i = 0; i < pool->num_nodes; i++) {
        nkit_node_pool_t* np = &pool->node_pools[i];

        // Wait only for threads that successfully started
        for (int t = 0; t < np->threads_started; t++) {
            pthread_join(np->threads[t], NULL);
        }

        // Cleanup Queues
        if (np->task_queue) nkit_ring_free(np->task_queue);
        if (np->free_queue) nkit_ring_free(np->free_queue);

        // Cleanup memory using numa_free
        if (np->task_array) {
            numa_free(np->task_array, sizeof(nkit_task_t) * np->capacity);
        }

        if (np->threads) free(np->threads);
        if (np->steal_order) free(np->steal_order);
    }
    free(pool->node_pools);
    free(pool);
}
