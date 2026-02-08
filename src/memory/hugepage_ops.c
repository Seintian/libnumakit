#define _GNU_SOURCE

#include "../internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

size_t _nkit_get_hugepage_size(void) {
    FILE* fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    char line[128];
    size_t size_kb = 0;

    // Scan for "Hugepagesize:     2048 kB"
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Hugepagesize:", 13) == 0) {
            if (sscanf(line, "%*s %zu", &size_kb) == 1) {
                break;
            }
        }
    }
    fclose(fp);

    // Return in bytes (0 if not found)
    return size_kb * 1024;
}
