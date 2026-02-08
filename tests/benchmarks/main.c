#include <stdio.h>
#include <string.h>
#include "benchmarks.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <benchmark_name>\n", argv[0]);
        printf("Available benchmarks:\n");
        printf("  throughput   - Test ring buffer throughput (00)\n");
        printf("  latency      - Test ring buffer latency (01)\n");
        printf("  bandwidth    - Test memory bandwidth (02)\n");
        printf("  contention   - Test lock contention (03)\n");
        printf("  rw_scaling   - Test reader-writer scaling (04)\n");
        printf("  all          - Run all benchmarks sequentially\n");
        return 1;
    }

    if (strcmp(argv[1], "throughput") == 0) {
        return bench_00_throughput(argc, argv);
    } 
    else if (strcmp(argv[1], "latency") == 0) {
        return bench_01_latency(argc, argv);
    }
    else if (strcmp(argv[1], "bandwidth") == 0) {
        return bench_02_bandwidth(argc, argv);
    }
    else if (strcmp(argv[1], "contention") == 0) {
        return bench_03_contention(argc, argv);
    }
    else if (strcmp(argv[1], "rw_scaling") == 0) {
        return bench_04_rw_scaling(argc, argv);
    }
    else if (strcmp(argv[1], "all") == 0) {
        printf(">>> RUNNING BENCHMARK 00: THROUGHPUT <<<\n");
        bench_00_throughput(argc, argv);

        printf("\n\n>>> RUNNING BENCHMARK 01: LATENCY <<<\n");
        bench_01_latency(argc, argv);

        printf("\n\n>>> RUNNING BENCHMARK 02: BANDWIDTH <<<\n");
        bench_02_bandwidth(argc, argv);

        printf("\n\n>>> RUNNING BENCHMARK 03: CONTENTION <<<\n");
        bench_03_contention(argc, argv);

        printf("\n\n>>> RUNNING BENCHMARK 04: RW SCALING <<<\n");
        bench_04_rw_scaling(argc, argv);
        return 0;
    }

    printf("Unknown benchmark: %s\n", argv[1]);
    return 1;
}
