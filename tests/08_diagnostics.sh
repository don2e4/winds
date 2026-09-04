#!/bin/bash
set -e

WINDS=./bin/winds

echo "Testing diagnostics system..."

# Test 1: Typo in identifier name
cat << 'TC1' > tests/test_err1.cpp
int main() {
    int counter = 100;
    return countr;
}
TC1
OUTPUT=$($WINDS tests/test_err1.cpp 2>&1 || true)
if echo "$OUTPUT" | grep -q "did you mean 'counter'?"; then
    echo "  [PASS] Typo diagnostic ('counter' suggested for 'countr')"
else
    echo "  [FAIL] Typo diagnostic did not suggest 'counter':"
    echo "$OUTPUT"
    exit 1
fi

# Test 2: Typo in struct member
cat << 'TC2' > tests/test_err2.cpp
struct Point {
    int x_coord;
    int y_coord;
};
int main() {
    Point p;
    return p.x_cord;
}
TC2
OUTPUT=$($WINDS tests/test_err2.cpp 2>&1 || true)
if echo "$OUTPUT" | grep -q "did you mean member 'x_coord'?"; then
    echo "  [PASS] Member typo diagnostic ('x_coord' suggested for 'x_cord')"
else
    echo "  [FAIL] Member typo diagnostic did not suggest 'x_coord':"
    echo "$OUTPUT"
    exit 1
fi

# Test 3: Missing semicolon
cat << 'TC3' > tests/test_err3.cpp
int main() {
    int a = 42
    return a;
}
TC3
OUTPUT=$($WINDS tests/test_err3.cpp 2>&1 || true)
if echo "$OUTPUT" | grep -q "insert a semicolon ';'"; then
    echo "  [PASS] Missing semicolon diagnostic with actionable help note"
else
    echo "  [FAIL] Missing semicolon diagnostic:"
    echo "$OUTPUT"
    exit 1
fi

# Test 4: Missing header file
cat << 'TC4' > tests/test_err4.cpp
#include "nonexistent_lib.h"
int main() { return 0; }
TC4
OUTPUT=$($WINDS tests/test_err4.cpp 2>&1 || true)
if echo "$OUTPUT" | grep -q "cannot find header file 'nonexistent_lib.h'"; then
    echo "  [PASS] Header not found diagnostic with -I advice"
else
    echo "  [FAIL] Header not found diagnostic:"
    echo "$OUTPUT"
    exit 1
fi

rm -f tests/test_err1.cpp tests/test_err2.cpp tests/test_err3.cpp tests/test_err4.cpp
echo "All diagnostics tests passed successfully!"
