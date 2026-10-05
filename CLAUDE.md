# Working on Flipso

Flipso is a Flipper Zero app that reads UK ITSO public-transport smartcards.
It is C against the Flipper firmware SDK, built with `ufbt`.

`docs/PROTOCOL.md` is the protocol reference: how the card is read, which parts
of the ITSO spec each field comes from, and where the bundled station and
operator data comes from. Read it before changing the decoder or the screens.
`README.md` is the introduction — what the app is, what each screen shows, and
the contributing conventions. This file is about *working on* the project — the
loop, the hardware, and the things that have wasted time before.

## The loop

| What | Command |
| --- | --- |
| Decoder tests on this machine | `tools/test/run.sh` (one line; `-v` for all of it) |
| Compile only | `tools/flipper/flipctl deploy --build-only` |
| Check / fix formatting | `ufbt lint` / `ufbt format` |
| Build, install and launch on the Flipper | `tools/flipper/flipctl deploy` |
| Check the environment and the device | `tools/flipper/flipctl doctor` |
| Prove a card can be tapped right now | `tools/flipper/flipctl arm` |
| Read a card left lying on the reader | `tools/flipper/flipctl scan` |
| Walk the UI: keys, a frame and the heap per step, one contact sheet | `tools/flipper/flipctl walk OUT ok right --until-same` |
| Retake the README's screenshots | `flipctl walk docs/screenshots --steps-file docs/screenshots/walk.txt ...` (see the file) |
| Every screen of a saved card, on this machine | `tools/test/screens.py card.flipso` |
| Search the ITSO spec, or rail's RSPS3002 | `tools/spec/itso_spec.py grep PATTERN [--part rsps3002]` |
| Check HEAD against the Apps Catalog | `tools/catalog/validate.sh` |
| Refresh the IDE's index of the sources | `tools/ide/compdb.py` |

Slash commands wrap the common ones: `/deploy`, `/drive`, `/watch`, `/mem`,
`/test`, `/dump`, `/doctor`. Skills carry the detail: **flipper-hardware** for
anything involving the device, **flipso-decoder** for card data and the ITSO
spec, **flipper-memory** for heap work and crashes, **new-card** for taking a
card Flipso has not seen from the reader to a committed operator entry,
**flipso-review** for a full review of the app or the branch.

Prefer the host tests. The decoder (`itso/`) builds on macOS under ASan and
UBSan, so a hypothesis about card bytes can be tested in about a second.
Reflashing and re-tapping a card takes a minute and needs the user holding the
card, so it is for confirming a fix, not for finding one.

## Hardware, in one paragraph

The Flipper has one USB serial port and exactly one process may hold it. `ufbt
launch`, the log stream and every screenshot all want it, so the failure mode is
a silent hang, not an error. `tools/flipper/flipctl` owns the port: it clears
stale holders, closes the running app before installing, reboots the device
when the loader wedges, and turns each known hang into a message. Use it
rather than raw `ufbt launch` or a one-off pyserial script — every one of those
behaviours is there because a past session lost time to it.

## Layout

