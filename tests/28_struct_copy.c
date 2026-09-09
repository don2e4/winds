struct Pair { long first, second; };
struct Holder { int pad; struct Pair pair; };
struct Unsigned { unsigned value; };
struct UnsignedList { struct Unsigned *first; };
struct Words { unsigned char byte; unsigned short half; int whole; };

static int calls;
static int pair_sum(struct Pair a, struct Pair b) {
    return a.first + a.second + b.first + b.second;
}
static struct Pair value = {11, 22};
static struct Unsigned unsigned_values[] = {{3}, {5}};
static const char fractions[] = "\100\040\040";
static const union {
    char a[8];
    short align;
} digits = {"42"};

static struct Pair *next_pair(void) {
    calls++;
    return &value;
}

static int first_letter(void) {
    static const char text[] = "winds";
    return text[0];
}

int main(void) {
    unsigned hash = 0;
    const char *key = "sqlite_master";
    while (*key) {
        hash += 0xdf & (unsigned char)*key++;
        hash *= 0x9e3779b1;
    }
    struct Holder holder;
    struct Unsigned u;
    struct Words words;
    struct UnsignedList list;
    struct Unsigned *up = &unsigned_values[1];
    struct Unsigned *walk = unsigned_values;
    unsigned *field = &up->value;
    list.first = up;
    list.first->value = 7;
    walk++;
    walk -= 1;
    walk += 1;
    u.value = (unsigned)-1;
    words.byte = 1;
    words.half = 65535;
    words.whole = 3;
    struct Unsigned hashed;
    hashed.value = hash;
    holder.pair = *next_pair();
    return &unsigned_values[1] - &unsigned_values[0] != 1 ||
           fractions[0] != 64 || fractions[1] != 32 || fractions[2] != 32 || '\101' != 'A' ||
           sizeof(struct Words) != 8 || words.byte != 1 || words.half != 65535 || words.whole != 3 ||
           hash != hashed.value || calls != 1 || pair_sum(holder.pair, value) != 66 || holder.pair.first != 11 || holder.pair.second != 22 ||
           first_letter() != 'w' || *field != 7 || walk->value != 7 || u.value < 2 ||
           digits.a[0] != '4' || digits.a[1] != '2' || digits.a[2] != 0 ||
           (u.value >> 31) != 1 || u.value / 2 != 2147483647;
}
