# Where the station table comes from

`assets/stations.dat` is built by `build_stations.py` from two sources. Nothing
in `assets/` is hand-edited: it is a build artefact, bundled into the `.fap`
and unpacked to `/ext/apps_assets/flipso/` when the app is installed.

- Station names and National Location Codes from the Office of Rail and Road's
  estimates of station usage (table 1410), © Crown copyright. Public sector
  information licensed under the Open Government Licence v3.0,
  <https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/>.
- Historic codes and fare groups from railwaycodes.org.uk, compiled and
  maintained by Phil Deaves.

**The second source carries no licence grant, so the table is not yet clear to
redistribute.** Resolve that before publishing the repository — see the
"Station names" section of the top-level README.

Only `assets/` is packaged, so this note stays in the repository rather than
being copied onto every user's SD card. The attribution above travels with the
project, which is what the Open Government Licence asks for.
