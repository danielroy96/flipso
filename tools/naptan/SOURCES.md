# Where the stop table comes from

`naptan.dat` is built by `build_naptan.py` from NaPTAN, the Department for
Transport's register of public transport access points — every bus stop, tram
stop, ferry berth and station entrance in Great Britain, with the `AtcoCode` and
`NaptanCode` that ITSO cards refer to them by.

- Download page: <https://beta-naptan.dft.gov.uk/download>
- The CSV the builder fetches:
  <https://naptan.api.dft.gov.uk/v1/access-nodes?dataFormat=csv>

© Crown copyright and database right. Public sector information licensed under
the Open Government Licence v3.0,
<https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/>.

Unlike the station table, this one carries no licensing problem: the OGL permits
redistribution with attribution, and the attribution is this file plus the note
in `README.md`.

## What ships, and where it goes

`data/naptan.dat` is the whole register: every active stop, both indexes, with
localities. Nothing in `data/` is hand-edited — it is a build artefact.

It ships ready built so that nobody has to run the builder, but it is **not**
packaged into the `.fap` the way `assets/stations.dat` is. At 21 MB it would be
re-uploaded over USB on every install, which takes upwards of ten minutes and
leaves an unloadable `.fap` behind if it is interrupted. It is copied to the SD
card instead — see `data/README.md`.

Either way the table costs no heap: it stays on the card and is searched in
place. `flipctl size` shows which sections actually reach RAM.

Anyone who would rather not carry the whole country can build one ATCO area
instead, which is a few hundred kilobytes.

## Refreshing it

NaPTAN is revised continuously and republished daily. There is no version field
to check, so a rebuild is the only way to pick up new stops; nothing breaks in
the meantime, since an unknown code shows as a code.

```bash
python3 tools/naptan/build_naptan.py -o data/naptan.dat
```
