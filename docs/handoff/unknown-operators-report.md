# Let users report the operators Flipso cannot name

## Goal

ITSO does not publish its OID register, so the operator table
(`itso/itso_operators.c`) grows one card at a time, and the README's first
request to contributors is operator names. But a user who sees
"Operator: Unknown (13870)" has no easy way to send the useful part - which
numbers, in which roles, on what kind of card - without also sending a saved
card full of their name and card number.

Give them a file that holds exactly what a contribution needs and nothing
personal.

Size: small to medium, a day. Pairs with the existing `operators-file-docs.md`,
which documents the user's own `operators.txt`.

## What to build

1. **Move the gathering to the app.** `tools/test/screens.c` already walks
   every operator a card names - shell owner, card issuer, directory writer's
   ISAM, each product's owner, retailer, creating ISAM and value-record ISAM,
   each tap's reader, entry operator and tap-in reader - and classifies each
   against TS 1000-2 table B2 (`oid_range()`). Move that walk and the
   classification into `itso/` (pure C, host-testable) as a callback or an
   iterator, and have `screens.c` use it, so the host table and the device
   report cannot disagree. Retailers that are station NLCs
   (`itso_product_sold_at()`) are not OIDs and are left out.
2. **A card menu row** "Report unknown operators", shown only when the card
   names at least one OID neither the built-in table nor the user's
   `operators.txt` knows (`flipso_operators_name()` returns NULL).
3. **The file**: appended to `/ext/apps_data/flipso/unknown_operators.txt`,
   one line per unknown OID and role, de-duplicated against what is already
   in the file, e.g.

   ```
   13870,product owner,Loyalty (TYP 17),card issuer 289 Southeastern,CMD7
   63519,value record ISAM,Period ticket (TYP 22),card issuer 289 Southeastern,CMD7
   ```

   Context that helps identify an operator - the role, the product type, the
   known operator that issued the card, the media type, and the brand Flipso
   showed - but **never** the card number, chip UID, ISAM serials, names,
   dates or places. A short header says what the file is for and where to
   send it (a GitHub issue on the repository).
4. **A confirmation screen** saying how many numbers were added and the path.
5. **README**: in the Contributing section, say how to make and send the file;
   `operators-file-docs.md` adds the section for `operators.txt` itself - do
   them together if both are open.

## Constraints

- Close storage handles even when an open fails (CLAUDE.md;
  `tools/test/lint_storage.py`).
- The OIDs in ITSO's reserved 8001-8191 range are ITSO's own and not worth
  reporting (8160 on SWR's gates, 8189 on every compact shell): leave them
  out, or mark them, as `oid_range()` would.
- Nothing card-specific: the test below enforces it.

## Tests

- Host: walking Demo 01 yields exactly the OIDs its "Operators on this card"
  table (`tools/test/screens.py`) marks as not in the table - 13870, 4410,
  3073, 32 and 63519 at the time of writing - and the report text contains
  none of the card's ISRN, UID, holder name or ISAM IDs.
- De-duplication: reporting the same card twice adds nothing the second time.

## Done when

- A user can produce, from the card menu, a file of unknown operator numbers
  with enough context to identify them and no personal data, and the README
  says where to send it.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
