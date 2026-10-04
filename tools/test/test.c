/**
 * @file test.c
 * @brief The host suite's harness: see test.h.
 */
#include "test.h"

#include <stdio.h>

int failures = 0;

void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}
