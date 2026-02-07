# libnumakit Architecture

This document describes the high-level design choices, memory layout, and component interaction within `libnumakit`.

## 1. Core Principles

- **Zero-Inference:** The library never guesses. If a user asks for memory on Node 0, we either provide it or fail. We do not fallback to Node 1 silently.
- **Opaque Handles:** All public types (`nkit_arena_t`, `nkit_lock_t`) are opaque pointers. The internal structs are defined in `src/internal.h` to allow ABI-compatible upgrades.
- **Kernel Bypass (where possible):** We use `mmap` directly for arenas and `futex` directly for locks, bypassing libc wrappers where performance dictates.

## 2. Module Hierarchy

The library is layered as follows:

| Layer | Components | Description |
| :--- | :--- | :--- |
| **L3: High-Level** | `nkit_ring`, `nkit_hash` | Data structures that use L2/L1. |
| **L2: Policies** | `nkit_sched`, `nkit_sync` | Thread migration logic, MCS locks. |
| **L1: Allocation** | `nkit_arena` | Hugepage management, slab allocators. |
| **L0: Hardware** | `hwloc_backend` | Raw topology discovery (internal only). |

## 3. Memory Arena Design

The `nkit_arena_t` is the fundamental building block.

```ascii
+-----------------------+ <--- Arena Base (Hugepage Aligned)
| Header (Node ID, Sz)  |
+-----------------------+
| Chunk 0 (Used)        |
+-----------------------+
| Chunk 1 (Free)        |
+-----------------------+
| ...                   |
+-----------------------+
```

- **Bump Pointer**: Default allocation is a simple pointer bump (`curr + size`).
- **Thread Safety**: Arenas are not thread-safe by default. The user must use `nkit_arena_local` (thread-local arena) or protect shared arenas with `nkit_lock`.

## 4. Locking Strategy (MCS)

We use MCS (Mellor-Crummey Scott) locks for high-contention scenarios.

- **Standard Mutex**: All threads fight for one cache line (Global).
- **MCS Lock**: Threads join a linked list. CPU A spins on its own cache line. CPU B releases the lock by writing to CPU A's cache line.
- **Benefit**: Reduces interconnect traffic by O(N).

## 5. Future Work

- **Auto-Balancing**: A background thread that monitors cache misses and migrates threads/memory automatically.
- **Hugepage coalescing**: returning unused hugepages to the OS.
