// Test 02: Functions, Overloading, Recursion, and References

extern int printf(const char *fmt, ...);

// Overloaded functions
int compute(int x) {
    return x * 2;
}

int compute(int x, int y) {
    return x + y;
}

// Pass-by-reference
void swap(int &a, int &b) {
    int tmp = a;
    a = b;
    b = tmp;
}

// Recursion
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    // 1. Overloading
    int r1 = compute(21);
    int r2 = compute(10, 32);

    if (r1 != 42) {
        printf("FAIL: compute(21) expected 42, got %d\n", r1);
        return 1;
    }
    if (r2 != 42) {
        printf("FAIL: compute(10, 32) expected 42, got %d\n", r2);
        return 2;
    }

    // 2. References / swap
    int x = 100;
    int y = 200;
    swap(x, y);

    if (x != 200 || y != 100) {
        printf("FAIL: swap failed: x=%d, y=%d\n", x, y);
        return 3;
    }

    // 3. Recursion
    int f5 = factorial(5);
    if (f5 != 120) {
        printf("FAIL: factorial(5) expected 120, got %d\n", f5);
        return 4;
    }

    printf("PASS: 02_functions completed successfully\n");
    return 0;
}
