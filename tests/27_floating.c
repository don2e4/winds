#include <stdio.h>
#include <string.h>

static double blend(double a, float b, int n) {
    return a + b * n;
}

int main(void) {
    double value = blend(1.25, 2.5f, 3);
    int whole = (int)value;
    double negated = -value;
    float half = 2.5f;
    float negated_half = -half;
    char text[16];
    snprintf(text, sizeof text, "%.2f", half);
    printf("%.2f\n", value);
    return !(value > 8.74 && value < 8.76 && whole == 8 &&
             negated < -8.74 && negated > -8.76 && negated_half == -2.5f &&
             strcmp(text, "2.50") == 0);
}
