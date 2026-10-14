# Name a rail ticket's route

## Goal

RSPS3002 sets RouteCode on TYP 22, 23 and 24 to "the 5 digit route code as
used to define the flow on which the rail product is priced (as defined within
the RSP fares data)" (sections 3.6.3, 3.7.3, 3.8.3). The route is part of
what a rail ticket is: "Not via London", "Via Stratford or London Terminals".
Flipso names only `00000` ("Any permitted") and shows every other code bare
under Technical (`format/product/flipso_product_technical.c`, the
`has_route_code` branch).

RDG publishes the names. Name the route on the ticket's first page.

Size: medium, two days - mostly the table builder, which has two models to
copy.

## The source

**Fare Route Restrictions reference data, v1.2** - RDG's IDMS reference data
(RSPS5044 section 5, `FareRouteRestrictionsRefData_v1.2.xsd`), listed as an
open XML feed on the Rail Data Marketplace beside the ticket types reference
data Flipso already ships (`tools/ticket_types/SOURCES.md`). One record:

```xml
<FareRouteRestriction>
  <Code>00003</Code>
  <Name>STRATFORD/LONDON</Name>
  <Description>STRATFORD/LONDON</Description>
  <StartDate>1996-11-14</StartDate>
  <EndDate>2999-12-31</EndDate>
  <OJPDisplayName>Valid only for travel via (changing trains or passing
    through) Stratford or London Terminals.</OJPDisplayName>
  <RSPDisplayName>Valid only for travel via Stratford or London
    Terminals</RSPDisplayName>
</FareRouteRestriction>
```

RSPS5044 says which name to show: on a screen, `OJPDisplayName`, then `Name`.
RSPS5044's own S3 link (`s3-eu-west-1.amazonaws.com/idms/...`) answered 403 on
2026-10-14, so the file has to come from the Marketplace, which needs the
user's account - ask them for it, as the ticket types XML was obtained.
**Check its licence** says what the ticket types data's does (free
redistribution) before committing it. The fallback source is RSPS5045's
ROUTES file (record type `R`, 16-character `DESCRIPTION`), which needs a
National Rail Data Portal registration and has only the terse names.

A code has dated records (`StartDate`, `EndDate`). Keep only the records live
at build time, or the newest per code; a ticket's route was the code's meaning
when it was sold, which is close enough to now for a table refreshed a few
times a year.

## What to build

1. **`tools/route_names/`**: the saved XML, `SOURCES.md`, `FORMAT.md` and a
   builder writing `assets/route_names.dat`, modelled on
   `tools/ticket_restrictions/` (the most recent table, which already keeps a
   short name and a longer text per code and stores shared texts once).
   `tools/test/run.sh` rebuilds it and fails when `assets/` is stale, as it
   does for the other tables.
2. **`lookup/flipso_route_names.c`**, opened per lookup through the shared
   reader (`lookup/flipso_table.c`), like the ticket type and restriction
   tables - only rail tickets carry a route, so it need not stay open.
   Add it to `application.fam` and `tools/test/sources.py`, and to
   `tools/test/screens.py`'s parts and stub assets (the restrictions table
   once broke `screens.py` by missing that step).
3. **On screen.** Only for a product that is rail (`itso_product_rail()`,
   `itso/ipe/itso_ipe.c`) - elsewhere RouteCode is the owner's own:
   - First page, after From and To: `Route: Not via London` - the short name,
     or the display name if it fits; the long text belongs on the Terms page
     if it differs.
   - Technical keeps `Route code: 00003` either way.
   - An unknown code shows only under Technical.
4. **About**: count the route names, as it counts ticket types and restriction
   codes (`format/flipso_format_about.c`).

## Constraints

- No RAM for the table: it is a file asset searched in place
  (`.fapassets`), like the other three.
- `00000` keeps its existing "Any permitted" reading, which works without the
  table.
- PROTOCOL.md: a "Route names" section beside "Ticket types", and the
  README's licensing section a paragraph like "Rail ticket types".

## Tests

- `tools/test/test_route_names.c` against a small built table, as
  `test_ticket_types.c` does: a known code, an unknown one, the first and last.
- `tools/test/screen_text/`: Demo 01's season ticket (or a synthetic rail
  ticket with a non-zero route) shows its route on page 1; a bus TYP 22 with a
  route code shows none.

## Done when

- A rail ticket says which way it may go, in RDG's words, at no cost in RAM.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
