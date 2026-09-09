#include <new>

namespace outer {
namespace {
int hidden() { return 3; }
}
int visible() { return hidden(); }
}

struct Value {
    int number;
    Value(int number) { this->number = number; }
};

namespace support {
struct Guard { int value; };
}

struct Box {
    typedef int result;
    result get() const;
};

Box::result Box::get() const {
    using support::Guard;
    Guard guard;
    guard.value = L'x';
    const wchar_t* text = L"x";
    return guard.value == text[0] ? 4 : 0;
}

static unsigned char scan_table[256];

static void scan(char* s, int type) {
    for (;;) {
        char current = s[0];
        if ((!(! (scan_table[static_cast<unsigned char>(current)] & type)))) break;
        current = s[1];
        if ((!(! (scan_table[static_cast<unsigned char>(current)] & type)))) { s += 1; break; }
        current = s[2];
        if ((!(! (scan_table[static_cast<unsigned char>(current)] & type)))) { s += 2; break; }
        current = s[3];
        if ((!(! (scan_table[static_cast<unsigned char>(current)] & type)))) { s += 3; break; }
        s += 4;
    }
}

int main() {
    char storage[8];
    Value* value = new (storage) Value(7);
    Box box;
    unsigned char table[1] = {1};
    int type = 1, index = 0;
    char scan_data[4] = {0};
    scan_table[0] = 1;
    scan(scan_data, type);
    return outer::visible() != 3 || value->number != 7 ||
           box.get() != 4 ||
           !(!(! (table[static_cast<unsigned char>(index)] & type)));
}
