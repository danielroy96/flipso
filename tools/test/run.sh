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
echo "Station table"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_stations.c "$ROOT/flipso_stations.c" \
  -o test_stations
./test_stations

echo
echo "Stop table"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_naptan.c "$ROOT/flipso_naptan.c" \
  -o test_naptan
./test_naptan

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
  test_operators.c "$ROOT/flipso_operators.c" "$ROOT/itso/itso_operators.c" \
  -o test_operators
./test_operators

echo
echo "Card media"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT" -Istub \
  test_media.c "$ROOT/flipso_media.c" \
  -o test_media
./test_media

echo
echo "Icon list view"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT/views" -Istub \
  test_menu_view.c "$ROOT/views/flipso_menu_view.c" \
  -o test_menu_view
./test_menu_view

echo
echo "Scrolling text view"
${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined \
  -I"$ROOT/views" -Istub \
  test_text_view.c "$ROOT/views/flipso_text_view.c" \
  -o test_text_view
./test_text_view
