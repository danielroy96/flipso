# Feature handoffs

Each file here is a self-contained brief for one feature. The first three came
out of the code review of 2026-09-27, the auto-renew brief out of the review of
the TYP 26 toll pass on 2026-10-05, and the rest out of the review of Flipso
against the whole of TS 1000 and RSPS3002 on 2026-10-13 and of RDG's other
published specifications (rspaccreditation.org's public list: RSPS5044,
RSPS5045, RSPS5052, RSPS5080) on 2026-10-14. Several rail data briefs need
files from the Rail Data Marketplace or the National Rail Data Portal, which
need the user's account; each says which. Start a session with:

> Build the feature in `docs/handoff/<file>.md`.

Every brief assumes the working rules in `CLAUDE.md` — in particular:

- Screen text is built in `flipso_format*.c`, never in a scene, and follows the
  house style in `format/flipso_format.h` (`Label: Value`, capitalised values, indented
  details that are themselves labelled, money as `£`). `tools/test/screen_text/`
  enforces it on every screen of every demo card.
- Every decoder change needs a case in `tools/test/`, usually a synthetic
  product in `tools/test/build_card.py`. Run `tools/test/run.sh` (ASan + UBSan).
- Memory is the constraint: no large static tables, and `flipctl mem` before and
  after anything structural. A decoded card allocates 268 bytes a product and
  128 a journey. The `.fap` puts about 95 KB in RAM, and the app leaves about
  26.6 KB free at its scan screen and 17.6 KB with Demo 01's reserved journey
  open (CLAUDE.md, 2026-10-08). Every brief below costs some of that; measure
  `flipctl size` before and after.
- Verify on the device with the demo cards (`tools/demo/build_demo_cards.py`)
  and `flipctl shot`; only ask for a card tap after a green `flipctl arm`.
- Remove the brief from this folder, and its row below, once the feature ships.

The table is in the suggested order: small fixes that make every screen more
trustworthy first, then navigation, then app features, and last the card
types that can only be built from the specification, because no real card of
them has been seen.

| Brief | What it adds | Size |
| --- | --- | --- |
| **Quick wins** | | |
| [clock-check.md](clock-check.md) | Warn when the Flipper's clock is behind the card, since every expiry verdict depends on it | Small |
| [first-page-wrapping.md](first-page-wrapping.md) | Keep each first page's key values above the fold, and measure line widths on the host | Small |
| [bank-cards.md](bank-cards.md) | Recognise contactless bank cards and phone wallets, without reading card data | Small |
| [journey-user-defined.md](journey-user-defined.md) | Show a revision 4 journey record's UserDefined bytes | Small |
| [loyalty-member-number.md](loyalty-member-number.md) | The membership number TS 1000-5 builds for TYP 3 and TYP 17 | Small |
| [operators-file-docs.md](operators-file-docs.md) | Make `operators.txt` discoverable, and say when it was cut short | Small |
| [auto-renew-off.md](auto-renew-off.md) | "Auto-renew: Off", and the renewal quantity whichever way the flag is set | Small |
| **Rail data** | | |
| [rail-profile-fields.md](rail-profile-fields.md) | RSPS3002's remaining rules: rail photocard IDs, ValidityCode's peak levels and test flag, railcards held as TYP 14 entitlements | Medium |
| [rail-route-names.md](rail-route-names.md) | Name a rail ticket's route code from RDG's Fare Route Restrictions data | Medium |
| [seat-properties.md](seat-properties.md) | Name TYP 24 seat features from RSPS5080's published codes, replacing the guessed list | Small-medium |
| [railcard-names.md](railcard-names.md) | Build the railcard names from RDG's Railcards reference data, out of RAM | Small-medium |
| [toc-and-supplement-names.md](toc-and-supplement-names.md) | Name a reserved journey's operator-only TOC code and its supplements | Small-medium |
| [fare-location-names.md](fare-location-names.md) | Passenger-facing names for fare groups ("London Zone R1256") from RDG's Fare Locations data | Small-medium |
| **Navigation** | | |
| [journeys-list.md](journeys-list.md) | Journeys as a list of rows, each opening its own screen | Medium |
| [trips.md](trips.md) | Show a tap in inside the tap out that names it, and pair older records by inference | Medium |
| [spending-summary.md](spending-summary.md) | What a purse has spent and been topped up with, since when | Medium |
| [save-failed-reads.md](save-failed-reads.md) | Save the raw data of a card that would not decode, for bug reports | Medium |
| **App features** | | |
| [settings-privacy.md](settings-privacy.md) | A Settings screen: privacy mode for screenshots, and auto-save after a scan | Medium-large |
| [export-text.md](export-text.md) | Export every screen of a card as a text file on the SD card | Medium |
| [unknown-operators-report.md](unknown-operators-report.md) | A file of unknown operator numbers, with no personal data, for contributors to send | Small-medium |
| **Media decoded from the spec alone** | | |
| [cmd12-check.md](cmd12-check.md) | Check CMD12 against its own file table and geometries (transcribed in the brief) | Small-medium |
| [cmd12-anonymous.md](cmd12-anonymous.md) | Read the issuer, directory and journeys an Anonymous (phone) CMD12 card leaves readable | Medium |
| [cmd11-pim.md](cmd11-pim.md) | Read CMD11 cards, whose products and log answer before authentication | Medium-large |
| [cmd10-counters.md](cmd10-counters.md) | Read a CMD10 card's one-way counters, and flag Retired only on evidence | Small |
