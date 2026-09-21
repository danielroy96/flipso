---
description: Build Flipso, install it on the Flipper and launch it
argument-hint: "[--build-only]"
allowed-tools: Bash(tools/flipper/flipctl:*), Bash(tools/test/run.sh), Read
---

Deploy Flipso to the connected Flipper: `tools/flipper/flipctl deploy $ARGUMENTS`

Run it in the background — a deploy that has to reboot the device takes 60-90s.

Then:

- If the compile failed, show the diagnostics and fix them. Nothing was uploaded.
- If it succeeded, take a screenshot (`tools/flipper/flipctl shot` into the
  scratchpad, then Read it) so the user can see what is on the device, and
  report the free heap the deploy printed.
- If the app is not running afterwards, it may have crashed on start — check
  with `tools/flipper/flipctl crash`.

A successful deploy says the `.fap` is installed and launched. It does **not**
mean a card can be tapped: the scan screen comes up with the reader switched
off. If the next step needs the user to tap, run `tools/flipper/flipctl arm`
and check its exit status first.

Follow the **flipper-hardware** skill for the failure modes.
