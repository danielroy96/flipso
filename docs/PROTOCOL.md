# Flipso protocol and data reference

How Flipso reads an ITSO card, which parts of the specification each field comes
from, and where the bundled reference data comes from.

Contents:

- [Oyster cards](#oyster-cards)
- [Operator names and card branding](#operator-names-and-card-branding)
- [Station names](#station-names)
- [How it reads the card](#how-it-reads-the-card)
- [Specification references](#specification-references)
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
Code. Flipso can currently decode National Rail NLCs but doesn't have bus
codes yet.

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
product that has one, and decodes those two tails.

Two things about value records are easy to get wrong, and both show up on an
ordinary rail ticket:

- The live record is the one with the highest **TS#**, not the latest timestamp.
  TS# counts up by one per record written (TS 1000-2 clause 7.2.4.2). The DTS
  only resolves to the minute, so a tap that spends a ride leaves two records
  stamped identically, and ordering by time picks between them at random.
- A blank record is all zeros, including its TS# and its DTS — and since the DTS
  epoch sits in 2028, an unwritten record otherwise reads as the most recent one
  on the card. Skip them.

Locations are rendered from the encoding the card uses: rail NLC codes, NaPTAN
and ATCO bus stop codes, zone numbers and bit maps, fare stages and service
numbers. Rail codes are resolved to station names from the packaged station
table — see above.


## Limitations

- Reads DESFire ITSO cards (CMD7, and CMD12 where the layout matches) and
  ISO 7816 ones (CMD2). The obsolete MIFARE Classic and Ultralight media
  definitions are not supported, nor is CMD11, which replaces the file system
  with a proprietary command set.
- Oyster cards are recognised and described, but the data is encrypted
- Seals are not verified. Flipso reports what the card says; it cannot tell you
  whether a card has been tampered with.
- Read only. Flipso never writes to a card.
- The built-in operator name table is small, because ITSO does not publish its
  OID register.

