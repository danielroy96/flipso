# Make Journeys a list

## Goal

Journeys is one text screen of pages (`flipso_format_taps()` in
`format/flipso_format_journeys.c`): "Last tap", then a page per journey, newest
first, the card's own before the ones only a saved file remembers, then one
Technical page for all of them. A saved card keeps up to twelve journeys
(`ITSO_MAX_TAPS`), so Demo 04 is fourteen pages, and finding last Tuesday's
trip means pressing Right until it appears - with no page counter, by design.

Products solved the same problem with a list (`scenes/flipso_scene_products.c`):
one row per product, tagged, each opening its own paged screen. Do the same for
journeys.

Size: medium, two days.

## What to build

1. **A Journeys scene** (`scenes/flipso_scene_journeys.c`, added to
   `flipso_scene_config.h` and `application.fam`), modelled on the products
   scene, using `FlipsoMenuView`:
   - header "Journeys" with the taps icon;
   - first row "Last tap" (or "Where you stand" - the log entry's page) when
     `card->log_entry_valid`;
   - then one row per tap, newest first, the card's own first, the file's after
     with the clock icon (`I_past_10px`), exactly the order and icons the pages
     use now.
2. **Row wording**, built in `format/` (never in a scene - house rule), e.g.
   label `20/09 Woking` and tag `£6.10`, or label `Tap out 20/09` and tag the
   place. The row is 128 px with an icon; `FLIPSO_MENU_LABEL_LEN` is 32 and
   `FLIPSO_MENU_TAG_LEN` 18, but what fits is far less - try both on the device
   and pick the one that tells journeys apart at a glance. Fare when there is
   one; otherwise the transaction ("Tap in").
3. **One journey's screen**: a new `FlipsoTextJourney` in `FlipsoTextScreen`
   (`flipso.h`) with `app->selected_tap`, as `FlipsoTextProduct` uses
   `app->selected_product`. Its pages: the journey page `flipso_cat_tap()`
   writes today, then its own Technical (`flipso_cat_tap_technical()`) when it
   has one. Split `flipso_format_taps()` into the log entry's page and a
   per-tap builder; keep a whole-log builder only if something still needs it
   (the export brief, `export-text.md`, will).
4. **The card menu** (`scenes/flipso_scene_menu.c`, `FlipsoCardMenuRowTaps`)
   opens the new scene instead of the text screen, and keeps its count tag.
   A card with a log entry and no taps can open the "Last tap" page directly
   rather than a one-row list.
5. **Selection is remembered** when coming back from a journey, as the
   products scene does with its scene state.

## Constraints

- Memory: the menu view holds the rows' strings; 13 rows of label and tag is
  well under a kilobyte, but measure with `flipctl mem` before and after, with
  Demo 04 open (CLAUDE.md, "Memory is the constraint").
- `FLIPSO_MENU_MAX_ITEMS` is 20: add a `_Static_assert` that it holds
  `ITSO_MAX_TAPS` + 1, as the products scene does for products.
- The README screenshots walk Journeys (`docs/screenshots/walk.txt`, the
  `journeys=` step); update the route and retake them.
- Coordinate with `trips.md`: if both are built, the list's rows are trips.

## Tests

- `tools/test/screen_text/`: the per-journey screen passes house style for
  every journey of every demo card, and the row text for Demo 04's journeys
  is pinned.
- `tools/test/test_menu_view.c` if the row needs anything new of the view.

## Done when

- Journeys opens a list that reaches any journey in a few presses, and every
  journey's screen says what its page says today.
- `tools/test/run.sh` passes, `ufbt lint` is clean, and the list is walked on
  the device with `flipctl walk`.
- This brief and its row in `README.md` here are deleted.
