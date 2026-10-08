# Where the ticket type table comes from

`assets/ticket_types.dat` is built by `build_ticket_types.py` from
`TicketTypesRefData_v1.2.xml`, the Rail Delivery Group's ticket types reference
data, published free on the Rail Data Marketplace. Its terms allow the raw data
to be made freely available or otherwise distributed to third parties, which
is why the file is kept here: the marketplace needs an account, and a build
from the copy in the repository is the same table on every machine.
`tools/test/run.sh` rebuilds the table from it and fails if `assets/` differs.

To take a newer release, put its XML here in place of this one, point `SOURCE`
in `build_ticket_types.py` at it, and run the builder.

Nothing in `assets/` is hand-edited: it is a build artefact, bundled into the
`.fap` and unpacked to `/ext/apps_assets/flipso/` when the app is installed.
Only `assets/` is packaged, so the XML stays in the repository rather than
being copied onto every user's SD card.
