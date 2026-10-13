# Pair tap-ins with their tap-outs

## Goal

A gated rail journey writes two Transient Ticket Records: a tap in at the
start, a tap out at the end. Flipso shows them as two journeys. Demo 04 has
"Tap in 18/09 07:44, From: Woking" and, separately, the tap out that closed
it. A holder thinks in trips.

A revision 4 tap out already carries its tap in: the ENTRY group (TS 1000-5
table 64) copies the check-in record's ISAM, ISAM sequence number and DTS, and
`flipso_cat_tap()` (`format/flipso_format_journeys.c`) already uses it to show
"In", "Out" and "Journey time". What is missing is recognising the tap-in
record that the ENTRY group points at and not showing it again as a journey of
its own.

Size: medium, a day and a half. Do it after, or with, `journeys-list.md`.

## What to build

1. **Exact pairs (revision 4).** A tap-in record *is* the one a tap out's
   ENTRY names when its own InstanceID matches: its writer ISAM equals
   `entry_isam`, its ISAM sequence number equals `entry_isam_seq`, and its DTS
   equals `entry_dts`.
   - `itso_parse_tap()` (`itso/itso_log.c`) reads the record's InstanceID ISAM
     (`writer_isam`, at `declared + 1`) but not the three-byte ISAM sequence
     number after it (`declared + 5` to `declared + 7`, TS 1000-2 table 11).
     Add `writer_isam_seq`; check `sizeof(ItsoTap)` on the device before and
     after, and fit it into padding if it grows the struct.
   - Mark the tap-in as consumed - a flag on `ItsoTap`, or a pass in the
     formatter - so it is shown inside its tap out's page, not as a page of
     its own.
2. **Likely pairs (revisions 1-3, and bus).** With no ENTRY group, a tap in
   followed by a tap out on the same day, with nothing between them and the
   tap out's origin equal to the tap in's place, is very probably one trip.
   Show such a pair together but say it is inferred - e.g. no "Journey time",
   or "Journey time: About 47 min" - and never pair across a record that is
   only in the file and one that is on the card.
3. **On screen.** The trip's page is the tap out's page as now, with the tap
   in's own lines that the ENTRY group does not carry (its product, its fare
   if any, the gate's operator) folded in under "In". A tap in with no tap out
   yet is the current journey: "Tap in 21/09", and the Last tap page's
   "Inside ticket gates: Yes" already says the holder is mid-journey.
4. **The Journeys count** on the card menu (`flipso_scene_menu.c`) counts
   trips, not records, once they are paired; so does the journeys list row
   count if `journeys-list.md` is built.

## Constraints

- A saved card merges records across reads (`itso_parse_log_history()`); a
  tap in may be in the file and its tap out on the card. Pairing by InstanceID
  is still exact then; the inferred pairing must not cross that boundary.
- `ItsoTap` is 128 bytes and up to twelve are kept; anything added is paid
  twelve times.
- Records are matched for de-duplication by DTS and transaction type
  (`itso_add_tap()`); do not change that.

## Tests

- `tools/test/build_card.py`: a revision 4 tap in and the tap out that names
  it; a tap out whose ENTRY names a record not on the card (rolled off); a
  revision 2 tap in / tap out pair on one day; two tap ins in a row.
- `tools/test/parse/test_synthetic_log.c` for the sequence number, and
  `tools/test/screen_text/` for the pages: one page per trip, the unmatched
  tap out still shows its "In" time from ENTRY, the unmatched tap in stands
  alone.
- Demo 04's journeys, re-pinned.

## Done when

- A tap in that a tap out names is shown inside it, an inferred pair says it
  is inferred, and nothing is hidden that is not shown elsewhere.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
