# Save the raw data of a card that would not decode

## Goal

The cards most worth a saved file are the ones Flipso could not decode — a file
is exactly what a bug report needs — yet they are the ones that cannot be saved:

- `flipso_capture_valid()` (`cards/flipso_capture.c`) requires a Shell or Type 2
  block, and a shell is only added to the capture **after** it parses
  (`flipso_read_shell()` in `reader/flipso_reader.c`, and the CMD2 path in
  `reader/flipso_cmd2.c`). A "Card not readable" (bad shell) read captures nothing.
- The error scene (`scenes/flipso_scene_error.c`) has one button, and no save.
- The CMD9/CMD10 "Unsupported" path in `reader/flipso_type2.c` decodes the shell but
  does not add the page block to the capture.
- A non-ITSO DESFire (Oyster, other) has a Chip block but no shell.

## What to build

1. **Capture what arrived even when it fails to parse.** Keep the raw shell bytes
   (a new block kind, e.g. `FlipsoBlockRawShell`, or keep `FlipsoBlockShell` and
   let decode fail) and the Type 2 pages for the Unsupported path. Keep the
   capture design principle: a saved file is what the card said, decoded by the
   build that is running.
2. **Offer "Save data" on the error screen** for BadShell, Unsupported, Oyster
   and non-ITSO DESFire with media. The widget has a Center button already;
   Left/Right are free.
3. **Loading such a file** must not say "The file is damaged" (the current
   message in `flipso_scene_saved.c`). Route it to the error screen or the Card
   details (media) screen, as a live read of that card would.
4. File naming: suggest something like "Unreadable 1234" (last digits of what
   identity there is — UID for Type 2, card number if the BCD is intact).

## Constraints

- Privacy: raw data holds the card number and possibly a name — the About screen
  already warns about saved cards; the save prompt for raw data should too.
- `flipso_capture_valid()` gates the menu's Save row and `flipso_saved_read()`;
  changing it changes both. Consider a separate "has anything worth saving".
- Keep the file format compatible: new block kinds are skipped by older builds
  (see `flipso_capture_parse_line()`); bump `FLIPSO_CAPTURE_VERSION` only if an
  older build would *misread* the new file.

## Tests

- `test_capture.c`: a bad shell round-trips through a file and decodes to the
  same rejection reason (`card.shell_reject`).
- `test_saved.c`: such a file loads without being called damaged.
- Replaying one with `tools/test/replay.py` should reproduce the failure.

## Done when

- Every error screen that had bytes offers to save them, and the saved file
  reopens to the same explanation.
