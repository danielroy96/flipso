---
description: Pull a card dump off the Flipper and replay it through the decoder
argument-hint: "[path on the device]"
allowed-tools: Bash(tools/flipper/flipctl:*), Bash(tools/test/replay.py:*), Read
---

Get the raw bytes of a scanned card onto this machine and decode them here:

```bash
tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt
tools/test/replay.py dump.txt
```

$ARGUMENTS

If there is no dump on the device yet, the instrumentation is not wired in —
see `tools/debug/flipso_dump.h` for the three edits, then deploy and ask the
user to tap the card once.

Afterwards: take the instrumentation back out, and delete the dump from the SD
card and the working tree. It contains the card number and the holder's name.

Follow the **flipso-decoder** skill.
