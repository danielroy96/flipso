---
name: flipper-hardware
description: Build, install, run and drive Flipso on a connected Flipper Zero - deploy over USB, send key presses, capture screenshots of the device screen, stream the device log, and recover a wedged device. Use whenever the task involves the physical Flipper: "deploy", "install it", "launch the app", "what does the screen show", "drive the UI", "check the log", "it hung", "the port is busy", "ufbt launch is stuck".
---

# Driving the Flipper

Everything goes through `tools/flipper/flipctl`, which owns the serial port.
Run it from the project root. It creates its virtualenv on first use.

Never use raw `ufbt launch`, and never write a one-off pyserial script: the
behaviours below exist because past sessions lost time to each of them.

## Before asking the user to tap a card

```bash
tools/flipper/flipctl arm --shot /tmp/.../armed.png
```

**This, and nothing else, is what earns the right to ask for a tap.** Run it,
check its exit status, and Read the screenshot. If it exits 1 it has printed
`NOT ARMED`; fix that or tell the user the device needs attention. If it exits
**3** it has printed `CARD READ`: a card was already lying on the reader and
the app has read it, so the field is off again and a tap would do nothing. Do
not ask for a tap on the strength of anything else - not `doctor`, not `ready`,
not a heap figure, and not because a deploy just succeeded.

`arm` relaunches the app, presses Scan, and then waits for the NFC poller's own
log line. The firmware logs `[D][Nfc] FWT Timeout` about every 100 ms while the
field is up and **nothing at all** while it is down, so seeing one is a
measurement that the field is radiating - which is only true when the app is on
screen, in the scan scene, and scanning. It exits 0 only when it has seen that
line.

The OK press goes down the log stream's own session, in the same write as
`log debug`. Sent separately, it lands before the stream is listening, and a
card already on the reader is read in the gap. For the same reason `arm` keeps
watching for 1.5 s after the first timeout line: the poller logs one timeout
before it finds a card that is already there. Both were measured on
2026-09-28, when `arm` first retried over a successful read and then said
ARMED a moment before the card was read.

It restarts and retries on its own if the first attempt does not arm, which
does happen - measured on 2026-09-21, attempt 1 pressed OK into an app that
never started polling and attempt 2 succeeded after a reboot. That retry is the
whole point: it is a failure that used to be handed to the user as "tap now".

Once armed it stays armed. Nothing in the app stops a scan on a timer, so the
gap between arming and the user actually picking up their card does not matter.
What does matter is not disturbing it afterwards: **anything that restarts the
app disarms the reader.** A `deploy`, a `ready`, a `close`, a `reboot` after
arming all mean you must `arm` again before asking.

### A card that is already on the reader

```bash
tools/flipper/flipctl scan --shot /tmp/.../scan.png; echo "SCAN=$?"
```

When the user leaves a card lying under the Flipper, nobody is going to tap,
so `arm` is the wrong question. `scan` relaunches, presses Scan the same way,
waits for a card to answer and follows the read until the log goes quiet. It
prints what the app logged, then screenshots the result. It exits 0 when a
card was read, 1 when the field never came up, 2 when the device is halted,
and 3 when the field polled and nothing answered (the card is off-centre).
See the **new-card** skill for what comes after.

### Why `ready` is not the check

`ready` used to be the rule on this page. It was wrong twice over, both
measured:

- It used to finish by asking `loader info`, which on 2026-09-21 printed
  `ready: Flipso` while the screen showed the desktop. The cause turned out to
  be a Flipso bug (below, "An app stuck in startup"), and `ready` now waits for
  the app's own `UI ready` log line instead - so that half is fixed.
- Even when the app really is on screen, Flipso's scan scene **starts idle on
  purpose** - the field costs power, so it only comes up when someone presses
  OK. A completely healthy `ready` still leaves a screen reading "Ready to read
  a card" above a reader that is switched off.

