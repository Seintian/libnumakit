#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <numakit/numakit.h>

int main() {
    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to initialize libnumakit\n");
        return 1;
    }

    int node_id = 0; // Try allocating on node 0
    size_t size = 1024 * 1024; // 1 MB

    nkit_arena_t *arena = nkit_arena_create(node_id, size);
    if (!arena) {
        fprintf(stderr, "Failed to create arena on node %d\n", node_id);
    } else {
        printf("Successfully created %zu bytes arena on node %d\n", size, node_id);

        void *ptr = nkit_arena_alloc(arena, 256);
        if (ptr) {
            strcpy((char*)ptr, "Hello, NUMA-aware arena!");
            printf("Allocated memory and wrote: %s\n", (char*)ptr);
        }

        nkit_arena_destroy(arena);
        printf("Arena destroyed.\n");
    }

    nkit_teardown();
    return 0;
}
