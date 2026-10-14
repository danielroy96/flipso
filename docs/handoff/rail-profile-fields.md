# Apply the rest of RSPS3002's rail rules

## Goal

RSPS3002 (*ITSO in National Rail*, 02-01, cached by
`tools/spec/itso_spec.py --part rsps3002`) gives rail meanings to three
elements Flipso still shows as bare numbers. Each is small; they are one
brief because they share one question - how Flipso knows a product is rail -
and one test file, `tools/test/parse/test_rail_profile.c`.

Size: medium, two days.

## 1. The photocard ID in HolderID (TYP 14 and TYP 16)

RSPS3002 sections 3.3.1, 3.4.1 and 3.4.4 set HolderID to the rail photocard
ID, packed:

> Encoding of the 3 alphabetic and 4 numeric characters in one four byte
> field. Three alphabetic characters each in a 5 bit field. Letter "A"
> represented by a value of 0 and letter "Z" would be represented by a value
> of 25. The numeric fields would require a maximum value of 9999 so can be
> stored as a binary value in 14 bits. The photocard check digit is omitted
> only the first 7 characters are needed. The Photocard ID can be encoded as
> 29 bits with 3 leading 0 bits. Inspection devices must clearly display this
> photocard ID.

Flipso shows `Holder number: 12345678` under Technical
(`format/product/flipso_product_technical.c`). On a rail product show
`Photocard: ABC1234` on the holder's page instead, and keep the raw number
under Technical. Decode only when the top three bits are zero and every
letter field is 0-25; otherwise it is not one, whatever the owner. Work out
the field order (letters first, most significant first, is the natural
reading) and say which reading was taken in PROTOCOL.md.

## 2. ValidityCode's flags (TYP 22 and TYP 23)

RSPS3002 sections 3.6.6 and 3.7.6 define the five bits:

| Bits 4-0 | Meaning |
| --- | --- |
| `00000` | Null - not for use in rail |
| `1xxx0` | Test product |
| `1xxx1` | Live product |
| `x000x` | No gradation |
| `x001x` | High peak |
| `x010x` | Mid peak |
| `x011x` | Shoulder peak |
| `x100x` | Shoulder off-peak |
| `x101x` | Mid off-peak |
| `x110x` | Low off-peak |
| `x111x` | Reserved |

Read the table again from the PDF before coding: the extracted text lost its
column alignment, and whether bit 4 must be set for both test and live is
worth confirming. `ItsoTicketTerms::validity_code` is decoded already
(`itso_ipe_period.c`, `itso_ipe_journey.c`); Technical shows
`Validity code: N`. On a rail product (`itso_product_rail()`):

- the gradation goes with the off-peak terms on the Conditions page, e.g.
  `Peak level: Mid off-peak`;
- a test product says so on the first page, as TYP 24's TestOrLive flag
  already does (PROTOCOL.md, "TYP24Flags (test ticket first ...)");
- Technical keeps the number.

## 3. Railcards held as entitlements (TYP 14)

RSPS3002 section 3.3.1 encodes a railcard as an EntitlementCode and a
ConcessionaryClass together:

| Railcard | EntitlementCode | ConcessionaryClass |
| --- | --- | --- |
| Disabled Persons | 24 | 5 |
| Senior | 24 | 4 |
| Network | 24 | 15 |
| 16-25 | 24 | 3 |
| Family & Friends | 24 | 1 |
| Jobcentre Plus | 24 | 8 |
| HM Forces | 24 | 10 |
| Two Together | 25 | 1 |
| Gold Card (annual season, London and South East) | 24 | 16 |
| Scholar | 19 | 3 |
| TOC scheme specific | 26-29 | 1 or 3 |
| Staff | 30 | 9 |

EN1545 leaves codes 16 and up to the scheme, so Flipso shows them all as
"Scheme specific" (`itso_entitlement_name()`, `itso/itso_names.c`). On a rail
TYP 14, name the railcard: the Summary's entitlement line and the ID screen's
first page say `Railcard: Senior Railcard`. A pair not in the table keeps the
generic names.

## The hard part: knowing a TYP 14 or 16 is rail

`itso_product_rail()` looks at the product's locations and retailer, and an
identity IPE has neither: its byte 3 is an accounting reference, not a
retailer (`itso_parse_ipe_common()`). Options, best first:

1. **A rail flag on known train operators' OIDs** in the built-in operator
   table (`itso/itso_operators.c`) - set only for OIDs confirmed as train
   operators (109, 287 and 289 are, from real cards), each citing its
   source as the table's entries already do - and an optional fourth
   column in `operators.txt` so users can add their own. The owner OID of
   the TYP 14 or 16 decides.
2. The card's other products: a card whose period or reserved tickets are
   rail is a rail card, so its railcard entitlement is rail. Weaker - an ENCTS
   pass may carry a rail season - so use it only alongside 1.

Do not decide from the codes alone: entitlement 24 means something else in
every other scheme.

## Tests

- `tools/test/parse/test_rail_profile.c`: the photocard packing round-trips
  (`ABC1234`), a HolderID with high bits set is not decoded, each ValidityCode
  gradation, test vs live, and every railcard pair.
- `tools/test/screen_text/`: a rail TYP 14 shows the railcard name and the
  photocard; the same codes under a non-rail owner show what they show now.
- `tools/test/build_card.py`: a rail TYP 14 Senior Railcard on a synthetic
  card; consider adding it to a rail demo card (`tools/demo/`), and update
  `tools/demo/new_encodings.py`.

## Done when

- Rail photocards, peak levels, test products and railcards are shown in
  rail's terms, only where the product or its owner is shown to be rail.
- PROTOCOL.md's "National Rail's profile" covers all three.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
