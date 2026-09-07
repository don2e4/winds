union Word { int word; char bytes[4]; };
typedef union { long wide; int narrow; } Number;
enum Mode { LOW = 1 << 2, HIGH = LOW + 3, NEXT };
enum { COUNT = 2 + 1 };
enum Mode mode;
struct Box { union { char bytes[24]; long word; } u; int after; };
int main(void) {
    enum { LOCAL = HIGH + 2 } local = LOCAL;
    struct Box box;
    box.u.word = 65;
    box.after = 7;
    union Word value;
    Number number;
    register int result = 0;
    auto int data[COUNT];
    value.word = 65;
    number.wide = 42;
    mode = HIGH;
    result = value.bytes[0];
    return !(result == 65 && number.narrow == 42 && sizeof(value) == 4 &&
        sizeof(number) == 8 && mode == 7 && NEXT == 8 && sizeof data == 12 && local == 9 && sizeof(box) == 32 &&
        box.u.bytes[0] == 65 && box.after == 7);
}
