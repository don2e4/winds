#!/usr/bin/env bash
set -euo pipefail

WINDS="${WINDS:-./bin/winds}"
WINDS="$(realpath "$WINDS")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cp tests/multifile_main.cpp tests/multifile_part.cpp tests/multifile_c.c "$WORK/"
gcc -c "$WORK/multifile_c.c" -o "$WORK/multifile_c.o"
ar rcs "$WORK/libmultifile.a" "$WORK/multifile_c.o"

"$WINDS" -DBUILD_VALUE=37 -U__linux__ \
    "$WORK/multifile_main.cpp" "$WORK/multifile_part.cpp" \
    -L "$WORK" -l multifile -o "$WORK/app"
"$WORK/app"

(
    cd "$WORK"
    "$WINDS" -DBUILD_VALUE=37 -U__linux__ -c multifile_main.cpp multifile_part.cpp
    test -f multifile_main.o
    test -f multifile_part.o
)
