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

## Operator names and card branding

ITSO operator IDs/names aren't published anywhere. I've made a start using 
cards in my possession.

`itso/itso_operators.c` holds one table with two columns for each OID:

- the **name** of the organisation, shown against every product that OID owns;
- the **brand**, the name the card is sold under, which titles the menu.

They are separate columns because they are rarely the same words, and because
they are true of different things. A name is true of the operator wherever its
OID turns up — a season ticket sold by one operator sits happily on another's
card. A brand is only true of the **shell owner**, the OID in the Shell
Environment Data Group (TS 1000-2 clause 4), which is the operator that issued
the card the user is holding. So the menu title comes from `card.oid` and never
from a product's OID.

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
Flipso has two transports and tries them in turn. The switch is invisible: the
card stays on the reader and the user sees one scan.

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
entirely. SPT's Glasgow Subway smartcard is one of these.

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
- The **cyclic log** therefore has no reserved sector. It is an ordinary data
  group starting at the sector its directory entry names, and is chained like a
  product.

Geometry is read from the shell rather than assumed. Real CMD2 cards do not
necessarily use the defaults in the specification: the Subway card reports
80-byte sectors, 64 of them and 16 directory entries, against defaults of 48, 32
and 8, which among other things widens each Sector Chain Table entry from five
bits to six.

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
```

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

`tools/test/replay.py` reads these files, so a saved card is also a decoder test
case that needs neither the Flipper nor the card.

## Specification references

Field offsets are taken from ITSO TS 1000 version 2.1.5 (March 2025), published
by ITSO Ltd under the Open Government Licence:

- **Part 1** — data types (`DATE`, `DTS`, `VALC`/`VALS`) and location definitions
- **Part 2** — Shell Environment, Directory, IPE, Value Record, Log Directory Entry
- **Part 5** — per-IPE-type datasets and the Transient Ticket Record
- **Part 10** — the customer media definitions: clause 3 for CMD2, clause 8 for
  CMD7

Date encoding comes in two forms:

- `DATE` is a 14-bit count of days from 1997-01-01, and a stored **zero means the
  top of the range** (10/11/2041), which schemes use as "no expiry".
- `DTS` is a 24-bit **two's complement** count of minutes from an epoch of
  2028-11-24 20:16, not an unsigned offset from 1997.

## Blocking

Two different things in the shell are called "blocked", and they are read from
different places.

A **product** is blocked when its Sector Chain Table terminator is S-2 rather
than S-1 (TS 1000-2 clause 5.1.4). That retires one ticket and leaves the rest
of the card working, and it shows as `[blocked]` beside the product.

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
| TYP 2 — Stored Travel Rights | Balance, currency, journey legs and cumulative fare; ceiling, overdraft, auto-top-up threshold/amount/state, deposit and how it was paid |
| TYP 3 — Loyalty type 1 | Points balance (three bytes, so it does not fit a purse's two) |
| TYP 4 — Charge to account 1 | Amount spent to date, credit limit, deposit, validity window |
| TYP 5 — Charge to account 2 | Transactions used, allowance per charge period, last reset date |
| TYP 14 — Entitlement (rev 1, 2) | Entitlement code, class, validity dates, locations, passback, ID flags |
| TYP 16 — ITSO ID (rev 1, 2) | Holder name, date of birth, gender, companion and photo flags, entitlement, class, validity dates, locations |
| TYP 22 — Period ticket (rev 1, 2, 3) | Validity start, from/to locations, passes remaining, expiry of the active pass and of the unused stock, auto-renew |
| TYP 23 — Journey ticket (rev 1, 2, 3) | Origin, destination, rides remaining, transfers made, auto-renew, used flag, stored-ride expiry (rev 3) |
| TYP 24 — Reservation | Journeys remaining |
| TYP 25 — Voucher | Vouchers remaining, auto-renew |
| TYP 26 — Tolling | Rides remaining, auto-renew |

Every product carrying a value record also reports its common header (TS 1000-2
table 15): what the last transaction was, when, how many times the record has
been written, and the ISAM of the POST that wrote it.

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

### The records behind the live one

The records the live one displaces are the transactions before it, and Flipso
keeps them all rather than only the newest: the balance as it was, what changed
it, and when. A card holds no statement anywhere else, so those few records are
the only history it carries.

They are shown under **Earlier** on the Pay as you go and product screens. Each
record contributes whichever of a balance or a counter its IPE type keeps in the
tail — the same field the screen shows above as the current value, decoded in one
place rather than two, because a history that disagreed with the balance above it
would be worse than no history.

The store is small: TS 1000-2 table 14 allows five records and every real card
to hand is issued with two, so a card straight off the reader shows at most one
earlier transaction. What makes the section worth having is that a saved card
accumulates them — see [Saved cards](#saved-cards).

Ordering matters more here than for picking the live record. Both records of a
journey ticket that has just been used carry the *same* DTS to the minute, so
sorting a history by time would show the ride being restored as often as spent.
TS# is the order; a wrap of the 12-bit counter takes 4096 transactions, and the
timestamp separates the two records that could then collide.

Locations are rendered from the encoding the card uses: rail NLC codes, NaPTAN
and ATCO bus stop codes, zone numbers and bit maps, fare stages and service
numbers. Rail codes are resolved to station names and bus stop codes to stop
names, from the tables described above.

LocDefType 216 carries a service number as well as a stop, so a resolved one
reads `Svc 42 @ High Street (adj), Hulme`: the table names only the stop half.
LocDefType 212 carries several stops and names the first, counting the rest.


## Limitations

- Reads DESFire ITSO cards (CMD7, and CMD12 where the layout matches) and
  ISO 7816 ones (CMD2). The obsolete MIFARE Classic and Ultralight media
  definitions are not supported, nor is CMD11, which replaces the file system
  with a proprietary command set.
- Oyster cards are recognised and described, but the data is encrypted
- Seals are not verified, so Flipso cannot tell you whether a card has been
  tampered with - only the shell's own checksum is checked. See
  [Integrity](#integrity).
- Read only. Flipso never writes to a card.
- The built-in operator name table is small, because ITSO does not publish its
  OID register.

