# Name rail products the way National Rail does

## Goal

A rail season ticket is shown as "Period ticket", a PlusBus as "Period
ticket", an off-peak return as "Journey ticket". The card says more than that:
RDG's *ITSO in National Rail Specification* (RSPS3002 02-01, cached by
`tools/spec/itso_spec.py --part rsps3002`) allocates the IPE sub-type (PTYP)
for rail products, and Flipso already reads RSPS3002 for TYP 24's railcards and
retailer NLCs. The PTYP is only shown as "Operator's sub-type: N" under
Technical (`format/product/flipso_product_technical.c`).

Use the allocation to title rail products by what they are.

Size: small, about a day.

## The allocation

| TYP | PTYP | Rail product | Source |
| --- | --- | --- | --- |
| 22 | 0, 1, 2, 3 | Season ticket | RSPS3002 3.6.2 |
| 22 | 4 | Charge To Account season ticket (Flexismart) | 3.6.2 |
| 22 | 5 | Season ticket with stored passes (Flexi Season) | 3.6.2 |
| 22 | 19 | Travelcard | 3.6.2 |
| 22 | 28 | Rover | 3.6.2 |
| 22 | 29 | PlusBus | 3.6.2; appendix C.4 ("reserved for use on National Rail for PlusBus") |
| 22 | 30 | Gate pass | 3.6.2 |
| 22 | 31 | Staff pass | 3.6.2 |
| 23 | 1 | Peak single | 3.7.2 |
| 23 | 2 | Off-peak single | 3.7.2 |
| 23 | 3 | Peak return | 3.7.2 |
| 23 | 4 | Off-peak return | 3.7.2 |
| 23 | 5 | Peak carnet | 3.7.2 |
| 23 | 6 | Off-peak carnet | 3.7.2 |
| 24 | 1 | All products | 3.8.2 - nothing to rename; FTOT already names it |
| 14, 16 | 1 | Railcard / personalisation; concession | 3.2, 3.5 - not worth a rename |

Read 3.6.2 and 3.7.2 again before writing the table: the PDF's text extraction
lost the table borders, and the four season-ticket rows are worth confirming.

## When a product is rail

PTYP is the owner's private number everywhere else - a bus operator's TYP 22
PTYP 2 means whatever that operator decided. So the names apply only when the
product shows it is rail. In order of strength:

1. Its `from` or `to` is a rail location: LocDefType 203 or 208
   (`ItsoLocation::def_type`). PlusBus is encoded this way (RSPS3002 C.3:
   both ends are LocDefType 203 NLCs).
2. Its retailer is a station NLC: `itso_product_sold_at()` succeeds
   (`itso/ipe/itso_ipe.c`).

Do not use the owner OID: the operator table has no notion of rail, and a
TOC's OID can own bus products (the same reasoning PROTOCOL.md gives for the
retailer test). A product with neither keeps its ITSO name.

## Where the code is

- `flipso_product_title()` (`format/product/flipso_product_lines.c`) is the one
  place a product's name is decided; the Summary, the product list, the
  product screen's first page title and every cross-reference ("Product:
  Period ticket" on a journey) go through it. Put the rail test and table
  beside it, or in `itso/itso_names.c` beside `itso_typ_name()` if the decoder
  should know.
- Names must fit `FLIPSO_MENU_LABEL_LEN` (32) and read well as a page title
  and a summary label: "Off-peak return: Until 13/03/2027" is 33 characters,
  so check the Summary line wraps acceptably on the device.
- Under Technical, keep "ITSO type: 22" and the sub-type, and add what the
  sub-type means: `  Operator's sub-type: 29 (PlusBus)`.
- The product list tells two products of one name apart by date
  (`flipso_scene_products_shares_name()`); that still works on the new names.
- `docs/PROTOCOL.md`: add the allocation under "National Rail's profile".

## Tests

- `tools/test/parse/test_rail_profile.c` or `tools/test/screen_text/`: a TYP 22
  PTYP 29 with NLC ends is titled PlusBus; the same PTYP with NaPTAN ends is
  "Period ticket"; a TYP 23 PTYP 4 sold at a station is "Off-peak return".
- Demo cards: Demo 01's period ticket (Southeastern, Tunbridge Wells - London
  Bridge, PTYP 2) becomes "Season ticket". `tools/test/screen_text/` pins
  titles and page order for every demo, so update those expectations
  deliberately, and retake the README screenshots (`docs/screenshots/walk.txt`)
  since frames 05 and 06 show the old name.

## Done when

- Rail products carry rail's names everywhere a product is named, and nothing
  that is not demonstrably rail changes.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
