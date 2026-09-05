// Test 09: System V AMD64 ABI Compliance & Stress Tests
// Covers:
// 1. 10-argument function (6 register args + 4 stack args)
// 2. Mixed pointer & value arguments across register-stack boundary
// 3. Callee-saved register preservation across complex calls & recursion
// 4. Glibc variadic call (printf with 10 args) testing strict 16-byte stack alignment
// 5. Method with 7 parameters (this + 7 args = 8 total)

extern int printf(const char *fmt, ...);

// 1. Function with 10 arguments
int sum_ten(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

// 2. Mixed pointer and integer arguments across register/stack boundary
int mutate_and_sum(int a1, int *p2, int a3, int *p4, int a5, int a6, int a7, int *p8) {
    *p2 = *p2 + 10;
    *p4 = *p4 + 20;
    *p8 = *p8 + 30;
    return a1 + *p2 + a3 + *p4 + a5 + a6 + a7 + *p8;
}

// 3. Callee-saved register stress test across recursion
int recursive_work(int n) {
    if (n <= 0) {
        return 0;
    }
    int v1 = n * 2;
    int v2 = n * 3;
    int v3 = n * 4;
    int v4 = n * 5;
    int v5 = n * 6;
    return v1 + v2 + v3 + v4 + v5 + recursive_work(n - 1);
}

int stress_caller() {
    int x1 = 111;
    int x2 = 222;
    int x3 = 333;
    int x4 = 444;
    int x5 = 555;
    int x6 = 666;

    int sub_result = recursive_work(10);

    // Verify all local caller variables survived intact
    if (x1 != 111) return -1;
    if (x2 != 222) return -2;
    if (x3 != 333) return -3;
    if (x4 != 444) return -4;
    if (x5 != 555) return -5;
    if (x6 != 666) return -6;

    return x1 + x2 + x3 + x4 + x5 + x6 + sub_result;
}

// 5. Method with 7 user parameters (this + 7 = 8 args)
class Accumulator {
public:
    int base;

    void init(int b) {
        this->base = b;
    }

    int add7(int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
        return this->base + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    }
};

int main() {
    // Test 1: 10 arguments
    int s1 = sum_ten(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (s1 != 55) {
        printf("FAIL: sum_ten(1..10) expected 55, got %d\n", s1);
        return 1;
    }

    int s2 = sum_ten(100, -20, 300, -40, 500, -60, 700, -80, 900, -100);
    if (s2 != 2200) {
        printf("FAIL: sum_ten with negatives expected 2200, got %d\n", s2);
        return 2;
    }

    // Test 2: Mixed pointers and values across register/stack boundary
    int val2 = 5;
    int val4 = 15;
    int val8 = 25;
    int m_res = mutate_and_sum(1, &val2, 3, &val4, 5, 6, 7, &val8);
    // val2 became 15, val4 became 35, val8 became 55
    // 1 + 15 + 3 + 35 + 5 + 6 + 7 + 55 = 127
    if (val2 != 15 || val4 != 35 || val8 != 55 || m_res != 127) {
        printf("FAIL: mutate_and_sum expected 127 (15, 35, 55), got %d (%d, %d, %d)\n",
               m_res, val2, val4, val8);
        return 3;
    }

    // Test 3: Callee-saved register persistence across recursion
    int caller_res = stress_caller();
    if (caller_res < 0) {
        printf("FAIL: stress_caller callee-saved registers clobbered\n");
        return 4;
    }

    // Test 4: Glibc variadic printf with 10 total arguments
    // Enforces 16-byte stack alignment at call boundary (glibc segfaults otherwise)
    printf("Variadic 10 args check: %d %d %d %d %d %d %d %d %d\n",
           10, 20, 30, 40, 50, 60, 70, 80, 90);

    // Test 5: Method with 7 parameters (this + 7 = 8 args)
    Accumulator acc;
    acc.init(1000);
    int acc_res = acc.add7(1, 2, 3, 4, 5, 6, 7);
    if (acc_res != 1028) {
        printf("FAIL: acc.add7 expected 1028, got %d\n", acc_res);
        return 5;
    }

    printf("PASS: 09_abi completed successfully\n");
    return 0;
}
