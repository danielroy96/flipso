# Show a journey record's operator-defined bytes

## Goal

A Transient Ticket Record of format revision 4 can end with a UserDefined
group: TTBitMap2 bit 11, "UserDefined element present", variable length, "may
occupy all the unused space available in the record" (TS 1000-5 table 66 and
clause 3.2 rules 3 and 4; the element is in table 64, group "TT UD").

`itso_parse_tap()` (`itso/itso_log.c`) walks bits 0 to 10 and stops, so an
operator's own bytes are never shown. Nothing can name them, but showing them
is the house rule - every element the card carries is on a screen - and it is
the field most likely to explain an odd record from an operator nobody has
documented.

Size: small, half a day.

## What to build

1. **Find it.** After the bit 0-10 groups, if bit 11 is set and
   `tap->format_rev >= 4`, the UserDefined group runs from `pos` to the end of
   the record's declared dataset (`TTLength` blocks), less trailing zero
   padding (table 64: "Pad to a whole number of blocks with 0x00's"). Trailing
   zeros are ambiguous - a UD byte may be zero - so show the bytes up to the
   declared end and say they include padding, or strip them and say so; pick
   one and write it in PROTOCOL.md.
2. **Do not hold it in `ItsoTap`.** A tap is 128 bytes on the device and a
   card keeps up to twelve; a 40-byte buffer in each is 480 bytes for a field
   almost no card uses. Keep two bytes - the group's offset and length within
   the record - and render the bytes when the screen is drawn, from the raw
   record in the capture (the `Log` and `Log history` blocks,
   `cards/flipso_capture.h`), as fare capping and TYP 24's reservations are
   decoded on demand (`flipso_decode_capping()` in
   `format/product/flipso_product_purse.c`). A record is found again by its
   DTS and transaction type, which is how `itso_add_tap()` already identifies
   one.
3. **On screen**: under the journey's Technical entry
   (`flipso_cat_tap_technical()` in `format/flipso_format_journeys.c`), as
   `  Operator data: 0A 1B 2C ...`, wrapping between byte groups as the media
   screen's file contents do. `flipso_tap_has_technical()` must count it.

## Tests

- `tools/test/build_card.py`: a revision 4 record with bit 11 set and a few UD
  bytes after an ENTRY group, built exactly as long as its TTLength says (the
  suite runs under ASan; an oversized buffer hides an over-read).
- `tools/test/parse/test_synthetic_log.c`: offset and length are right; a
  record with bit 11 set but no room left decodes with no UD and no over-read;
  bit 11 on a revision 3 record is ignored (it is RFU there).
- `tools/test/screen_text/`: the bytes appear under Technical.

## Done when

- A record's UserDefined bytes are on its Technical entry, at no cost to the
  size of `ItsoTap`.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
