# Say when the Flipper's clock cannot be trusted

## Goal

Every verdict Flipso gives about time - Status, Expired, "Time left", which
pass is current, the Summary's "Not valid" page - compares the card's dates
with `flipso_now()` (`flipso.c`), which is the Flipper's RTC. Nothing checks the
RTC. A Flipper whose clock was never set, or has drifted, or lost its time to
a flat battery, says a season ticket has expired, or that an expired one is
fine, on the card's own authority as far as the holder can tell.

The card itself can say when the clock is wrong: every POST that writes to it
stamps the time. If the newest time on the card is later than the Flipper's
"now", the Flipper is behind.

Size: small, about half a day, most of it in the tests.

## What to build

1. **The card's newest time.** A pure function in `format/` (or `itso/`, since
   it is a fact about the card) returning the latest of:
   - `card->log_dts` (the Log Directory Entry),
   - every `card->taps[i].dts`,
   - every `products[i].value_history[j].dts`.
   Only times a POST wrote; product validity dates are the future by design.
   Skip records that are not `on_card` - a saved file's history is older than
   the card by definition, and adds nothing.
2. **The verdict**, two cases:
   - **Behind**: newest card time > `now` + a margin. The margin must cover the
     DTS being local time (BST) while the RTC is set from a computer that may
     not be, and DTS's one-minute resolution - one hour and a few minutes.
   - **Not set**: `now` is before 2024-01-01, whatever the card says (a
     factory-fresh RTC). Pick the cut-off from what an unset Flipper actually
     reports - check on the device with `flipctl` before choosing.
3. **The line.** First thing on the Summary's first page, before the card's
   state, because it qualifies every line below it:
   `Flipper's clock: Behind this card` with a detail
   `  Card last written: 21/09/2026 17:46`, or `Flipper's clock: Not set`.
   Then the same line at the top of each product's first page, where "Status"
   and "Time left" sit - or, if that crowds page 1, only the Summary; decide
   by looking at it on the device.
4. Consider leaving "Time left" out entirely while the clock is untrusted: it
   is the one figure that is pure arithmetic on `now`.

## Constraints

- A saved card is judged the same way: the file's taps and value records are
  older than the RTC on any correctly set Flipper, so they never trip it.
- Demo cards must not trip it on a Flipper set to the right date. The host
  tests render demos with a fixed `now` (`--now` in `screens.py`,
  `FlipsoFormat::now` in `tools/test/screen_text/`): make sure that fixed date
  is after every demo card's newest DTS, or the tests will show the warning.
- No new heap: compute the newest time when the Summary is drawn.

## Tests

- `tools/test/screen_text/`: a synthetic card whose newest tap is after `now`
  shows the line, one whose newest tap is 30 minutes after `now` does not
  (margin), and one with `now` in 2000 shows "Not set".
- House style still passes on every demo screen.

## Done when

- A Flipper with a wrong clock says so before it says anything is valid or
  expired; a correctly set one says nothing new.
- `tools/test/run.sh` passes, `ufbt lint` is clean, and the line is checked on
  the device with the RTC set back a day. `flipctl` has no clock command
  today; the Flipper CLI's `date` sets the RTC, so add a `flipctl date` (it
  owns the serial port) rather than opening the port by hand, and set the
  clock back afterwards.
- This brief and its row in `README.md` here are deleted.
