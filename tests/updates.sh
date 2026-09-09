#!/usr/bin/env bash
set -euo pipefail
WINDS="$(realpath "${WINDS:-./bin/winds}")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
for source in tests/22_multidim_arrays.c tests/23_unions_and_enums.c tests/24_branch_fusion.c tests/26_varargs.c tests/28_struct_copy.c tests/29_cpp_compat.cpp; do
    for level in 0 1 2; do
        "$WINDS" "-O$level" "$source" -o "$WORK/app"
        "$WORK/app"
    done
done
"$WINDS" tests/27_floating.c -o "$WORK/floating"
[[ "$("$WORK/floating")" == "8.75" ]]
WINDS="$WINDS" bash tests/25_preprocessor_e.sh
"$WINDS" -O0 -S tests/24_branch_fusion.c -o "$WORK/branch.s"
python3 - "$WORK/branch.s" <<'PY'
import pathlib, sys
assembly = pathlib.Path(sys.argv[1]).read_text()
branch = assembly.split('branch:\n', 1)[1].split('.cfi_endproc', 1)[0]
assert '\tcmpq\t' in branch and '\tjge\t' in branch
assert '\tset' not in branch and '\ttestq\t' not in branch
PY
cat > "$WORK/invalid.c" <<'SRC'
int values[1] = {1, 2};
SRC
if "$WINDS" -c "$WORK/invalid.c" -o "$WORK/invalid.o" >"$WORK/error" 2>&1; then exit 1; fi
cat > "$WORK/invalid.c" <<'SRC'
int value(void) { return 2; }
int values[] = {value()};
SRC
if "$WINDS" -c "$WORK/invalid.c" -o "$WORK/invalid.o" >"$WORK/error" 2>&1; then exit 1; fi
printf '  [PASS] updates: arrays, unions, enums, branches, preprocessing, varargs, struct copies, C++ compatibility, floating point, diagnostics\n'
