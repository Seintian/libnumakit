# Contributing to libnumakit

First off, thank you for considering contributing to `libnumakit`!

This library aims to be the standard foundation for high-performance NUMA-aware applications. To achieve this, we maintain strict standards for **performance**, **memory safety**, and **API stability**.

Please read this document carefully before opening a Pull Request.

## 🛠️ Development Environment

### Prerequisites

You will need the following tools installed:

- **C Compiler**: GCC 9+ or Clang 10+ (Must support C11/C17).
- **Build System**: CMake 3.15+ and Ninja (recommended) or Make.
- **Dependencies**: `libhwloc-dev` (v2.0+).
- **Testing**: `valgrind` (for memory leaks) and `qemu-system-x86_64` (for NUMA topology emulation).

### Setting Up

1. **Clone The repo**:

    ```sh
    git clone https://github.com/your-username/libnumakit.git
    cd libnumakit
    ```

2. **Configure for Debugging**: We use strict warning flags (`-Wall -Wextra -Werror`) in development builds.

    ```sh
    mkdir build_dev && cd build_dev
    cmake -DCMAKE_BUILD_TYPE=Debug -DNKIT_BUILD_TESTS=ON ..
    ```

## 🧪 Testing Protocol

Testing is the most critical part of this library. Because most developers do not have 4-socket servers at home, we rely on two tiers of testing.

### 1. Unit Tests (Run Locally)

These test the logic of data structures (Ring buffers, Hash maps) without relying on actual hardware topology.

```sh
cd build_dev
ctest -R unit_
```

### 2. NUMA Emulation (The "QEMU Test")

**Do not skip this**. If your code touches `scheduler`, `affinity`, or `arena` logic, you **must** verify it under a simulated NUMA environment. We provide a script in [scripts/run_qemu_tests.sh](scripts/run_qemu_tests.sh) that boots a minimal kernel with 4 NUMA nodes.

*If you cannot run QEMU, please mark your PR as "Requires Hardware Verification" so a maintainer can run it on a real rig.*

## 📐 Coding Standards

We follow a strict "Modern C" style guide to ensure portability and readability.

### 1. Naming Conventions

- **Namespace**: All public symbols must start with `nkit_`.
- **Structs**: Typedefs end in `_t`.

  * `nkit_arena_t` (Good)
  * `Arena` (Bad - pollutes global namespace)

- **Functions**: `snake_case`.

  * `nkit_arena_create(...)` (Good)
  * `nkitArenaCreate` (Bad)
  
- **Private/Internal**: Functions not exported in the public headers should be `static` or prefixed with `_nkit_` if shared internally.

### 2. Header Hygiene

- **Public API**: Place headers in `include/numakit/`. These should contain **zero** implementation details. Use opaque pointers (forward declarations) whenever possible.
- **Private Implementation**: struct definitions go in `src/internal.h` or within the `.c` file if only used there.

### 3. Memory & Safety

- **Zero Allocations in Critical Paths**: Once an `arena` or `lock` is initialized, functions operating on them (like `lock_acquire` or `ring_push`) **must not** call `malloc` or `syscall`.
- **Const Correctness**: Use `const` for any pointer that is not modified.

    ```c
    // Good
    int nkit_arena_get_id(const nkit_arena_t* arena);
    ```

- **Error Handling**: Return `int` (0 for success, negative errno for failure) rather than using `errno` directly.

## 📝 Pull Request Process

1. **Feature Branch**: Create a branch for your work (`feat/mcs-lock-optimization` or `fix/memory-leak-topology`).
2. **Atomic Commits**: Keep commits small and focused.
3. **Commit Messages**: We follow [Conventional Commits](https://www.conventionalcommits.org/).

  - `feat: add ticket lock implementation`
  - `fix: resolve segfault in arena destroy`
  - `perf: reduce cache-line bouncing in spinlock`
  - `docs: update installation guide`

4. **Update Documentation**: If you added a new API function, you must add the Doxygen comments in the header file.

## ⚠️ Performance Guidelines

`libnumakit` is a performance library. We will reject PRs that introduce:

1. **False Sharing**: Placing two actively written atomic variables on the same cache line. Use `alignas(64)` padding where necessary.
2. **Unnecessary Barriers**: Do not use `memory_order_seq_cst` unless absolutely required. Prefer `acquire/release` semantics.
3. **Bloat**: Do not pull in external dependencies unless discussed in an Issue first.

## ⚖️ License

By contributing to `libnumakit`, you agree that your contributions will be licensed under the GPL License.