```
flipso.c/.h           app entry and lifecycle, the icon table
itso/                 the decoder: pure C, no firmware dependency, host-testable.
                      itso.h is the one header the app includes; its comment
                      maps the parts:
  media/                the card as storage: itso_shell.c, itso_directory.c (the
                        directory and sector chains), itso_type2.c (CMD9/CMD10),
                        itso_cmd4.c (paper tickets)
  ipe/                  what a product holds: itso_ipe.c (shared elements and the
                        table of types), one itso_ipe_<kind>.c per TYP family,
                        itso_value.c (value records), itso_capping.c,
                        itso_space_saving.c
  itso_log.c, itso_card.c, itso_location.c, itso_names.c
  names/                the long name tables and the operator table: host-only.
                        The device reads them from assets/names.dat, which
                        tools/names/build_names.sh builds; run.sh fails when it
                        is stale
format/               the text of every screen, one file per screen
                      (flipso_format_<screen>.c) and the line builders they share
                      (flipso_format_lines.c, flipso_format_i.h); host-tested by
                      tools/test/screen_text/
  product/              the product, Pay as you go and ID screens: the pages each
                        kind has (flipso_product_pages.c), the common lines
                        (details), Technical, and one flipso_product_<kind>.c per
                        kind of product
reader/               getting a card off the reader:
                        flipso_reader.c        which transport runs, the pollers
                                               that run it, the lifecycle
                        flipso_transport.c     what the transports share: the shell
                                               owner logged, the walk of products and
                                               log for the ones that read sector chains
                        flipso_desfire.c       DESFire transport (CMD7/CMD12)
                        flipso_desfire_media.c what a non-ITSO DESFire says about
                                               itself (incl. Oyster)
                        flipso_scan_session.c  which transport next, retries and the
                                               verdict; pure C, host-tested by
                                               test_scan_session.c
                        flipso_cmd2.c          ISO 7816 transport for CMD2 media
                        flipso_type2.c         NFC Type 2 tag transport: CMD4 (SPT paper
                                               tickets), and CMD9/CMD10 (a full shell on
                                               an NTAG or Ultralight EV1)
                        flipso_media.c         the model of what a DESFire says about
                                               itself; the text is format/'s
cards/                flipso_capture*.c: the raw blocks a read produced (the store,
                      decode, merging an earlier read, the file format); saved cards
                      decode from these. flipso_saved.c: those blocks on the SD card -
                      write, read, browse, match, rename, delete; beside it the demo
                      cards (flipso_saved_demos.c), power-cut recovery
                      (flipso_saved_recover.c) and the rule a saved card's name
                      follows (flipso_name_validator.c)
lookup/               flipso_names.c the long name tables from assets/names.dat,
                      and flipso_names_itso.c the device's itso_ functions over it;
                      flipso_operators.c operator id -> name, built-in table plus the
                      user's file; flipso_stations.c NLC -> station name, binary search
                      over the SD card table; flipso_naptan.c NaptanCode/AtcoCode ->
                      bus stop name, same design
scenes/               one file per scene; every paged text screen is the one
                      text scene (flipso_open_text()); list in flipso_scene_config.h
views/                custom views (the icon list, the text panel, the scan screen),
                      and the hand-drawn £ and € the fonts lack
tools/flipper/        flipctl: the device driver described above
tools/ide/            compile_commands.json, so CLion and clangd index the tree
tools/test/           host test suites (run.sh), synthetic card builder, card replay,
                      screens.py (every screen of a saved card). The larger suites
                      are directories - parse/, screen_text/, capture/, saved/ - of topic
                      files sharing test.h's check(); sources.py lists the app files
                      each host build compiles
tools/spec/           itso_spec.py: fetch and search the TS 1000 parts
tools/debug/          opt-in card-dump instrumentation
tools/catalog/        the Apps Catalog manifest, and validate.sh to run the
                      catalog's own bundler over HEAD
tools/demo/           the builder for the synthetic demo cards the About menu opens;
                      new_encodings.py says what a real card has that they lack
assets/demo/          those demo cards, generated - rerun the builder, never edit;
                      run.sh fails when they are stale
tools/stations/       station table builder and its data provenance
tools/names/          names.dat builder: runs the itso/names/ tables into the asset
data/                 reference data shipped but not packaged; see its README
tools/naptan/         stop table builder; data/naptan.dat is its output
tools/icons/          pixel art the images/ icons are generated from
```

`itso/` must stay free of firmware headers. That is what lets `tools/test/run.sh`
and `tools/test/replay.py` build it on the host, which is the fast loop.
`cards/flipso_capture*.c` are held to the same rule for the same reason: the save
and load path, including the file parser, is tested on the host by
`tools/test/capture/`.

A saved card is the raw blocks, not the decoded fields, so loading one runs the
live decoder over them - which means a saved card is also a test case.
`tools/test/replay.py` reads a `.flipso` file as happily as a debug dump, so the
fastest way to get a real card into the host loop is now to save it in the app
and pull the file:

    tools/flipper/flipctl pull /ext/apps_data/flipso/cards/NAME.flipso card.flipso
    tools/test/replay.py card.flipso

That needs no instrumentation and no rebuild, so reach for it before wiring up
`tools/debug/flipso_dump.c` - which still earns its place for reads that fail
before there is a card worth saving.

## Memory is the constraint

