# libnumakit Locking Protocol Design

In NUMA systems, traditional synchronization primitives suffer from extreme performance degradation due to interconnect saturation (cache-line bouncing across sockets). `libnumakit` mitigates this via specialized lock implementations.

## 1. MCS Locks (`nkit_mcs_lock_t`)

The MCS lock is the primary primitive for highly contended, uncontested, or critical sections.

- **Local Spinning**: Threads do not spin on a global lock variable. Instead, each thread allocates a node (`nkit_mcs_node_t`) on its local stack.
- **Queue-Based**: The global lock merely acts as a tail pointer. Threads enqueue their local nodes and spin on their *local* `locked` flag.
- **NUMA Advantage**: Since spinning occurs on thread-local cache lines, it generates zero cross-socket interconnect traffic. When the lock is released, only one cache-line invalidation occurs (updating the next node's flag).

## 2. Reader-Writer Spinlocks (`nkit_rws_lock_t`)

Used for scenarios with high read concurrency and infrequent writes.

- **Writer Preference**: Prevents writer starvation. If a writer is waiting, new readers are blocked from acquiring the lock.
- **Compact State**: A single 32-bit atomic (`state`) tracks the active writer, waiting writers, and reader count.

## 3. Ticket Locks with Proportional Backoff (`nkit_ticket_lock_t`)

Ensures strict FIFO ordering.

- **Proportional Backoff**: To mitigate the "thundering herd" problem and cache-line contention, threads polling the `serving` counter apply a backoff delay proportional to their distance from the front of the queue.
- **Cache-Line Padding**: The `ticket` and `serving` atomics are padded to 64 bytes (`alignas(64)`) to eliminate false sharing.

## 4. Partitioned Counters (`nkit_pcounter_t`)

A distributed, eventually consistent counter designed for high-frequency updates without interlocking.

- **Per-Node Slots**: The counter allocates an aligned slot on every NUMA node.
- **Local Increments**: A thread incrementing the counter only modifies its local node's slot using relaxed atomics.
- **Lazy Aggregation**: Reads iterate over all slots and sum them up using acquire semantics. This shifts the overhead from the critical path (writers) to the readers.
