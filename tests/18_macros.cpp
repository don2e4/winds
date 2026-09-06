#include <iostream>

#define BUFFER_SIZE 1024
#define OFFSET 50
#define EMPTY_MACRO

#define ADD(a, b) ((a) + (b))
#define MULTIPLY(a, b) ((a) * (b))
#define DOUBLE(x) ADD(x, x)
#define TRIPLE(x) ADD(DOUBLE(x), (x))

#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

#define CONCAT(a, b) a ## b
#define MAKE_NAME(prefix, suffix) prefix ## _ ## suffix

#define SET_POINT(pt, xval, yval) \
    (pt).x = (xval);              \
    (pt).y = (yval)

#define ZERO_ARGS() 777

#define PASS_EXPR(expr) expr

struct Point {
    int x;
    int y;
};

int helper_add(int a, int b) {
    return a + b;
}

int main() {
    /* 1. Object-like macro */
    int buf_size = BUFFER_SIZE;
    EMPTY_MACRO;
    if (buf_size != 1024) {
        std::cout << "FAIL: BUFFER_SIZE\n";
        return 1;
    }

    /* 2. Function-like macro */
    int sum = ADD(15, 25);
    if (sum != 40) {
        std::cout << "FAIL: ADD\n";
        return 2;
    }

    int prod = MULTIPLY(3 + 2, 4 + 1); /* ((3 + 2) * (4 + 1)) = 25 */
    if (prod != 25) {
        std::cout << "FAIL: MULTIPLY\n";
        return 3;
    }

    /* 3. Nested macro expansion */
    int d = DOUBLE(21);
    if (d != 42) {
        std::cout << "FAIL: DOUBLE\n";
        return 4;
    }

    int t = TRIPLE(10);
    if (t != 30) {
        std::cout << "FAIL: TRIPLE\n";
        return 5;
    }

    /* 4. Zero-parameter macro */
    int z = ZERO_ARGS();
    if (z != 777) {
        std::cout << "FAIL: ZERO_ARGS\n";
        return 6;
    }

    /* 5. Stringification # */
    const char *str1 = STR(hello_winds);
    if (str1[0] != 'h' || str1[5] != '_' || str1[10] != 's') {
        std::cout << "FAIL: STR\n";
        return 7;
    }

    /* 6. Token pasting ## */
    int CONCAT(var, 1) = 123;
    if (var1 != 123) {
        std::cout << "FAIL: CONCAT\n";
        return 8;
    }

    int MAKE_NAME(tested, value) = 456;
    if (tested_value != 456) {
        std::cout << "FAIL: MAKE_NAME\n";
        return 9;
    }

    /* 7. Multi-line continuation with backslash \ */
    Point pt;
    SET_POINT(pt, 100, 200);
    if (pt.x != 100 || pt.y != 200) {
        std::cout << "FAIL: SET_POINT\n";
        return 10;
    }

    /* 8. Arguments with nested parentheses and commas */
    int nested_arg = PASS_EXPR(helper_add(10, 20));
    if (nested_arg != 30) {
        std::cout << "FAIL: PASS_EXPR\n";
        return 11;
    }

    /* 9. #undef and redefinition */
#define TEMP_VAL 50
    int v1 = TEMP_VAL;
    if (v1 != 50) {
        std::cout << "FAIL: TEMP_VAL v1\n";
        return 12;
    }
#undef TEMP_VAL
#define TEMP_VAL 100
    int v2 = TEMP_VAL;
    if (v2 != 100) {
        std::cout << "FAIL: TEMP_VAL v2\n";
        return 13;
    }

    /* 10. Conditional compilation (#ifdef, #ifndef, #if defined) */
    int cond_check = 0;
#ifdef BUFFER_SIZE
    cond_check += 1;
#else
    cond_check += 100;
#endif

#ifndef NEVER_DEFINED_MACRO
    cond_check += 2;
#else
    cond_check += 200;
#endif

#if defined(BUFFER_SIZE)
    cond_check += 4;
#endif

#if !defined(NOT_DEFINED)
    cond_check += 8;
#endif

    if (cond_check != 15) {
        std::cout << "FAIL: cond_check\n";
        return 14;
    }

    /* 11. Built-in line and file macros */
    int cur_line = __LINE__;
    if (cur_line <= 0) {
        std::cout << "FAIL: __LINE__\n";
        return 15;
    }

    const char *cur_file = __FILE__;
    if (cur_file == nullptr || cur_file[0] == '\0') {
        std::cout << "FAIL: __FILE__\n";
        return 16;
    }

    std::cout << "PASS: 18_macros completed successfully\n";
    return 0;
}
