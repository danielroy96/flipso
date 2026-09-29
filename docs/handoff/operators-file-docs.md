# Make the operators file discoverable

## Goal

Users can name unknown operators (and brand their cards) with
`/ext/apps_data/flipso/operators.txt`, but nothing a user reads says how:

- `README.md` never mentions it.
- The About screen says "Add names to apps_data/flipso/operators.txt on the SD
  card." without the format.
- `operators.example.txt` (repo root) documents it well, but only
  `docs/PROTOCOL.md` points at it.
- Only the first 48 entries are read (`FLIPSO_OPERATORS_MAX` in
  `lookup/flipso_operators.c`); extra lines are dropped silently, and About reports the
  count as if it were complete.

## What to do

1. **README**: a short "Operator names" section under Getting started — what the
   file is for, where it goes, the one-line format `<number>,<name>[,<brand>]`,
   and a link to `operators.example.txt`.
2. **About screen** (`flipso_format_about()` in `format/flipso_format.c`): add the
   format, e.g. `One per line: number,name`. Keep lines short; the text view
   wraps, but a long unbroken example reads badly on 128 px.
3. **Truncation**: have `flipso_operators_alloc()` record that it stopped at
   the cap with lines still unread (a `bool truncated` in `FlipsoOperators` and
   an accessor), and have About say `Your operators file: 48 names (more were
   skipped)`. Raising the cap is also reasonable — each entry is 58 bytes, and
   the array is trimmed with `realloc` to what was read — but measure with
   `flipctl mem` first.
4. Where the app shows `Unknown (1234)`, the number is exactly what goes in the
   file; consider saying so once on the About screen.

## Tests

- `tools/test/test_operators.c`: a file with more than the cap sets the
  truncated flag; one at or under it does not.
- `flipso_format_about()` is host-testable now — add a check in
  `test_format.c` that the format line is present.

## Done when

- README and About both explain the format; a file over the cap says so.
