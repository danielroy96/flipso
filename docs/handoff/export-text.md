# Export a card as text

## Goal

A saved `.flipso` file is the card's raw blocks (PROTOCOL.md, "Saved cards") -
right for decoding, unreadable to a person. To read a card on a computer, print
it, or paste it into a bug report, you need Flipso's screens, and on a
computer that means building `tools/test/screens.py` from the repository.

Add "Export as text" to the card menu: every screen of the card, as plain text,
written to the SD card.

Size: medium, a day and a half.

## What to build

1. **A card menu row** (`scenes/flipso_scene_menu.c`), "Export as text", for
   any card with something to show - a live read or a saved one, demo cards
   included.
2. **The file**: `/ext/apps_data/flipso/exports/<name>.txt`, where the name is
   the saved card's name, or the suggestion Save would offer
   (`flipso_saved_suggest_name()`) for a card not yet saved. Overwrite an
   export of the same name. Say where it went on a short confirmation screen.
3. **The content**, in the order the menu offers the screens: Summary, Card,
   Pay as you go, ID & entitlement, Journeys, each product, as
   `tools/test/screens.c` walks them. Each screen built with the same
   `flipso_format_*()` function the app uses, then turned into plain text:
   - `FLIPSO_TEXT_PAGE` (`\f`) becomes a blank line;
   - a heading `\e#` + icon byte + title becomes the title, underlined or in
     capitals (`views/flipso_text_view.h` has the markup);
   - `£` stays as UTF-8.
   A header first: app version, export time, and that the file contains
   personal information.
4. **Memory**: build one screen into a `FuriString`, write it, free it, then
   the next - never all of them at once. The longest screen is about 3 KB
   (Demo 01's reserved journey); measure the peak with `flipctl mem`.
5. **Privacy**: if `settings-privacy.md` is built, privacy mode applies to the
   export too, and the confirmation says which way it was written.

## Constraints

- Close every storage handle even when its open failed (CLAUDE.md; checked by
  `tools/test/lint_storage.py`). Create the `exports` folder if missing.
- Keep the plain-text conversion in `format/` so the host tests can run it; the
  scene only writes the bytes.
- Consider making `tools/test/screens.py` use the same conversion, so the host
  tool and the device export cannot drift apart.

## Tests

- A host test that exports each demo card through the conversion and checks
  it contains every page title its screens have and no `\e` or `\f` bytes.
- On the device: export Demo 01, `flipctl pull` the file, and read it.

## Done when

- Any card can be exported from its menu to a readable text file that holds
  every screen, at a bounded memory cost.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
