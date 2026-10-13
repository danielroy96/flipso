# A Settings screen, with a privacy mode

## Goal

Flipso has no settings. Two behaviours would benefit from one:

- **Privacy mode.** People share Flipper screenshots. Flipso's screens show
  the holder's name, date of birth, card number, chip serial and other
  identifiers - which is why the README and the catalog only ever show demo
  cards (`docs/screenshots/walk.txt` says so). A switch that hides personal
  details lets a holder show what is on their card without showing who they
  are.
- **Auto-save.** A card is saved by choosing Save card after each scan
  (`scenes/flipso_scene_save.c`). The value of saving is cumulative - a saved
  card remembers journeys and transactions the card has rolled off (PROTOCOL.md,
  "A saved card remembers more than the card does") - so a holder who forgets
  to save loses exactly the history that made it worth doing.

Size: medium to large, three days.

## What to build

1. **Settings storage.** One small struct in `/ext/apps_data/flipso/settings`,
   loaded at start and saved on change, with the SDK's `saved_struct`
   (`lib/toolbox/saved_struct.h`): a magic, a version and the fields. A
   missing or unreadable file is all defaults. Follow CLAUDE.md's storage rule
   - close a handle even when its open failed - which
   `tools/test/lint_storage.py` checks.
2. **A Settings scene** reached from the About menu (`scenes/flipso_scene_about.c`,
   a row beside "About Flipso" and "Demo cards"), built on `FlipsoMenuView` or
   the firmware's `VariableItemList`, whichever costs less RAM - measure both
   with `flipctl size`. Rows:
   - `Privacy mode: On/Off`
   - `Auto-save cards: On/Off`
3. **Privacy mode** as one flag on `FlipsoFormat` (`format/flipso_format.h`),
   so every screen honours it from one place and the host tests can render
   both ways. What it hides (search `format/` for each - the list below is
   from a sweep and may not be complete):
   - holder name (`id->name`; Summary "Holder", ID page "Name") and TYP 24's
     passenger name (`format/product/flipso_product_reservation.c`);
   - date of birth;
   - the card number (`flipso_cat_isrn()`, `format/flipso_format_card.c`):
     keep IIN and OID, which say whose card it is, and hide the serial - e.g.
     `633597 0289 •••• ••16`;
   - MCRN, chip UID and the media screen's UID, holder and second-holder
     numbers, photocard number, railcard number, ID document;
   - the saved card's own name on the Card screen (users name files after
     people).
   Hidden values are replaced with a fixed word ("Hidden") or a mask, never
   removed - the line stays, so the screen still shows the card holds it.
   The menu header gets a small marker while it is on, so a screenshot shows
   it was taken that way.
4. **Auto-save.** After a successful scan, before the card menu, do what Save
   card does: update the matching record (merging history), or create one with
   the suggested name (`flipso_saved_suggest_name()`), without asking. Show a
   one-line confirmation (a short notification or the menu header) and keep
   Rename and Delete on the menu. A paper ticket, matched by chip serial,
   works the same way. A read that is not `flipso_capture_valid()` is not
   saved.

## Constraints

- Memory: a new scene and its view are code in RAM. Measure `flipctl size`
  and `flipctl mem` before and after; the app has about 26 KB free at idle and
  17.6 KB with Demo 01's reserved journey open (CLAUDE.md).
- Auto-save writes to the SD card on every scan; the save path's power-cut
  safety (`.tmp` then rename, `cards/flipso_saved_recover.c`) already applies,
  but check a scan that ends in an error writes nothing.
- Privacy mode must hold for the export (`export-text.md`) and any future
  shareable output as well as the screen.

## Tests

- `tools/test/screen_text/`: every demo card rendered with privacy on contains
  none of its name, birth date, full ISRN or UID strings (assert by searching
  the rendered text for each), and still passes house style.
- `tools/test/saved/`: auto-save of a known card merges as Save card does.

## Done when

- Settings persist across launches; privacy mode hides every personal
  identifier on every screen; auto-save keeps a card's record current without
  a prompt.
- `tools/test/run.sh` passes, `ufbt lint` is clean, memory is measured on the
  device.
- This brief and its row in `README.md` here are deleted.
