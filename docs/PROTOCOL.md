# Flipso protocol and data reference

How Flipso reads an ITSO card, which parts of the specification each field comes
from, and where the bundled reference data comes from.

Contents:

- [Oyster cards](#oyster-cards)
- [Operator names and card branding](#operator-names-and-card-branding)
- [Station names](#station-names)
- [Bus stop names](#bus-stop-names)
- [How it reads the card](#how-it-reads-the-card)
- [Specification references](#specification-references)
- [Blocking](#blocking)
- [Integrity](#integrity)
- [Product coverage](#product-coverage)
- [Limitations](#limitations)

## Oyster cards

A London Oyster is a MIFARE DESFire card, but not an ITSO card: what is on it is
Transport for London's own scheme, in TfL's own application (AID `4F5931`, which
spells `OY1`), under TfL's own keys. There is no ITSO Shell to decode and no way
to read the balance or the journey history without those keys.

Information we can read:

- the chip — DESFire generation, storage size, free memory, hardware and
  software versions;
- its 7-byte UID, batch number, and the week and year it was manufactured;
- every application on the card
- every file in the Oyster application (although these are all encrypted)

Any other DESFire that has no ITSO application gets the same description of the
chip and its applications, offered as **Card details** from the "Not an ITSO
card" screen. And an ITSO DESFire read ends by asking the same questions, so
its Card screen names the chip, its UID and when it was made.

A card that answers the ISO 14443-3 wake-up but will not activate as ISO
14443-4 goes to the Type 2 transport below instead, since an Ultralight-class
tag may be an ITSO paper ticket. A MIFARE Classic cannot be an ITSO card Flipso
reads, so after about a second of that the scan stops and says **Unsupported
card**, rather than waiting on it for ever.

## Operator names and card branding

ITSO operator IDs/names aren't published anywhere. I've made a start using 
cards in my possession/cards I could borrow off friends/colleagues.
The rest come from two public documents that happen to name real OIDs: Rail
Settlement Plan's RSPS3002, whose worked examples use real organisations, and
table 1 of Harley Watson's 2019 dissertation on ITSO, which lists the OID of
each of eighteen UK cards the author collected. Each entry cites its source.

`itso/itso_operators.c` holds one table with two columns for each OID:

- the **name** of the organisation, shown against every product that OID owns;
- the **brand**, the name the card is sold under, which titles the menu.

They are separate columns because they are rarely the same words, and because
they are true of different things. A name is true of the operator wherever its
OID turns up — a season ticket sold by one operator sits happily on another's
card. A brand is only true of the **shell owner**, the OID in the Shell
Environment Data Group (TS 1000-2 clause 4), which is the operator that issued
the card the user is holding. So the menu title comes from `card.oid` and never
from a product's OID - with one exception, the compact-shell paper ticket, whose
shell OID is a generic one; see [Type 2 tags](#type-2-tags-cmd4-compact-page-media).

Most entries have no brand: the operator owns products on other issuers' cards
but issues none of its own, or we have simply never seen one of its cards. A
brand is only filled in for an OID whose card has been read and whose printed
branding was checked against it. A card titled with someone else's scheme is
worse than one titled "ITSO Card".

One OID can outlive the brand it issues. OID 109 was read from a "South West
Trains Smart" card and from an "SWR Touch" card: South West Trains was the
precursor to South Western Railway, the OID carried across the rebrand, and
nothing in the shell says which of the two is printed on the card in hand. The
brand there is the current scheme, which is right for cards being issued now and
wrong for legacy stock. So a brand is a good guess at what the card says, not a
fact the card asserts, and an entry whose OID spans two schemes says so in its
comment.

Users can add or correct either column without a rebuild, in
`/ext/apps_data/flipso/operators.txt` — see `operators.example.txt`. A line that
gives a name but no brand overrides the name only, so correcting one column does
not silently discard the other.

## Station names

Rail locations are stored on the card as a four-character National Location
Code (LocDefType 203, or 208 with a UIC country code in front of it). The
packaged table maps those to station names and fare group names — see
`tools/stations/FORMAT.md` and `tools/stations/SOURCES.md`.

## Bus stop names

Bus locations come from NaPTAN, the Department for Transport's register of
public transport access points, and ITSO carries them two different ways.

**AtcoCode** (LocDefType 211) is the easy one: up to twelve ASCII characters,
`1800ALTRNHM0`, stored whole. It is a key into the register as it stands.

**NaptanCode** (LocDefTypes 206, 212 and 216) is not. A NaptanCode is eight
characters such as `cumfatda`, and TS 1000-1 clause 4.2.4.3.4 packs it into four
bytes of BCD by **folding its letters onto a telephone keypad** — table 28 maps
`ABC`→2, `DEF`→3, `GHI`→4, `JKL`→5, `MNO`→6, `PQRS`→7, `TUV`→8, `WXYZ`→9, upper
and lower case alike. A code shorter than eight characters is right-justified
with leading zeros.

So what the card holds is a number, and the mapping is **lossy**. `cumfatda`
(Park Road, Heathwaite) and `cumdatda` (Brook House Farm, Matterdale End) both
fold to 28632832, and nothing on the card says which was meant. The letters
cannot be recovered, which is why a bare 206 location can only ever be shown as
`Stop 28632832`.

The way back is to fold the register the same way and key the table on the
result. That is what `tools/naptan/build_naptan.py` does. Across the full
register the fold is very nearly injective — about 770 of 408,000 folded keys
collide, roughly one stop in five hundred — and a colliding key keeps the first
stop and is counted at build time.

The table is the whole register — around 390,000 active stops, about 20 MB,
which is two orders of magnitude larger than the station table. It ships ready
built in `data/` and is read from `/ext/apps_data/flipso/naptan.dat`, copied to
the card rather than packaged into the `.fap`: at that size it would be
re-uploaded over USB on every install, and an interrupted transfer leaves a
`.fap` the loader rejects as "invalid file". `data/README.md` covers installing
it, `tools/naptan/FORMAT.md` the layout, `tools/naptan/SOURCES.md` the licensing.

Both tables stay on the SD card and are binary-searched in place, so neither
costs memory that grows with the number of entries, and a stop that is in
neither shows as its bare code.

## How it reads the card

ITSO defines several customer media, and they do not share a command set, so
Flipso has three transports. The detect stage decides which apply: a card that
speaks ISO 14443-4 is tried as DESFire and then ISO 7816, and a Type A card that
does not is read as a Type 2 tag. The two paths are kept apart rather than tried
in turn, because a 14443-4 poller started on a Type 2 tag sends a RATS it can
never answer and polls for ever. The switch is invisible: the card stays on the
reader and the user sees one scan.

### DESFire (CMD7, and CMD12 where the layout matches)

1. Select the ITSO application (AID `16 02 A0`).
2. Read the **ITSO Shell Environment** (file 15) — card number, expiry, geometry.
3. Read the **Directory** — one 5-byte entry per product, plus the log entry.
4. Follow the **Sector Chain Table** to read only the files that actually hold a
   product, concatenating chained sectors.
5. Read the **cyclic log** for the transient ticket records.

That is typically six to ten short reads. Sector numbers are mapped to DESFire
file numbers relative to wherever the shell was found, so a card that places the
shell elsewhere still works.

If step 1 comes back with no such application, the transport tries TfL's AID
before handing the card on to the next command set, and collects the card's own
description of itself if it is there — see [Oyster cards](#oyster-cards).

### ISO 7816 (CMD2, the generic micro-processor media)

Not every ITSO card is a DESFire one. CMD2 puts the ITSO application in an
ISO 7816-4 file system instead, and such a card ignores DESFire commands
entirely. SPT's Glasgow Subway smartcard is one of these. This generic
card media is normally implemented as a cost saving measure, as DESFire
cards often cost up to 5x as much as a generic card.

1. Select the ITSO application by AID (`A0 00 00 02 16` + `"ITSO-1"`).
2. Take the **Shell Environment** and the **Parameter EF** from the File Control
   Information the select returns, when the card includes them — many do, which
   makes the first two data groups free.
3. Read each logical sector as `EF 0001` of `DF 0100+n`, using the path to the
   ITSO DF that the Parameter EF advertises so a sector costs one SELECT and one
   READ BINARY rather than three commands.

Two structural differences from DESFire follow from CMD2 using *software*
anti-tear rather than the hardware anti-tear DESFire backup files provide:

- The last two sectors hold two **copies of the Directory** rather than a
  Directory and a log. Flipso reads both and takes the one whose sequence number
  is newer, wrapping at 255 as TS 1000-2 clause 5.1.6 requires.
- The **cyclic log** therefore has no reserved sector. It starts at the sector
  its directory entry names, like a product, but holds one 48-byte record per
  sector, the second in the sector the Sector Chain Table links to - see the
  CMD9 section below, which shares the layout.

Geometry is read from the shell rather than assumed. Real CMD2 cards do not
necessarily use the defaults in the specification: the Subway card reports
80-byte sectors, 64 of them and 16 directory entries, against defaults of 48, 32
and 8, which among other things widens each Sector Chain Table entry from five
bits to six.

### Type 2 tags (CMD4, compact page media)

SPT's Glasgow Subway paper tickets are ITSO too, on an NFC Forum Type 2 tag - a
MIFARE Ultralight-class chip (an Infineon my-d on the tickets seen so far).
There is no application to select and no file system: the tag is sixteen
4-byte pages, read with the Type 2 READ command (`0x30`, four pages at a time),
and TS 1000-10 section 5 puts each data group at a fixed page:

| Pages | Bytes | Holds |
| --- | --- | --- |
| 0-2 | 0-11 | chip serial, check bytes, lock bytes |
| 3 | 12-15 | IPE dynamic data, one-time-programmable |
| 4-5 | 16-23 | IPE dynamic data, rewritable |
| 6 | 24-26 | **Compact ITSO Shell** |
| 6-7 | 27-31 | the single IPE Directory Entry |
| 8-9 | 32-39 | IPE InstanceID |
| 10-13 | 40-55 | IPE static data |
| 14-15 | 56-63 | Seal |

The read goes over the raw ISO 14443-3A poller rather than the firmware's
Ultralight poller, because a my-d is Ultralight-command compatible without
always being identified as an Ultralight. Reads continue until the tag refuses
one, and what came back is classified by what sits at page 6:

- **fewer than 64 bytes** - less than the smallest Type 2 tag holds, so the
  ticket left the field mid-read. That is a failed read and is retried; a
  half-read ticket is never shown or saved, which matters because a save would
  replace the good copy of the same ticket.
- **a compact shell** - a CMD4 ticket, decoded as below.
- **a full shell** - an ITSO card on the other Type 2 media, CMD9 (NTAG215/216)
  or CMD10 (Ultralight EV1), read as the next section describes. A full shell
  whose FVC is neither 9 nor 10 is some media definition this build does not
  know, and is called an ITSO card Flipso cannot read yet, rather than not ITSO
  at all.
- **neither** - not an ITSO card.

A **Compact ITSO Shell** (TS 1000-2 clause 4.2) stores only ShellLength,
ShellBitMap, ShellFormatRevision and FVC - three bytes, `18 01 04` on every
ticket. Everything else is implied by the CMD (TS 1000-10 table 42): IIN 633597,
OID 8189 (reserved for compact shells), ISSN 0, no expiry, one 32-byte sector,
one directory entry and no Sector Chain Table. So every such ticket has the same
card number, `633597 8189 0000000 3`, and the shell owner names no operator. The
ticket's one product does: its owner, SPT's OID 8323, stands in for the shell
owner as the card's issuer, and so titles the menu "SPT Subway" and fills the
Card screen's first page. A saved ticket is named and matched on its chip
serial for the same reason - the card number would be identical on all of them.
The Card screen marks the number as shared, shows the chip serial and its maker
(the first byte of the UID: `04` NXP, `05` Infineon - every real Subway ticket
so far is Infineon), and leaves out the implied key set, geometry and expiry,
which the card does not hold.

That is all the chip says about itself. An Ultralight-class chip has no
equivalent of a DESFire's GetVersion - no batch, production week or storage
report - so the rest of the Chip page comes from the page memory itself:

- **Memory**, the bytes the tag gave up before refusing a read: 64 on a CMD4.
- **Locked pages**, from the two static lock bytes in page 2 (the MIFARE
  Ultralight layout: lock byte 0 bits 3-7 lock pages 3-7, lock byte 1 bits 0-7
  lock pages 8-15). Locking is one-way. TS 1000-10 clause 5.10.2 requires an
  issued CMD4 to lock pages 6-13 - shell, directory entry, InstanceID and IPE
  static data - so the screen says whether it did, and lists any of those still
  writable. Real Subway tickets have `C0 3F`: exactly pages 6-13.
- **Lock bits frozen**, from the three block-lock bits in lock byte 0, which fix
  the lock bits for page 3, pages 4-9 and pages 10-15. None are set on the
  tickets seen so far.

Nor does the shell say whether the ticket is still good - it never expires and
cannot be blocked - so a paper ticket's state is its product's: blocked, expired,
used up (no rides or passes left) or active, on the Summary and the Card screen
alike. The product has no Sector Chain Table to give it a status either, so it
claims none, with one exception: TS 1000-10 clause 5.16 blocks a CMD4 product by
setting its **Seal** to all zeros, and Flipso reads that as blocked. The
**InstanceID** in pages 8-9 has the full IPE's structure, so the product's
Technical page names the ISAM that sold it - on the real tickets, one
registered to SPT.

The ticket itself is a **Space Saving IPE** (TS 1000-5 clauses 2.14-2.16): a
fixed field sequence rather than a bitmap of optional elements, split across the
static, rewritable and OTP regions above. Reassembled in that order - static,
then pages 4-5, then page 3 - it is the dataset TS 1000-5 table 48 defines.
The three types share that shape: 16 static bytes, 8 rewritable and 4 one-time
programmable, agreeing on the first 31 bits and on where the pass flags and the
area sit.

- **TYP 27, the Period ticket**, is a Subway day ticket; confirmed field for
  field against real tickets read on 2026-09-27.
- **TYP 29, the Multi-Use ticket**, is a Subway single or return (revision 1:
  a carnet of single tickets, or coupons). Checked against Ryan Murphy's
  published dump of 21 Subway tickets (blog.ry4n.org, 2022), whose fields map
  onto it exactly - and the test builder, written from the spec, reproduces his
  return byte for byte. What he found by hand is, in ITSO's terms:
  - his "journey type" byte is the directory entry's TYP and subtype
    (`A0`/`A2`/`A7` are TYP 29 subtypes 0, 2 and 7; `60` is TYP 27);
  - his "in/out" and "journeys left" bytes are the 3-bit TYP29UsageRecCode
    (boarding or alighting, and a LocDefType of 200 plus two bits) and the
    13-bit QtyRemaining, which counts *up* from 8191 minus the rides bought;
  - his "gate" and "station" bytes are UsageRec, a LocDefType 202 fare stage:
    a 3-byte machine number (the gate) and a stage number, which on SPT's
    tickets is the station, 1-15 anticlockwise from Govan - so Flipso names it
    from the Subway station table;
  - his one-time-programmable counter (`3F`, `7F`, `FF`) is the
    ScaledQtyBackup, a bit per ride set from the bottom of page 3 up. Flipso
    counts the bits left unset, times the ScalingFactor's multiplier (table
    58b), and says whether that agrees with QtyRemaining; on all three of his
    single and return tickets it does.
  Revision 2, multi-leg journeys, is decoded from the spec alone.
- **TYP 28, a carnet of day passes**, is decoded from the spec alone: six
  5-bit ticks in the OTP page record the days passes were used, as days before
  the directory expiry. The pass on the day of expiry spends no tick (clause
  2.15.2), so it is counted among the passes left. No Subway ticket of this
  type has been seen.

Every element a Space Saving IPE carries is shown, default or not - the house
style for a medium this small. A few read differently from a full ticket's:

- **Area.** The top nibble of GeoValidity / AreaValidity picks a reference fare
  code, a fare value or a location (tables 50, 53, 57). A fare code is the
  operator's own, so even code 0 - which every Subway ticket carries - is shown
  as "Set by the operator" with the code, not as the whole network it happens to
  mean on the Subway. A location is a LOC4 (TYP 27's 100 bits) or a LOC3 (TYP 28
  and 29's 68): the nibble is LocDefType less 200, and four-byte slots follow
  for an origin, a destination and in a LOC4 a via (TS 1000-1 clauses
  4.2.4.2.3-4). Each slot is decoded as the LOCE a LOC2 would hold, except that
  a fare stage's destination and via are bare stage numbers on the origin's
  machine, and a zone map fills four bytes rather than three. A location with
  only its first slot is shown as the area; one with more, as From, To and Via.
- **Seq#.** Bitmap bit 4 would add a one-byte sequence number after the
  dataset, but a CMD4 has no room for it and no anti-tear to need it
  (TS 1000-10 table 46), so no CMD4 ticket carries one.
- **Ends at.** ExpiryTimeFlag clear is 23:59; set, it is a time the owner
  configures in its readers, which Subway day tickets use.
- **Events.** TYP 27 carries two EventTypeCodes, Event1 and Event2, which the
  spec does not order, so both are shown as numbered. A used Subway day ticket
  has `0` and `12`, tap out.
- **Journeys that day.** A TYP 29 revision 2 ticket's DailyJnyCounter counts the
  day its latest journey began, which is not necessarily today, so it is shown
  under that journey's start time rather than as today's count.
- **Re-use wait.** PassbackTime 0 means the reader's own rule applies, and is
  shown as that; this holds for every IPE that carries it.

### Type 2 tags with a full shell (CMD9, CMD10)

TS 1000-10 defines two more Type 2 media, which carry a full shell, a real
directory and the same IPEs a smartcard does: **CMD9** on an NXP NTAG215 or
NTAG216 (section 10) and **CMD10** on a MIFARE Ultralight EV1 MF0UL51 (section
11). The two are laid out identically - figures 4.1, 4.2 and 7 - and differ
only in the chip and the FVC. There is still no file system: logical sectors
sit at fixed pages, and are read with the same `0x30` READ as a CMD4 ticket.

| Pages | Holds |
| --- | --- |
| 0x00-0x03 | chip serial, lock bytes, one-time-programmable page (a CMD9's Abacus) |
| 0x04-0x0B | the Shell Environment, **rotated** by one byte |
| 0x0C-0x15 | Directory copy A: logical sector S-2 |
| 0x16-0x1F | Directory copy B: logical sector S-1 |
| 0x20- | logical sectors 1 to 6, B bytes each, in order |

The geometry is fixed, and neither CMD allows it to be overridden (clauses
10.11.5 and 11.14.5; tables 104, 105, 109, 110): **S = 9, E# = 2, SCTL = 3**,
and **B = 64** on an NTAG215, **128** on an NTAG216 or Ultralight EV1. So a
card holds one IPE (E1) and the Log Directory Entry (E2), and its ITSO data
ends at page 0x80 or 0xE0. Flipso takes which chip it is from the FVC and B,
and refuses a shell stating any other geometry, because the page map is only
true of this one.

**The rotated shell** (clauses 10.11.3 and 11.14.3, figures 5 and 8). The
first byte of a shell holds ShellLength and the two top bits of the bitmap,
which are always zero; the media move it from the front of the 32-byte block
to the back ("Len" on page 0x0B), so every other element sits one byte early
and the FVC lands on page 6 byte 2 - the byte a CMD4's FVC occupies. A POST
reads page 6 to tell the family apart, and so does Flipso, but clause 10.24.1
warns that a byte in that position is a weak signal: Flipso puts the shell back
together and holds it to ITSO's IIN before calling it one. The SECRC is over
the shell in its ordinary order, and verifies on the rebuilt bytes.

**Software anti-tear** (clauses 10.18 and 11.20, annex A). There are no backup
files as on a DESFire, so everything that changes is kept twice:

- **Two Directory copies**, as on CMD2. The one with the newer DIRS# is live,
  FF to 00 counting as a step forward (TS 1000-2 clause 5.1.6); annex A.3.1.2
  starts A at 00 and B at 01. A copy that is all zeros - torn, or never written
  - loses to one that is not. The Seals that decide a tie in a POST cannot be
  checked without keys.
- **Two copies of every Value Record Data Group**, chained after the IPE in the
  order current then previous (annex A.3.2.1). A transaction is written into
  the previous copy, which is then relinked in the Sector Chain Table as
  current - so the copies hold **alternate records**, odd TS# in one and even
  in the other (A.3.2.3), and a product's history is only whole with both.
  Flipso reads the live record from the copy the chain names first and the
  rest of the history from both. A record in the previous copy newer than the
  live one is a transaction torn before the directory was relinked, which a
  POST overwrites (A.3.2.4.1), so it is neither live nor history. If the
  current copy holds nothing, the previous copy is what a POST falls back on
  (A.3.2.4.3), and so does Flipso. The previous copy is told apart from
  anything else a chain might run into by starting in the sector after the
  current copy ends and carrying the same VGBitMap (A.3.2.2); the same applies
  to CMD2, which uses annex A too.
- **The cyclic log** keeps one record per sector rather than a chain of them
  (TS 1000-2 clauses 2.4.8 and 5.1.5.5; annex A.3.3): record T0 at the start of
  sector 2, the sector E2 names, and T1 in the sector SCT(2) names - sector 3,
  "Log File B" - whose own SCT is 0. A record is 48 bytes and a sector 64 or
  128, so Flipso takes the first 48 of each; Record Offset then indexes them as
  it does a DESFire log. **This is also how CMD2 reads its log now.** Its
  default B is exactly 48, where concatenating the chain came out the same,
  but SPT's cards have 80-byte sectors, where a 48-byte stride would have
  read T1 from the middle of a sector.

The IPE itself can run from sector 1 into sector 4, "IPE optional second
sector" in figure 4.1, and is chained there like any other.

**What the chip pages say.** Pages 0-2 are laid out as on a CMD4: the 7-byte
serial less BCC0, then the two static lock bytes, which lock pages 3 to 15 and
freeze the lock bits in three blocks. Clause 10.23.1 recommends `F7 0F` - the
shell's pages 4-11 locked and the lock bits frozen, leaving the directory that
starts on page 12 writable - and the Card screen says whether the shell pages
are locked. The dynamic lock bytes (page 0x82 or 0xE2) and the configuration
pages lie past the ITSO data and are not read.

A **CMD9's Abacus** (clause 10.24.4, table 107) is bytes 1 and 3 of the OTP
page 3, and its value is the count of bits set: 1 as delivered, then one more
per value record written, since each value record's TS# must be at least the
Abacus - which is what stops an old copy of the card being written back.
Sixteen bits set means the card is **retired**, and a POST rejects it (clause
10.24.2); Flipso shows the uses left, 15 less the count, and a retired card's
status as Retired. A **CMD10** keeps the same count in the Ultralight EV1's
one-way counter #1 instead (clause 11.26.4), which only READ_CNT reaches, and
which Flipso does not read: the clause does not say whether "counter #1" is
the chip's counter 0 or counter 1, and no real card has been seen to settle it.

**Reading.** Every page has unconditional read access (clauses 10.14 and
11.17): the NTAG password protects writes only, so no page Flipso reads can
NAK for want of PWD_AUTH. A read runs in two passes: the first reads until the
tag refuses or 256 bytes, which is past a CMD4's memory and past the 128 bytes
of shell and directories, and classifies the tag; the second continues to the
end of sector 6, which the shell has by then said is at 512 or 896 bytes.
Neither pass comes near the end of the chip's memory, where a READ that crosses
the last page wraps round to page 0 and one that starts past it is refused, so
how the chip ends its memory does not matter here. Every sector is on the chip
whether used or not, so a second pass that stops short is a card that left the
field, and is retried rather than shown half read.

A read is **saved** as the blocks a smartcard is - Shell (put back in order),
the live Directory copy, Product 1 and Log - plus a **Tag** block of pages 0-3,
rather than as the page memory. That way it goes through the same decoder and
the same history merge as a smartcard, and a value record that has moved from
the current copy to the previous one is recognised as still on the card, not as
new or rolled off.

## Saved cards

A saved card is the raw blocks of the read above, not the decoded fields: the
Shell Environment, the Directory, each product's concatenated sector chain, and
the cyclic log, exactly as they came off the card. Loading one skips the two
transports and runs the same parsers over the same bytes, in the order a live
read runs them, so a saved card and the card itself produce the same screens.

Storing the decode instead would have frozen each card at the build that wrote
it. Storing the bytes means a decoder fix applies retrospectively to cards
already on the SD card, and that nothing new has to be serialised when a field
is added.

The file is the firmware's key-value text format, one key per block, in
`/ext/apps_data/flipso/cards/`:

```
Filetype: Flipso card
Version: 1
Read at: 1758400000
Shell: 18 11 63 35 97 ...
Directory: 00 21 13 48 40 ...
Product 1: 18 01 FF 00 F7 ...
Product 2: 2C 42 FF 00 00 ...
Log: 14 02 00 DB EE 5A ...
Chip: 04 01 01 01 00 18 05 ...
```

A CMD4 paper ticket is saved as a single block of its whole page memory
instead, `Type 2: 05 79 76 82 ...`, and decodes from that alone. Its identity
for matching a saved file is the chip serial rather than the card number, which
a compact shell shares with every other ticket. A CMD9 or CMD10 card is saved
as the blocks above, plus `Tag: 04 63 08 2E ...`, its chip pages 0-3.

`Chip` is the one block that is not part of the ITSO shell: a DESFire's
GetVersion reply (seven bytes of hardware version, seven of software, the UID,
batch number and production week and year) and then GetFreeMemory's three
bytes, when the card answered it. It is what lets a saved card's Card screen
name its chip as a live read does. A CMD2 card has no such block, and neither
does a file saved before Flipso 1.3 kept it.

`Product n` is keyed by directory entry E(n), which is how a block is matched
back to the product it belongs to; the sector chain behind it has already been
followed and concatenated, so the file holds no chain of its own. Keys the build
does not recognise are skipped rather than rejected, so a file from a later
Flipso loses the blocks it does not know about and no more. A `Version` newer
than the build's is refused outright, because a block that has changed shape
would decode to plausible nonsense.

Saving a card that has been saved before rewrites that file rather than adding
another. The match is on the 18-digit card number in the shell, which is the
only unique identity a card has - there is no serial number anywhere else in
the shell, and the file name belongs to the user rather than to the card. Only
each candidate's header is read to find it, as far as its `Shell` key, because
nothing after that says which card the file holds.

Rewriting is the risky moment, because the old file may hold journeys that
exist nowhere else any more. So the new file is written whole to
`<name>.flipso.tmp` first and renamed over the old one only once it is complete;
a write that fails - a full SD card - fails before the record is touched, and
removes its temporary file. So does a rename that fails: the old record is
never removed to make way for a second try, which would leave nothing if that
failed too. Renames guard the same way: firmware 1.4's rename
replaces whatever holds the destination name, so Flipso refuses one that would
land on another card, and changes only the case of a name by way of a free one.

`tools/test/replay.py` reads these files, so a saved card is also a decoder test
case that needs neither the Flipper nor the card.

### A saved card remembers more than the card does

Everything a card tells you about its own past is a rolling window. The DESFire
cyclic log has four slots and the Value Record Data Group of a product has two,
and each new record is written over the oldest. Read a card in March and again
in June and the June read cannot see the March journeys: they are gone from the
card, and the only place they still exist is the file written in March.

So a re-save merges rather than replaces. The records the file holds and the
read does not are kept alongside the read, under two further keys:

```
Log history: 14 02 00 DB EE 5A ...
Value history 1: 40 64 EE 5E 09 ...
```

Both hold **raw records**, exactly as some earlier read found them — 48 bytes
each for a tap, 15 for a value record — so a saved card stays a record of bytes
rather than of decisions. They go through the same decoders the live blocks do,
and the build that is running decides what they mean.

A few things that matter about the merge:

- **Records are matched byte for byte.** A record is written once and never
  altered, so a record still on the card is the same bytes in the file, and the
  ones that are not in the new read are exactly the ones that have rolled off.
  Nothing has to be decoded to work that out.
- **The `Version` stays 1.** Nothing existing changed shape; two keys were
  added, and unknown keys were always skipped rather than rejected. So a build
  that predates this reads such a file, loses the history and shows the card.
- **The caps are set by what the decoder can show**: eight tap records, which
  with a full log fills `ITSO_MAX_TAPS` exactly, and eight value records per
  product. What falls off the end of the file is what would have fallen off the
  end of the screen.
- **Value records only carry forward for an unchanged directory entry.** Entry
  numbers are reused: a ticket that expires and is replaced leaves its slot to
  another product, and the old one's transactions are not the new one's. The
  five entry bytes - owner, type, subtype, VGP and expiry - are what decide it.
  The journey log is the card's rather than a product's, so it always carries.
- **The live blocks are decoded first**, so the card is the authority on what it
  holds now and the file only fills in what has since rolled off. That is also
  what keeps the card's own newest record flagged as the latest tap.

### And products the card has dropped

A directory entry is not the product's for good. When a ticket expires and is
removed the entry is freed, and the next product sold takes the slot - so the
Directory Data Group is only ever a statement about the card as it is now, and a
read taken after that has no way of knowing a ticket was ever there.

A record that saw it does. Alongside the record histories, a re-save carries the
whole product forward under a third key:

```
Product history 100: 69 0F 1C 00 01 40 64 EE 5E 09 ...
```

The block is the IPE group exactly as that read assembled it, behind a ten-byte
header - four bytes of Unix time for the read that last found the product on the
card, one for the directory entry E(i) it held, and the five bytes of IPE
Directory Entry that described it. The header is the only part of any block in
the file that is not card bytes, and it is there because a product the card has
forgotten has nothing else to say when it was last true, and because the
directory that numbered it is not the one in the file any more.

The key is a **history slot**, numbered from 100, rather than a directory entry:
the entry is the one thing about a gone product that is no longer its own, and a
ticket that expired and was replaced shares its number with whatever took the
slot. `Value history 100` then holds that product's own archived transactions,
keyed by the same slot for the same reason. 100 is clear of any entry number a
shell can carry, so old and new keys cannot collide.

What counts as gone is the entry bytes changing, which is the test the value
records already used: same owner, type, subtype, VGP and expiry, or it is not
the same product. A renewed season ticket therefore leaves its old self behind
here in the same way a replaced one does - both are products the card used to
hold and does not now.

Four are kept, which is what `ITSO_MAX_HISTORIC_PRODUCTS` is for and what the
product list has room to show; the newest by last-seen date win. A read that
lost its Directory block keeps none, because a read that could not see the
directory is not a card that has shed its products, and from here the two look
alike.

The decoder appends them to `ItsoCard::products` after the live ones, with
`on_card` false and `last_seen` set, so a screen walking the array in order
shows the card before it shows the card's past. `ItsoValueRecord::on_card`
carries the same distinction one level down: true for a record read out of the
group the card just offered, false for one that only a file remembers.

Because the merge happens before the "update this card?" screen rather than
after it, that screen can say what the update is worth - how many journeys and
transactions are new since the record was written, how many older records are
being carried forward, and how many products the card no longer carries. A read
that adds nothing says so.

## Specification references

Field offsets are taken from ITSO TS 1000 version 2.1.5 (March 2025), published
by ITSO Ltd under the Open Government Licence:

- **Part 1** — data types (`DATE`, `DTS`, `VALC`/`VALS`) and location definitions
- **Part 2** — Shell Environment, Directory, IPE, Value Record, Log Directory Entry
- **Part 5** — per-IPE-type datasets and the Transient Ticket Record
- **Part 10** — the customer media definitions: clause 3 for CMD2, clause 5 for
  CMD4, clause 8 for CMD7, clauses 10 and 11 for CMD9 and CMD10, and annex A
  for the software anti-tear CMD2, CMD9 and CMD10 share. The CMD9 and CMD10
  page maps (figures 4.1, 4.2, 5, 7 and 8) are images in the PDF, which text
  extraction does not reach

Date encoding comes in two forms:

- `DATE` is a 14-bit count of days from 1997-01-01, and a stored **zero means the
  top of the range** (10/11/2041), which schemes use as "no expiry".
- `DTS` is a 24-bit **two's complement** count of minutes from an epoch of
  2028-11-24 20:16, not an unsigned offset from 1997.

In the decoder they are `ItsoDate` and `ItsoDts` (`itso/itso_types.h`), and
what `itso_date_to_unix()` and `itso_dts_to_unix()` make of them is an
`ItsoUnixTime` - seconds since 1970, as the Flipper's clock and a saved card's
read time count. All three are plain integers, a DTS and a Unix time both 32
bits, so the type a field or parameter is declared with is what says which it
holds.

## Blocking

Two different things in the shell are called "blocked", and they are read from
different places.

A **product** is blocked when its Sector Chain Table terminator is S-2 rather
than S-1 (TS 1000-2 clause 5.1.4). That retires one ticket and leaves the rest
of the card working, and it shows as **Blocked** at the end of the product's
row.

A **shell** is blocked by the low bit of `DIRBitMap` in the Directory Data Group
(TS 1000-2 clause 5.1.2), immediately below the two log-configuration bits. That
retires the whole card: a POST rejects it regardless of what products it holds
or how long they have left to run. Flipso therefore treats it as the headline
fact about a card rather than one field among many — the menu is titled "Blocked
Card" with a warning triangle in place of the card's branding, the Card screen
opens with a banner explaining it, and the scan ends on the error tone rather
than the success one, because the sound is the whole result for anyone not
looking at the screen. A shell that is not blocked says so, since silence would
otherwise cover both "the issuer is happy with it" and "the directory never
decoded".

Confirmed on 2026-09-19 against a pair of real cards: an English National
Concessionary Travel Scheme pass issued by Reading Borough Council, which the
holder confirmed no longer works, reads blocked, and a Freedom Pass in daily use
reads active.

## Integrity

ITSO protects data groups with **seals** — a MAC over a key held in an ISAM
(TS 1000-7). That is what makes a ticket unforgeable, and it is why Flipso can
read a card without keys while being unable to say whether what it read is
genuine. Every field on every screen is reported on the card's own word.

One check needs no key. The Shell Environment Data Group ends with a
**SECRC** (TS 1000-2 clause 4.1.15): a two-byte CRC over every element before
it, of the CRC_B variety that Annex A of the same part defines and gives test
vectors for. Flipso computes it and says on the Card screen whether it matches.

Two details are worth recording, because neither is in the clause:

- **The checksum is stored low byte first**, which is the order Annex A appends
  a CRC to a transmission in rather than anything clause 4.1.15 states. The
  other order fails every card; this one verifies all five real cards on hand,
  across four schemes and both command sets.
- **ShellLength locates it**, not a fixed offset. The table gives byte 22 or 30
  depending on whether the shell carries an MCRN, and the buffer is longer than
  the dataset in either case — a CMD7 reader gets a whole file back, not as many
  bytes as the shell says it uses. So the CRC covers `ShellLength × 4 - 2` bytes
  and the stored value follows them.

What this catches is a misread, not tampering: anyone able to rewrite a shell
can recompute a CRC. That is still worth having, because a misread is the
failure that actually happens — and a wrong geometry or a phantom byte offset
shows up here as a mismatch rather than as plausible nonsense further down the
screen.

## Product coverage

Every product is reported from its directory entry: operator, type, expiry,
expired/blocked/unused status.

Every IPE type also reports the elements TS 1000-5 puts in the same place for
all of them: the remove date, the retailer that sold the product, the optional
owner IIN, and the **IPE InstanceID** — the ISAM that created the product and
its sequence number, which is the only thing in the shell that identifies one
particular ticket rather than a kind of ticket.

Beyond that:

| Type | Decoded |
| --- | --- |
| TYP 2 — Stored Travel Rights | Balance, currency, journey legs and cumulative fare; ceiling, overdraft, auto-top-up threshold/amount/state and whether it draws on another purse - all priced in the value record's currency code, scaling included - deposit with how it was paid and its VAT; print flags; Complex Capping extension (below) |
| TYP 3 — Loyalty type 1 | Points balance (three bytes, so it does not fit a purse's two), the owner's two UserDefined bytes |
| TYP 4 — Charge to account 1 | Amount spent to date, credit limit, deposit with its VAT, validity window, priority, print flags |
| TYP 5 — Charge to account 2 | Transactions used, allowance per charge period, last reset date, the per-transaction limit in the value record's currency, deposit with its VAT, priority, print flags |
| TYP 14 — Entitlement (rev 1, 2) | Entitlement code, class, validity dates, locations and half-day validity, passback, ID flags; CPICC, HolderID and SecondaryHolderID, fare rounding rule, deposit with payment, VAT and refundability, PrintTicket |
| TYP 16 — ITSO ID (rev 1, 2) | Holder name, date of birth, gender, companion and photo flags, entitlement, class, validity dates, locations; CPICC (the concessionary pass issuer), HolderID and SecondaryHolderID, language (annex A.24), HalfDayOfWeek, fare rounding rule, deposit and card deposit with payment, VAT and refundability, PrintTicket; the language is marked as not in use when IDFlags bit 3 sends a POST to another application |
| TYP 22 — Period ticket (rev 1, 2, 3) | Validity start (DTS in rev 1–2, date and time in rev 3), from/to locations — or, when both are absent, that the area is the operator's to define — passes remaining, expiry of the active pass and of the unused stock, auto-renew and what it adds, stored-pass mode; days and AM/PM periods it is valid (ValidOnDayCode and TYP22Flags together), off-peak, transferable, end time, pass length and unit, party size, class, issue date, amount paid with payment and VAT, CPICC, validity and promotion codes, RouteCode, print flags; in rev 3, what a top-up does with expired passes (TreatmentOfExpiredSP) and the identity document it is valid only with, as a number, text or another product on the card, at the top of its screen and on the Summary |
| TYP 23 — Journey ticket (rev 1, 2, 3) | Origin, destination, rides remaining, transfers made, auto-renew, used flag, stored-ride expiry (rev 3); issue date, validity start (rev 3), end time, class, party size, amount paid with payment and VAT, photocard number, CPICC, validity and promotion codes, RouteCode, print flags, and the mode group — how rides are counted (rev 3 adds return pairs), transfer and time limits, ride value in its own currency code |
| TYP 24 — Reserved journey (rev 2) | The railcard it is valid only with, and the railcard number, on the first page and on the Summary; journeys remaining, transfers remaining (one total), part-used flag; single, return or either-way and journeys sold; outward and return portions, each a start and a period in days; origin, destination and their alternatives, Route, the station or operator that sold it; TYP24Flags (test ticket first, the others only when set, the clear ones under Technical); days it may be used and days restrictions apply, the operator it is limited to, class, party size, amount paid and how; the eight optional groups - associated products, out-of-station interchanges, break of journey and other transfers, valid times, specific trains, routing points, and under Technical the discount code, percentage and code type, and the supplement codes - the passenger's name and gender, ticket number, FTOT, restriction code and ID type; and the reserved legs of its VGXRef 3 extension: the kind of place, coach, seat or berth, which way it faces and where it is (below) |
| TYP 25 — Voucher | Vouchers remaining and auto-renew only; the dataset is not decoded ([handoff](handoff/typ25-voucher.md)) |
| TYP 26 — Tolling | Rides remaining and auto-renew only; the dataset is not decoded ([handoff](handoff/typ26-tolling.md)) |
| TYP 27 — Period ticket (space saving) | Issue date, price paid and currency, adult or child, class, passback, off-peak and weekday restrictions, expiry time, where it is valid (fare code, fare value, or a LOC4 of origin, destination and via), last use, both event codes, photocard number, the expiry offset from the directory date, the InstanceID, and blocking by a zero Seal |
| TYP 28 — Carnet of day passes (space saving) | As TYP 27 without the child flag, photocard or events, and with a LOC3 area; passes left (counting the expiry-day pass), the day each used pass was used, validity on the day of issue and of expiry |
| TYP 29 — Multi-use ticket (space saving) | Revision 1: rides or coupons left, issue date, price paid, class, restrictions, area, and where it was last used and whether getting on or off (an SPT fare stage named as its Subway station). Revision 2: journeys left, when the latest journey began with the journeys begun that day and the changes made on it, the daily journey limit, the changes allowed, passback, last use. Both: area (fare code, fare value or LOC3), expiry time, the ScaledQtyBackup's count and whether it agrees with the ride count, the InstanceID, and blocking by a zero Seal |

Every product carrying a value record also reports its common header (TS 1000-2
table 15): what the last transaction was, when, how many times the record has
been written, and the ISAM of the POST that wrote it.

### Whose machine it was

An ISAM ID is not an opaque serial. TS 1000-2 annex B builds it from the OID of
the operator the ISAM is registered to — the top 13 bits, with bits 18, 17 and
16 extending it into the 8192, 24576 and 57344 ranges — and a serial number in
the rest. So every ISAM on a card names an operator, and Flipso shows it:

- the **IPE InstanceID**: who sold or created the product;
- the **value record** header: whose machine last changed it;
- the **Directory InstanceID** (TS 1000-2 table 8), after DIRS#: the last
  device to change anything on the card, which is how a London Freedom Pass
  turns out to have been used on a Reading bus. Its KID - the version of the
  key the directory's seal is made with - and the shell's iteration number
  INS# (which hotlists pair with the ISRN) are shown with it;
- each **journey record's own InstanceID** — a Transient Ticket Record is an
  Orphan IPE Data Group, so it carries one after its dataset: whose gate or bus
  took the tap. SWR's gates report OID 8160, in ITSO's reserved 8001–8191
  range;
- a revision 4 record's **ENTRY group** (TS 1000-5 table 64): the ISAM and
  sequence number of the tap-in record, so a tap out names the gate the holder
  came in through.

A value record that has never been written holds ISAM zero, which is not
operator zero and is not shown.

### Fare capping

A value record group may carry a **Value Group Extension** after its records,
flagged by the LSB of VGBitMap (TS 1000-2 clause 7.5). TS 1000-5 clause 4.1
defines three; Flipso decodes the two Complex Capping ones, VGXRef 1 (reduced)
and 2 (full), which SPT's Subway purse carries: a strategy code and four
accumulator sets, each with its rule (day, n days, m days), what has been spent
towards the cap, what the fares would have been uncapped, the day count of a
multi-day cap, the last fare, and where and when the cap was last applied.
Which of the two forms the card keeps is under Technical, since the reduced
one has a single place for all four sets and no last fare.

It is decoded when a screen asks for it rather than held in every product, since
four locations make it bigger than anything else a product carries.

A product may also carry a **Value Record Data Group**, a small cyclic store of
the parts of the product that change as it is used. It is not a purse feature —
the directory entry's VGP flag can be set on any product — and the records share
a common header (TS 1000-2 table 15) whatever the product is. What the last five
bytes of a record mean depends on the type: a purse keeps a balance there, a
journey ticket keeps a count of rides remaining. Flipso reads the header for any
product that has one, decodes those two tails, and keeps every record in the
group rather than only the live one.

Two things about value records are easy to get wrong, and both show up on an
ordinary rail ticket:

- The live record is the one with the highest **TS#**, not the latest timestamp.
  TS# counts up by one per record written (TS 1000-2 clause 7.2.4.2). The DTS
  only resolves to the minute, so a tap that spends a ride leaves two records
  stamped identically, and ordering by time picks between them at random.
- A blank record is all zeros, including its TS# and its DTS — and since the DTS
  epoch sits in 2028, an unwritten record otherwise reads as the most recent one
  on the card. Skip them.

### Reserved journeys

TYP 24 (TS 1000-5 clause 2.11, table 136) is the rail ticket that carries what a
paper one does. Only format revision 2 is defined, and it is the only one
decoded. Its value group holds one record (table 139), and a Value Group
Extension with VGXRef 3 (clause 4.1.3, table AD3) after it holds the booking
reference, where and when it was last validated, and one 32-byte entry per
reserved leg: departure, retail service ID, from, to, coach, seat, attribute,
which way the seat faces, a berth and whether its cabin is shared, and the
owner's reservation type. NumberOfReservations, in the value record, says how
many there are, and is only read when IPEBitMap bit 3 is set.

The product keeps only what its summary and list row need - the portions, the
journeys sold, the flags, the price and the first discount code, 20 bytes more
than before. The
rest of the dataset, its optional groups and its reservations are decoded from
the saved blocks when the product's screen is drawn, like the capping
extension, into an `ItsoReservation` whose groups with locations in them are
allocated to fit. So a ticket the card no longer lists (one only a saved file
remembers) shows its terms but not its reservations.

Table 136's offsets assume six-byte LOC1s - its note 1, "for UK Rail
applications" - but a LOC1 is as long as its own length byte says, and an
AtcoCode makes one eleven or more. So everything after Origin, in the dataset
and in the extension, is found by walking the locations, never by offset; the
host tests hold a ticket whose stops are AtcoCodes to that.

The specification is inconsistent in a few places, and these are the readings
Flipso takes:

- **Optional group order.** The counts come Associated, Discounts, Supplements,
  Transfers, Interchanges, but table 136 lists the Interchange group before
  Transfers. The table is the layout - it is what gives each group its size -
  so the groups are read Associated, Discounts, Supplement, Interchange,
  Transfers, Restriction1, Restriction2, Route, then PaxDetail.
- **TransfersRemaining** is 11 bits, though its comment allows "up to 3
  transfer types each with up to 511 transfers", which would need 27. It is
  read as one count and shown as the total.
- **Restriction2**'s elements come to 13.5 bytes with a six-byte LOC1 and then
  put an RFU "at 15.5", but its byte count says 14: eight bytes are read after
  the location, the last four bits RFU.
- **Table AD3**'s byte count gives 36 for the part before the reservations,
  but its elements total 19 with a six-byte LOC1. VGXLength is the bound.
- **AmountPaidCurrencyCode** is the high nibble of its byte and the payment
  method the low, the reverse of TYP 22 and 23.
- The clause numbering jumps from 2.11 to "2.12.1.1.1 IPEBitMap Definition";
  that is a slip, and table 137 is TYP 24's bitmap.
- Where the table leaves a coding undefined, Flipso reads it this way and says
  so here: a period of validity counts days on from its start, so a period of
  0 is the start day alone; TimeBandOnOutOrReturn's two bits are 01 outward and
  10 return, as BerthUpperLower's are assigned; a flag named "XOrY" -
  TimeBandOnArriveOrDepart, TimeBandIncludeExcludeFlag,
  RestrictionOrEasementFlag - means X when set, as TestOrLive is defined;
  ViaNotVia is 1 via and 0 not via; SeatDirection is 1 facing, 2 back, 3
  airline, in the order the table lists them.

#### National Rail's profile

TS 1000-5 leaves much of TYP 24 to its owner. RDG's *ITSO in National Rail
Specification* (RSPS3002, version 02-01, 2015, section 3.8) is what National
Rail does with it, and where it gives a user-defined element a meaning Flipso
uses it - TYP 24 being, in practice, rail's:

- **DiscountCode** is the railcard the ticket was priced with, a three-letter
  code such as `DIS`, or `XXXXX` when the discount came from an entitlement on
  the card. A ticket is not valid without its railcard, so the railcard leads
  the product screen and goes on the Summary as **Valid only with: Disabled
  Persons Railcard**. The names come from a table in `itso/itso_names.c`,
  compiled from SAP Concur's published rail discount codes, a list of
  fares-data railcards, and the RailUK fares guide; an unknown code is shown as
  **Discount:** and the code. A TYP 22's IdentityDocumentID is the same kind of
  requirement, and uses the same line, in the same places.
- **DiscountCodeType** is 1 a status code, 2 a discount code, 3 an entitlement
  on the card. **DiscountPercentage** is a whole percent on rail ("33.3% = 33")
  where TS 1000-5 has tenths; a discount with one of rail's three code types is
  read rail's way.
- **IdDocumentReference** is a five-digit number: the kind of ID (numbered in
  RSPS3008, which is not published), then the last four digits of the
  railcard's or photocard's number. It is shown as **Railcard number: Ends
  1372**, and the kind under Technical.
- **ProductRetailer** with bit 15 set is the retailing station's NLC, not an
  operator: five bits of first character ('0'-'9', 'A'-'V') and ten of the
  last three digits, shown as **Sold by:** and the station. TYP 22, 23 and 24
  all carry it that way (sections 3.6.3, 3.7.3 and 3.8.3), but bit 15 alone
  does not rule out an OID: TS 1000-2 table B2 gives 57344-65535 to service
  operators and retailers, and TYP 22 and 23 are bus tickets as often as rail
  ones. What does is the gap below that. Table B2's 32768-57343 "shall not be
  used", and it holds every NLC whose first character is '0' to 'N' - all of
  the numeric ones, which is every NLC the station table names. So a retailer
  in the gap is a station, and one at 57344 or above stays an operator, on all
  three types: an NLC from 'O' to 'V' would be misread as an OID, but it would
  have no station name to show either. The owner would have been a weaker
  test - the operator table has no notion of rail - and so would the
  locations, which a TYP 22 need not carry.
- **VendorLoc** is where a TYP 24 was sold, and on rail it is the same station
  as the retailer. It is shown as **Sold at:** only when it differs.
- **ReservationType** is 0 seat, 1 berth, 2 bike, 3 no place, 4 wheelchair;
  **SeatDirection** 01 facing, 10 back, 11 airline. Coach and seat are
  left-padded with spaces, which are dropped.
- **TransferEntitlementType** 2 is a break of journey, with NumberOfTransfers
  and TransfersRemaining at 511, which is shown as unlimited. The Interchange
  group holds out-of-station interchanges, and a PermittedInterchangeTime of 0
  leaves the time to the gates.
- Both portions open at 00:01, which is shown as the day, not a time.
- **AccommodationAttribute** is "4 characters from NRS data feed", the National
  Reservation System's reference data (RSPS5048), which is not published. The
  only codes from it in public view are `SEAT` and `HTMS`, quoted in RDG's
  Guide to Rail Retailing. Flipso names those, and the codes that cannot
  reasonably be read any other way - `WNDW` and `WIND` window, `AISL` aisle,
  `TABL` table, `QUIE` quiet coach, `POWR` power socket and a few more - as
  **Feature:**, and shows any other code as it stands. Treat the list as a
  best reading until a real ticket, or RSPS5048, confirms it.
- Rail uses neither the time bands, nor the train restrictions, nor the
  routing points ("DO NOT USE"). They are decoded all the same.
- The flags are mostly fixed on rail - duplicate, follow-on and warrant never
  set, companion always clear - so the product screen shows only those that
  are set, and Technical the rest.

A dataset is at most 256 bytes, which a six-bit IPELength cannot exceed, and a
group count that runs past its end stops the walk there: the groups before it
are shown, and Technical says the rest ran past the dataset. No real TYP 24 has
been seen; Demo 01 carries one built from the specification.

### The records behind the live one

The records the live one displaces are the transactions before it, and Flipso
keeps them all rather than only the newest: the balance as it was, what changed
it, and when. A card holds no statement anywhere else, so those few records are
the only history it carries.

They are shown on the **History** page of the Pay as you go and product
screens, after the last transaction, and the ones only a saved file remembers
on a page of their own, **Off card**. The journey log is split the same way:
a page to each journey, the card's own first, then the ones only the file
remembers, each titled with the clock the product list gives a dropped
product. Each
record contributes whichever of a balance or a counter its IPE type keeps in the
tail — the same field the screen shows above as the current value, decoded in one
place rather than two, because a history that disagreed with the balance above it
would be worse than no history.

The store is small: TS 1000-2 table 14 allows five records and every real card
to hand is issued with two, so a card straight off the reader shows at most one
earlier transaction. What makes the page worth having is that a saved card
accumulates them — see [Saved cards](#saved-cards).

Ordering matters more here than for picking the live record. Both records of a
journey ticket that has just been used carry the *same* DTS to the minute, so
sorting a history by time would show the ride being restored as often as spent.
TS# is the order; a wrap of the 12-bit counter takes 4096 transactions, and the
timestamp separates the two records that could then collide.

A record holds the balance *after* its transaction, never the amount, so what a
fare cost or a top-up added is worked out (`itso_value_change()`): this
record's balance or counter minus the one before it, shown signed as
**Amount:** (`-£3.55`, `+£20.00`) or, for a counter, **Change:** (`-1`). The
live record's goes on the **Last transaction** line. It is only worked out
across two consecutive writes - a TS# one ahead of the other, modulo 4096 -
because a saved card's history can skip records that rolled off between
reads, and the difference across a gap is several transactions presented as
one. The oldest record, a pair whose DTS runs backwards (a lap of the counter
apart), and a pair in different currencies show none either; nor does a TYP 5,
whose count of transactions a new charge period clears, maybe in the same
write as a fare. A TYP 4 counts spend up, so its difference is turned round to
say what the transaction did to the holder's money, as a purse's does.

Locations are rendered from the encoding the card uses: rail NLC codes, NaPTAN
and ATCO bus stop codes, zone numbers and bit maps, fare stages and service
numbers. Rail codes are resolved to station names and bus stop codes to stop
names, from the tables described above.

A decoded location holds the card's own bytes, not its text: the LocDefType, the
record's length, and the first 15 bytes of its body (`ItsoLocation`,
`itso/itso_location.h`) - 19 bytes on the device where the rendered text and
code took 45. A LOC1 may run to 255 bytes (TS 1000-1 clause 4.2.4.2.2), but no
LocDefType's element is longer than nine (table 6, 216's) bar an over-long
AtcoCode, whose first fifteen characters are shown; a 212's further stops are
counted from the length. `itso_location_text()` and `itso_location_code()`
render the text and the lookup code when a screen draws the line, so a card's
twelve taps hold none of it. The one thing the bytes cannot say - that a fare
stage on an SPT Subway ticket is a station - the decoder marks from the
product's OID when it reads the place.

LocDefType 216 carries a service number as well as a stop, so a resolved one
reads `Route 42 at High Street (adj), Hulme`: the table names only the stop
half, and without the table it reads `Route 42, stop 28632832`.
LocDefType 212 carries several stops and names the first, counting the rest.


## Limitations

- Reads DESFire ITSO cards (CMD7, and CMD12 where the layout matches), ISO 7816
  ones (CMD2), compact-shell Type 2 tags (CMD4) and full-shell ones (CMD9 on an
  NTAG215/216, CMD10 on an Ultralight EV1). The obsolete MIFARE Classic media
  definition is not supported, nor is CMD11, which replaces the file system
  with a proprietary command set.
- CMD9 and CMD10 are decoded from the specification alone: no real card of
  either has been read. A CMD10's one-way transaction counter is not read (see
  above), so a retired CMD10 is not flagged as one.
- TYP 25 (vouchers) and TYP 26 (tolling) are reported from their directory
  entry and value record alone; their datasets are not decoded. Briefs for
  each are in [docs/handoff](handoff/README.md).
- TYP 24 (reserved journeys) is decoded from the specification alone: no real
  one has been read, and table 136 leaves several codings undefined - see
  [Reserved journeys](#reserved-journeys). Its reservations and optional
  groups are not shown for a ticket the card has dropped.
- A revision 3 period ticket's IdentityDocumentID keeps its first 16 bytes of
  up to 31, and says how many more there are.
- Space Saving IPEs: the TYP 29 subtypes are shown as numbers - SPT's appear to
  be 0 adult single, 2 adult return and 7 child single, but that is inferred
  from prices, not stated anywhere. An area given as a location has been
  decoded from the spec alone: every real ticket seen carries a fare code.
- Oyster cards are recognised and described, but the data is encrypted
- MIFARE Classic and non-Type A cards are reported as unsupported
- Seals are not verified, so Flipso cannot tell you whether a card has been
  tampered with - only the shell's own checksum is checked. See
  [Integrity](#integrity).
- Read only. Flipso never writes to a card.
- The built-in operator name table is small, because ITSO does not publish its
  OID register.

