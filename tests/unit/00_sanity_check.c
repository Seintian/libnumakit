/**
 * @file 00_sanity_check.c
 * @brief Minimal unit test to verify build system integrity.
 */

#include <stdio.h>
#include <assert.h>

#include <numakit/numakit.h>

int main(void) {
    printf("[UNIT] Sanity check started...\n");

    // 1. Basic assertion to prove assert() works
    assert(1 + 1 == 2);

    printf("[UNIT] Sanity check passed.\n");
    return 0; // Return 0 means SUCCESS to CTest
}
