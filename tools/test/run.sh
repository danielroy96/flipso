#!/bin/sh
# Build and run the ITSO decoder tests on the host, under ASan and UBSan.
set -e
cd "$(dirname "$0")"
ROOT=../..

python3 build_card.py >/dev/null
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT/itso" -I. \
  test_parse.c "$ROOT/itso/itso_parse.c" "$ROOT/itso/itso_util.c" "$ROOT/itso/itso_names.c" \
  -o test_parse
./test_parse

echo
echo "Scan session"
# The transport and retry policy, which is pure C: every path a card can take
# through a scan, without a card.
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" \
  test_scan_session.c "$ROOT/reader/flipso_scan_session.c" \
  -o test_scan_session
./test_scan_session

echo
echo "Saved cards"
# The save/load round trip, which is the decoder's other entry point: a saved
# card is raw blocks, so loading one runs the same parsers a tap does.
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -I"$ROOT/itso" -I. \
  test_capture.c "$ROOT/cards/flipso_capture.c" \
  "$ROOT/itso/itso_parse.c" "$ROOT/itso/itso_util.c" "$ROOT/itso/itso_names.c" \
  -o test_capture
./test_capture

echo
echo "Saved card files"
# The layer around the blocks: naming, and real files in a real directory.
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -I"$ROOT/itso" -Istub -I. \
  test_saved.c "$ROOT/cards/flipso_saved.c" "$ROOT/cards/flipso_capture.c" \
  "$ROOT/itso/itso_parse.c" "$ROOT/itso/itso_util.c" "$ROOT/itso/itso_names.c" \
  -o test_saved
./test_saved

echo
echo "Station table"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_stations.c "$ROOT/lookup/flipso_stations.c" \
  -o test_stations
./test_stations

echo
echo "Stop table"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_naptan.c "$ROOT/lookup/flipso_naptan.c" \
  -o test_naptan
./test_naptan

echo
echo "Storage opens close on failure"
# A failed open that is not closed leaves the path registered as open, and the
# next launch of the app hangs on it behind the desktop. See the script.
python3 "$ROOT/tools/test/lint_storage.py"

echo
echo "flipctl serial recovery"
# Pure Python and needs no Flipper: it injects the USB CDC drop that cannot be
# provoked on demand from a real device.
python3 "$ROOT/tools/flipper/test_flipctl.py"

echo
echo "Operator names and branding"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_operators.c "$ROOT/lookup/flipso_operators.c" "$ROOT/itso/itso_operators.c" \
  -o test_operators
./test_operators

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
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -I"$ROOT/itso" -I. -Istub \
  test_format.c "$ROOT/format/flipso_format.c" "$ROOT/format/flipso_format_product.c" \
  "$ROOT/format/flipso_format_card.c" "$ROOT/format/flipso_format_journeys.c" "$ROOT/cards/flipso_capture.c" "$ROOT/reader/flipso_media.c" \
  "$ROOT/itso/itso_parse.c" "$ROOT/itso/itso_util.c" "$ROOT/itso/itso_names.c" \
  "$ROOT/itso/itso_operators.c" \
  -o test_format
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
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -I"$ROOT/itso" -I. -Istub \
  test_media.c "$ROOT/reader/flipso_media.c" "$ROOT/format/flipso_format.c" "$ROOT/format/flipso_format_product.c" \
  "$ROOT/format/flipso_format_card.c" "$ROOT/format/flipso_format_journeys.c" "$ROOT/cards/flipso_capture.c" \
  "$ROOT/itso/itso_parse.c" "$ROOT/itso/itso_util.c" "$ROOT/itso/itso_names.c" \
  "$ROOT/itso/itso_operators.c" \
  -o test_media
./test_media

echo
echo "Icon list view"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT/views" -Istub \
  test_menu_view.c "$ROOT/views/flipso_menu_view.c" "$ROOT/views/flipso_glyphs.c" \
  -o test_menu_view
./test_menu_view

echo
echo "Scrolling text view"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT/views" -Istub \
  test_text_view.c "$ROOT/views/flipso_text_view.c" "$ROOT/views/flipso_glyphs.c" \
  -o test_text_view
./test_text_view
