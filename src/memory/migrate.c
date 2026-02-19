#define _GNU_SOURCE

#include <errno.h>
#include <numa.h>
#include <numaif.h>
#include <stdint.h>
#include <unistd.h>

#include <numakit/memory.h>

int nkit_memory_migrate(void *ptr, size_t size, int target_node) {
    if (numa_available() < 0 || target_node > numa_max_node()) {
        errno = EINVAL;
        return -1;
    }

    // Kernel page migration requires page-aligned addresses.
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t uptr = (uintptr_t)ptr;

    // Align address down, size up
    uintptr_t aligned_ptr = uptr & ~(page_size - 1);
    size_t offset = uptr - aligned_ptr;
    size_t aligned_size = size + offset;

    // Use libnuma to create the required nodemask format for mbind
    struct bitmask *mask = numa_allocate_nodemask();
    numa_bitmask_setbit(mask, target_node);

    // MPOL_MF_MOVE: Move pages to the node specified by the policy.
    // MPOL_MF_STRICT: Fail if the pages cannot be moved.
    int ret = mbind((void *)aligned_ptr, aligned_size, MPOL_BIND, mask->maskp,
                    mask->size + 1, MPOL_MF_MOVE);

    numa_free_nodemask(mask);

    if (ret != 0) {
        // errno is set by mbind (e.g., EPERM if memory is locked)
        return -1;
    }
    return 0;
}
