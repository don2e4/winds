extern int puts(const char *s);

template <typename T>
class Box {
public:
    T value;

    Box(T v) {
        value = v;
    }

    T get() {
        return value;
    }

    void set(T v) {
        value = v;
    }
};

template <typename T1, typename T2>
class Pair {
public:
    T1 first;
    T2 second;

    Pair(T1 f, T2 s) {
        first = f;
        second = s;
    }
};

int main() {
    Box<int> b(123);
    if (b.get() != 123) {
        puts("Box<int> initial get failed");
        return 1;
    }

    b.set(456);
    if (b.get() != 456) {
        puts("Box<int> set/get failed");
        return 2;
    }

    Pair<int, int> p(10, 20);
    if (p.first != 10 || p.second != 20) {
        puts("Pair<int, int> failed");
        return 3;
    }

    puts("Template monomorphization tests passed!");
    return 0;
}
