# libnumakit Topology Mapping Design

The topology subsystem (`nkit_topology_t`) bridges the gap between hardware architecture and software task placement.

## 1. Hardware Discovery

`libnumakit` queries the OS/Kernel layers (e.g., ACPI SLIT tables, libnuma mappings) to construct a localized view of the system.

- **Nodes & CPUs**: Identifies the total number of NUMA nodes, logical processing units (PUs), and their node assignments.
- **Distance Matrix**: Extracts the relative latency values between nodes (`nkit_topo_distance`). A local node is typically distance `10`, while remote nodes scale upwards (e.g., `20`, `21`, `31` depending on the number of hops).
- **Graceful Degradation**: If the system is UMA (Uniform Memory Access) or NUMA support is disabled/unavailable, the library gracefully degrades to a single-node mapping (`nkit_topo_is_numa() == 0`), ensuring applications continue to run transparently without modification.

## 2. Distance and Balancing

The topology data directly drives the work-stealing scheduler and the memory allocator.

- **Hierarchical Stealing**: In the task scheduler, a worker thread will first attempt to steal work from other CPUs on its local NUMA node. Only if the local node is idle will it consult the distance matrix and attempt to steal from the closest remote node.
- **Memory Placement**: Similarly, memory pools use topology IDs to serve requests strictly from local arenas.

## 3. Memory Capacity Tracking

The library provides real-time querying of node capacity and available free memory (`nkit_topo_node_memory`), which the scheduler uses to avoid assigning massive memory tasks to already pressured nodes.
