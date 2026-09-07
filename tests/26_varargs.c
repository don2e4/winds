#include <stdarg.h>
#include <stdio.h>
#include <string.h>
int total(int n, ...) {
    va_list ap;
    va_list copy;
    va_start(ap, n);
    va_copy(copy, ap);
    int sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(ap, int);
    int first = va_arg(copy, int);
    va_end(ap);
    va_end(copy);
    return sum + first;
}
int format(char *buffer, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(buffer, 64, fmt, ap);
    va_end(ap);
    return result;
}
int main(void) {
    char buffer[64];
    return total(8, 1, 2, 3, 4, 5, 6, 7, 8) != 37 ||
        format(buffer, "%d %s", 42, "ok") != 5 || strcmp(buffer, "42 ok");
}