The Flipper has a 190 KB heap and the whole `.fap` is loaded into it before
`main()` runs. `tools/flipper/flipctl size` shows which sections reach RAM:
88.9 KB of the 294 KB file as of 2026-10-05, down from 91.0 KB when the long
name tables moved to `assets/names.dat` (`itso/names/`) - 1.8 KB of that came with
splitting the sources a responsibility to a file (a call between files is not
inlined, and a string used in several files is stored once in each), 2.2 KB
with the paged screens and the title icons, 10.4 KB with the TYP 24 decoder, its screen and the rail
railcard and seat tables, 76 KB before - because the 79 KB station table and the 26 KB of demo cards live in
`.fapassets`, which the firmware unpacks to the SD card and never maps.
Anything added as a `const` array *does* reach RAM, and so does every string
literal - which is why the long name tables are an asset. With the app at its
idle scan screen 43.5 KB of the heap is free (measured 2026-10-05; 30.9 KB the
day before). Most of that came from the reader: `nfc_alloc()` allocates the
NFC worker's 8 KB stack straight away, not when the thread starts, so the
reader now allocates the NFC stack, and each transport its buffers, only while
a scan runs. A scan takes about 15 KB back while it does, 4 KB of it the
firmware scanner's own thread. A card on
screen costs what it holds: `ItsoCard` allocates its products (268 bytes each
on the device since 2026-10-04; 672 before, when every product carried every
type's fields, eight value records and its locations as display text), each
product's value records (20 bytes each - two on most products, none on an ID,
up to eight on a saved card that remembers more than the card keeps) and
journeys (128 bytes each, 204 before) to fit rather than keeping room for
twenty and twelve, which held 15 KB whatever the card and left only 25 KB
free. A location is kept as the card's bytes and rendered as it is drawn
(`itso_location_text()`), and a paper ticket's Space Saving record and a
non-ITSO DESFire's file list are allocated only for the cards that have them.
A TYP 24's screen decodes the rest of its dataset and its reservations as it
is drawn, about 750 bytes for Demo 01's two legs, once for the whole screen
and freed before the text is shown. With it open, 22.1 KB is free (measured
2026-10-04). In that boot the build before the struct review of 2026-10-04
left 29.4 KB at idle and 18.0 KB with it open, so the review gave back 1.5 KB
at idle and 4.1 KB with a card on screen.

A screen's text is handed to the text panel rather than copied
(`flipso_text_view_take_text()`), so it is never held twice, and the panel
gives the buffer back when the screen closes; the long name tables' file is
open only while a screen is being built (`flipso_names_release()`). So a
screen costs nothing once it is closed - but still compare like with like:
measure a leak as cycles against the same screen, not against a fresh launch.
The firmware's file browser takes 7.4 KB while the saved-card list is open,
and a screenshot or push borrows about 12 KB for its RPC session, which takes
the low-water mark down to 8.4 KB on a screenshot-heavy check. Read the free
heap with the app up and compare within one boot: the desktop's own idle
figure moves by 15 KB between boots and gives it back when an app opens, so a
difference against it is not the app's cost.

So: no large static tables, no growing a scene's buffers without checking, and
`tools/flipper/flipctl mem` before and after anything structural.

The NaPTAN stop table is about 20 MB and costs no heap either, but it is kept
out of `assets/` all the same: anything packaged is re-uploaded on every install,
and 21 MB over USB takes upwards of ten minutes and leaves an unloadable `.fap`
if it is interrupted. It ships in `data/` and is copied to the card - see
`data/README.md`.

## Conventions

- C is formatted by the firmware's clang-format style (`.clang-format`, which
  ufbt installs). CI runs `ufbt lint` on every push; `ufbt format` fixes it.
  Generated headers opt out with `/* clang-format off */`.
- Comments explain *why*, and cite the spec clause when a constant comes from
  one (`TS 1000-2 table 11`). Do not narrate what the code already says.
- New sources must be listed explicitly in `application.fam`. A bare `*.c` is
  matched recursively and would pull in the host-side tests under `tools/`;
  `dir/*.c` takes no subdirectory of `dir/`, so a new one needs its own line -
  and it takes *any* directory called `dir`, `tools/` included, so no tools
  directory may share a name with an app directory (a `tools/test/format/`
  once put the screen tests into the `.fap`). The host builds take their sources from `tools/test/sources.py`, so a
  new subdirectory goes there too.
- One responsibility to a file. The decoder has a file per medium and per IPE
  type family, the screen text a file per screen and per kind of product, the
  reader a file per transport, and the tests a file per topic. A file growing
  past a few hundred lines is usually two.
- Every text screen is a set of pages, turned with Left and Right, each opening
  with its title (`flipso_cat_page()`), drawn as the icon list's header - icon
  and text centred over a rule - and every title has an icon that says what
  the page holds; draw a new one in `tools/icons/build_icons.py` rather than
  borrow one that does not fit. The first page answers for where
  the ticket is good, until when, whether it still is and with what, and Technical is
  always last. A new line goes on the page that answers the question it
  answers, never on a page of its own. `tools/test/screen_text/` pins each kind's page
  order and checks every screen's pages are titled, non-empty and end with
  Technical.
- Screen text is built in `format/`, never in a scene, and follows the
  house style its header sets out: `Label: Value` with the value capitalised, a
  detail indented two spaces and itself labelled, money as `£`. `tools/test/screen_text/`
  holds every screen of every demo card to that, so a line that breaks it fails
  the host tests.
