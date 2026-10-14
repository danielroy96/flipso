# Give fare groups their passenger-facing names

## Goal

Rail tickets are often sold to or from a fare group rather than a station -
"London Terminals", "Colchester Stns" - and the card holds the group's NLC
(LocDefType 203 or 208). The station table names groups from railwaycodes.org.uk
(`tools/stations/SOURCES.md`), whose group names are the industry's own and
sometimes read like codes: Demo 07's season ticket shows
`To: London Zone R1256`.

RDG publishes the names a passenger is meant to see. Use them for fare groups
and, where they are better, for stations.

Size: small to medium, a day, most of it in the station table builder.

## The source

RDG's IDMS reference data (RSPS5044), listed as open XML feeds on the Rail
Data Marketplace beside the ticket types data Flipso already ships:

- **Fare Locations reference data, v1.2** (section 3,
  `FareLocationsRefData_v1.2.xsd`): one record per fare location, by NLC -
  ```xml
  <FareLocation>
    <Nlc>0254</Nlc>
    <Name>COLCHESTER STNS</Name>
    <OJPDisplayName>Colchester or Colchester Town</OJPDisplayName>
    <RSPDisplayName>Colchester/Colchester Town</RSPDisplayName>
  </FareLocation>
  ```
- **Stations reference data, v1.2** (section 4): station NLC, CRS, TIPLOC and
  display names.

RSPS5044's rule for a screen: `OJPDisplayName`, then `Name`. The S3 links in
RSPS5044 answered 403 on 2026-10-14; the files come from the Marketplace with
the user's account. Check their licence allows redistribution, as the ticket
types data's does.

Optionally, **Welsh names** from RSPS5052 section 5 (`WelshStationNamesByNlc`,
CSV keyed by NLC, published at `datafeeds.rdg.s3.amazonaws.com/RSPS5052/`;
version `02-01` of the file answered 404 on 2026-10-14, so find the current
`nn-nn`). That would need a language choice - leave it unless the Settings
screen (`settings-privacy.md`) exists to hold one.

## What to build

1. **`tools/stations/build_stations.py`** takes a third source: for a fare
   group NLC, the Fare Locations display name wins over railwaycodes'; for a
   station, keep the ORR name unless RDG's is clearly better - compare a
   sample before deciding, and record the rule in `tools/stations/FORMAT.md`
   or `SOURCES.md`.
2. Names must still fit the screen: `From:` and `To:` lines wrap, and the
   first page shows four lines (see `first-page-wrapping.md`). Where
   `OJPDisplayName` is long ("Colchester or Colchester Town"), prefer
   `RSPDisplayName` ("Colchester/Colchester Town").
3. The table format is unchanged - a name per NLC - so only the builder and
   `assets/stations.dat` change, and nothing in the app. Check the asset's
   size; it is packaged and uploaded on every install.

## Tests

- `tools/test/test_stations.c`: a fare group NLC from the new source resolves
  to its display name.
- `tools/test/screen_text/`: Demo 07's season ticket no longer says
  "London Zone R1256"; re-pin the demo screens that change, and retake the
  README screenshots if a pictured name changes.
- `run.sh` rebuilds the table from its sources and fails when `assets/` is
  stale, as now.

## Done when

- Fare groups read as a passenger would recognise them, from RDG's names.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
