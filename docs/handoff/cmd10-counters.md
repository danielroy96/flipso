# Read a CMD10 card's one-way counters

## Goal

A CMD10 card (ITSO on a MIFARE Ultralight EV1 MF0UL51) keeps its anti-rollback
count in the chip's **one-way counter #1** rather than in a CMD9's Abacus page
(TS 1000-10 clause 11.26.4): counter #1 tracks the TS# of the latest value
record, "the TS# of the latest VR must be greater than or equal to the number
represented by the Counter" (11.26.5), and **0xFFFFFF means the card is
retired**.

Flipso does not read it, so a retired CMD10 is not flagged (PROTOCOL.md,
Limitations), because the clause does not say whether "counter #1" is the
chip's counter 0 or counter 1 - the MF0UL51 has three, numbered 0 to 2.

Read all three and let the card settle the question.

Size: small, a day.

## What to build

1. **READ_CNT** (`0x39` and one byte, the counter number 0-2; the answer is
   three bytes and a CRC - take the byte order from NXP's MF0ULX1 data sheet,
   not from memory; TS 1000-10 table 108 lists the command). Send it three times from the Type 2 transport
   (`reader/flipso_type2.c`) after the page reads, over the raw ISO 14443-3A
   poller it already uses, only when the shell says FVC 10. A NAK or no
   answer is "not readable" for that counter, not a failed read.
2. **Keep them**: three 24-bit values and a readable flag each, on `ItsoCard`
   beside `chip_abacus` - and in the capture, as a new key (e.g.
   `Counters: 00 00 05 00 00 00 FF FF FF`), so a saved card shows them too.
   Older builds skip unknown keys (PROTOCOL.md, Saved cards).
3. **Which one is #1.** For each counter, compare with the highest TS# among
   the card's value records (`value_history[].ts` across products). The
   counter that is at or just below the latest TS#, while the others sit at
   zero or elsewhere, is the ITSO counter. Show all three under the Card
   screen's Technical page, e.g.
   `One-way counter 0: 5`, `1: 0`, `2: 0`, and note which one tracks TS#.
4. **Retired.** Only once a real card - or a statement from ITSO - fixes which
   counter is #1, flag 0xFFFFFF in it as Retired (`itso_card_retired()`,
   which today covers only the CMD9 Abacus). Until then show a counter at
   0xFFFFFF as "At its limit" without calling the card retired.

## Constraints

- Each READ_CNT is a round trip with the card in the field; three extra
  commands are well within a tap, but a counter that times out must not turn
  a good read into a retry.
- A CMD9 (NTAG) also has READ_CNT for its NFC counter; ITSO does not use it
  there (the Abacus does that job), so do not read it on a CMD9.

## Tests

- `tools/test/build_card.py`: a CMD10 card whose counter 0 equals its latest
  TS# - the Technical page names counter 0; and one with a counter at
  0xFFFFFF - shown as at its limit, not retired.
- Capture round trip of the new key in `tools/test/capture/`.
- No real CMD10 card has been read. If one turns up, the counters on it decide
  step 4; record which in PROTOCOL.md.

## Done when

- A CMD10 card's three counters are read, saved and shown, with the one that
  tracks TS# identified, and Retired is flagged only on evidence.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