`ready` is still the right way to get the app up before driving the UI. It is
not a pre-tap check, and it now says so in its own output.

### What can and cannot tell you the device is usable

| What you might check | Why it does not work |
| --- | --- |
| `loader info` | says the app is running in every broken state |
| free heap | the app's memory is still held, so it looks like a healthy run |
| `top` thread table | measured byte-for-byte identical, healthy vs unusable |
| `ready` exiting 0 | measured printing `ready: Flipso` at the desktop |
| an empty log | an unarmed app and an armed one waiting are equally silent |
| a screenshot | says whether Flipso is on screen - see below - but not whether the reader is on |
| **`flipctl arm`** | **the field is polling: the only positive proof** |

A screenshot *does* tell you which screen is up, and the old claim here that it
cannot was wrong. The firmware never names the app that owns the display, but
the picture is unmistakable, and all three of these have been captured:

| What the screen shows | What it means |
| --- | --- |
| the dolphin | the app is not on screen, whatever `loader info` says |
| "Ready to read a card" with a `Scan` button | app up, **reader off** - a tap does nothing |
| "Hold a card or ticket against the back" | armed |

So screenshot freely, and show the user the armed one. Just do not use a
screenshot *instead of* `arm`: the middle row is the trap, because it looks
ready and reads nothing.

### Check the exit status, and never hide it

`flipctl` reports failure in its exit status, and a pipe throws that away:

```bash
tools/flipper/flipctl arm | tail -5      # WRONG: $? is tail's, always 0
tools/flipper/flipctl arm; echo "ARM=$?" # right
```

This is not hypothetical. In the session that produced this page, a
`flipctl ready | tail -5 && ...` swallowed a hard "the serial port is held by
another process" failure and carried on as though the device had been made
ready. If you must trim the output of a command whose verdict matters, use
`set -o pipefail`, or echo `$?` and read it.

## Check first, once per session

```bash
tools/flipper/flipctl doctor
```

Reports the port and who is holding it, the host tooling, and — if the device
answers — what is running and how much heap is free. If there is no port, the
Flipper is unplugged or in DFU; say so and stop rather than retrying.

What it says about the running app comes from the loader, which is not a
witness: it names Flipso in states where the desktop owns the screen. Treat
`doctor` as a check on the *connection*, not on the app.

## Get the app up, to drive it

```bash
tools/flipper/flipctl ready
```

Closes and relaunches the app so that whatever scene it was left in is gone and
the UI can be driven from a known start. Takes about 15 s, and reboots first if
the loader will not let go.

The launch goes down the same CLI session as a log stream, and only counts when
Flipso logs `UI ready` - which it does just before `view_dispatcher_run()`, once
the first scene is up and input will reach it. If the line does not come
within 15 s, `ready` reboots, tries once more, and exits non-zero if that fails
too. Exit 0 therefore means the app is on screen. It still does not mean the
reader is on: use `arm`, never this, before a card tap.

**Screenshot before sending keys**, every time after a launch. Keys go to
whatever owns the screen; at the desktop they open menus and other apps, and on
2026-09-26 a `Left` meant for Flipso's Saved button went to the desktop because
the app was not on screen.

### An app stuck in startup

The loader calls an app "running" from the moment its thread exists. An app
blocked inside its own startup therefore looks running to `loader info`, holds
its heap, leaves the desktop on screen, and cannot be closed - `loader close`
answers "has to be closed manually", because the exit handler is only
registered by `view_dispatcher_run()`.

That was Flipso on every second launch after a boot until 2026-09-26: a failed
`storage_file_open()` of the optional `/data/stations.dat` was never closed,
so the path stayed registered with the storage service after the app exited,
and the next launch's open of it waited for ever. `tools/test/lint_storage.py`
now guards the pattern.

If a launch ever fails to reach `UI ready` again, **find out where startup
stops rather than rebooting past it**:

```bash
tools/flipper/flipctl close
tools/flipper/flipctl log --launch --all --seconds 15
```

