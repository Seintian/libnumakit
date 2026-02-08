/**
 * @file 00_topology_check.c
 * @brief Minimal integration test for NUMA hardware/emulation.
 */

#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <numakit/numakit.h>

int main(void) {
    // 1. Mount virtual filesystems so hwloc can read topology
    // We need to create the mount points first because our initramfs is empty
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    printf("[INTEGRATION] Topology check started...\n");

    // 2. Initialize Topology
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    printf("libnumakit initialized successfully.\n");

    // 3. Cleanup
    nkit_teardown();

    printf("[INTEGRATION] Topology check passed.\n");
    return 0;
}
