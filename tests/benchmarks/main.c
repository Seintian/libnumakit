#include <stdio.h>
#include <string.h>
#include "benchmarks.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <benchmark_name>\n", argv[0]);
        printf("Available benchmarks:\n");
        printf("  throughput   - Test ring buffer throughput (00)\n");
        printf("  latency      - Test ring buffer latency (01)\n");
        printf("  all          - Run all benchmarks sequentially\n");
        return 1;
    }

    if (strcmp(argv[1], "throughput") == 0) {
        return bench_00_throughput(argc, argv);
    } 
    else if (strcmp(argv[1], "latency") == 0) {
        return bench_01_latency(argc, argv);
    }
    else if (strcmp(argv[1], "all") == 0) {
        printf(">>> RUNNING BENCHMARK 00: THROUGHPUT <<<\n");
        bench_00_throughput(argc, argv);

        printf("\n\n>>> RUNNING BENCHMARK 01: LATENCY <<<\n");
        bench_01_latency(argc, argv);
        return 0;
    }

    printf("Unknown benchmark: %s\n", argv[1]);
    return 1;
}