`--launch` opens the app in the same session the stream starts in, so every
line from the first instant of startup is caught - which nothing else can do,
because the stream and a separate `loader open` cannot share the port and the
firmware keeps no log history. Add temporary `FURI_LOG_I` checkpoints to
`flipso_alloc()` if the existing lines do not narrow it down.

## Deploy

```bash
tools/flipper/flipctl deploy
```

Compiles, copies the `.fap` to `dist/`, closes any running app, uploads,
launches, then reports what is running and the free heap. `ufbt launch` cannot
say whether the app reached the screen, so deploy then closes that instance and
relaunches it the checked way (above), and exits non-zero if `UI ready` never
comes. About 13 s when the
app closes cleanly, about 45 s when it has to reboot to get there — which
happens, and needs nothing from you or the user.

- Run it in the background (`run_in_background: true`) — a deploy that has to
  reboot the device takes 60–90 s and will otherwise hit the foreground timeout.
- `--build-only` compiles without touching the device. Use it for a
  syntax check; it needs no hardware and takes about 3 s.
- Compile errors are printed on their own, before any upload is attempted.
- An upload failure is retried once after a reboot, because it is nearly always
  the previous instance still holding the loader. `--no-reboot` opts out.

If it still fails, the device screen has the answer. Take a screenshot.

## Drive the UI

Looking at more than one screen is a walk: one call, one port session, a
frame after every step and a contact sheet of them all.

```bash
tools/flipper/flipctl walk <scratch>/w --launch 'right down ok' ok ok right --until-same
```

That relaunches to the scan screen, then About, Demo cards, Demo 01's card
menu, its Summary, and Right until the page stops changing - every page of
the Summary, numbered, on `<scratch>/w/sheet.png`. Read the sheet, not the
frames one by one. Each step also prints the free heap, the low-water mark
and the largest block, and flags a step whose screen did not change - a key
that did nothing, or the last page.

- **A step** is a key sequence, quoted when it has spaces. The frame is taken
  once the screen has held still for 0.4 s, so there is no `@` wait to guess
  after opening a card; `@N` still works inside a step. `NAME=keys` saves that
  frame as `NAME.png`; `-` captures without a key.
- **Frame 00 is the screen before any key.** Check it on the sheet before
  trusting the rest. Without `--launch` the walk refuses when no app is
  running, so keys never go to the desktop that way, but the loader can name
  Flipso while the desktop has the screen (see "An app stuck in startup").
- **`--repeat N`** runs the steps N times and reports the heap drift: get to
  the screen in one walk, then `walk OUT ok back --repeat 5` is a leak check
  (see flipper-memory).
- **`--steps-file`** reads steps from a file, one a line;
  `docs/screenshots/walk.txt` is the README's.
