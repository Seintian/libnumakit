#define _GNU_SOURCE

#include <numakit/sched.h>
#include "../internal.h"

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Internal: Thread-Local State
// -----------------------------------------------------------------------------
// We use TLS so multiple threads can profile themselves simultaneously 
// without fighting over a global struct.
static __thread int t_fd_miss = -1;
static __thread int t_fd_instr = -1;

// -----------------------------------------------------------------------------
// Internal: perf_event_open Wrapper
// -----------------------------------------------------------------------------
static long _sys_perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                                 int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static int _open_counter(uint64_t type, uint64_t config) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = type;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = config;
    pe.disabled = 1;          // Start disabled
    pe.exclude_kernel = 1;    // Don't count kernel noise
    pe.exclude_hv = 1;        // Don't count hypervisor noise

    // pid=0 (current thread), cpu=-1 (any cpu), group_fd=-1 (new group)
    return (int) _sys_perf_event_open(&pe, 0, -1, -1, 0);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void nkit_balancer_set_threshold(double mpki) {
    if (mpki > 0.0) {
        g_nkit_ctx.balancer_threshold_mpki = mpki;
    }
}

int nkit_balancer_start(void) {
    // 1. Close existing counters if any
    if (t_fd_miss != -1) close(t_fd_miss);
    if (t_fd_instr != -1) close(t_fd_instr);

    // 2. Open "Hardware Cache Misses"
    // This is the #1 indicator of NUMA badness.
    t_fd_miss = _open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
    if (t_fd_miss == -1) {
        // Fallback: If HW counters aren't available (e.g. VM), return error
        return -1;
    }

    // 3. Open "Instructions Retired"
    // We need this to calculate the ratio (Misses per Instruction).
    t_fd_instr = _open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
    if (t_fd_instr == -1) {
        close(t_fd_miss);
        t_fd_miss = -1;
        return -1;
    }

    // 4. Reset and Enable
    ioctl(t_fd_miss, PERF_EVENT_IOC_RESET, 0);
    ioctl(t_fd_instr, PERF_EVENT_IOC_RESET, 0);
    ioctl(t_fd_miss, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(t_fd_instr, PERF_EVENT_IOC_ENABLE, 0);

    return 0;
}

nkit_advice_e nkit_balancer_check(void) {
    if (t_fd_miss == -1 || t_fd_instr == -1) {
        return NKIT_ADVISE_ERROR;
    }

    // 1. Stop counting
    ioctl(t_fd_miss, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(t_fd_instr, PERF_EVENT_IOC_DISABLE, 0);

    // 2. Read values
    long long count_miss = 0;
    long long count_instr = 0;

    if (read(t_fd_miss, &count_miss, sizeof(long long)) == -1) return NKIT_ADVISE_ERROR;
    read(t_fd_instr, &count_instr, sizeof(long long)); // Best effort

    // 3. Close FDs (Cleanup)
    close(t_fd_miss);
    close(t_fd_instr);
    t_fd_miss = -1;
    t_fd_instr = -1;

    // 4. Analyze
    if (count_instr < 1000) {
        // Not enough data to make a decision
        return NKIT_ADVISE_STAY;
    }

    // Calculate MPKI (Misses Per Kilo-Instruction)
    double mpki = (double)count_miss / ((double)count_instr / 1000.0);

    // COMPARE AGAINST CONFIGURABLE THRESHOLD
    if (mpki > g_nkit_ctx.balancer_threshold_mpki) {
        return NKIT_ADVISE_MIGRATE;
    }

    return NKIT_ADVISE_STAY;
}
