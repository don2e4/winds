#!/usr/bin/env bash
set -euo pipefail

WINDS="$(realpath "${WINDS:-./bin/winds}")"
ROOT="$(pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

declare -A VERSION REVISION URL UNITS
while IFS=$'\t' read -r project version revision url units; do
    VERSION[$project]="$version"
    REVISION[$project]="$revision"
    URL[$project]="$url"
    UNITS[$project]="$units"
done < tests/corpus/manifest.tsv

git_source() {
    local project="$1" env_name="$2" source
    source="${!env_name:-$WORK/$project}"
    if [[ ! -d "$source/.git" ]]; then
        git clone --quiet --depth 1 --branch "${VERSION[$project]}" "${URL[$project]}" "$source" || return
    fi
    [[ "$(git -C "$source" rev-parse HEAD)" == "${REVISION[$project]}" ]] || return
    printf '%s\n' "$source"
}

zlib_source="$(git_source zlib ZLIB_SOURCE)"
IFS=, read -ra zlib_units <<< "${UNITS[zlib]}"
zlib_objects=()
for unit in "${zlib_units[@]}"; do
    object="$WORK/${unit%.c}.o"
    "$WINDS" -O0 -I"$zlib_source" -c "$zlib_source/$unit" -o "$object"
    zlib_objects+=("$object")
done
gcc "$ROOT/tests/corpus/zlib_harness.c" "${zlib_objects[@]}" -I"$zlib_source" -no-pie -lm -o "$WORK/zlib-test"
"$WORK/zlib-test"
echo "zlib ${VERSION[zlib]}: full build and round trip passed"

cjson_source="$(git_source cjson CJSON_SOURCE)"
"$WINDS" -O0 -I"$cjson_source" -c "$cjson_source/${UNITS[cjson]}" -o "$WORK/cjson.o"
gcc "$ROOT/tests/corpus/cjson_harness.c" "$WORK/cjson.o" -I"$cjson_source" -no-pie -lm -o "$WORK/cjson-test"
"$WORK/cjson-test"
echo "cJSON ${VERSION[cjson]}: build, mutation, and serialization passed"

sqlite_source="${SQLITE_SOURCE:-$WORK/sqlite-amalgamation-3530400}"
if [[ ! -f "$sqlite_source/${UNITS[sqlite]}" ]]; then
    curl -fsSL "${URL[sqlite]}" -o "$WORK/sqlite.zip"
    unzip -q "$WORK/sqlite.zip" -d "$WORK"
fi
[[ "$(sha256sum "$sqlite_source/${UNITS[sqlite]}" | cut -d' ' -f1)" == "${REVISION[sqlite]}" ]]
"$WINDS" -O0 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
    -I"$sqlite_source" -c "$sqlite_source/${UNITS[sqlite]}" -o "$WORK/sqlite.o"
gcc "$ROOT/tests/corpus/sqlite_harness.c" "$WORK/sqlite.o" -I"$sqlite_source" -no-pie -ldl -lm -o "$WORK/sqlite-test"
"$WORK/sqlite-test"
echo "SQLite ${VERSION[sqlite]}: build and in-memory query passed"

pugixml_source="$(git_source pugixml PUGIXML_SOURCE)"
"$WINDS" -O0 -D__cplusplus=199711L -DPUGIXML_NOEXCEPT= -DPUGIXML_NO_STL \
    -DPUGIXML_NO_EXCEPTIONS -DPUGIXML_NO_XPATH -fno-exceptions -fno-rtti \
    -I"$pugixml_source/src" -c "$pugixml_source/${UNITS[pugixml]}" -o "$WORK/pugixml.o"
echo "pugixml ${VERSION[pugixml]}: no-STL/no-exceptions/no-RTTI compile passed"
