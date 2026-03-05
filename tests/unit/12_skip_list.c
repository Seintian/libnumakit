#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <numakit/numakit.h>
#include <numakit/structs/skip_list.h>
#include "unit.h"

static void test_skip_basic(void) {
    printf("  [Check] Basic Put/Get/Remove\n");
    nkit_skip_t* sl = nkit_skip_create(16);
    assert(sl != NULL);
    assert(nkit_skip_count(sl) == 0);

    const char* key1 = "apple";
    const char* key2 = "banana";
    const char* key3 = "cherry";
    int val1 = 100, val2 = 200, val3 = 300;

    // Put
    assert(nkit_skip_put(sl, key1, strlen(key1), &val1) == 0);
    assert(nkit_skip_put(sl, key2, strlen(key2), &val2) == 0);
    assert(nkit_skip_put(sl, key3, strlen(key3), &val3) == 0);
    assert(nkit_skip_count(sl) == 3);

    // Get
    assert(*(int*)nkit_skip_get(sl, key1, strlen(key1)) == 100);
    assert(*(int*)nkit_skip_get(sl, key2, strlen(key2)) == 200);
    assert(*(int*)nkit_skip_get(sl, key3, strlen(key3)) == 300);
    assert(nkit_skip_get(sl, "unknown", 7) == NULL);

    // Update
    int val1_new = 101;
    assert(nkit_skip_put(sl, key1, strlen(key1), &val1_new) == 0);
    assert(*(int*)nkit_skip_get(sl, key1, strlen(key1)) == 101);
    assert(nkit_skip_count(sl) == 3);

    // Remove
    assert(nkit_skip_remove(sl, key2, strlen(key2)) == 0);
    assert(nkit_skip_get(sl, key2, strlen(key2)) == NULL);
    assert(nkit_skip_count(sl) == 2);

    assert(nkit_skip_remove(sl, "unknown", 7) == -1);

    nkit_skip_destroy(sl);
}

static void test_skip_stress(void) {
    printf("  [Check] Stress Insert (Large Volume)\n");
    nkit_skip_t* sl = nkit_skip_create(16);
    
    int num_items = 1000;
    static char keys[1000][16];
    static int values[1000];

    for (int i = 0; i < num_items; i++) {
        sprintf(keys[i], "key_%04d", i);
        values[i] = i;
        assert(nkit_skip_put(sl, keys[i], strlen(keys[i]), &values[i]) == 0);
    }

    assert(nkit_skip_count(sl) == (size_t)num_items);

    for (int i = 0; i < num_items; i++) {
        void* val = nkit_skip_get(sl, keys[i], strlen(keys[i]));
        assert(val != NULL);
        assert(*(int*)val == i);
    }

    nkit_skip_destroy(sl);
}

int test_12_skip_list(void) {
    printf("[UNIT] Skip List Test Started...\n");

    if (nkit_init() != 0) {
        fprintf(stderr, "Failed to init libnumakit\n");
        return 1;
    }

    test_skip_basic();
    test_skip_stress();

    nkit_teardown();
    printf("[UNIT] Skip List Test Passed\n");
    return 0;
}
