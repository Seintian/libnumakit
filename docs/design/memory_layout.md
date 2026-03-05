# libnumakit Memory Layout Design

The memory management subsystem in `libnumakit` provides a layered, NUMA-aware allocation strategy designed for maximum throughput and minimal cross-node memory traffic.

## 1. NUMA-Aware Arenas (`nkit_arena_t`)

The base of the memory system is the **Arena**, a contiguous chunk of virtual memory bound to a specific NUMA node.

- **Bump-Pointer Allocation**: Arenas use an extremely fast, lock-free bump pointer. This is ideal for phase-based allocations where memory is freed all at once (`nkit_arena_reset`).
- **Hugepage Support**: Arenas can be backed by transparent hugepages. The `nkit_arena_coalesce` function allows returning unused 2MB regions to the OS (`MADV_DONTNEED`) while keeping the virtual mappings intact.
- **Node Pinning & Migration**: Memory is pinned to the target node upon creation. If thread affinities change, memory can be forcibly migrated to a new node using kernel page migration (`nkit_memory_migrate`).

## 2. Slab Allocator (`nkit_slab_t`)

Built on top of the Arena is the **Slab Allocator**, which manages fixed-size objects.

- **Lock-Free O(1) Operations**: Uses an underlying ring buffer to provide an O(1) lock-free free-list.
- **Node-Local**: Each slab is allocated from a single node's arena.
- **Alignment**: Objects are strictly aligned to cache-line boundaries (64 bytes) to prevent false sharing.

## 3. Global Multi-Size-Class Memory Pool (`nkit_mempool_t`)

The highest-level abstraction is the **Memory Pool**, a drop-in thread-safe allocator suitable for general workloads.

- **Per-Node Slabs**: It initializes multiple slab allocators on *each* NUMA node for various size classes (e.g., 32B up to 16KB).
- **Topology-Aware Routing**: When a thread calls `nkit_mempool_alloc`, the pool detects the thread's current NUMA node and routes the allocation to the corresponding local slab. This guarantees memory locality without explicit user hinting.
- **Lock-Free Fast Paths**: Fully lock-free in the fast path due to the underlying lock-free slab implementation.

## Summary

By stacking these allocators, `libnumakit` abstracts away the complexities of NUMA affinity while providing bare-metal performance:

`mempool (routes by node) -> slab (O(1) reusable slots) -> arena (node-pinned virtual memory)`
