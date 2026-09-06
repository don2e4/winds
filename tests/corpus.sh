#!/usr/bin/env bash
set -euo pipefail

WINDS="$(realpath "${WINDS:-./bin/winds}")"
ROOT="$(pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
IFS=$'\t' read -r _ ZLIB_TAG ZLIB_COMMIT ZLIB_URL ZLIB_UNIT < tests/corpus/manifest.tsv

ZLIB_SOURCE="${ZLIB_SOURCE:-$WORK/zlib}"
if [[ ! -d "$ZLIB_SOURCE/.git" ]]; then
    git clone --quiet --depth 1 --branch "$ZLIB_TAG" "$ZLIB_URL" "$ZLIB_SOURCE"
fi
[[ "$(git -C "$ZLIB_SOURCE" rev-parse HEAD)" == "$ZLIB_COMMIT" ]]

"$WINDS" -I"$ZLIB_SOURCE" -c "$ZLIB_SOURCE/$ZLIB_UNIT" -o "$WORK/adler32.o"
gcc "$ROOT/tests/corpus/zlib_adler_harness.c" "$WORK/adler32.o" -no-pie -o "$WORK/zlib-adler"
"$WORK/zlib-adler"

echo "zlib $ZLIB_TAG $ZLIB_UNIT: compile, C ABI link, and checksum passed"
