# Feature handoffs

Each file here is a self-contained brief for one feature. The first four came
out of the code review of 2026-09-27. The TYP brief came out of the review
of Flipso against TS 1000-5 on 2026-09-29: that IPE type is still reported
from its directory entry and value record alone. (The TYP 24 and TYP 25
briefs from the same review have shipped.) Start a session
with:

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
  128 a journey, and the app leaves about 31 KB free at its scan screen.
- Verify on the device with the demo cards (`tools/demo/build_demo_cards.py`)
  and `flipctl shot`; only ask for a card tap after a green `flipctl arm`.
- Remove the brief from this folder, and its row below, once the feature ships.

| Brief | What it adds | Size |
| --- | --- | --- |
| [days-remaining.md](days-remaining.md) | "Days left" on expiries, and the current pass in the Summary | Small |
| [operators-file-docs.md](operators-file-docs.md) | Make `operators.txt` discoverable, and say when it was cut short | Small |
| [save-failed-reads.md](save-failed-reads.md) | Save the raw data of a card that would not decode, for bug reports | Medium |
| [typ26-tolling.md](typ26-tolling.md) | Decode the TYP 26 toll pass's dataset: vehicle class, dates, passback | Small |
