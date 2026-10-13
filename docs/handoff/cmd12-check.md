# Check CMD12 against its own tables

## Goal

PROTOCOL.md says Flipso reads "CMD12 where the layout matches" CMD7, and the
DESFire transport (`reader/flipso_desfire.c`) treats the two alike. That has
never been checked against CMD12's own definition, because its file table
(TS 1000-10 table 119, page 153) and its geometries (table 120, page 154) are
images in the PDF. They were rendered on 2026-10-13 for this brief; what they
say:

**Table 119 - application files**

| File | Holds | Read access, Std | Read access, Anon |
| --- | --- | --- | --- |
| 0 | Shell, 32 bytes | free | Key 2 |
| 1 … S-3 | Sector storage (IPE data), B bytes each | free | Key 1 |
| S-2 | Log, 192 bytes (4 × 48) | free | free |
| S-1 | Directory | free | free |
| 31 | FCI (the SELECT response's data) | free | free |

**Table 120 - permitted geometries**

| # | B | S | E | SCTL | IPE files | Chained | Log | Dir | Note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 0x40 | 0x10 | 0x08 | 0x07 | 01-07 | 08-0D | 0E | 0F | CMD7 look-alike |
| 2 | 0x80 | 0x10 | 0x08 | 0x07 | 01-07 | 08-0D | 0E | 0F | 7 large products + log |
| 3 | 0xA0 | 0x10 | 0x08 | 0x07 | 01-07 | 08-0D | 0E | 0F | 7 v-large + log (GWR compatible) |
| 4 | 0x40 | 0x18 | 0x0C | 0x0E | 01-0B | 0C-15 | 16 | 17 | 11 products + log |
| 5 | 0x80 | 0x18 | 0x0C | 0x0E | 01-0B | 0C-15 | 16 | 17 | 11 large products + log |
| 6 | 0x60 | 0x1F | 0x0F | 0x12 | 01-0E | 0F-1C | 1D | 1E | 14 products + log |
| 7 | 0x60 | 0x05 | 0x02 | 0x01 | 01 | 02 | 03 | 04 | small, one product + log |
| 8 | 0x60 | 0x07 | 0x03 | 0x01 | 01-02 | 03-04 | 05 | 06 | small, two products + log |

So CMD12 is laid out **ascending** - shell in file 0, directory in file S-1 -
where CMD7 is descending (shell in file 15, directory in file 0, TS 1000-10
table 66). The transport already copes: it tries file 15 then file 0 for the
shell, and sets `descending = (shell_fid + 1) >= sector_count`, so a shell in
file 0 maps sector n to file n. GWR's real cards are CMD7 with 160-byte
sectors (Demo 07), the geometry of profile 3, so B = 0xA0 is proven.

What is not known is whether the read gets that far. Re-render the two pages
and check the transcription above before relying on it.

Size: small to medium, a day, mostly reading and tests.

## What to check and fix

1. **Selecting the application.** Flipso selects ITSO with the native DESFire
   SelectApplication and AID `A0 02 16` (`flipso_itso_aid`). CMD12 is
   specified as selected by ISO DF name, `A0 00 00 02 16` + "ITSO-1"
   (clause 13.8.2), and nothing in clause 13 says the native AID is the same.
   If the native select fails, try the ISO SELECT by DF name (the same APDU
   the CMD2 transport sends, wrapped for DESFire) before handing the card on.
   Find out from NXP's DESFire EV2 data sheet whether an application created
   with an ISO DF name can be selected natively by its AID, and what that AID
   would be.
2. **The FCI.** On CMD12 the SELECT returns TLV FCI (clause 13.8.1): C0 shell
   (on Std media a copy of file 0), C1 capabilities (EV1/EV2/EV3 bits, bit 6
   LAST COMMAND), C2 frame size, C3 authentication algorithm, C4 pseudo-MID
   (Anonymous only). File 31 holds the same. Read C1 and show it on the Card
   screen's Technical page; C4's presence is how the Anonymous variant is
   recognised (see `cmd12-anonymous.md`).
3. **The shell search.** On a profile with S ≥ 16 file 15 is a sector-storage
   file. `itso_looks_like_shell()` should reject an IPE there and fall through
   to file 0 - prove it with a test whose file 15 holds a real IPE.
4. **File types.** `flipso_read_file()` reads only standard and backup data
   files and returns nothing for any other type. Table 119 gives sizes and
   access rights but not file types; if CMD12's log (192 bytes, four records)
   were a cyclic record file, Flipso would read no journeys. Look for the type
   elsewhere in clause 13 and handle a record file if it is one.
5. **Profiles 7 and 8**: S = 5 and 7. Check every place that assumes a
   minimum sector count (the CMD2 transport refuses `sector_count < 4`; make
   sure the DESFire path is happy with 5).
6. **FVC 12 everywhere**: the Card screen names CMD12, the capture saves it
   like CMD7, and PROTOCOL.md's "where the layout matches" becomes a
   statement of what was checked.

## Tests

- Host: a synthetic CMD12 card for profiles 1, 4, 6 and 7 through the same
  decode the DESFire transport's blocks go through (`tools/test/parse/`), with
  the shell's FVC 12 and the geometry from table 120.
- The transport itself cannot run on the host; if the select or the FCI
  changes, cover the TLV parsing with a pure-C test as `test_cmd2.c` does.
- No real CMD12 card is known to have been read. Ask on the user's behalf -
  never tap a card without a green `flipctl arm` - only if one turns up.

## Done when

- PROTOCOL.md describes CMD12 from its own tables, the transport selects and
  reads every Std profile, and the tests hold it to that.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
