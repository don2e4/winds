extern int puts(const char *s);

typedef int integer_t;
typedef integer_t *int_ptr_t;

using counter_t = long;
using byte_t = unsigned char;

struct Container {
    using value_type = int;
    value_type val;
};

int main() {
    integer_t a = 42;
    int_ptr_t pa = &a;
    if (*pa != 42) {
        puts("typedef test failed");
        return 1;
    }

    counter_t cnt = 1000;
    if (cnt != 1000) {
        puts("using alias test failed");
        return 2;
    }

    byte_t b = 255;
    if (b != 255) {
        puts("byte alias test failed");
        return 3;
    }

    Container c;
    c.val = 99;
    if (c.val != 99) {
        puts("nested type alias test failed");
        return 4;
    }

    puts("Typedef and alias tests passed!");
    return 0;
}
