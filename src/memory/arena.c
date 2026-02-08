#include "internal.h"
#define _GNU_SOURCE
#include <numakit/memory.h>
#include <numakit/numakit.h> // For error codes if needed

#include <sys/mman.h>
#include <numaif.h>          // Linux NUMA syscalls (mbind)
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// Internal Definition of the Arena
struct nkit_arena_s {
    int node_id;        // The NUMA node this belongs to
    size_t total_size;  // Total capacity
    size_t used;        // Current usage offset
    uint8_t* base;      // Pointer to the start of memory
};

nkit_arena_t* nkit_arena_create(int node_id, size_t size) {
    // 1. Allocate the struct (metadata)
    // We allocate this on the general heap, but the *payload* will be on the specific node.
    nkit_arena_t* arena = malloc(sizeof(nkit_arena_t));
    if (!arena) return NULL;

    // 2. Align size to Hugepage boundary (required for MAP_HUGETLB)
    size_t huge_sz = _nkit_get_hugepage_size();
    if (huge_sz == 0) huge_sz = 2 * 1024 * 1024; // Fallback to 2MB

    size = (size + huge_sz - 1) & ~(huge_sz - 1);

    // 3. Reserve Address Space (mmap)
    // PROT_READ|PROT_WRITE: Read/Write access
    // MAP_PRIVATE|MAP_ANONYMOUS: Not backed by a file
    void* ptr = mmap(NULL, size, 
                     PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, 
                     -1, 0);

    if (ptr == MAP_FAILED) {
        free(arena);
        return NULL;
    }

    // 4. BIND memory to the specific NUMA node
    // This is the "Magic" step. We force physical allocation on node_id.
    unsigned long nodemask = (1UL << node_id);
    long ret = mbind(ptr, size, MPOL_BIND, &nodemask, sizeof(nodemask) * 8 + 1, 0);

    if (ret != 0) {
        // Fallback: If strict binding fails (e.g. node full), unmap and fail
        munmap(ptr, size);
        free(arena);
        errno = EINVAL; // Or appropriate error
        return NULL;
    }

    // 5. Setup Arena
    arena->node_id = node_id;
    arena->total_size = size;
    arena->used = 0;
    arena->base = (uint8_t*)ptr;

    return arena;
}

void* nkit_arena_alloc(nkit_arena_t* arena, size_t size) {
    if (!arena) return NULL;

    // Align allocation to 8 bytes (sizeof(void*)) to avoid unaligned access faults
    size_t aligned_size = (size + 7) & ~7;

    if (arena->used + aligned_size > arena->total_size) {
        return NULL; // OOM
    }

    void* ptr = arena->base + arena->used;
    arena->used += aligned_size;

    return ptr;
}

void nkit_arena_reset(nkit_arena_t* arena) {
    if (arena) {
        arena->used = 0;
        // Optional: madvise(arena->base, arena->total_size, MADV_DONTNEED);
        // This would tell OS to reclaim physical pages, but it's slow.
        // For high-perf, we just reset the counter.
    }
}

void nkit_arena_destroy(nkit_arena_t* arena) {
    if (arena) {
        munmap(arena->base, arena->total_size);
        free(arena);
    }
}
