// Test 01: Core basics - variables, arithmetic, control flow

extern int printf(const char *fmt, ...);

int main() {
    int a = 10;
    int b = 20;
    int c = a + b * 2; // Operator precedence: 10 + 40 = 50

    if (c != 50) {
        printf("FAIL: precedence test: expected 50, got %d\n", c);
        return 1;
    }

    // Loops and increments
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        sum = sum + i;
    }

    if (sum != 55) {
        printf("FAIL: for loop sum: expected 55, got %d\n", sum);
        return 2;
    }

    // While loop
    int count = 0;
    while (count < 5) {
        count++;
    }

    if (count != 5) {
        printf("FAIL: while loop count: expected 5, got %d\n", count);
        return 3;
    }

    printf("PASS: 01_basics completed successfully\n");
    return 0;
}
