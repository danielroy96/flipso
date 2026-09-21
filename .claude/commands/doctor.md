---
description: Check the Flipper connection, the host tooling and the device state
allowed-tools: Bash(tools/flipper/flipctl:*)
---

Run `tools/flipper/flipctl doctor` and report the result in two or three lines:
whether the Flipper is connected and free, whether the host tooling is complete,
and what the device is currently running with how much heap.

If anything is wrong, say what to do about it. No port means the Flipper is
unplugged or in DFU — ask the user to check the cable rather than retrying.

A clean `doctor` is not permission to ask for a card tap. It reports what the
loader claims, and the loader claims Flipso is running even when the desktop
owns the screen. `tools/flipper/flipctl arm` is the only check that settles
that; see the **flipper-hardware** skill.
