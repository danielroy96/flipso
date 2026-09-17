---
description: Stream the Flipper's debug log into the chat while the user taps a card
argument-hint: "[seconds] [--grep PATTERN]"
allowed-tools: Bash(tools/flipper/flipctl:*)
---

Arm the device log and stream it into the chat so the user can watch the scan
happen: $ARGUMENTS

Use the **Monitor** tool so the lines appear live:

```
Monitor(command: "tools/flipper/flipctl log --seconds 120",
        description: "Flipso debug log from the Flipper")
```

Arm it *before* asking the user to do anything. Then ask them to tap the card
once, and interpret the lines as they arrive — the `E<n>: TYP x.y` lines are one
per directory entry on the card.

An idle app logs nothing; the tool says so rather than leaving you guessing.
Keys cannot be sent while the log holds the port — pass `--keys` to send them
first. See the **flipper-hardware** skill.
