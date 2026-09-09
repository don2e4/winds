#include <stdio.h>

static double blend(double a, float b, int n) {
    return a + b * n;
}

int main(void) {
    double value = blend(1.25, 2.5f, 3);
    int whole = (int)value;
    printf("%.2f\n", value);
    return !(value > 8.74 && value < 8.76 && whole == 8);
}
