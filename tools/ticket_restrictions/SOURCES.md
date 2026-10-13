# Where the ticket restriction table comes from

`assets/ticket_restrictions.dat` is built by `build_ticket_restrictions.py`
from `TicketRestrictions_v4.xml`: the Rail Delivery Group's Knowledgebase
Ticket Restrictions data feed, from the Rail Data Marketplace
(https://raildata.org.uk/dataProduct/P-2d177d2e-dcb8-43dc-ac4d-3a967dd35c5e/overview).
It is an API rather than a file, so this is one response from it, saved. Its
licence is the ticket types data's (`tools/ticket_types/SOURCES.md`): the raw
data may be made freely available or otherwise distributed to third parties.
It is kept here so that a build from the repository is the same table on every
machine, and `tools/test/run.sh` rebuilds the table from it and fails if
`assets/` differs.

To take a newer response, save it here in place of this one, point `SOURCE`
in `build_ticket_restrictions.py` at it, and run the builder.

Nothing in `assets/` is hand-edited: it is a build artefact, bundled into the
`.fap` and unpacked to `/ext/apps_assets/flipso/` when the app is installed.
Only `assets/` is packaged, so the XML stays in the repository rather than
being copied onto every user's SD card.
