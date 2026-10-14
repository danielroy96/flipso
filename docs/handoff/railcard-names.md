# Name railcards from RDG's own list

## Goal

A TYP 24 carries the railcard it was priced with as a three-letter
DiscountCode (RSPS3002 section 3.8.3), and Flipso leads the ticket with it:
"Valid only with: Senior Railcard". The names come from a table in
`itso/itso_names.c` (`itso_railcards[]`, 29 entries) compiled from SAP
Concur's published discount codes, a fares-data gist and the RailUK fares
guide - secondary sources, a fixed list, and about 350 bytes of constant data
in RAM.

RDG publishes the list itself. Build the table from it.

Size: small to medium, a day.

## The source

**Railcards reference data, v1.2** - RDG's IDMS reference data (RSPS5044
section 7, "Discount Schemes Reference Data", `RailcardsRefData_v1.2.xsd`),
listed as an open XML feed on the Rail Data Marketplace beside the ticket
types data Flipso already ships. "It includes both national and local
schemes." One record:

```xml
<Railcard>
  <Name>WEST HIGHLAND RAILCD</Name>
  <Code>WHC</Code>
  <OJPEnabled>false</OJPEnabled>
  <OJPDisplayName>Highland Railcard : West Scotland</OJPDisplayName>
  <RSPDisplayName>West Highland Railcard</RSPDisplayName>
  ...
</Railcard>
```

On a screen RSPS5044 says to use `OJPDisplayName`, then `Name`; here
`RSPDisplayName` reads better on 128 px - pick per name length and say which
in `SOURCES.md`. The S3 link in RSPS5044 answered 403 on 2026-10-14, so the
file comes from the Marketplace with the user's account; check its licence
allows redistribution, as the ticket types data's does. RSPS5045's RAILCARDS
file (`RAILCARD_CODE`, 20-character `DESCRIPTION`, `HOLDER_TYPE`) is the
fallback, behind a National Rail Data Portal registration.

## What to build

1. **The table.** Either a file asset like `assets/ticket_types.dat`
   (`tools/ticket_types/` is the model, `lookup/flipso_table.c` the reader),
   which moves the names out of RAM; or, if the list is short, a generated
   `const` table replacing `itso_railcards[]`. Measure both with `flipctl
   size`; the file asset is the default.
2. **Keep what the current table knows that RDG's may not.** The `card` flag
   (`itso_railcards[].card`: a card to carry, rather than a group or online
   offer) has no equivalent in the reference data; check whether
   `<OJPEnabled>` or another element says the same, or keep a small override
   list for the codes that are offers, not cards. `XXXXX` ("entitlement on
   the card", PROTOCOL.md) stays special-cased.
3. **Callers**: `itso_railcard_name(code, len, &card)` (`itso/itso_names.h`)
   has two, both in `format/`: the "Valid only with" line
   (`format/product/flipso_product_lines.c`, used by the Summary and the
   first pages) and the reserved journey's discount
   (`format/product/flipso_product_reservation.c`). Moving the names to a
   file means `itso/` can no longer name a railcard on its own (it has no
   firmware access): make it a `format/` lookup passed through
   `FlipsoFormat`, as ticket types are, and keep the `card` out-parameter.
4. If `rail-profile-fields.md` is built first, its TYP 14 railcard names can
   come from here too (by code where it has one).

## Constraints

- The host tests and `tools/test/screens.py` need the new table and a stub
  asset (`tools/test/sources.py`, `screens.py`'s `PARTS` and stub list).
- PROTOCOL.md's DiscountCode paragraph and the README's licensing section
  name the new source.

## Tests

- A lookup test against a small built table, as `test_ticket_types.c` does.
- `tools/test/screen_text/`: Demo 01's reserved journey still reads "Valid
  only with: Disabled Persons Railcard" (or RDG's wording for `DIS`), and an
  unknown code still shows as "Discount:" and the code.

## Done when

- Railcard names come from RDG's list, at no more RAM than today.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
