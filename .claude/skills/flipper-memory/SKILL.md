---
name: flipper-memory
description: Profile Flipso's memory on the Flipper Zero and diagnose crashes - heap usage and leaks, what the .fap costs in RAM, fragmentation, and turning a furi_check failure or halted device into a file and line. Use for "does this leak", "how much memory does it use", "it crashed", "furi_check failed", "the app won't start", "is this too big for the Flipper".
---

# Memory and crashes

The Flipper has a **190 KB heap**, and the whole `.fap` is loaded into it before
`main()` runs. Flipso costs roughly 48 KB of heap while running. That is the
budget for every decision here.

## What the binary costs before it does anything

```bash
tools/flipper/flipctl size
```

Lists every section with whether it reaches RAM. Only `ALLOC` sections do:

```
.fapassets              78,859  no     <- unpacked to the SD card by the firmware
.text                   22,556  yes
.rodata                  7,653  yes
TOTAL IN RAM            30,209  (16% of the 190 KB heap, before any allocation)
```

This is why the 3,983-entry station table is a file in `assets/` rather than a
`const` array: as `.rodata` it would cost ~128 KB of heap — more than everything
else in Flipso combined — and the app would not load at all. As `.fapassets` it
costs one file handle, a 47-byte buffer and a binary search.

**Any new `const` table reaches RAM.** If it is more than a couple of KB, it
belongs in `assets/` behind a lookup, not in the binary. Check with `flipctl
size` before and after.

## Heap at runtime

```bash
tools/flipper/flipctl mem                          # one snapshot
tools/flipper/flipctl mem --samples 20 --interval 2  # watch it over time
tools/flipper/flipctl mem --cost                   # what the app costs (closes it)
```

Three numbers matter:

- **Free heap** — what is left right now.
- **Minimum heap size** — the low-water mark since boot. This is the one that
  says whether a change ever came close to pressuring memory. If it does not
  move while you exercise the new code, the new structures never became the
  high-water allocation.
- **Maximum heap block** — the largest single allocation that would succeed. A
  wide gap between this and free heap is fragmentation, which is what makes a
  large allocation fail on a device that looks like it has plenty free.

`--cost` closes the running app to get a clean baseline, so do not use it while
the user is mid-way through something on the device.

## What each screen costs

```bash
tools/flipper/flipctl walk <scratch>/m --launch 'right down ok' ok 'down down down ok' ok
```

A walk prints the free heap, the low-water mark and the largest block after
every step, with a screenshot of the screen it was read on - here the scan
screen, the demo list, Demo 01's card menu, its product list and a product.
That is the table past sessions built by hand from `keys` and `mem` calls, a
scratch `measure.sh` among them. Add `--no-heap` when only the pictures
matter; a reading costs 0.2 s.

## Checking for a leak

Get to the screen, then repeat the suspect path and watch the heap return to
where it started:

```bash
tools/flipper/flipctl walk <scratch>/to --launch 'right down ok'   # to the demo list
tools/flipper/flipctl walk <scratch>/leak ok back --repeat 5       # open and close Demo 01
```

It ends with the free heap at the end of each repeat and the drift from the
first to the last. `--repeat` repeats every step it is given, which is why the
navigation is a walk of its own. Compare repeats with each other, not with the
idle figure: the text panel keeps its longest string until the app exits
(CLAUDE.md), so the first open of a long screen costs once and then holds.

Then exit the app entirely and confirm the heap comes back to the idle figure —
that is the test for teardown, and it catches anything the scene manager did not
free. Scan start/cancel cycles are the highest-value path: they allocate and
free the NFC poller each time.

Note the baseline moves between boots (110–135 KB free idle depending on
firmware state), so compare within one session, never against a number written
down earlier.

Much of that spread is the screenshot. `flipctl shot` works over an RPC
session, and the memory that session takes stays allocated after it. A walk's
frames all come from one RPC session: on 2026-10-03 thirty of them in a row
moved the free heap by under 100 bytes, which came back when the session
closed, so readings within one walk compare. Measured on
2026-09-26 at the desktop after closing Flipso: 138,904 bytes free with no
screenshot since boot, 113,600 with one. So take the idle figure and the
after-exit figure on the same side of a screenshot, or a ~25 KB gap reads as a
leak the app does not have.

## Crashes

A `furi_check` or `furi_assert` failure **halts the device**. Know what that
means before trying to debug one, because most of the obvious approaches do not
work:

