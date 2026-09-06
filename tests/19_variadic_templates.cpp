// Test 19: Variadic Templates, Parameter Packs, and Standard Tuple
#include <cstdio>
#include <cassert>
#include <tuple>

// 1. Variadic template forward declaration
template <typename... Args>
class Container;

// 2. Concrete variadic class template
template <typename Head, typename... Tail>
class Box {
public:
    Head value;
    Box<Tail...> rest;

    Box() {}
    Box(Head h, Tail... t) : value(h), rest(t...) {}
};

template <typename Head>
class Box<Head> {
public:
    Head value;

    Box() {}
    Box(Head h) : value(h) {}
};

int main() {
    // 3. Test Box instantiation with 1, 2, 3, and 4 type arguments
    Box<int> b1(10);
    assert(b1.value == 10);

    Box<int, char> b2(20, 'a');
    assert(b2.value == 20);
    assert(b2.rest.value == 'a');

    Box<int, char, long> b3(30, 'b', 50000);
    assert(b3.value == 30);
    assert(b3.rest.value == 'b');
    assert(b3.rest.rest.value == 50000);

    Box<int, char, long, int> b4(40, 'c', 99999, 100);
    assert(b4.value == 40);
    assert(b4.rest.value == 'c');
    assert(b4.rest.rest.value == 99999);
    assert(b4.rest.rest.rest.value == 100);

    // 4. Test std::tuple instantiation with 3 arguments and element access
    std::tuple<int, char, long> t(42, 'w', 100000);
    assert(t.head == 42);
    assert(t.tail.head == 'w');
    assert(t.tail.tail.head == 100000);

    // 5. Test std::make_tuple
    std::tuple<int, int> mt = std::make_tuple(10, 20);
    assert(mt.head == 10);
    assert(mt.tail.head == 20);

    printf("PASS: 19_variadic_templates completed successfully\n");
    return 0;
}
