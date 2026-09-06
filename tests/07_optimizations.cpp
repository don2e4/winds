// Test 07: Constant propagation, copy propagation, algebraic simplification, and CFG optimizations
extern int printf(const char *fmt, ...);

int test_algebraic(int x) {
    // Identity laws
    int a1 = x + 0;      // => x
    int a2 = 0 + x;      // => x
    int a3 = x - 0;      // => x
    int a4 = x - x;      // => 0
    int a5 = x * 1;      // => x
    int a6 = 1 * x;      // => x
    int a7 = x * 0;      // => 0
    int a8 = 0 * x;      // => 0
    int a9 = x / 1;      // => x
    int a10 = x / x;     // => 1 (x != 0)
    int a11 = x % 1;     // => 0
    int a12 = x % x;     // => 0
    int a13 = x & 0;     // => 0
    int a14 = x & x;     // => x
    int a15 = x | 0;     // => x
    int a16 = x | x;     // => x
    int a17 = x ^ 0;     // => x
    int a18 = x ^ x;     // => 0
    int a19 = x << 0;    // => x
    int a20 = x >> 0;    // => x
    int a21 = (x == x);  // => 1
    int a22 = (x != x);  // => 0
    int a23 = (x <= x);  // => 1
    int a24 = (x >= x);  // => 1
    int a25 = (x < x);   // => 0
    int a26 = (x > x);   // => 0

    return (a1 == x) && (a2 == x) && (a3 == x) && (a4 == 0) &&
           (a5 == x) && (a6 == x) && (a7 == 0) && (a8 == 0) &&
           (a9 == x) && (a10 == 1) && (a11 == 0) && (a12 == 0) &&
           (a13 == 0) && (a14 == x) && (a15 == x) && (a16 == x) &&
           (a17 == x) && (a18 == 0) && (a19 == x) && (a20 == x) &&
           (a21 == 1) && (a22 == 0) && (a23 == 1) && (a24 == 1) &&
           (a25 == 0) && (a26 == 0);
}

int test_constant_propagation() {
    int c1 = 15;
    int c2 = 25;
    int c3 = (c1 * 4 + c2 * 2 - 10) / 10; // (60 + 50 - 10) / 10 = 100 / 10 = 10
    int c4 = (c3 << 2) >> 1;               // (10 * 4) / 2 = 20
    int c5 = c4 & 31;                      // 20 & 31 = 20
    int c6 = c5 | 3;                       // 20 | 3 = 23
    int c7 = c6 ^ 7;                       // 23 ^ 7 = 16
    return c7;
}

int test_copy_propagation(int val) {
    int x = val;
    int y = x;
    int z = y;
    int w = z;
    return w;
}

int test_unreachable() {
    int res = 42;
    if (0) {
        res = 999;
    }
    return res;
    // Dead code after return
    res = 12345;
    return res;
}

int test_strength_reduction(int x) {
    int m1 = x * 2;
    int m2 = 4 * x;
    int m3 = x * 8;
    int m4 = 16 * x;
    int m5 = x * 64;
    int m6 = x * 1024;
    return (m1 == (x << 1)) && (m2 == (x << 2)) && (m3 == (x << 3)) &&
           (m4 == (x << 4)) && (m5 == (x << 6)) && (m6 == (x << 10));
}

int test_store_load_forwarding(int a, int b) {
    int x = a + b;
    int y = x * 2;
    int z = x + y;
    x = z - b;
    int w = x + y + z;
    return w;
}

int test_dead_store_elimination(int v) {
    int val = 10;
    val = 20;
    val = v + 5;
    return val;
}

int main() {
    if (!test_algebraic(42)) {
        printf("FAIL: algebraic simplification\n");
        return 1;
    }

    int cp = test_constant_propagation();
    if (cp != 16) {
        printf("FAIL: constant propagation expected 16, got %d\n", cp);
        return 2;
    }

    int cop = test_copy_propagation(777);
    if (cop != 777) {
        printf("FAIL: copy propagation expected 777, got %d\n", cop);
        return 3;
    }

    int ur = test_unreachable();
    if (ur != 42) {
        printf("FAIL: unreachable block expected 42, got %d\n", ur);
        return 4;
    }

    if (!test_strength_reduction(7) || !test_strength_reduction(-13)) {
        printf("FAIL: strength reduction\n");
        return 5;
    }

    int slf = test_store_load_forwarding(3, 4);
    if (slf != 52) {
        printf("FAIL: store load forwarding expected 52, got %d\n", slf);
        return 6;
    }

    int dse = test_dead_store_elimination(100);
    if (dse != 105) {
        printf("FAIL: dead store elimination expected 105, got %d\n", dse);
        return 7;
    }

    printf("PASS: 07_optimizations completed successfully\n");
    return 0;
}

