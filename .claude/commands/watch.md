---
description: Arm the reader, prove it, and stream the log while the user taps a card
argument-hint: "[seconds] [--grep PATTERN]"
allowed-tools: Bash(tools/flipper/flipctl:*), Read
---

Get the Flipper into the one state where a tap will be read, prove it, then
stream the scan into the chat: $ARGUMENTS

**Step 1 — arm the reader and check that it worked.** Nothing else in this
command matters if this step is skipped:

```bash
tools/flipper/flipctl arm --shot /tmp/.../armed.png; echo "ARM=$?"
```

- `ARM=0` and a screenshot reading "Hold a card or ticket against the back":
  go on. Read the PNG, and show it to the user — it is the evidence that their
  tap will do something.
- `ARM=3`: a card was already on the reader and has just been read, so the
  field is off. Nobody needs to tap: the read is in `arm`'s output and on
  screen. Use `flipctl scan` for this case.
- Anything else: **do not ask for a tap.** `arm` prints why and has already
  retried and rebooted on its own. Say what is wrong and what you are doing
  about it.

Do not substitute `ready`, `doctor` or a successful deploy. The app's scan
screen starts with the reader switched off, and `loader info` reports Flipso
running even when the desktop owns the screen — so both of those pass in states
where a tap does nothing. See the **flipper-hardware** skill.

**Step 2 — arm the log** with the **Monitor** tool, so the lines appear live:

```
Monitor(command: "tools/flipper/flipctl log --seconds 120",
        description: "Flipso debug log from the Flipper")
```

**Step 3 — ask the user to tap, once**, and say what to expect.

Then read the lines as they arrive: `E<n>: TYP x.y` is one per directory entry
on the card. A stream of `[D][Nfc] FWT Timeout` is the field polling with
nothing on it — that is the armed idle state, not a fault.

`flipctl log --arm` does steps 1 and 2 in one process and refuses to stream
unless the reader came up, which is the better shape when the whole thing runs
under a single Monitor.

Keys cannot be sent while the log holds the port; pass `--keys` to send them
first. Do not restart the app after arming — a deploy, a `ready`, a `close` or
a reboot all disarm the reader and the arming has to be redone.
