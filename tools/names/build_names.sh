#!/bin/sh
# Build the name tables the device reads, from the C tables in itso/names/.
#
#   tools/names/build_names.sh [OUT]     OUT defaults to assets/names.dat
#
# See build_names.c. Run it after editing a table in itso/names/; run.sh fails
# until the packaged file matches.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$ROOT/assets/names.dat}
BIN=$(mktemp "${TMPDIR:-/tmp}/build_names.XXXXXX")
trap 'rm -f "$BIN"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I"$ROOT/itso" \
  "$HERE/build_names.c" "$ROOT/itso/itso_names.c" -o "$BIN"
"$BIN" "$OUT"
