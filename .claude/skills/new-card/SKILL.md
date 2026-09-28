---
name: new-card
description: Take a card Flipso has not seen before from the reader to a commit - scan it, find its operator number, add the operator name and card brand to the table, check every ticket screen decodes correctly, turn any data encoding no demo card has into a synthetic demo card, deploy, review and commit. Use for "scan this card and add the operator", "add the brand X", "this card comes up as ITSO card", "Operator: Unknown (NNN)", "I've left a new card on the Flipper", "we've got another card to test", "make a demo card from this card".
---

# A new card, end to end

The usual request names an operator and a brand ("GWR Touch", "Great Western
Railway") and leaves the card on the Flipper. What it actually needs is:

1. read the card and get its bytes onto this machine
2. find the operator number (OID) the brand belongs to
3. add `{oid, "Name", "Brand"}` to the table, with its provenance and a test
4. check every screen of the card, because a new card is a new test case
5. turn any data shape no demo card has into a synthetic demo card
6. deploy, and see the brand in the title bar on the device
7. review the change, then commit it

Most of the time goes on steps 1, 4 and 5. The tools below exist so that neither
needs a one-off script. Read **flipper-hardware** before touching the device;
this page only covers what is specific to a new card.

| Step | Command |
| --- | --- |
| Is the device usable | `tools/flipper/flipctl doctor` |
| Read a card already on the reader | `tools/flipper/flipctl scan --shot <scratch>/scan.png` |
| Read a card someone will tap | `tools/flipper/flipctl arm --shot <scratch>/armed.png`, then ask |
| Get the bytes off the device | save in the app, then `flipctl pull` (below) |
| What the decoder made of them | `tools/test/replay.py card.flipso` |
| Every screen, the real station names, every OID | `tools/test/screens.py card.flipso` |
| What the spec says | `tools/spec/itso_spec.py grep 'PATTERN' [--part N] [-C N]` |
| What this card has that no demo card does | `tools/demo/new_encodings.py card.flipso` |

Keep everything pulled off the device in the scratchpad, never the repo. A
saved card holds the card number, and a named card the holder's name.

## 1. Read it

If the card is lying on the reader, which is how it is usually left:

```bash
tools/flipper/flipctl scan --shot <scratch>/scan.png; echo "SCAN=$?"
```

`scan` relaunches the app, presses Scan down the log stream's own session,
and follows the read to the end. The exit status is the verdict:

| Exit | Meaning | Do |
| --- | --- | --- |
| 0 | a card was read; it prints what the app logged | Read the screenshot |
| 1 | the field never came up | `flipctl shot`, then see flipper-hardware |
| 2 | the device is halted | ask the user for the screen text and a LEFT+BACK reset |
| 3 | the field polled and no card answered | the card is off-centre: ask the user to move it |

The log it prints includes `Shell owner: OID 287 (unknown)`. That line
already answers step 2 for a DESFire or CMD2 card. A paper ticket (CMD4) has
the generic shell OID 8189 instead; see step 2.

If the user is going to tap instead, use `arm`, and only ask once it has
exited 0 (see flipper-hardware). `arm` exits **3** when a card was already on
the reader: the card has been read, the field is off, and a tap now does
nothing. That case is `scan`'s job.

A read that fails is a decoder job: follow **flipso-decoder**, starting with
the log lines `scan` printed.

## 2. Get the bytes, and find the OID

Save the card in the app, then pull the file. The card menu's last row is
**Save card**, and Up from the top wraps to it. Look at a screenshot before
every key press: keys go to whatever is on screen.

```bash
tools/flipper/flipctl shot <scratch>/menu.png up       # expect "Save card" highlighted
tools/flipper/flipctl shot <scratch>/name.png ok @1    # "Name this card", suggested name
tools/flipper/flipctl shot <scratch>/saved.png ok @1.5 # saves it
tools/flipper/flipctl ls /ext/apps_data/flipso/cards
tools/flipper/flipctl pull "/ext/apps_data/flipso/cards/ITSO Card 1234.flipso" <scratch>/card.flipso
```

If the card was saved before, the second screen offers to update the record
instead of naming it. Right replaces the record, and only with the user's
say-so, because it is their file.

The suggested name is the brand plus the card number's last four digits (a
paper ticket's comes from its chip, since every one has the same number).
Saved before the brand exists, it is "ITSO Card NNNN", and it keeps that name. Tell the user
rather than renaming their file.

Then, on this machine:

```bash
tools/test/replay.py <scratch>/card.flipso     # the decoder's own view, under ASan/UBSan
tools/test/screens.py <scratch>/card.flipso    # the screens, then "Operators on this card"
```

The operators table at the end of `screens.py` lists every OID on the card,
where it came from and whether the table names it. For the new entry, the row
that matters is:

- **shell owner**: the OID the brand belongs to, on DESFire (CMD7) and CMD2.
- **card issuer (titles the menu)**: shown only when it differs from the
  shell owner, which is a compact-shell paper ticket (CMD4). There the brand
  belongs to the product owner, not the generic 8189.

A card commonly carries products owned by someone else. The rail season
tickets seen so far belong to OID 246, SEFT Central Products, whoever issued
the card. The
brand never comes from a product owner on a full shell (docs/PROTOCOL.md,
"Operator names").

## 3. Add the entry

`itso/itso_operators.c`, table `itso_operator_table`:

- **Keep it sorted by OID.** Lookup is a binary search, so an entry out of
  order silently disappears.
- **Write a provenance comment**, as every other entry has: which card it was
  read from, the date, what else on the card it owns, and that the brand is
  as printed on the card. If the brand is the user's word rather than
  something printed, say that.
- **Keep personal data out of comments and the commit.** That means no card
  number, holder name or route. "Shell owner of a GWR Touch card, read
  2026-09-28" is enough.
- **Name and brand are different things.** The name is the operator ("Great
  Western Railway"); the brand is the card as sold ("GWR Touch"). NULL brand
  for an operator whose card has not been seen.
- **The brand is the title bar.** Check it fits on the device screenshot in
  step 5. The menu elides what does not fit, so an elided brand is a sign to
  shorten it, as "Chiltern Smartcard" was.

Then `tools/test/test_operators.c`: add the OID to `known[]`, which checks the
table is still sorted, and a `same()` pair for the name and the brand.

Only add the OIDs the user asked for. Other unknown OIDs on the card go in
the report, with what the operators table says about them, not into the table
on a guess. A wrong name is worse than a number (see the header comment in
`itso_operators.c`).

## 4. Check the ticket details

A new card is the best test case there is. Read every screen `screens.py`
printed, and check that the values agree with each other, not just that they
decode:

- **Dates.** Consecutive season tickets should run back to back: one ends
  the day before the next starts. The last tap should fall inside a ticket
  that was valid that day. `--now YYYY-MM-DD` renders the card as of another
  day.
- **Prices.** UK rail seasons are fixed multiples of the weekly price:
  monthly = weekly × 3.84, annual = weekly × 40. So a monthly should be about
  annual ÷ 10.4. A price that fails this is a sign of a misread amount.
- **Places.** Station names come from the real table. A code shown instead of
  a name is a gap in the table, or a misread location. Taps should fit the
  tickets' stations.
- **OIDs.** `screens.py` marks any number in a TS 1000-2 table B2 gap as
  "NOT A VALID OID". That means the field is not holding an OID, whatever the
  layout says: a misread offset, or a scheme reusing the field. Check the
  layout with `itso_spec.py` before calling it either.
- **Wording.** Anything a person would read as wrong counts: "1 days", a
  doubled label, an unexplained raw number. `test_format.c` holds screens to
  the house style, but it only knows the demo cards.

For each thing that looks wrong, look up the field before changing anything:

```bash
tools/spec/itso_spec.py grep 'ProductRetailer' --part 5
tools/spec/itso_spec.py grep 'Table B2' --part 2 -C 25
tools/spec/itso_spec.py page 5 51
```

Offsets in the TS 1000-5 tables are absolute from the start of the data
group; see flipso-decoder for that and the other traps. A real decode bug
follows flipso-decoder's order: a failing case in `tools/test/` first, then
the fix. A wording bug gets a `check()` in `test_format.c`.

Report what you checked and found. For anything left as it is, say why:
"decodes as the spec lays it out, but the value is outside every OID range"
is a finding, not a fix.

## 5. Make a demo card of anything new

Every data encoding Flipso meets on a real card should also be on a synthetic
demo card. A demo card is card bytes, so it keeps that shape under test after
the real card is back in someone's wallet. It also lets anyone see the screens
it reaches without owning the card.

```bash
tools/demo/new_encodings.py <scratch>/card.flipso
```

This lists the structures and the screen lines on the card that no demo card
has: geometry, each product's type, revision, bitmap and value records, each
journey record's revision and groups, the Directory InstanceID. If it says
"Nothing new", this step is done. Otherwise, build what it lists into
`tools/demo/build_demo_cards.py`, following `tools/demo/README.md` ("Adding
one"):

- **Where it goes.** Extend an existing card when the new shape fits its
  story. Add a card (`card_<name>()`, appended to `CARDS`, named "Demo N ...")
  when it is a different kind of card. `card_gwr_touch()` is the worked
  example: it came from this skill's first run.
- **Shapes from the real card, values invented.** Decode the real bytes field
  by field against the parser and the spec. Then write the same fields,
  revisions and bitmaps with an invented serial, dates, prices, ISAMs and
  **stations**, so the holder's route is not copied into the repo. Real
  operator numbers are fine; the demos use them.
- **Builders.** Use `tools/test/itso_build.py`, shared with the host tests.
  If a builder cannot express the new shape, extend it with an optional
  argument rather than a copy, as `tt_record_rev4()` gained optional groups
  for the GWR card's records. Existing callers must build the same bytes.
- **Compare with the real card.** Run `screens.py` on both, and diff the
  screens the new shape reaches: they should differ in values only. Every
  difference in which lines appear is a field you missed. On the GWR card
  that caught an unset concession class and an ID that should have been
  unused.
- **Pin it.** Add a `demo_<n>()` in `tools/test/test_format.c` checking the
  lines only the new shape produces, as `demo_seven()` does. Bump the card
  count there (`cards == N`), because every demo card is also held to the
  house style on every screen.
- **Close the loop.** `new_encodings.py` on the real card should now say
  "Nothing new". Add a row for the card to the table in `tools/demo/README.md`.

To see it on the device, push the file and open it from Saved (it needs no
card on the reader):

```bash
tools/demo/build_demo_cards.py <scratch>/demo
tools/flipper/flipctl push "<scratch>/demo/Demo N Name.flipso" "/ext/apps_data/flipso/cards/Demo N Name.flipso"
```

## 6. Build, deploy, and see it

```bash
tools/test/run.sh                  # everything, under ASan/UBSan
ufbt format && ufbt lint
tools/flipper/flipctl deploy       # run in the background; a reboot makes it slow
tools/flipper/flipctl scan --shot <scratch>/after.png; echo "SCAN=$?"
```

The screenshot should show the brand in the title bar. Then walk a few
screens on the device, with a screenshot before each key press. At least
check the card screen's issuer and one product, since station names on the
device come from the SD-card table rather than the host copy.

## 7. Review and commit

Review the diff with `/code-review`, or read every hunk yourself, fix what it
finds, and re-run `run.sh` and `ufbt lint`. Then:

```bash
git status --short     # only source files: no .flipso, dump.txt or replay_data.h
git add <the files you changed>
git commit             # "Name <Operator> and title its <Brand> cards"
```

The subject line follows the history: `git log --oneline -- itso/itso_operators.c`.
The body says where the OID was read, anything the check in step 4 found and
fixed, and what the new demo card covers. The operator entry and the demo card
can be one commit or two; two reads better when the demo card needed builder
changes. Commit to main only when the user asked for that; do not push
unless asked.

## Things that have caught this out before

- **A card left on the reader beats `arm`.** The poller logs one idle timeout
  before it finds the card, and the read then finishes in under a second. A
  log stream opened after the key press misses all of it. `scan`, `arm` and
  `log --keys` now send the last key down the stream's own session, and `arm`
  waits 1.5 s past the first timeout before saying ARMED. Do not work around
  that with a separate `keys ok` and then a `log`.
- **An app left running from an earlier session may not relaunch.** On
  2026-09-28 the first `arm` of the session never reached `UI ready`, the
  recovery reboot was sent but never acted on, and the CLI then answered a
  keystroke but ran no command. The cause was not found. `flipctl` now says so ("never restarted", "the
  CLI accepted ... and printed nothing back") instead of misreporting the
  desktop. The only way out is the user's LEFT+BACK reset, so ask for it with
  the screen text.
- **A screenshot you have not looked at does not license a key.**
  `flipctl shot x.png ok` sends the key *then* captures. Capture, Read, then
  press.
- **Rail season tickets have their own owner.** On the Southeastern, Greater
  Anglia and GWR cards read so far it was 246, the shared SEFT OID, not the
  train operator. Do not name or brand a train operator from a ticket's owner.
- **A number in a B2 gap is not an operator.** `Sold by: Unknown (40111)` on
  the GWR card is a ProductRetailer read exactly where the spec puts it,
  holding a value no OID can have. It was reported, not "fixed".
