#!/bin/bash
set -e

WINDS=${WINDS:-./bin/winds}

echo "Testing -MMD -MP -MF dependency generation..."

# 1. Test basic -MMD -MP
rm -f tests/06_headers.d tests/06_headers.o
$WINDS -MMD -MP -c tests/06_headers.cpp -o tests/06_headers.o

if [ ! -f tests/06_headers.d ]; then
    echo "FAIL: tests/06_headers.d was not generated"
    exit 1
fi

if ! grep -q "tests/06_headers.o: tests/06_headers.cpp" tests/06_headers.d; then
    echo "FAIL: dependency rule missing target or source in tests/06_headers.d"
    cat tests/06_headers.d
    exit 1
fi

if ! grep -q "math_utils.h:" tests/06_headers.d; then
    echo "FAIL: phony target for math_utils.h not generated"
    cat tests/06_headers.d
    exit 1
fi

echo "  [PASS] -MMD -MP default rule generation"

# 2. Test -MF with custom output path
rm -f tests/custom_dep.d
$WINDS -MMD -MP -MF tests/custom_dep.d -S tests/06_headers.cpp -o tests/06_headers.s

if [ ! -f tests/custom_dep.d ]; then
    echo "FAIL: tests/custom_dep.d was not generated"
    exit 1
fi

if ! grep -q "tests/06_headers.s: tests/06_headers.cpp" tests/custom_dep.d; then
    echo "FAIL: custom dependency file target mismatch"
    cat tests/custom_dep.d
    exit 1
fi

echo "  [PASS] -MF custom dependency path"

# Cleanup
rm -f tests/06_headers.d tests/06_headers.o tests/06_headers.s tests/custom_dep.d
echo "All dependency generation tests passed successfully!"
