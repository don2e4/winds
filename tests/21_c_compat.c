#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef enum { MODE_LOW = 2, MODE_HIGH } mode_t;
static int values[] = { 4, 5, 6 };

static int choose(int condition, int yes, int no) {
    return condition ? yes : no;
}

static int next_value(void) {
    static int value = 7;
    return value++;
}

int main(void) {
    int index = 0, total = 0;
    do {
        total += ++index;
    } while (index < 4);
    switch (index) {
        case 3: total = 0; break;
        case 4: total += 1; break;
        default: return 2;
    }

    int side_effect = 0;
    int result = (side_effect = 3, choose(total == 11, 39, 0));
    goto checked;
    return 3;
checked:
    size_t width = sizeof(uint64_t);
    mode_t const mode = MODE_HIGH;
    return result + side_effect == 42 && width == 8 && INT_MAX == 2147483647 &&
           mode == 3 && values[2] == 6 && next_value() == 7 && next_value() == 8 ? 0 : 1;
}