- Every decoder change needs a case in `tools/test/` — usually a new synthetic
  product in `tools/test/build_card.py`, checked in the `tools/test/parse/` file
  for its topic. The suite runs under ASan and UBSan;
  make the test buffer exactly as long as the data claims to be, or an over-read
  lands inside an oversized array and the sanitiser sees nothing.
- Icons in `images/` are generated by `tools/icons/build_icons.py`; edit the
  generator, not the PNGs.
- No `FURI_LOG_D` or `FURI_LOG_T` in the app sources: the Apps Catalog sends
  back an app that ships its development logging, and a debug line about card
  bytes puts the card number in the log. Add one locally to chase a read and
  take it out before committing; `tools/test/lint_logs.py`, run by `run.sh`,
  fails on any left behind.
- **Close a storage handle even when its open failed** — `storage_file_close()`,
  `storage_dir_close()` or `file_stream_close()` on the failure branch too. The
  storage service registers the path before trying it, and only a close takes it
  off; the registration outlives the app, and the next launch's open of the
  same path waits for ever. `tools/test/lint_storage.py`, run by `run.sh`,
  catches the `if(!open(...)) { return; }` shape.

## Known noise, already investigated

- `ViewPort lockup` and `Incorrect BacklightEnforce use` fire on **every scan**.
  They come from the scan path, not from the detail scenes, and predate the
  current code. Not a symptom of whatever you just changed.
- **Never ask the user to tap a card until `tools/flipper/flipctl arm` has
  exited 0.** It relaunches the app, presses Scan, and waits for the NFC
  poller's own log line — the firmware logs `[D][Nfc] FWT Timeout` every 100 ms
  while the field is up and nothing at all while it is down, so that line is a
  measurement that the reader is live rather than an inference that it ought to
  be. Check the exit status, and Read the `--shot` screenshot. `doctor`,
  `ready`, a heap figure and a successful deploy are all worthless here: each of
  them passes in states where a tap does nothing.
- **"The desktop is showing but the loader says Flipso is running" was a Flipso
  bug**, found on 2026-09-26, not device flakiness. `flipso_stations_try()` and
  `flipso_naptan_try()` did not close the file after a failed open of the
  optional `/data` override tables, which leaves the path registered with the
  storage service after the app exits. The *next* launch then blocked for ever
  in `storage_file_open()` of that same missing file: thread alive, heap held,
  desktop on screen, every key press going to the desktop, and `loader close`
  answering "has to be closed manually" because the exit handler only exists
  once `view_dispatcher_run()` is reached. It only ever bit the second launch
  after a boot, which is why a reboot always "fixed" it and why so many
  launches looked flaky. The lesson generalises: if a launch does not reach the
  screen, find where startup blocks — `flipctl close; flipctl log --launch
  --all` streams the log from the instant the app starts — rather than
  rebooting and moving on.
- A **launch is only trusted when the app logs `UI ready`**, which
  `flipso_app()` does just before `view_dispatcher_run()`. `ready`, `arm` and
  `deploy` all launch that way now and exit non-zero, after one reboot and
  retry, when the line does not come. Do not remove that log line.
- `flipctl ready` is still **not** the pre-tap check. It proves the app's UI is
  up, but the scan scene starts idle by design — the field only comes up on OK
  — so a healthy `ready` leaves "Ready to read a card" above a reader that is
  switched off. Only `arm` proves the field.
- **Never send keys without a screenshot showing Flipso first.** Keys go to
  whatever owns the screen, and at the desktop they open menus and other apps.
  After any launch, `flipctl shot` and look before `flipctl keys`. `flipctl
  walk` captures frame 00 before its first key and refuses outright when no
  app is running, but read frame 00 on its sheet all the same: the loader
  names Flipso in states where the desktop has the screen.
- **Every reboot is visible on the desk and looks like a crash.** `flipctl`
  reboots on its own to recover (`wedged ... rebooting`); when it does, or when
  you run `flipctl reboot`, tell the user, and treat a second one in a session
  as a bug to investigate, not a routine step.
- **Anything that restarts the app disarms the reader.** A deploy, a `ready`, a
  `close` or a reboot after arming all mean arming again. Arm last, then ask.
- A screenshot *does* say which screen is up — the dolphin means the app is not
  on screen no matter what the loader claims, and "Hold a card or ticket"
  means armed. What it cannot do is prove the reader is on when the idle
  "Ready to read a card" screen is showing, which is the state that looks
  fine and reads nothing.