- **`--text X,Y,W,H`** prints each frame's pixels as text (so does `shot
  --text`). Use it for the questions a picture cannot settle - whether a
  glyph sits on the baseline, how many rows lie between text and a rule.

Measured on 2026-10-03: keys over RPC take 20 ms and a frame 35 ms, against
about 3 s for each separate `keys` or `shot` call, which open and sync the
port every time. A walk through the eight README screens, launch
included, took 30 s in one call; by hand it was six calls and as many turns.

For a single key or capture, `keys` and `shot` are still there:

```bash
tools/flipper/flipctl keys down down ok            # navigate
tools/flipper/flipctl keys ok:long                 # long press
tools/flipper/flipctl keys ok @1.5 down            # '@N' waits N seconds
tools/flipper/flipctl keys down --check            # print what is running after
```

Keys are `up down left right ok back`. Presses go to whatever has the screen,
so check where you are with a screenshot before a long sequence.

**Do not send Back presses to leave the app** — use `flipctl close`, which asks
the loader first. A Back press that lands after the app has already exited
navigates the desktop's app browser, and the browser's frame then sits on screen
with input going nowhere while the next launch runs invisibly behind it. The
only way out of that is `flipctl reboot`.

## Screenshots

```bash
tools/flipper/flipctl shot /tmp/.../screen.png                 # capture now
tools/flipper/flipctl shot /tmp/.../menu.png back up ok @0.6   # keys, then capture
```

Writes a 3x-scaled PNG of the 128x64 screen. Read it back with the Read tool —
this is the only way to see what the app actually looks like, so use it freely
when changing a screen, and show the user before-and-after when a layout
changes.

- `--qflipper` writes the file qFlipper's *Save Screenshot* writes, to the
  byte: black on qFlipper's #FE8A2C, 4x, and encoded as Qt's PNG writer does
  (libpng's default filters and compression, a 72 dpi pHYs chunk). The
  README's screenshots in `docs/screenshots/` are qFlipper's, so take any
  replacement with this, straight to its final path, from the demo cards so no
  real card number or name is in it - `docs/screenshots/walk.txt` retakes
  the whole set in one walk, and its header has the command. `test_flipctl.py` re-encodes
  `tools/flipper/testdata/qflipper_menu.png`, a file qFlipper saved, and checks
  it comes out identical, and on
  2026-10-03 live captures of the Demo 01 menu and product list matched
  qFlipper's files byte for byte. The old `--amber` (#FF8200, plain zlib) was
  not qFlipper's output - it is now an alias for `--qflipper`. They are also
  the Apps Catalog's screenshots (`tools/catalog/manifest.yml` lists them), and
  its bundler rejects anything but 4x or 8x, and turns every pixel lighter
  than (15,15,15) transparent - so the grey default palette would publish
  blank.
- Write screenshots to the scratchpad directory, not into the repo, unless
  they are the README's.
- Straight after a `deploy`, the first capture can show the pre-launch frame.
  Send any key first, or capture twice.
- The screenshot uses the RPC protocol rather than the text CLI. If it is
  interrupted mid-session the port stays in RPC mode and the CLI stops answering
  with `Device not configured`; it heals within a second or two and `flipctl`
  retries automatically.

## The device log

```bash
tools/flipper/flipctl log --seconds 30            # filtered to app/NFC/errors
tools/flipper/flipctl log --all                   # everything, until stopped
tools/flipper/flipctl log --grep 'E[0-9]:'        # just the product lines
tools/flipper/flipctl log --arm --seconds 120     # arm, prove it, then watch
```

`--arm` runs `arm` first and **refuses to stream unless the field came up**,
which is the right shape when the whole thing runs under one Monitor: it means
the silence the user is looking at is an armed reader waiting, not a dead app.

For anything the user has to do — tapping a card, above all — run the log in the
background and stream it into the chat with the **Monitor** tool, so they can
see the scan happening as it happens:

```
Monitor(command: "tools/flipper/flipctl log --arm --seconds 120",
        description: "Flipso debug log from the Flipper")
