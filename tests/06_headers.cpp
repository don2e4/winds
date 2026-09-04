// Test 06: Headers, #include "...", #include <...>, #pragma once, #ifndef/#define guards
#include "tests/include/math_utils.h"
#include "tests/include/nested_guard.h"
#include "tests/include/common.h" // Second inclusion tests #pragma once deduplication

int main() {
    int v1 = double_val(15);
    if (v1 != 30) {
        printf("FAIL: double_val(15) expected 30, got %d\n", v1);
        return 1;
    }

    int v2 = add3(10, 20, 30);
    if (v2 != 60) {
        printf("FAIL: add3(10, 20, 30) expected 60, got %d\n", v2);
        return 2;
    }

    int v3 = square(7);
    if (v3 != 49) {
        printf("FAIL: square(7) expected 49, got %d\n", v3);
        return 3;
    }

    int v4 = get_magic_number();
    if (v4 != 42) {
        printf("FAIL: get_magic_number() expected 42, got %d\n", v4);
        return 4;
    }

    printf("PASS: 06_headers completed successfully\n");
    return 0;
}