- `loader info` is not reliable on its own: it keeps reporting an app as running
  after it has exited. The heap is the ground truth, because an app's ~48 KB
  comes back the moment it goes. `flipctl` already cross-checks that way.
- `loader close` is the only clean way to exit the app. Sending Back presses
  instead wedges the GUI when one lands after the app has gone — the app browser
  is left on screen, input stops being processed, and only a reboot clears it.
  `flipctl` used to fall back to Back presses when the loader refused, checking
  between each one; the check costs a round trip, so the app could exit inside
  the window and the next press still land. That fallback is gone — nothing in
  `flipctl` injects Back any more, and a close the loader refuses becomes a
  reboot instead. The wedge it caused is nasty precisely because the device
  still answers: `loader info` names the app, the heap still shows it resident,
  and every check says "running" while the desktop owns the screen.
- **A pipe throws away the exit status.** `flipctl ready | tail -5 && ...`
  reports `tail`'s success, so a hard failure — a held port, a device that will
  not come back — reads as a pass. This has already cost one session: the port
  was held, `ready` never ran, and the device sat at the desktop while
  everything downstream assumed it was ready. Use `; echo $?`, or
  `set -o pipefail`.
- **A launch late in a long session can fail with `Preload failed ...: Not
  enough memory`** (seen 2026-10-03, after about 90 minutes and dozens of
  screenshot sessions). The loader needs room for the whole .fap's RAM image at
  once, and a heap fragmented by RPC sessions may not have it; `deploy` reboots
  and retries, and the fresh boot loads it. It is the cost of the app's size,
  not a startup bug - but every KB added makes it likelier.
- **Pushing a file while Flipso is up can run the Flipper out of memory.**
  Measured on 2026-09-28: straight after a deploy, with 13 KB of heap free,
  two 2 KB `flipctl push`es and the firmware rebooted with "out of memory" on
  screen. A storage RPC session needs more room than the app leaves, and a
  screenshot's session holds about 25 KB after it ends, which is why the heap
  low-water drops to a few KB during a screenshot-heavy UI check. `push` now
  closes the app first; do the same before any other RPC-heavy work.
- **Do not move megabytes over USB.** The CDC port manages a few KB/s for bulk
  file transfer: the 21 MB NaPTAN table ran for a full hour through
  `storage.py send` without finishing, and halted the device doing it. The same
  file inside a `.fap` starves `ufbt launch` the same way, and an interrupted
  `.fap` upload leaves a half-written file the loader rejects as **"invalid
  file"**. Anything above a few MB goes on the SD card with a reader;
  `flipctl push` now refuses past 4 MB without `--force`. Measure the rate on a
  small file before assuming a large one will finish.
- A dump of hex transcribed by hand introduced a phantom one-byte offset once.
  Generate C arrays from the pulled file — `tools/test/replay.py` does.
- The USB CDC endpoint drops for a second or two after an RPC call, and every
  read against it then fails with `OSError 6, Device not configured`. `flipctl`
  now reopens and carries on - a log stream says `serial dropped -
  reconnecting` and keeps watching - so this is not a device fault and not a
  reason to reboot. A log line cut off mid-word is the same thing.
- A crash prints **nothing** over USB — the handler writes to the GPIO UART, not
  the CDC port (measured: zero bytes across a deliberate crash). The message is
  on the device screen only, the device stays halted, and only a hardware reset
  (hold LEFT + BACK, five seconds) clears it. `flipctl crash --watch` therefore
  detects a crash by the device going silent and reports the log lines leading
  up to it; see the **flipper-memory** skill.

## Working with the user

The Flipper is on the user's desk. Scanning a card needs them to physically tap
it, so batch those requests and ask once. Everything else — installing,
navigating the UI, screenshots, heap samples, reboots — is done from here
without involving them.

The order for a tap is fixed, and the last two steps are what make the request
honest rather than hopeful:

1. Get the build on the device (`flipctl deploy`).
2. `tools/flipper/flipctl arm --shot <scratchpad>/armed.png`, and check the
   exit status. Non-zero means do not ask.
3. Read the screenshot. It should say "Hold a card or ticket against the back".
4. Arm the log stream — `Monitor` on `flipctl log` — so the scan is visible as
   it happens.
5. *Then* ask, once, and say what should happen.

Asking without step 2 wastes the user's time in the worst way: they hold a card
against a dead app, nothing happens, and the device looks broken to them. It
has happened more than once, so treat a tap request without a green `arm` as a
mistake, not a shortcut.

Card dumps contain the card number and the holder's name, and so do the cards
the user saves in the app. Keep them out of the repo. Delete a *dump* from the
SD card when finished; a saved card is the user's own file, so leave it alone
unless they ask.
