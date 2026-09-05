#!/bin/bash
set -e

WINDS=${WINDS:-./bin/winds}

echo "Testing -Wall, -Werror, and -run mode..."

# 1. Test -Wall warning
cat << 'TC1' > tests/test_warn_unused.cpp
int main() {
    int unused_calc = 100;
    return 0;
}
TC1

OUTPUT=$($WINDS -Wall tests/test_warn_unused.cpp -o tests/test_warn_unused.out 2>&1 || true)
if echo "$OUTPUT" | grep -q "unused variable 'unused_calc'"; then
    echo "  [PASS] -Wall unused variable detection"
else
    echo "  [FAIL] -Wall did not warn on unused variable:"
    echo "$OUTPUT"
    exit 1
fi

# 2. Test -Werror escalation
ERR_OUTPUT=$($WINDS -Wall -Werror tests/test_warn_unused.cpp -o tests/test_warn_unused.out 2>&1 || true)
if echo "$ERR_OUTPUT" | grep -q "error: unused variable 'unused_calc'"; then
    echo "  [PASS] -Werror escalation from warning to error"
else
    echo "  [FAIL] -Werror did not escalate warning to error:"
    echo "$ERR_OUTPUT"
    exit 1
fi

# 3. Test -run mode
cat << 'TC2' > tests/test_script.cpp
#!/usr/bin/env winds -run
extern int printf(const char *fmt, ...);
int main() {
    printf("Script execution successful!\n");
    return 0;
}
TC2

RUN_OUTPUT=$($WINDS -run tests/test_script.cpp)
if echo "$RUN_OUTPUT" | grep -q "Script execution successful!"; then
    echo "  [PASS] -run direct script execution"
else
    echo "  [FAIL] -run output mismatch:"
    echo "$RUN_OUTPUT"
    exit 1
fi

# Cleanup
rm -f tests/test_warn_unused.cpp tests/test_warn_unused.out tests/test_script.cpp
echo "All warning and script execution tests passed successfully!"
