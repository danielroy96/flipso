---
name: flipper-hardware
description: Build, install, run and drive Flipso on a connected Flipper Zero - deploy over USB, send key presses, capture screenshots of the device screen, stream the device log, and recover a wedged device. Use whenever the task involves the physical Flipper: "deploy", "install it", "launch the app", "what does the screen show", "drive the UI", "check the log", "it hung", "the port is busy", "ufbt launch is stuck".
---

# Driving the Flipper

Everything goes through `tools/flipper/flipctl`, which owns the serial port.
Run it from the project root. It creates its virtualenv on first use.

Never use raw `ufbt launch`, and never write a one-off pyserial script: the
behaviours below exist because past sessions lost time to each of them.

## Check first, once per session

```bash
tools/flipper/flipctl doctor
```

Reports the port and who is holding it, the host tooling, and — if the device
answers — what is running and how much heap is free. If there is no port, the
Flipper is unplugged or in DFU; say so and stop rather than retrying.

## Deploy

```bash
tools/flipper/flipctl deploy
```

Compiles, copies the `.fap` to `dist/`, closes any running app, uploads,
launches, then reports what is running and the free heap. About 13 s when the
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

- Write screenshots to the scratchpad directory, not into the repo.
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
tools/flipper/flipctl log --keys ok --seconds 60  # start a scan, then watch
```

For anything the user has to do — tapping a card, above all — run the log in the
background and stream it into the chat with the **Monitor** tool, so they can
see the scan happening as it happens:

```
Monitor(command: "tools/flipper/flipctl log --seconds 120",
        description: "Flipso debug log from the Flipper")
```

Then ask them to tap the card once. Arm the log *before* asking.

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
  empty capture to be misread as a broken stream.

## When it goes wrong

| Symptom | Cause | Do this |
| --- | --- | --- |
| `ufbt launch` hangs at `Using flip_...`, or `Error 4`/`-15` | the app is still running and the loader will not replace it | `flipctl deploy` already handles it; otherwise `flipctl close` |
| `Application "X" has to be closed manually` | the loader refuses to close it from the scene it is in | `flipctl reboot` |
| The port exists but nothing answers | a crashed app halts the device, or an RPC session was left open | `flipctl crash`, then `flipctl reboot` |
| `the serial port is held by another process` | a log stream or `ufbt` from an earlier session | `flipctl` clears its own helpers; `--force` clears anything |
| `[flipctl] serial dropped - reconnecting` | macOS dropped the CDC endpoint, usually just after an RPC call (a screenshot, `ufbt launch`) | nothing: the stream reopens and re-arms itself and keeps watching |
| The screen shows the app browser and nothing responds | a Back press landed after the app exited and wedged the GUI | `flipctl reboot` |

`flipctl reboot` is the universal escape hatch: it reboots, waits for the port
to come back, and reports the fresh heap baseline. It takes about 20 s and is
safe — it is a development device, and the alternative is asking the user to
press buttons.

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
device that has stopped enumerating. Batch them, ask once, and have the log or
the screenshot armed first. Installing, navigating, screenshotting, sampling the
heap and rebooting are all done from here.
