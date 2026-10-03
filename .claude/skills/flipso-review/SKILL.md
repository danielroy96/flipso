---
name: flipso-review
description: Review Flipso's code the way the user asks for it - the whole app, or everything on the branch including uncommitted work - for memory misuse, correctness, architecture, screen wording and icons, missing data, and tests, then report numbered findings and fix the ones picked. Use for "full code review", "review the entire app", "review everything on the branch", "review all the changes", "re-review after the fixes", "code review everything you've done".
---

# A Flipso review

Six sessions so far opened with much the same brief: memory first, because
this is manual C on a 190 KB heap; then usability - wording, menus, icons,
jargon; then functionality - is there data on the card Flipso does not show.
This page is that brief, the rulings that came back from those reviews so
they are not raised again, and the report shape the user answers with
"fix 1-4" or "all except 11".

## Scope

Ask nothing; work it out:

| The request says | Review |
| --- | --- |
| "the entire app", "the whole app" | every source in `application.fam`, plus `itso/`, `format/`, `views/` |
| "the branch", "all the changes", "everything you've done" | `git diff main...HEAD` **and** `git diff HEAD` **and** untracked files |
| "re-review after the fixes" | the same scope again, from scratch - not just the fix commits |

The branch scope means *all* of it. On 2026-10-02 a review of only the
session's own commits had to be redone: "Please review all of the changes on
this branch, not just the ones you committed." Uncommitted work in the tree
counts too - check `git status` before deciding what is in scope.

## What to look at

### 1. Memory and correctness - first, and in the most detail

- Every allocation freed on **every** path, the failure branches included:
  `malloc`/`furi_string_alloc`/`*_alloc` against their frees, scene
  `on_exit` against `on_enter`, views freed in `flipso_free()`.
- A storage handle closed even when its open failed (CLAUDE.md, Conventions;
  `tools/test/lint_storage.py` catches only the simplest shape).
- Lengths read off a card bounded before use - an over-read of a data group
  is the commonest decoder bug. Host tests' buffers exactly as long as the
  data claims, or ASan sees nothing (flipso-decoder).
- Integer widths: bit fields widened before shifting, `uint8_t` counters that
  a card can push past 255.
- Nothing large added as a `const` table - it reaches RAM (flipper-memory).

Then measure rather than infer, when the change touches anything that
allocates:

```bash
tools/flipper/flipctl size                                    # the .fap's RAM image, before and after
tools/flipper/flipctl walk <scratch>/to --launch 'right down ok'  # to the demo card list
tools/flipper/flipctl walk <scratch>/leak ok back --repeat 5      # open Demo 01, close it, five times
```

`--repeat` repeats every step it is given, so get to the screen first and
repeat only the open and close. The drift line at the end should read
`+0 bytes (steady)`. The text panel keeps its longest string
until the app exits, so compare cycles against the same screen, never against
a fresh launch (CLAUDE.md, "Memory is the constraint").

### 2. Architecture

- `itso/` and `cards/flipso_capture.c` include no firmware header.
- Screen text is built in `format/flipso_format*.c`, never in a scene.
- New sources are listed by name in `application.fam`.
- One thing in one place: a second copy of a table, a formatter or a lookup is
  a finding even when both copies are right today.

When the user asks for architecture fixes, they want them refactor-only: no
change to any screen. `tools/test/run.sh` passing with `test_format.c`
unchanged is the evidence.

### 3. Screens - wording, pages and icons

Read every screen the change reaches, on the host and on the device:

```bash
tools/test/screens.py "assets/demo/Demo 01 The Key Kent.flipso"     # every page, as text
tools/flipper/flipctl walk <scratch>/p --launch 'right down ok' ok ok right --until-same
```

The walk opens Demo 01, its Summary, and pages Right to the end, one contact
sheet with every page on it; vary the rows to reach the changed screen.

Check against CLAUDE.md's conventions: `Label: Value` with the value
capitalised, a detail indented two spaces and labelled, money as `£`; the
first page answers whether the ticket is good, for where, until when and
with what; Technical last; a new line on the page that answers its question.
Every page title has an icon that says what the page holds.

Then read it as a passenger would: abbreviations and acronyms that are not
everyday English, jargon from the spec (`IPE`, `TYP`, `ISAM` belong on
Technical), a raw number with no unit or name, "1 days". Give each wording
finding as `before -> after`.

### 4. Functionality

Data the card holds that no screen shows: compare the decoded structs with
the TS 1000 layout (`tools/spec/itso_spec.py grep FIELD --part 5`; rail
fields also `--part rsps3002`). New features found this way are proposals,
not fixes: the user has twice asked for them as handoff docs in
`docs/handoff/` (see its README) to build in a later session.

### 5. Tests and hygiene

- Every decoder change has a case in `tools/test/` (CLAUDE.md, Conventions).
- `tools/test/run.sh` and `ufbt lint` pass.
- `assets/demo/` regenerated by the builder, not edited.
- No card number, holder name or route in code, comments, tests or commit
  messages.

## Settled - do not raise again

These were decided in earlier reviews. Raising them again costs the user a
reply each time.

- **Show what the card holds, defaults included.** "Class: Standard",
  "Off-peak only: No" stay. Hide only what the card does not carry
  (2026-09-27).
- **Wording kept on purpose:** "Not an ITSO card"; "Off card" for products
  and journeys from past reads; "Companion: travels at the same rate" (a
  specific fare scheme); the EN1545 renderings "Senator", "Scholar", "Owned
  haulage", "Warrant", "Private product" (2026-09-27).
- **Pages:** no wrapping at either end, no page counter, no vibration at the
  last page - the missing arrow is the indicator. The operator is on page 1
  of every product. Technical is always last (2026-10-03).
- **The railcard a ticket needs is page-1 information** on every ticket
  type; its percentage and code type stay on Technical (2026-10-02).
- **Known log noise** (`ViewPort lockup`, `Incorrect BacklightEnforce use`)
  is not a finding - CLAUDE.md, "Known noise".

When the user settles something new in reply to a review, add it here.

## The report

Numbered once across the whole report, so a reply can pick them by number,
grouped and most severe first:

1. **Bugs and correctness** - wrong output, crashes, leaks, over-reads
2. **Memory** - waste, fragmentation risk, anything measured
3. **Architecture and structure**
4. **Usability** - wording (`before -> after`), navigation, icons
5. **Missing functionality and data** - each with a size estimate
6. **Tests**

Each finding: `file:line`, what is wrong, what you would do. Put measured
numbers in, with the state they came from. Say what you checked and found
clean, briefly - a review that only lists problems leaves the reader unsure
what was looked at.

Fix nothing unless the request said to fix ("review and fix", "fix what you
find"). Then fix in the same numbered order, re-run `tools/test/run.sh` and
`ufbt lint`, deploy, and re-walk every screen the fixes touched before
reporting back, with the finding numbers each change answers.
