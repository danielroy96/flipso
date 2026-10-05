#!/bin/sh
# Build and run the host test suites, under ASan and UBSan.
#
#   tools/test/run.sh       one line on success; on failure every [FAIL] line
#                           and the end of the log, where a compiler error or
#                           a sanitiser report lands
#   tools/test/run.sh -v    everything, as it happens (CI runs this)
#
# The full output is some 7,000 lines, nearly all [PASS] and screen renders.
# Read whole it cost a session tens of thousands of tokens, and the habit that
# grew up instead - `run.sh | grep FAIL` - reports grep's exit status, not the
# suite's, so a build that failed before any test ran read as a pass.
#
# Each suite is one binary: its test files - a directory of them for the larger
# suites, one file for the rest - the harness in test.c, and the parts of the
# app it tests, listed in sources.py.
set -e
if [ "$1" != "-v" ]; then
  T=${TMPDIR:-/tmp}
  LOG=$(mktemp "${T%/}/flipso-run.XXXXXX")
  start=$(date +%s)
  if "$0" -v >"$LOG" 2>&1; then
    echo "run.sh: $(grep -c '\[PASS\]' "$LOG") checks passed in" \
      "$(($(date +%s) - start))s - full output in $LOG"
    exit 0
  else
    status=$?
  fi
  # Each failure with the detail lines indented under it ("got X, wanted Y").
  awk '/\[FAIL\]/ { p = 1; print; next } p && /^      / { print; next } { p = 0 }' "$LOG"
  echo "--- the last 40 lines of $LOG"
  tail -40 "$LOG"
  echo "run.sh: FAILED (exit $status) - full output in $LOG"
  exit $status
fi
cd "$(dirname "$0")"
ROOT=../..

# Which files make up each part of the app: see sources.py.
src() { python3 sources.py "$@"; }
ITSO=$(src itso)
FORMAT=$(src format)
CAPTURE=$(src capture)

