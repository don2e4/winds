extern int puts(const char *s);

int add(int a, int b) {
    return a + b;
}

int sub(int a, int b) {
    return a - b;
}

int mul(int a, int b) {
    return a * b;
}

int square(int x) {
    return x * x;
}

int apply_unary(int x, int (*op)(int)) {
    return op(x);
}

int apply_binary(int a, int b, int (*op)(int, int)) {
    return (*op)(a, b);
}

using binary_fn = int (*)(int, int);
typedef int (*unary_fn)(int);

struct callback_handler {
    int (*handler)(int);
};

int (*g_op)(int, int) = add;

int main() {
    /* 1. Basic function pointer assignment and direct call */
    int (*fp1)(int, int) = add;
    if (fp1(10, 20) != 30) {
        puts("fail: fp1(10, 20) != 30");
        return 1;
    }

    /* 2. Explicit address-of and dereferenced call */
    int (*fp2)(int, int) = &sub;
    if ((*fp2)(30, 10) != 20) {
        puts("fail: (*fp2)(30, 10) != 20");
        return 2;
    }

    /* 3. Reassignment */
    fp1 = mul;
    if (fp1(5, 6) != 30) {
        puts("fail: fp1 reassigned != 30");
        return 3;
    }

    /* 4. Pass as argument to higher-order function */
    if (apply_unary(7, square) != 49) {
        puts("fail: apply_unary(7, square) != 49");
        return 4;
    }

    if (apply_binary(12, 8, sub) != 4) {
        puts("fail: apply_binary(12, 8, sub) != 4");
        return 5;
    }

    /* 5. Using type alias */
    binary_fn bfn = mul;
    if (bfn(4, 7) != 28) {
        puts("fail: binary_fn alias != 28");
        return 6;
    }

    /* 6. Typedef type alias */
    unary_fn ufn = square;
    if (ufn(9) != 81) {
        puts("fail: unary_fn alias != 81");
        return 7;
    }

    /* 7. Array of function pointers (dispatch table) */
    int (*dispatch[3])(int, int);
    dispatch[0] = add;
    dispatch[1] = sub;
    dispatch[2] = mul;

    if (dispatch[0](15, 5) != 20) {
        puts("fail: dispatch[0] != 20");
        return 8;
    }
    if (dispatch[1](15, 5) != 10) {
        puts("fail: dispatch[1] != 10");
        return 9;
    }
    if (dispatch[2](15, 5) != 75) {
        puts("fail: dispatch[2] != 75");
        return 10;
    }

    /* 8. Global function pointer */
    if (g_op(25, 15) != 40) {
        puts("fail: g_op(25, 15) != 40");
        return 11;
    }

    /* 9. Struct member holding function pointer */
    callback_handler cb;
    cb.handler = square;
    if (cb.handler(8) != 64) {
        puts("fail: cb.handler(8) != 64");
        return 12;
    }

    puts("all function pointer tests passed successfully!");
    return 0;
}
