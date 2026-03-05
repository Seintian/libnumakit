#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#define _GNU_SOURCE

#include <numakit/numakit.h>
#include <numakit/structs/hash_table.h>
#include "unit.h"

// ============================================================================
// Test 1: Create / Destroy (Smoke Test)
// ============================================================================
static void test_create_destroy(void) {
    nkit_hash_t* ht = nkit_hash_create(0, 16);
    assert(ht != NULL);
    assert(nkit_hash_count(ht) == 0);
    nkit_hash_destroy(ht);
    printf("  [Check] Create/Destroy: OK\n");
}

// ============================================================================
// Test 2: Put / Get (Basic Key-Value)
// ============================================================================
static void test_put_get(void) {
    nkit_hash_t* ht = nkit_hash_create(0, 32);
    assert(ht != NULL);

    const char* key = "hello";
    int val = 42;

    int ret = nkit_hash_put(ht, key, strlen(key), &val);
    assert(ret == 0);
    assert(nkit_hash_count(ht) == 1);

    void* result = nkit_hash_get(ht, key, strlen(key));
    assert(result == &val);
    assert(*(int*)result == 42);

    // Non-existent key
    void* miss = nkit_hash_get(ht, "nope", 4);
    assert(miss == NULL);

    nkit_hash_destroy(ht);
    printf("  [Check] Put/Get: OK\n");
}

// ============================================================================
// Test 3: Overwrite (Same Key, New Value)
// ============================================================================
static void test_overwrite(void) {
    nkit_hash_t* ht = nkit_hash_create(0, 32);
    assert(ht != NULL);

    const char* key = "counter";
    int v1 = 100;
    int v2 = 200;

    nkit_hash_put(ht, key, strlen(key), &v1);
    assert(*(int*)nkit_hash_get(ht, key, strlen(key)) == 100);
    assert(nkit_hash_count(ht) == 1);

    // Overwrite with new value
    nkit_hash_put(ht, key, strlen(key), &v2);
    assert(*(int*)nkit_hash_get(ht, key, strlen(key)) == 200);
    assert(nkit_hash_count(ht) == 1); // Count should NOT increase

    nkit_hash_destroy(ht);
    printf("  [Check] Overwrite: OK\n");
}

// ============================================================================
// Test 4: Remove
// ============================================================================
static void test_remove(void) {
    nkit_hash_t* ht = nkit_hash_create(0, 32);
    assert(ht != NULL);

    const char* k1 = "alpha";
    const char* k2 = "beta";
    int v1 = 1, v2 = 2;

    nkit_hash_put(ht, k1, strlen(k1), &v1);
    nkit_hash_put(ht, k2, strlen(k2), &v2);
    assert(nkit_hash_count(ht) == 2);

    int ret = nkit_hash_remove(ht, k1, strlen(k1));
    assert(ret == 0);
    assert(nkit_hash_count(ht) == 1);
    assert(nkit_hash_get(ht, k1, strlen(k1)) == NULL);

    // k2 must still be retrievable
    assert(nkit_hash_get(ht, k2, strlen(k2)) == &v2);

    // Removing non-existent key is an error
    assert(nkit_hash_remove(ht, "ghost", 5) == -1);

    nkit_hash_destroy(ht);
    printf("  [Check] Remove: OK\n");
}

// ============================================================================
// Test 5: Collision Stress (Fill past 50% to exercise Robin Hood probing)
// ============================================================================
static void test_collision_stress(void) {
    // Small table to force many collisions
    size_t cap = 64;
    nkit_hash_t* ht = nkit_hash_create(0, cap);
    assert(ht != NULL);

    // Insert up to 75% load (48 entries into 64 buckets)
    int values[48];
    char keys[48][16];

    for (int i = 0; i < 48; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key_%04d", i);
        values[i] = i * 10;
        int ret = nkit_hash_put(ht, keys[i], strlen(keys[i]), &values[i]);
        assert(ret == 0);
    }

    assert(nkit_hash_count(ht) == 48);

    // Verify all entries are retrievable
    for (int i = 0; i < 48; i++) {
        void* v = nkit_hash_get(ht, keys[i], strlen(keys[i]));
        assert(v != NULL);
        assert(*(int*)v == i * 10);
    }

    // Remove half, verify the other half survives
    for (int i = 0; i < 24; i++) {
        assert(nkit_hash_remove(ht, keys[i], strlen(keys[i])) == 0);
    }
    assert(nkit_hash_count(ht) == 24);

    for (int i = 24; i < 48; i++) {
        void* v = nkit_hash_get(ht, keys[i], strlen(keys[i]));
        assert(v != NULL);
        assert(*(int*)v == i * 10);
    }

    nkit_hash_destroy(ht);
    printf("  [Check] Collision Stress: OK\n");
}

