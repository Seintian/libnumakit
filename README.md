# libnumakit

**The "Batteries-Included" C Toolkit for High-Performance NUMA Systems.**

`libnumakit` bridges the gap between hardware topology discovery (like hwloc) and high-performance application logic. While existing tools tell you where your CPU cores and memory nodes are, libnumakit provides the optimized data structures, allocators, and synchronization primitives to use them efficiently.

## ⚡ Why libnumakit?

On modern multi-socket servers (AMD EPYC, Intel Xeon), accessing memory across the interconnect (UPI/xGMI) is significantly slower than accessing local memory. Standard OS primitives (`malloc`, `pthread_mutex`) are often unaware of this topology, leading to:

- **False Sharing**: Cache lines bouncing between sockets.
- **Remote Allocation**: Memory pages allocated on the wrong node.
- **Thundering Herds**: Lock contention saturating the interconnect bandwidth.

**libnumakit** solves this by providing Topology-Aware replacements for standard primitives.

## 🚀 Key Features

### 1. Local-First Memory Management

- **Arena Allocators**: Pre-allocate hugepages (2MB/1GB) on specific nodes.
- **Zero-Overhead Allocation**: Bump-pointer allocation for `O(1)` performance.
- **Strict Binding**: Ensure critical data never leaves the local NUMA node.

### 2. Interconnect-Friendly Synchronization

- **MCS Locks**: Queue-based spinlocks that spin on *local* cache lines, eliminating interconnect traffic during contention.
- **Partitioned Counters**: Distributed atomic counters that aggregate lazily.

### 3. Policy-Based Thread Scheduling

- **Declarative Pinning**: Don't manually calculate bitmasks. Use policies like `NKIT_POLICY_COMPACT` (fill one socket first) or `NKIT_POLICY_SCATTER` (spread evenly across sockets).
- **Automatic Migration**: (Experimental) Move threads closer to their data.

## 🛠️ Installation

### Prerequisites

- **CMake** (3.15+)
- **C Compiler** (GCC 9+ or Clang 10+)
- **hwloc** (2.0+) - *Used internally for topology discovery*.

### Build from Source

```sh
git clone https://github.com/your-username/libnumakit.git
cd libnumakit

# Create build directory
mkdir build && cd build

# Configure (Release mode recommended for performance)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build and Install
make -j$(nproc)
sudo make install
```

## 📖 Quick Start Example

Here is how to create a thread pinned to Node 0 and allocate memory that is physically guaranteed to reside on Node 0.

```c
#include <stdio.h>
#include <numakit/numakit.h>

void worker_routine(void* arg) {
    nkit_arena_t* arena = (nkit_arena_t*)arg;
    
    // 3. High-speed allocation (No syscalls, no locks, local memory)
    int* data = nkit_arena_alloc(arena, sizeof(int) * 1024);
    
    printf("Thread running on Node %d\n", nkit_get_current_node());
}

int main() {
    // 1. Initialize Topology
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    // 2. Create a memory arena strictly bound to Node 0 (1GB size)
    nkit_arena_t* node0_arena = nkit_arena_create(
        0,              // Node ID
        1024 * 1024,    // Size (1MB)
        NKIT_PAGE_HUGE  // Use Hugepages
    );

    // 3. Launch a thread pinned to Node 0
    nkit_thread_policy_t policy = { .type = NKIT_POLICY_BIND_NODE, .node_id = 0 };
    nkit_thread_launch(policy, worker_routine, node0_arena);

    // Cleanup
    nkit_thread_join_all();
    nkit_arena_destroy(node0_arena);
    nkit_teardown();
    
    return 0;
}
```

## 🏗️ Architecture

The library is organized into three core modules:

| Module | Include Header | Description |
|--------|----------------|-------------|
| **Memory** | `<numakit/memory.h>` | Hugepage-backed arenas, slab allocators. |
| **Sched** | `<numakit/sched.h>` | Thread affinity, CPU set manipulation, migration. |
| **Sync** | `<numakit/sync.h>` | MCS locks, Ticket locks, RWS locks. |

## 📊 Performance (Benchmarks)

### MOCK DATA

| Operation | Standard `malloc`/`pthread` | libnumakit | Improvement |
|-----------|-----------------------------|------------|-------------|
| **Allocation (1KB)** | 45ns | **4ns** | **11x** |
| **Lock Contention** | 1200ns (Mutex) | **180ns** (MCS) | **6x** |
| **Remote Access** | 110ns (latency) | **60ns** (Local) | **1.8x** |

*(Full benchmark suite available in `tests/benchmarks/`)*

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for details on coding standards and how to run the QEMU-based NUMA test suite.

1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/amazing-feature`).
3. Commit your changes (`git commit -m 'Add some amazing feature'`).
4. Push to the branch (`git push origin feature/amazing-feature`).
5. Open a Pull Request.

## 📄 License

Distributed under the GPL License. See [LICENSE](LICENSE) for more information.
