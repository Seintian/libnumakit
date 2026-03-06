# CI & Testing Strategy

To ensure correctness and performance of `libnumakit` on Non-Uniform Memory Access (NUMA) systems while maintaining a fast and reliable CI pipeline, we use a multi-tiered testing strategy.

## Environments

### 1. UMA (Uniform Memory Access) - Native Host
- **Platform**: GitHub Actions `ubuntu-latest` (x86_64) and `ubuntu-24.04-arm` (ARM64).
- **Purpose**: Baseline verification of compilation, unit tests, and basic logic.
- **Limitation**: Cannot verify cross-node memory placement or thread affinity beyond core 0-1.

### 2. NUMA (Non-Uniform Memory Access) - User-Mode Linux (UML)
- **Platform**: x86_64 host running a virtualized Linux kernel as a user-space process.
- **Strategy**: The UML kernel is booted with the `numa=fake=2` parameter to simulate a multi-socket machine.
- **Pros**: 
    - Real kernel implementation of `mbind()`, `move_pages()`, and `set_mempolicy()`.
    - No hardware virtualization requirements (runs on standard GHA runners).
    - Boots in under 2 seconds.
- **Verification**: Used to validate task pool routing, page migration, and NUMA-aware data structures.

## Known Limitations

### ARM64 NUMA Testing
Currently, native NUMA testing on ARM64 CI runners is limited:
- **No KVM**: GitHub's standard ARM64 runners do not provide access to `/dev/kvm`, preventing high-speed MicroVM testing.
- **No UML Support**: As of Linux 6.13, User-Mode Linux (`ARCH=um`) does not yet support the `arm64` architecture in the mainline kernel.
- **Fallback**: NUMA-aware features on ARM64 are verified for compilation and basic correctness in UMA mode, but full cross-node verification requires a self-hosted NUMA-capable ARM64 runner or QEMU emulation (which is currently deactivated due to slowness).
