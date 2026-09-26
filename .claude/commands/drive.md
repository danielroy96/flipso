---
description: Drive Flipso's UI on the device and screenshot each step
argument-hint: "<what to walk through, e.g. 'the products list'>"
allowed-tools: Bash(tools/flipper/flipctl:*), Read
---

Walk through this part of Flipso on the connected Flipper: $ARGUMENTS

Method:

1. Screenshot first to see where the device already is. If it is not Flipso -
   the dolphin, or any other app - do not send keys: they would go to the
   desktop. Run `tools/flipper/flipctl ready`, check it exited 0, and
   screenshot again.
2. Send keys with `tools/flipper/flipctl keys ...` (`up down left right ok back`,
   `ok:long` for a long press, `@1.5` to wait).
3. Screenshot after each meaningful step, write the PNGs into the scratchpad,
   and Read them so you can actually see the screens.
4. Show the user the screenshots that matter, and say what each one is.

Do not spam Back to leave the app — use `tools/flipper/flipctl close`. See the
**flipper-hardware** skill.