- **Nothing is printed over USB.** The crash handler runs with the RTOS already
  down and writes to the GPIO UART console, not the CDC port. A 40-second raw
  capture across a deliberate crash returned *zero bytes*. There is no crash
  text to grep for.
- **The failure message is on the Flipper's screen, and nowhere else** reachable
  from here. RPC is dead, so it cannot be screenshotted.
- **The device stays halted indefinitely.** The CLI, RPC and `power reboot` are
  all dead, so `flipctl reboot` cannot recover it. Only a hardware reset can:
  hold **LEFT + BACK for about five seconds**.

So a crash always costs the user a moment. Get everything out of that moment.

### The workflow

```bash
tools/flipper/flipctl crash               # is it halted right now?
tools/flipper/flipctl crash --watch 120   # watch, and capture the run-up
```

`--watch` streams the log and detects the crash by the device going silent,
confirming with a second connection so that an idle app is not mistaken for a
dead one. It then prints the last 40 log lines before the silence. **The fault
is just after the last line that appeared** — that is the best localisation
available without a hardware debugger, and it is the whole reason to arm this
before reproducing rather than after.

If nothing was logged, add `FURI_LOG_D` along the suspect path and reproduce.
The furthest line that appears bounds the fault.

Then ask the user, in one message: read the message off the screen, and hold
LEFT + BACK for five seconds. Both at once — do not make it two round trips.

Every `flipctl` command detects the halted state and says the same thing, so a
deploy against a crashed device fails in a second with the explanation rather
than hanging.

### Turning an address into a line

```bash
tools/flipper/flipctl sym 0x5650
tools/flipper/flipctl sym 0x2001a3f4 --base 0x20014000
```

A `.fap` is relocated when it is loaded, so a runtime address is the ELF address
plus wherever the loader put the app. The loader logs that base at debug level;
pass it as `--base`. Without a base, addresses from a map file or
`arm-none-eabi-nm` map directly.

`sym` reads `dist/debug/flipso_d.elf`, which `flipctl deploy` refreshes — so
symbolise against the build that crashed, not a later one.

### Prefer to crash on the host

Most memory bugs in the decoder are reachable on the host, where ASan gives a
full stack trace instead of a screen the user has to read out:

```bash
tools/test/run.sh
tools/test/replay.py dump.txt   # the bytes from the card that crashed it
```

An over-read of a card data group is by far the most common kind. Make test
buffers exactly as long as the data claims to be — see the **flipso-decoder**
skill for why an oversized buffer hides them.

Leaks are the exception: **ASan's leak checker does not run on this Mac** (Apple
Silicon's ASan says `detect_leaks is not supported on this platform`), while
CI's Linux runner has it on by default — so a leak in a host suite passes here
and fails CI. Two ways to see one locally:

- Count the heap in the test itself: `__sanitizer_get_current_allocated_bytes()`
  before and after a decode-and-free, as `tools/test/capture/test_history_memory.c`
  does for the card's allocations (its products, their value histories, its
  taps), and `__sanitizer_get_allocated_size()` to check a block was allocated
  to fit.
- Build a suite without `-fsanitize` and run it under `leaks --atExit -- ./suite`,
  macOS's own leak checker, which names the allocation site of each leak.

A device crash that cannot be reproduced on the host is worth the effort of
turning into a host test anyway: it is the difference between a one-second loop
and a loop that needs someone to hold down two buttons.

### Verifying the crash tooling itself

`tools/debug/crashcanary/` is a throwaway app that fails a `furi_check` on
purpose, so the detection path can be proved out without touching Flipso:

```bash
cd tools/debug/crashcanary && ufbt launch   # counts down 5s, then crashes
cd - && tools/flipper/flipctl crash --watch 20
```

It should report the countdown lines and then the halt. Recovering costs a
hardware reset, so only run it when you are actually changing the crash tooling.

## Before calling a memory change done

- `flipctl size` — the RAM total moved by what you expect, and no new ALLOC
  section appeared.
- `flipctl mem` before and after exercising the changed path — low-water mark
  unchanged, or changed by an amount you can account for.
- Exit the app and confirm the heap returns to the idle baseline.
- `tools/test/run.sh` clean under ASan and UBSan.

Quote the measured numbers rather than an estimate, and say which device state
they were measured in — the idle baseline varies between boots.
