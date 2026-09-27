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
| Check / fix formatting | `ufbt lint` / `ufbt format` |
| Build, install and launch on the Flipper | `tools/flipper/flipctl deploy` |
| Check the environment and the device | `tools/flipper/flipctl doctor` |
| Prove a card can be tapped right now | `tools/flipper/flipctl arm` |
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
flipso.c              app entry, the icon table, the name validator
flipso_format*.c      the text of every screen, one file per screen group, sharing
                      flipso_format_i.h; host-tested by test_format.c
flipso_reader.c       card reading: DESFire (CMD7/CMD12), and the pollers
flipso_scan_session.c which transport next, retries and the verdict; pure C,
                      host-tested by test_scan_session.c
flipso_cmd2.c         ISO 7816 transport for CMD2 media
flipso_type2.c        NFC Type 2 tag transport for CMD4 (SPT paper tickets)
flipso_media.c        what a DESFire says about itself (incl. Oyster); the text is flipso_format.c's
flipso_capture.c      the raw blocks a read produced; saved cards decode from these
flipso_saved.c        those blocks on the SD card: write, read, browse, match, rename, delete
flipso_operators.c    operator id -> name, built-in table plus the user's file
flipso_stations.c     NLC -> station name, binary search over the SD card table
flipso_naptan.c       NaptanCode/AtcoCode -> bus stop name, same design
itso/                 the decoder: pure C, no firmware dependency, host-testable
scenes/               one file per scene; every scrolling text screen is the one
                      text scene (flipso_open_text()); list in flipso_scene_config.h
views/                custom views (the icon list, the text panel, the scan screen),
                      and the hand-drawn £ and € the fonts lack
tools/flipper/        flipctl: the device driver described above
tools/ide/            compile_commands.json, so CLion and clangd index the tree
tools/test/           host test suite, synthetic card builder, card replay
tools/debug/          opt-in card-dump instrumentation
tools/demo/           synthetic demo cards for the device, as saved-card files
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
about 66 KB of the 214 KB file as of 2026-09-27, because the 79 KB station
table lives in `.fapassets`, which the firmware unpacks to the SD card and never
maps. Anything added as a `const` array *does* reach RAM. Flipso costs about
101 KB of heap all told while running (measured 2026-09-27 with `flipctl mem
--cost`; the CMD4 paper-ticket support added 5 KB, almost all of it code), of
which that 66 KB is the image and the rest is what it allocates - about 15 KB
of that is `ItsoCard`, twenty products and twelve taps. Measure it
as the difference between `flipctl mem` with the app up and with the desktop
showing, which is the only honest way to read it.

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
  matched recursively and would pull in the host-side tests under `tools/`.
- Screen text is built in `flipso_format*.c`, never in a scene, and follows the
  house style its header sets out: `Label: Value` with the value capitalised, a
  detail indented two spaces and itself labelled, money as `£`. `test_format.c`
  holds every screen of every demo card to that, so a line that breaks it fails
  the host tests.
- Every decoder change needs a case in `tools/test/` — usually a new synthetic
  product in `tools/test/build_card.py`. The suite runs under ASan and UBSan;
  make the test buffer exactly as long as the data claims to be, or an over-read
  lands inside an oversized array and the sanitiser sees nothing.
- Icons in `images/` are generated by `tools/icons/build_icons.py`; edit the
  generator, not the PNGs.
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
  After any launch, `flipctl shot` and look before `flipctl keys`.
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
