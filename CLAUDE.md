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
| Decoder tests on this machine | `tools/test/run.sh` |
| Compile only | `tools/flipper/flipctl deploy --build-only` |
| Build, install and launch on the Flipper | `tools/flipper/flipctl deploy` |
| Check the environment and the device | `tools/flipper/flipctl doctor` |
| Refresh the IDE's index of the sources | `tools/ide/compdb.py` |

Slash commands wrap the common ones: `/deploy`, `/drive`, `/watch`, `/mem`,
`/test`, `/dump`, `/doctor`. Skills carry the detail: **flipper-hardware** for
anything involving the device, **flipso-decoder** for card data and the ITSO
spec, **flipper-memory** for heap work and crashes.

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
flipso.c              app entry, shared formatting helpers used by the scenes
flipso_reader.c       card reading: DESFire (CMD7/CMD12) and the retry logic
flipso_cmd2.c         ISO 7816 transport for CMD2 media
flipso_media.c        what a non-ITSO card says about itself (incl. Oyster)
flipso_capture.c      the raw blocks a read produced; saved cards decode from these
flipso_saved.c        those blocks on the SD card: write, read, browse, match, rename, delete
flipso_operators.c    operator id -> name, built-in table plus the user's file
flipso_stations.c     NLC -> station name, binary search over the SD card table
flipso_naptan.c       NaptanCode/AtcoCode -> bus stop name, same design
itso/                 the decoder: pure C, no firmware dependency, host-testable
scenes/               one file per screen; scene list in flipso_scene_config.h
views/                custom views (the icon list, the scan screen)
tools/flipper/        flipctl: the device driver described above
tools/ide/            compile_commands.json, so CLion and clangd index the tree
tools/test/           host test suite, synthetic card builder, card replay
tools/debug/          opt-in card-dump instrumentation
tools/stations/       station table builder and its data provenance
data/                 reference data shipped but not packaged; see its README
tools/naptan/         stop table builder; data/naptan.dat is its output
tools/icons/          pixel art the images/ icons are generated from
```

`itso/` must stay free of firmware headers. That is what lets `tools/test/run.sh`
and `tools/test/replay.py` build it on the host, which is the fast loop.
`flipso_capture.c` is held to the same rule for the same reason: the save and
load path, including the file parser, is tested on the host by
`tools/test/test_capture.c`.

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
today about 44 KB of the 176 KB file, because the 79 KB station table lives in
`.fapassets`, which the firmware unpacks to the SD card and never maps. Anything
added as a `const` array *does* reach RAM. Flipso costs about 70 KB of heap all
told while running, of which that 44 KB is the image and the rest is what it
allocates - measured as the difference between `flipctl mem` with the app up
and with the desktop showing, which is the only honest way to read it.

So: no large static tables, no growing a scene's buffers without checking, and
`tools/flipper/flipctl mem` before and after anything structural.

The NaPTAN stop table is about 20 MB and costs no heap either, but it is kept
out of `assets/` all the same: anything packaged is re-uploaded on every install,
and 21 MB over USB takes upwards of ten minutes and leaves an unloadable `.fap`
if it is interrupted. It ships in `data/` and is copied to the card - see
`data/README.md`.

## Conventions

- Comments explain *why*, and cite the spec clause when a constant comes from
  one (`TS 1000-2 table 11`). Do not narrate what the code already says.
- New sources must be listed explicitly in `application.fam`. A bare `*.c` is
  matched recursively and would pull in the host-side tests under `tools/`.
- Scenes get their data from `Flipso*` in `flipso.h` and format it with the
  `flipso_cat_*` helpers, so wording and date formats stay consistent.
- Every decoder change needs a case in `tools/test/` — usually a new synthetic
  product in `tools/test/build_card.py`. The suite runs under ASan and UBSan;
  make the test buffer exactly as long as the data claims to be, or an over-read
  lands inside an oversized array and the sanitiser sees nothing.
- Icons in `images/` are generated by `tools/icons/build_icons.py`; edit the
  generator, not the PNGs.

## Known noise, already investigated

- `ViewPort lockup` and `Incorrect BacklightEnforce use` fire on **every scan**.
  They come from the scan path, not from the detail scenes, and predate the
  current code. Not a symptom of whatever you just changed.
- **Run `tools/flipper/flipctl ready` before asking the user to tap a card.** It
  closes and relaunches the app so it is certainly on screen, rebooting first if
  the loader will not let go. Nothing else tells a usable device from an
  unusable one: in the state where the app refuses to open, `loader info`, the
  free heap and the whole `top` thread table are all identical to a healthy run,
  and only `loader open` answering "Loader is locked" gives it away. Do not
  send the user to tap on the strength of `doctor` looking fine.
- `loader info` is not reliable on its own: it keeps reporting an app as running
  after it has exited. The heap is the ground truth, because an app's ~48 KB
  comes back the moment it goes. `flipctl` already cross-checks that way.
- `loader close` is the only clean way to exit the app. Sending Back presses
  instead wedges the GUI when one lands after the app has gone — the app browser
  is left on screen, input stops being processed, and only a reboot clears it.
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
it, so batch those requests: get the build on the device, arm the log, then ask
once. Everything else — installing, navigating the UI, screenshots, heap
samples, reboots — is done from here without involving them.

Card dumps contain the card number and the holder's name, and so do the cards
the user saves in the app. Keep them out of the repo. Delete a *dump* from the
SD card when finished; a saved card is the user's own file, so leave it alone
unless they ask.
