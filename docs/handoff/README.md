# Feature handoffs

Each file here is a self-contained brief for one feature suggested in the
code review of 2026-09-27. Start a session with:

> Build the feature in `docs/handoff/<file>.md`.

Every brief assumes the working rules in `CLAUDE.md` — in particular:

- Screen text is built in `flipso_format*.c`, never in a scene, and follows the
  house style in `flipso_format.h` (`Label: Value`, capitalised values, indented
  details that are themselves labelled, money as `£`). `tools/test/test_format.c`
  enforces it on every screen of every demo card.
- Every decoder change needs a case in `tools/test/`, usually a synthetic
  product in `tools/test/build_card.py`. Run `tools/test/run.sh` (ASan + UBSan).
- Memory is the constraint: no large static tables, and `flipctl mem` before and
  after anything structural. `ItsoCard` is already ~15 KB.
- Verify on the device with the demo cards (`tools/demo/build_demo_cards.py`)
  and `flipctl shot`; only ask for a card tap after a green `flipctl arm`.
- Remove the brief from this folder, and its row below, once the feature ships.

| Brief | What it adds | Size |
| --- | --- | --- |
| [transaction-amounts.md](transaction-amounts.md) | The amount of each transaction in a balance history, worked out from consecutive balances | Small |
| [days-remaining.md](days-remaining.md) | "Days left" on expiries, and the current pass in the Summary | Small |
| [paper-ticket-summary.md](paper-ticket-summary.md) | A Summary for paper tickets that says when and where it was last used | Small |
| [operators-file-docs.md](operators-file-docs.md) | Make `operators.txt` discoverable, and say when it was cut short | Small |
| [save-as-new-copy.md](save-as-new-copy.md) | Keep a snapshot of a card instead of updating its record | Medium |
| [unsaved-card-warning.md](unsaved-card-warning.md) | Don't let Back silently throw away a card that was just read | Small |
| [save-failed-reads.md](save-failed-reads.md) | Save the raw data of a card that would not decode, for bug reports | Medium |
| [cmd9-cmd10-media.md](cmd9-cmd10-media.md) | Read ITSO cards on NTAG and Ultralight EV1 tags with a full shell | Large |
