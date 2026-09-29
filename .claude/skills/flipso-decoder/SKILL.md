---
name: flipso-decoder
description: Debug and extend the ITSO card decoder in itso/ - a card that will not read, a field that decodes wrong, a product type that is not recognised, an unsupported card, or checking a layout against the ITSO TS 1000 specification. Use for "the card isn't recognised", "this field is wrong", "value record that could not be read", "add support for X card", "what does the spec say about Y".
---

# Decoding ITSO cards

`itso/` is pure C with no firmware dependency. That is deliberate: it builds on
this machine, so a hypothesis about card bytes is tested in a second instead of
a reflash-and-tap cycle. Keep it that way — no `furi.h` in `itso/`.

## The order to work in

1. **Reproduce on the host.** If the bytes are already captured, run them
   through `tools/test/replay.py`. If not, capture them once (below).
2. **Check the layout against the spec** before changing an offset. Guessing at
   offsets is how a whole screen ends up subtly wrong.
3. **Add the case to `tools/test/build_card.py`** as a synthetic product, and
   make `tools/test/run.sh` fail before you make it pass.
4. **Only then** deploy and confirm on the real card.

## Capturing the bytes from a real card

The one thing that needs the user. Do it once and get everything:

```bash
# 1. wire in the dump module - see tools/debug/flipso_dump.h for the three edits
# 2. tools/flipper/flipctl deploy
# 3. tools/flipper/flipctl arm --shot /tmp/.../armed.png; echo "ARM=$?"
#    ARM must be 0 before step 4. A deploy leaves the reader switched off, so
#    "it just deployed" is not a reason to think a tap will be read.
# 4. ask the user to tap the card once
tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt
tools/test/replay.py dump.txt
```

`replay.py` generates the C arrays from the file and runs the decoder over them
under ASan and UBSan, printing the shell, the directory, every product and the
journey log. Iterate against that, not against the device.

For a saved card, `tools/test/screens.py card.flipso` prints every screen as
the device would draw it, with station and stop names from the real tables.
It ends with every operator number on the card, flagging any that falls in a
TS 1000-2 table B2 gap, which no real OID can. Check the screens there, not
by scrolling the device.

For full ground truth on a card whose geometry is in doubt, loop every DESFire
file id 0..31 reading settings *and* data. That shows each file's true size,
which the chained `GROUP` buffers alone do not.

Take the instrumentation back out when finished, and delete `dump.txt` from the
SD card and the working tree — it contains the card number and holder's name.

## Reading the specification

Every part of ITSO TS 1000 except part 8 is published free under the Open
Government Licence:

```
https://www.itso.org.uk/hubfs/TS_1000-<N>_V2_1_5_2025_03.pdf
```

The parts the decoder is written against: **part 1** data types, **part 2**
shell / directory / IPE / value record, **part 5** per-IPE-type datasets,
**part 10** customer media definitions.

Search them with `tools/spec/itso_spec.py`, which downloads a part the first
time it is needed and caches its text in `~/.cache/flipso/itso-spec`:

```bash
tools/spec/itso_spec.py grep 'ProductRetailer' --part 5    # every hit, page numbered
tools/spec/itso_spec.py grep 'Table B2' --part 2 -C 25     # with context
tools/spec/itso_spec.py page 5 51                          # one whole page
```

Parts 1, 2, 4, 5 and 10 are searched when no `--part` is given. The layout
tables survive text extraction well enough to read field offsets off them.

**Offsets in the TS 1000-5 IPE and value-record tables are absolute from the
start of the data group.** A value record's own byte N is table offset N+2,
because the 2-byte VG header precedes it. Getting this wrong shifts every field
in the record, and the result looks plausible rather than obviously broken.

## Things that have bitten before

- **A blank value record is all zeros, and a DTS of zero decodes to 2028** —
  later than any real timestamp, so an unwritten record beats the live one when
  picking "newest". Check `itso_is_blank` before comparing timestamps.
- **A DATE of zero is the top of the 14-bit range (2041-11-10)**, which schemes
  use to mean "never expires". Print that, not a bewildering 2041 date. `0x3FFF`
  means the same on a compact shell (TS 1000-10 table 42); `itso_date_open()`
  covers both.
- **CMD2 cards use a different geometry** from CMD7 — 80-byte sectors, 64 of
  them, 16 directory entries, and therefore a six-bit Sector Chain Table rather
  than a four-bit one. Both are covered in `tools/test/build_card.py`.
- **Every paper ticket (CMD4) has the same card number.** A compact shell
  stores no identity - `633597 8189 0000000 3` is implied by the CMD - so saved
  tickets are matched and named on the chip UID, and the operator comes from the
  product owner (`itso_card_issuer_oid()`), not the shell's generic OID 8189.
- **Never start an ISO 14443-4 poller on a Type 2 tag.** It sends a RATS the tag
  cannot answer and polls for ever. The detect stage routes Type A cards that do
  not speak -4 straight to the Type 2 transport (`flipso_scan_session_next_transport()` in `reader/flipso_scan_session.c`).
- **A Type 2 read shorter than 64 bytes is a failed read**, not a small card:
  `itso_type2_kind()` calls it incomplete, so it is retried instead of being
  shown - or saved over a good copy - half decoded.
- **Oyster is deliberately unsupported.** It is DESFire but runs a proprietary
  application; `reader/flipso_media.c` detects it and explains rather than failing.
- **Operator ids are scheme-specific.** The published ENCTS list names the local
  authorities that administer concessionary bus passes; rail operators are
  numbered separately. Do not resolve one from the other's table.

## Testing

`tools/test/run.sh` builds and runs four suites under ASan and UBSan: the
decoder, the station table, card media, and the icon list view.

Make the fuzz buffers **exactly** as long as the data claims to be. An earlier
sweep used `uint8_t body[128]` while telling the decoder the dataset was much
smaller, so every over-read landed inside the array and the sanitiser saw
nothing. Allocate on the heap at the declared length and ASan catches it.

Sweep the awkward values: all 64 bitmaps, every sector size **including zero**,
and format revisions the card might not carry.

## Debug logging

`FURI_LOG_D(TAG, ...)` in the reader is the cheapest way to see what a real card
produced. The existing line reports each directory entry's type, size, format
revision and bitmap:

```
[D][Flipso] E3: TYP 23.4, 192 bytes, rev 2, bitmap 0x0A
```

Watch it with `tools/flipper/flipctl log --arm --grep 'E[0-9]'` while the user
taps — `--arm` starts the scan, proves the NFC field is polling and refuses to
stream if it is not, so the silence before the tap means something. See the
**flipper-hardware** skill for streaming it into the chat live, and for why no
other check settles whether a tap will be read.