// ============================================================================
// Test 6: Multi-Threaded Contention
// ============================================================================
#define MT_NUM_THREADS 4
#define MT_KEYS_PER_THREAD 256

static nkit_hash_t* mt_ht;
// Persistent key storage: keys must outlive worker threads because the hash
// table stores key *pointers*, not copies.
static char mt_keys[MT_NUM_THREADS][MT_KEYS_PER_THREAD][32];

static void* mt_worker(void* arg) {
    int id = *(int*)arg;

    for (int i = 0; i < MT_KEYS_PER_THREAD; i++) {
        // Write into persistent storage
        snprintf(mt_keys[id][i], sizeof(mt_keys[id][i]), "T%d_%d", id, i);
        int ret = nkit_hash_put(mt_ht, mt_keys[id][i],
                                strlen(mt_keys[id][i]), arg);
        assert(ret == 0);
    }
    return NULL;
}

static void test_multithread(void) {
    // Need enough capacity for all threads' keys at 75% load
    // 4 threads * 256 keys = 1024 entries -> need capacity >= 1024 / 0.75 = 1366 -> 2048
    mt_ht = nkit_hash_create(0, 2048);
    assert(mt_ht != NULL);

    int ids[MT_NUM_THREADS];
    pthread_t threads[MT_NUM_THREADS];

    for (int i = 0; i < MT_NUM_THREADS; i++) {
        ids[i] = i;
        int ret = pthread_create(&threads[i], NULL, mt_worker, &ids[i]);
        assert(ret == 0);
    }

    for (int i = 0; i < MT_NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    size_t expected = (size_t)MT_NUM_THREADS * MT_KEYS_PER_THREAD;
    printf("  [Check] Multi-thread: Expected %zu, Actual %zu\n",
           expected, nkit_hash_count(mt_ht));
    assert(nkit_hash_count(mt_ht) == expected);

    // Verify every key is retrievable
    for (int t = 0; t < MT_NUM_THREADS; t++) {
        for (int i = 0; i < MT_KEYS_PER_THREAD; i++) {
            void* v = nkit_hash_get(mt_ht, mt_keys[t][i],
                                    strlen(mt_keys[t][i]));
            assert(v != NULL);
        }
    }

    nkit_hash_destroy(mt_ht);
    printf("  [Check] Multi-thread Contention: OK\n");
}

static void test_dynamic_resizing(void) {
    // Start with minimal capacity
    nkit_hash_t* ht = nkit_hash_create(0, 16);
    assert(ht != NULL);

    int values[512];
    char keys[512][16];

    for (int i = 0; i < 512; i++) {
        snprintf(keys[i], sizeof(keys[i]), "res_%d", i);
        values[i] = i;
        assert(nkit_hash_put(ht, keys[i], strlen(keys[i]), &values[i]) == 0);
    }

    assert(nkit_hash_count(ht) == 512);

    // Verify all keys are still there after multiple resizes
    for (int i = 0; i < 512; i++) {
        void* v = nkit_hash_get(ht, keys[i], strlen(keys[i]));
        assert(v != NULL);
        assert(*(int*)v == i);
    }

    nkit_hash_destroy(ht);
    printf("  [Check] Dynamic Resizing: OK\n");
}

// ============================================================================
// Entry Point
// ============================================================================
int test_06_hash_table(void) {
    printf("[UNIT] Hash Table API Test Started...\n");

    test_create_destroy();
    test_put_get();
    test_overwrite();
    test_remove();
    test_collision_stress();
    test_multithread();
    test_dynamic_resizing();

    printf("[UNIT] Hash Table API Test Passed\n");
    return 0;
}
