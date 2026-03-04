#define _GNU_SOURCE

#include <numakit/memory.h>
#include <numakit/numakit.h>
#include <sys/mman.h>
#include <numaif.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

// 2MB Hugepage size (standard on x86)
#define HUGE_PAGE_SIZE (2 * 1024 * 1024)

struct nkit_arena_s {
    void* base;        // Pointer to the start of memory
    size_t size;        // Total size
    size_t used;        // Bytes allocated
    int    node_id;     // NUMA node this arena belongs to
    int    use_huge;    // 1 if backed by hugepages, 0 if standard pages
};

nkit_arena_t* nkit_arena_create(int node_id, size_t size) {
    if (size == 0) return NULL;

    // 1. Allocate the struct (small, just malloc is fine)
    nkit_arena_t* arena = malloc(sizeof(nkit_arena_t));
    if (!arena) return NULL;

    // 2. Align size up to 2MB to be safe for Hugepages
    size_t aligned_size = (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);

    arena->size = aligned_size;
    arena->used = 0;
    arena->node_id = node_id;
    arena->use_huge = 1; // Optimistic default

    int default_prot_flags = PROT_READ | PROT_WRITE;
    int default_map_flags = MAP_PRIVATE | MAP_ANONYMOUS;

    // 3. PLAN A: Try to allocate Hugepages
    // MAP_HUGETLB: Allocate 2MB pages
    // MAP_ANONYMOUS: Not backed by a file
    // MAP_PRIVATE: Copy-on-write (standard for memory)
    arena->base = mmap(NULL, aligned_size, 
                       default_prot_flags, 
                       default_map_flags | MAP_HUGETLB, 
                       -1, 0);

    // 4. PLAN B: Fallback to Standard Pages (4KB)
    if (arena->base == MAP_FAILED) {
        arena->use_huge = 0; // Mark as standard pages

        // Try again without MAP_HUGETLB
        arena->base = mmap(NULL, aligned_size, 
                           default_prot_flags, 
                           default_map_flags, 
                           -1, 0);

        if (arena->base == MAP_FAILED) {
            // Total failure (OOM?)
            free(arena);
            return NULL;
        }
    }

    // 5. Apply NUMA Policy (mbind)
    // Even if we are using standard pages, we still want them on the correct node!
    // We create a bitmask for the specific node_id.
    unsigned long nodemask = (1UL << node_id);

    // MPOL_BIND: Strict policy. Only allocate on this node.
    // If we are on UMA (Node 0 only) and request Node 1, this might fail.
    // We handle that gracefully.
    long ret = mbind(arena->base, aligned_size, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, MPOL_MF_MOVE);

    if (ret < 0) {
        // If strict binding fails (e.g. Node 1 doesn't exist on this machine),
        // we try MPOL_PREFERRED (soft preference).
        mbind(arena->base, aligned_size, MPOL_PREFERRED, &nodemask, sizeof(nodemask) * 8, MPOL_MF_MOVE);
    }

    return arena;
}

void* nkit_arena_alloc(nkit_arena_t* arena, size_t size) {
    if (!arena) return NULL;

    // 1. Align allocation to 64 bytes (Cache Line)
    // This prevents false sharing between objects allocated sequentially.
    size_t aligned_size = (size + 63) & ~63;

    // 2. Check capacity
    if (arena->used + aligned_size > arena->size) {
        return NULL; // Out of memory in this arena
    }

    // 3. Bump pointer
    void* ptr = (char*)arena->base + arena->used;
    arena->used += aligned_size;

    return ptr;
}

void nkit_arena_destroy(nkit_arena_t* arena) {
    if (arena) {
        if (arena->base && arena->base != MAP_FAILED) {
            munmap(arena->base, arena->size);
        }
        free(arena);
    }
}

void nkit_arena_reset(nkit_arena_t* arena) {
    if (arena) {
        arena->used = 0;
    }
}

// ---------------------------------------------------------------------------
// Hugepage Coalescing
// ---------------------------------------------------------------------------

size_t nkit_arena_coalesce(nkit_arena_t* arena) {
    if (!arena) return 0;
    if (!arena->base || arena->base == MAP_FAILED) return 0;
    if (arena->size == 0) return 0;

    // Determine the page size to use for coalescing.
    // For hugepage-backed arenas we use 2MB; for standard pages we use
    // the normal page size (typically 4KB).
    size_t page_size = arena->use_huge ? HUGE_PAGE_SIZE : (size_t)sysconf(_SC_PAGESIZE);
    if (page_size == 0) return 0;

    // Find the first page boundary ABOVE the current watermark.
    // Everything from that point to the end of the arena is unused.
    uintptr_t base_addr = (uintptr_t)arena->base;
    uintptr_t used_end  = base_addr + arena->used;

    // Align used_end UP to the next page boundary
    uintptr_t first_free_page = (used_end + page_size - 1) & ~(page_size - 1);

    // End of the arena (already page-aligned from create)
    uintptr_t arena_end = base_addr + arena->size;

    if (first_free_page >= arena_end) {
        return 0; // No full pages to return
    }

    size_t free_bytes = arena_end - first_free_page;
    size_t pages_to_return = free_bytes / page_size;

    if (pages_to_return == 0) return 0;

    // Use madvise(MADV_DONTNEED) to tell the kernel these pages are no longer
    // needed.  The kernel zeros the pages and may reclaim the physical frames.
    // The virtual mapping remains valid — future accesses will fault in fresh pages.
    int ret = madvise((void*)first_free_page, pages_to_return * page_size, MADV_DONTNEED);
    if (ret != 0) {
        return 0;
    }

    return pages_to_return;
}

// ---------------------------------------------------------------------------
// Arena Query Functions
// ---------------------------------------------------------------------------

size_t nkit_arena_used(nkit_arena_t* arena) {
    if (!arena) return 0;
    return arena->used;
}

size_t nkit_arena_size(nkit_arena_t* arena) {
    if (!arena) return 0;
    return arena->size;
}

int nkit_arena_is_huge(nkit_arena_t* arena) {
    if (!arena) return 0;
    return arena->use_huge;
}

