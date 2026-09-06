#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <cassert>

int main() {
    /* Test cstdint */
    int32_t x = 42;
    uint64_t big = 100000;
    assert(x == 42);
    assert(big == 100000);

    /* Test utility pair */
    std::pair<int, int> p = std::make_pair(10, 20);
    assert(p.first == 10);
    assert(p.second == 20);

    /* Test algorithm */
    int mn = std::min(15, 25);
    int mx = std::max(15, 25);
    assert(mn == 15);
    assert(mx == 25);

    int a = 1;
    int b = 2;
    std::swap(a, b);
    assert(a == 2 && b == 1);

    int numbers[5];
    numbers[0] = 50;
    numbers[1] = 20;
    numbers[2] = 40;
    numbers[3] = 10;
    numbers[4] = 30;

    std::sort(numbers, numbers + 5);
    assert(numbers[0] == 10);
    assert(numbers[1] == 20);
    assert(numbers[2] == 30);
    assert(numbers[3] == 40);
    assert(numbers[4] == 50);

    /* Test string */
    std::string s("winds");
    assert(s.size() == 5);
    assert(s == "winds");

    s.push_back('!');
    assert(s.size() == 6);
    assert(s[5] == '!');

    std::string greeting = s + " fast compiler";
    assert(greeting.size() == 20);

    /* Test vector */
    std::vector<int> vec;
    assert(vec.empty());

    vec.push_back(100);
    vec.push_back(200);
    vec.push_back(300);

    assert(vec.size() == 3);
    assert(vec[0] == 100);
    assert(vec[1] == 200);
    assert(vec[2] == 300);

    vec.pop_back();
    assert(vec.size() == 2);
    assert(vec[1] == 200);

    /* Test iostream */
    std::cout << "Testing standard library stream: " << greeting << std::endl;
    std::cout << "Vector elements: " << vec[0] << ", " << vec[1] << std::endl;
    std::cout << "Standard library compatibility test passed!" << std::endl;

    return 0;
}