# Every suite is compiled alike: build NAME SOURCES-AND-INCLUDE-FLAGS...
build() {
  name=$1
  shift
  ${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
    -fsanitize=address,undefined \
    "$@" test.c \
    -o "$name"
}

python3 build_card.py >/dev/null
build test_parse -I"$ROOT/itso" -I. parse/*.c $ITSO
./test_parse

echo
echo "Scan session"
# The transport and retry policy, which is pure C: every path a card can take
# through a scan, without a card.
build test_scan_session -I"$ROOT" test_scan_session.c $(src scan_session)
./test_scan_session

echo
echo "Saved cards"
# The save/load round trip, which is the decoder's other entry point: a saved
# card is raw blocks, so loading one runs the same parsers a tap does.
build test_capture -I"$ROOT" -I"$ROOT/itso" -I. capture/*.c $CAPTURE $ITSO
./test_capture

echo
echo "Saved card files"
# The layer around the blocks: naming, and real files in a real directory.
build test_saved -I"$ROOT" -I"$ROOT/itso" -Istub -I. saved/*.c $(src saved) $CAPTURE $ITSO
./test_saved

echo
echo "Station table"
build test_stations -I"$ROOT" -Istub test_stations.c $(src stations)
./test_stations

echo
echo "Stop table"
build test_naptan -I"$ROOT" -Istub test_naptan.c $(src naptan)
./test_naptan

echo
echo "Storage opens close on failure"
# A failed open that is not closed leaves the path registered as open, and the
# next launch of the app hangs on it behind the desktop. See the script.
python3 "$ROOT/tools/test/lint_storage.py"

echo
echo "No debug logging"
# The Apps Catalog sends back an app that ships its development logging. See
# the script.
python3 "$ROOT/tools/test/lint_logs.py"

echo
echo "Firmware sources"
# fbt tries every pattern in application.fam from every directory, so a test
# directory named like an app one ends up in the .fap. See the script.
python3 "$ROOT/tools/test/lint_sources.py"

echo
echo "Product terms"
# A product's family terms share their room (ItsoTerms, itso_product.h), so
# outside the decoder they are read through the accessors, which give another
# family's product nothing set rather than its bytes.
if grep -rnE '(->|\.)terms\.(purse|id|ticket)' "$ROOT/format" "$ROOT/scenes" "$ROOT/views" \
  "$ROOT/cards" "$ROOT/reader" "$ROOT/lookup" "$ROOT/flipso.c"; then
  echo "  [FAIL] only the decoder reads ItsoProduct::terms directly"
  exit 1
fi
echo "  [PASS] only the decoder reads ItsoProduct::terms directly"

echo
echo "flipctl serial recovery"
# Pure Python and needs no Flipper: it injects the USB CDC drop that cannot be
# provoked on demand from a real device.
python3 "$ROOT/tools/flipper/test_flipctl.py"

echo
echo "Operator names and branding"
build test_operators -I"$ROOT" -Istub test_operators.c $(src operators) "$ROOT/itso/names/itso_operators.c"
./test_operators

echo
echo "Name tables"
# The device reads the long name tables from assets/names.dat, built from the C
# tables the other suites use: it has to be what the builder writes now, and
# the device's reader has to agree with those tables code by code.
NAMES=$(mktemp)
"$ROOT/tools/names/build_names.sh" "$NAMES" >/dev/null
if cmp -s "$NAMES" "$ROOT/assets/names.dat"; then
  echo "  [PASS] the packaged names.dat is the builder's"
else
  echo "  [FAIL] the packaged names.dat is stale: run tools/names/build_names.sh"
  exit 1
fi
rm -f "$NAMES"
build test_names -I"$ROOT" -I"$ROOT/itso" -Istub test_names.c $(src names)
./test_names "$ROOT/assets/names.dat"

echo
echo "Screen text"
# Every screen of the synthetic card and of every demo card, held to the house
# style in flipso_format.h: capitalised values, labelled detail lines, pounds.
DEMO=$(mktemp -d)
python3 "$ROOT/tools/demo/build_demo_cards.py" "$DEMO" >/dev/null
# The app packages its own copy, which the About menu opens: it has to be what
# the builder writes now, or the device shows cards these tests never checked.
if diff -r "$DEMO" "$ROOT/assets/demo" >/dev/null; then
  echo "  [PASS] the packaged demo cards are the builder's"
else
  diff -rq "$DEMO" "$ROOT/assets/demo" || true
  echo "  [FAIL] the packaged demo cards are stale: run tools/demo/build_demo_cards.py"
  exit 1
fi
build test_format -I"$ROOT" -I"$ROOT/itso" -I. -Istub screen_text/*.c $FORMAT $CAPTURE $(src media) $ITSO
./test_format "$DEMO"

echo
echo "Screens tool"
# tools/test/screens.py is how a newly read card is checked, so it has to keep
# building against the format code it renders: run it over one demo card.
if python3 "$ROOT/tools/test/screens.py" "$DEMO/Demo 01 The Key Kent.flipso" >screens.out 2>&1 &&
  grep -q "Operators on this card" screens.out; then
  echo "  [PASS] screens.py renders a demo card"
else
  cat screens.out
  echo "  [FAIL] screens.py renders a demo card"
  exit 1
fi
rm -f screens.out
# replay.py is how a pulled card gets into the host loop, and builds the decoder
# from sources.py the same way.
if python3 "$ROOT/tools/test/replay.py" "$DEMO/Demo 04 SWR Touch.flipso" >replay.out 2>&1 &&
  grep -q "check digit ok" replay.out; then
  echo "  [PASS] replay.py decodes a demo card"
else
  cat replay.out
  echo "  [FAIL] replay.py decodes a demo card"
  exit 1
fi
rm -f replay.out
# And the check for what a new card adds, which renders through the same tool:
# a demo card is by definition nothing new.
if python3 "$ROOT/tools/demo/new_encodings.py" "$DEMO/Demo 07 GWR Touch.flipso" >encodings.out 2>&1 &&
  grep -q "Nothing new" encodings.out; then
  echo "  [PASS] new_encodings.py finds nothing new on a demo card"
else
  cat encodings.out
  echo "  [FAIL] new_encodings.py finds nothing new on a demo card"
  exit 1
fi
rm -f encodings.out
rm -rf "$DEMO"

echo
echo "Card media"
build test_media -I"$ROOT" -I"$ROOT/itso" -I. -Istub test_media.c $(src media) $FORMAT $CAPTURE $ITSO
./test_media

echo
echo "Icon list view"
build test_menu_view -I"$ROOT/views" -Istub test_menu_view.c $(src menu_view views)
./test_menu_view

echo
echo "Scrolling text view"
build test_text_view -I"$ROOT/views" -Istub test_text_view.c $(src text_view views)
./test_text_view
