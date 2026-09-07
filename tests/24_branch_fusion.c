#include <assert.h>
#include <ctype.h>
int branch(int x, int y) {
    if (x < y) return 1;
    if (x == y) return 2;
    return 3;
}
long algebra(long x) {
    return x * -1 + x / -1 + (x - (-x)) + (~(~x) - x) + (-(-x) - x) + ((x & ~0) - x);
}
int main(void) {
    assert(branch(2,3) == 1);
    assert(branch(3,3) == 2);
    assert(branch(4,3) == 3);
    assert(algebra(7) == 0 && algebra(-8) == 0);
    assert(isalpha('a') && isdigit('7') && toupper('b') == 'B');
    return 0;
}
