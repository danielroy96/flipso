# Read CMD11 cards (ITSO Programmable Intelligent Media)

## Goal

CMD11 (TS 1000-10 clause 12) is ITSO's JavaCard-style media: an ITSO applet
with its own command set instead of a file system. Flipso lists it as
unsupported (PROTOCOL.md, Limitations: "nor is CMD11, which replaces the file
system with a proprietary command set"). But almost all of it can be read
without keys:

- The SELECT response (FCI) carries the **Shell** (tag C0), the **Directory**
  (C2) and an **OID list** (C3) - "the 16-bit OIDs of the entries within the
  DIR", which the directory's 13-bit owner fields cannot hold (table 113).
- **ReadIPE** `90 12 <entry> 00`, **ReadVRG** `90 14 <entry> 00` and
  **ReadLog** `90 16 <count> 00` work before authentication. Before a secure
  session the applet "will redact the IPE InstanceID and Seal (a total of 16
  bytes), replacing them with 0xFF" (clauses 12.4.8-12.4.9) - everything else
  comes back.

And today such a card is half read: CMD11 answers the same SELECT as CMD2
(clause 12.4.6: "This application shall respond to the same SELECT APDU as
CMD2 and CMD12"), so the CMD2 transport selects it, parses the C0 shell, then
fails to read a directory it looks for in ISO 7816 files and returns success
with the shell alone (`reader/flipso_cmd2.c`, the `flipso_cmd2_read_directory()`
failure branch). The user sees a card number and nothing else.

No CMD11 card has been seen. Build it from the spec, with a synthetic demo
card, as CMD9 and CMD10 were.

Size: medium to large, three to four days.

## Read first

- TS 1000-10 clause 12 in full: `tools/spec/itso_spec.py grep "CMD11|12\.4\." --part 10`.
- Figures 9, 10 and 11 (the FCI structures, pages 136, 137 and 142) are
  images, which text extraction cannot reach. Render the cached PDF
  (`~/.cache/flipso/itso-spec/TS_1000-10.pdf`) to PNG - there is no poppler
  on the Mac, but a short Swift script with PDFKit (`PDFDocument.page(at:)`
  drawn into an `NSBitmapImageRep` at 2.5x) works - and transcribe their tag
  layout into PROTOCOL.md.
- Table 114 onwards (page 145): the CMD11 shell's fixed values and geometry.

## What to build

1. **Branch on FVC 11** in the CMD2 transport, straight after the C0 shell
   parses (`card->fvc == 11`): CMD11 has no Parameter EF and no DFs, so none
   of the CMD2 sector reading applies. A `reader/flipso_cmd11.c` (one
   transport, one file - CLAUDE.md) called from there, sharing the CMD2 APDU
   plumbing.
2. **Directory from C2**, parsed with `itso_parse_directory()`, captured as
   the `Directory` block. **OID list from C3**: one 16-bit OID per directory
   entry; use it to fill each product's full OID where the directory's own
   field is 13 bits (`ItsoProduct::oid_extended` today comes from the IPE's EF
   flag - check TS 1000-2 clause 6.1.2 for how the two relate before
   overriding anything).
3. **Products**: for each live directory entry, ReadIPE then (when VGP is set)
   ReadVRG. ReadIPE returns "the IPE's five byte directory entry, the IPE body
   & its Instance ID with Seal" (12.4.8): strip the five-byte entry, then hand
   the rest to the decoder as the IPE group, with the VRG appended after it as
   a sector chain would deliver it. Check against `flipso_transport_read_groups()`
   (`reader/flipso_transport.c`) whether this fits as a `FlipsoGroupSource` or
   needs its own walk. CMD11 has one VRG per IPE with up to four records
   (12.4.9) - hardware anti-tear, like CMD7, so none of annex A's two-copy
   logic applies.
4. **Log**: ReadLog with P1 = 5 returns all five 48-byte records, newest
   first (12.4.10) - feed them to `itso_parse_log()`.
5. **Redacted InstanceIDs.** An InstanceID of all 0xFF is "hidden by the
   card", not ISAM `FFFFFFFF` of operator 8191: the decoder must mark it
   (`instance_valid` false, or a `instance_redacted` flag), and the screens
   say "Created by machine: Hidden by the card" rather than naming an
   operator. The same for the value records' InstanceID. The journey log is
   not redacted.
6. **Random-MID media** (12.4.3): C0 is a pseudo-shell shared by every card of
   that operator and C6 a pseudo-MID. Recognise it (C6 present), read what
   can be read - the directory and the OID list are still in the FCI - and
   say on the Card screen that the card hides its own number. Save is
   disabled or named generically: the pseudo-shell's number is the same on
   every card of that operator, so matching on it would merge strangers.
7. **Capture and saved files**: the blocks are the ones a smartcard saves
   (Shell, Directory, Product n, Log), so a saved CMD11 decodes through the
   same path. Record the FVC so the Card screen says "CMD11".
8. **Card screen**: "Card type: Programmable media (CMD11)" in
   `format/flipso_format_card.c`, beside the other FVCs.

## Constraints

- Read only. Never send AUTHENTICATE, an Update*, or EndSession.
- Memory: a new transport is code in RAM. The ReadIPE response for one IPE
  is at most 247 bytes (12.4.11); reuse the CMD2 transport's buffers rather
  than adding new ones, and measure `flipctl size`.
- Remove "nor is CMD11" from PROTOCOL.md's Limitations and say it is decoded
  from the specification alone, as CMD9 and CMD10 are.

## Tests

- `tools/test/build_card.py` / `tools/test/itso_build.py`: an FCI with C0, C2
  and C3, ReadIPE and ReadVRG responses with the 16 redacted bytes, a ReadLog
  of five records. Exactly as long as the data says, for ASan.
- A host test of the FCI parsing and the IPE/VRG reassembly (pure C, like
  `tools/test/parse/test_cmd2.c`).
- A demo card (`tools/demo/build_demo_cards.py`) so the screens can be checked
  on the device without a real card; update `tools/demo/new_encodings.py` and
  the demo README.

## Done when

- A CMD11 card shows its products, value records and journeys, with hidden
  InstanceIDs said to be hidden, and the random-MID variant shows what it can
  without merging cards.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
