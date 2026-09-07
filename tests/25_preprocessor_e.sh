#!/usr/bin/env bash
set -euo pipefail
WINDS="$(realpath "${WINDS:-./bin/winds}")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cat > "$WORK/input.c" <<'SRC'
#include <assert.h>
#include <ctype.h>
#include <math.h>
#define TWICE(x) ((x) + (x))
int main(void) { assert(TWICE(VALUE) == 6); return !isdigit('8') || sizeof(1UL) != sizeof(long); }
SRC
"$WINDS" -E -DVALUE=3 "$WORK/input.c" > "$WORK/expanded.c"
"$WINDS" -E -DVALUE=3 "$WORK/input.c" -o "$WORK/file.c"
cmp "$WORK/expanded.c" "$WORK/file.c"
gcc -fno-builtin -x c "$WORK/expanded.c" -o "$WORK/app"
"$WORK/app"
cd "$WORK"
"$WINDS" -pipe -g0 -g1 -g2 -g3 -std=c11 -pthread -DVALUE=3 -c input.c
test -s input.o
"$WINDS" -DVALUE=3 -S input.c
test -s input.s
