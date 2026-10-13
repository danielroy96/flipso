# Keep the first page's answers on the first screen

## Goal

A text page shows four rows under its title before it scrolls (64 px, an
11 px row pitch, `views/flipso_text_view.c`), and a row of FontSecondary holds
about 21 characters. A `Label: Value` line longer than that wraps, and when it
is the fourth line the value lands on row five - below the fold of the page
that is meant to answer "is it good, and until when".

The README's own screenshot shows it: frame 06 of `docs/screenshots/sheet.png`,
Demo 01's period ticket, ends on "Current pass until:" with the date out of
sight. Others from the demo cards:

| Line | Length | Where | Built in |
| --- | --- | --- | --- |
| `Current pass until: 20/10/2026` | 30 | period ticket, page 1, line 4 | `format/product/flipso_product_details.c:193` |
| `Valid from: 05/09/2026 00:00` | 28 | Demo 07 period ticket, page 1, line 4 | `flipso_product_details.c:220` |
| `Last used: 21/09/2026 17:47` | 27 | Demo 05 paper day ticket | `format/product/flipso_product_space_saving.c:125` |
| `Journey details: Not recorded` | 29 | Last tap page (frame 09) | `format/flipso_format_journeys.c` |

Size: small, a day, most of it deciding words and checking them on the device.

## What to do

1. **Measure rather than guess.** The host stub canvas
   (`tools/test/test_text_view.c`) is a fixed 5 px a character, which is not
   FontSecondary. Give the host tests the real advance widths: confirm which
   u8g2 font the firmware maps `FontSecondary` to (its `canvas.c`), take that
   font's per-glyph widths from u8g2's published source into a small table in
   `tools/test/`, and add a check over every demo screen that reports each
   page's first four lines that wrap - with the page and line, as the house
   style test does. Report-only at first, so it can be tuned.
2. **Shorten the labels that wrap**, keeping house style (`Label: Value`,
   values capitalised, details indented and labelled):
   - "Current pass until" → e.g. "Pass ends" / "Pass ended".
   - A validity start at 00:00 is a day, not a time: say the date alone, as
     TYP 24's portions already do for 00:01 (PROTOCOL.md "Both portions open
     at 00:01").
   - Date and time together go on a detail line, as the Summary already does
     for "Last tap" (`flipso_cat_datetime_line(out, "  ", "When", ...)`):
     `Last used:` + `  When: 21/09/2026 17:47` - or simply `Last used: 21/09 17:47`
     where the year is the current one.
   Check every renamed label against the glossary (`docs/GLOSSARY.md`) and the
   other screens that use the same words (commit 415ed40 made them agree).
3. **Then decide whether to enforce it.** If, after the fixes, no first-page
   line in the demos wraps, turn the report into a failing check for the first
   four lines of each screen's first page only - later pages may wrap freely.

## Constraints

- Nothing is dropped to make room: the house rule is that every element the
  card carries is shown, defaults included. Move a line, shorten its label,
  or put its time on a detail line; never remove it.
- Page order and content are pinned by `tools/test/screen_text/`; the tests
  will name every line that changed. Update them as the new wording, not as a
  regression.
- Retake the README screenshots afterwards (`docs/screenshots/walk.txt`).

## Done when

- No first page of any demo card wraps a line among its first four rows, and a
  host check says so.
- `tools/test/run.sh` passes, `ufbt lint` is clean, and the screens are checked
  with `flipctl walk` on the device.
- This brief and its row in `README.md` here are deleted.