```

Then ask them to tap the card once. Arm the log *before* asking — and the
reader before that, which is what `--arm` is for. Watch the first lines: if
`ARMED` does not appear, the stream stopped instead of starting, and nobody
should be tapping anything.

- The log level belongs to the session that starts it. `flipctl log` sends
  `log debug` on its own connection, which is why it works; sending `log debug`
  from one connection and `log` from another silently gives the system default
  and the app's `FURI_LOG_D` lines never appear. A reconnect re-arms the level
  for the same reason.
- A USB log line can arrive **cut off mid-word** - `Shell owner` with no OID
  after it. That is the serial stream, not the app: the device carries on and
  the next line is whole. Do not read a truncated line as the app stopping
  where the text does. `flipctl` drops a partial buffer across a reconnect so
  the cut line is not printed at all.
- Streaming holds the port for its whole duration, so keys cannot be sent while
  it runs. Send them first with `--keys`.
- An idle app logs nothing. The tool says so explicitly rather than leaving an
  empty capture to be misread as a broken stream. An *armed* app is not idle:
  the NFC poller logs `[D][Nfc] FWT Timeout` every 100 ms or so with nothing on
  the reader. Those lines are the field working, not a fault, and their absence
  during a scan someone is about to make means the scan is not running.

## When it goes wrong

| Symptom | Cause | Do this |
| --- | --- | --- |
| `ufbt launch` hangs at `Using flip_...`, or `Error 4`/`-15` | the app is still running and the loader will not replace it | `flipctl deploy` already handles it; otherwise `flipctl close` |
| `Application "X" has to be closed manually` | the loader refuses to close it from the scene it is in | `flipctl reboot` |
| The port exists but nothing answers | a crashed app halts the device, or an RPC session was left open | `flipctl crash`, then `flipctl reboot` |
| `the CLI accepted ... and printed nothing back`, or `power reboot was sent but the Flipper never restarted` | the CLI answers a keystroke but runs no command (seen 2026-09-28, after a launch that never came up) | nothing sent over USB will act: ask the user for LEFT + BACK |
| `the serial port is held by another process` | a log stream or `ufbt` from an earlier session | `flipctl` clears its own helpers; `--force` clears anything |
| `[flipctl] serial dropped - reconnecting` | macOS dropped the CDC endpoint, usually just after an RPC call (a screenshot, `ufbt launch`) | nothing: the stream reopens and re-arms itself and keeps watching |
| The screen shows the app browser and nothing responds | a Back press landed after the app exited and wedged the GUI | `flipctl reboot` |
| The app will not open, or the desktop is showing while `loader info` says it runs | the app is blocked in its own startup (see "An app stuck in startup"); `loader open` answers "Loader is locked" and `loader close` "has to be closed manually" | `flipctl reboot` to recover, then **find the cause** with `flipctl log --launch` - it is a bug, not noise |
| `arm` says `NOT ARMED` | the app is not on screen, or the OK press did not reach the scan scene | it has already retried and rebooted; `flipctl shot` and look |

Reboots are visible on the user's desk and look like crashes. Say so whenever
`flipctl` reboots (its output says `rebooting`), and treat a second reboot in a
session as a bug to investigate, not a routine step.

`flipctl reboot` is the universal escape hatch: it reboots, waits for the port
to come back, and reports the fresh heap baseline. It takes about 20 s and is
safe — it is a development device, and the alternative is asking the user to
press buttons.

`flipctl` itself never injects Back. It used to, as a fallback when the loader
refused `loader close`, checking the heap between presses — but that check
costs a round trip, so the app could exit inside the window and the next press
still land, which is the wedge in the first row of that table. The fallback is
now a reboot.

## Files on the device

```bash
tools/flipper/flipctl ls /ext/apps_data/flipso
tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt
tools/flipper/flipctl cmd "storage info /ext" "device_info"
```

`flipctl cmd` runs any Flipper CLI command — `free`, `loader info`,
`storage list`, `power reboot`. Useful ones are listed by `flipctl cmd help`.

## Anything the user must do

Only two things need the user: tapping a card on the reader, and unplugging a
device that has stopped enumerating. Batch them and ask once. Installing,
navigating, screenshotting, sampling the heap and rebooting are all done from
here.

For a tap, the order is fixed, and the check is not optional:

1. `flipctl deploy`, if there is anything to install.
2. `flipctl arm --shot <scratchpad>/armed.png; echo "ARM=$?"` — **and read the
   status**. Non-zero means do not ask.
3. Read the screenshot; show it to the user.
4. `Monitor` the log so the scan is visible as it happens.
5. Ask, once, and say what should happen.

Steps 1 and 4 both touch the device, but only step 1 disarms it — a log stream
does not. Anything that restarts the app after step 2 means going back to it.

The cost of getting this wrong is not a wasted command, it is the user holding
a card against a dead app and concluding the thing is broken. It has happened
more than once. A tap request without a green `arm` behind it is a mistake.
