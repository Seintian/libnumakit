# libnumakit Roadmap

This document tracks the planned features and improvements for the `libnumakit` library.

## 🚀 Priority Features

- [x] **NUMA-Aware Work-Stealing Queue**
  - Lock-free deque for task stealing.
  - Topology-aware stealing: prefer local nodes before remote ones.
  - Performance-optimized for fork-join workloads.

- [x] **NUMA-Aware Skip List / Linked List**
  - Sharded data structures where each shard is local to a NUMA node.
  - Minimal cross-node synchronization for global operations.

- [x] **Advanced Memory Pool**
  - General-purpose object pool with per-node free-lists.
  - Support for multiple size classes.
  - Lock-free allocation from thread-local or node-local pools.

## 🛠️ Testing & Refinement

- [x] **Fill Missing Unit Tests**
  - [x] MCS Lock (`sync/mcs_lock.c`)
  - [x] RW Spinlock (`sync/rws_lock.c`)
  - [x] Ring Buffer (`structs/ring_buffer.c`)
  - [x] Messaging System (`sched/messaging.c`)
  - [x] Basic Balancer logic (`sched/balancer.c`)

- [x] **Hardware Compatibility**
  - [x] Test on ARM64 Graviton/Ampere systems.
  - [x] Verify behavior on systems without SLIT tables.

## 📖 Documentation & Examples

- [x] **Populate Design Docs**
  - [x] `docs/design/memory_layout.md`
  - [x] `docs/design/locking_protocol.md`
  - [x] `docs/design/topology_mapping.md`

- [x] **Complete Examples**
  - [x] `examples/01_hello_numa.c`
  - [x] `examples/02_arena_alloc.c`
  - [x] Add a new `examples/03_topology_query.c` demonstration.

## 📈 Optimization

- [ ] **LTO/IPO Performance Audit**: Measure the impact of link-time optimization on cross-module calls.
- [ ] **False Sharing Audit**: Ensure all structures are properly padded and aligned for modern cache line sizes.
