---
description: Drive Flipso's UI on the device and screenshot each step
argument-hint: "<what to walk through, e.g. 'the products list'>"
allowed-tools: Bash(tools/flipper/flipctl:*), Read
---

Walk through this part of Flipso on the connected Flipper: $ARGUMENTS

Method:

1. Work out the route from the scan screen as a list of steps - each step a
   key sequence (`up down left right ok back`, `ok:long`, `@1.5` to wait).
   From the scan screen, `'right down ok'` is the demo card list and a further
   `ok` opens Demo 01; Right and Left turn a text screen's pages.
2. Run it as one walk, into the scratchpad:

   ```bash
   tools/flipper/flipctl walk <scratch>/drive --launch 'right down ok' ok ...
   ```

   `--launch` starts from a fresh scan screen; leave it off to carry on from
   where the device is (the walk refuses if no app is running).
   `--until-same` repeats the last step until the screen stops changing:
   `ok right --until-same` opens a row and captures every page.
3. Read `<scratch>/drive/sheet.png`. Frame 00 is the screen before any key:
   if it is not Flipso, nothing after it means anything.
4. Show the user the frames that matter, and say what each one is. The walk
   prints the free heap at every step too; mention it if it moves oddly.

Do not spam Back to leave the app - use `tools/flipper/flipctl close`. See the
**flipper-hardware** skill.
