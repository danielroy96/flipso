---
description: Work out whether the Flipper has crashed and turn the fault into a file and line
argument-hint: "[what you were doing]"
allowed-tools: Bash(tools/flipper/flipctl:*), Read
---

Diagnose a crash on the Flipper. Context: $ARGUMENTS

A crash prints nothing over USB and halts the device, so the order matters:

1. `tools/flipper/flipctl crash` — is it halted right now? Every other flipctl
   command detects this too, so a failing deploy will already have said so.
2. If it is halted, there is nothing more to read from here. Ask the user, in
   **one** message, to read the message off the Flipper's screen and then hold
   **LEFT + BACK for five seconds** to reset it.
3. If it is not halted, arm `tools/flipper/flipctl crash --watch 120` **before**
   reproducing. It detects the crash by the device going silent and prints the
   log lines leading up to it — the fault is just after the last one.
4. Map any address from the screen with `tools/flipper/flipctl sym 0x...`,
   passing `--base` if you have the loader's load address.
5. Try to reproduce it on the host — `tools/test/run.sh`, or
   `tools/test/replay.py` with the card's bytes. ASan there gives a full stack
   instead of a screen someone has to read out.

Follow the **flipper-memory** skill.
