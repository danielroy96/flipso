# Demo cards

Twelve synthetic ITSO cards, written as saved-card files and copied to the
Flipper, so that Flipso can be seen without owning the cards that carry the
features. Nobody has a wallet with a loyalty IPE, a charge-to-account product,
a blocked shell and a revision 1 period ticket in it; between them these
have all of it.

A saved card is the raw blocks a read produced, not the decoded fields, so a
file built here goes through the same parsers a tap does. That is what makes
this honest: a demo card is not a mock-up of the screens, it is card bytes, and
what appears on the device is the decoder's reading of them.

    tools/demo/build_demo_cards.py <outdir>
    tools/flipper/flipctl push "<outdir>/Demo 01 The Key Kent.flipso" \
        "/ext/apps_data/flipso/cards/Demo 01 The Key Kent.flipso"

They then appear under **Saved cards** alongside real ones, which is the reason
each name starts with "Demo": the app has no way to tell a synthetic card from
a card that was tapped, and nor would anyone reading the screen.

## What each one is for

| Card | What it covers |
| --- | --- |
| **Demo 01 The Key Kent** | A Tunbridge Wells commuter's Southeastern card, with eleven products - every full-shell IPE type, plus the three states the list flags: blocked, expired and never read. Shell with an MCRN. A monthly season to London Bridge (revision 3, naming the ITSO ID the holder must carry and keeping expired passes at a top-up), a book of Highspeed journeys from Ashford International, a purse with a journey in progress, loyalty with owner data, a charge-to-account marked to be used first, a voucher, a Disabled Persons Railcard (holder number, rounding down to 5p), a partner's loyalty scheme in the extended OID range, blocked, an expired seat reservation and a hypothetical Dartford Crossing toll pass. A log with both tap record revisions, a season journey via Sevenoaks, and a gate that flagged the season as not valid and charged the purse. |
| **Demo 02 Freedom Pass** | A London pensioner's pass that London Councils has stopped, which retitles the menu and banners the Card screen. What a real Freedom Pass carries: the revision 1 ID (its language set aside by the URI flag), the Greater London entitlement (issuer, holder, rounding and deposit) and a purse nobody has topped up. A log entry written in basic mode, so the card knows its last tap and has no journey record of it. |
| **Demo 03 Subway card** | A Glasgow Subway smartcard, the other customer media (CMD2): 80-byte sectors, 64 of them, 16 directory entries and a six-bit Sector Chain Table. A purse with no expiry date and a value record that has never been written, and four-week Subway passes good anywhere on the loop. Journeys between Subway stations by NaptanCode, one written as a list (LocDefType 212). |
| **Demo 04 SWR Touch** | A Surrey student's card, and what only a saved card holds: journeys and transactions that have rolled off the card, and four products the card no longer lists - a season to London Waterloo, a voucher, a National Rail purse and a 16-25 Railcard. Twelve journeys between Woking, Surbiton, Wimbledon and Richmond, which is as many as the decoder keeps, two of them with a UIC country code in front of the NLC (LocDefType 208). |
| **Demo 05 Subway day** | An NFC Type 2 tag (CMD4): a compact shell and one TYP 27 day ticket at fixed page offsets, in the shape of a real SPT paper ticket. |
| **Demo 06 Subway return** | A TYP 29 multi-use return with one ride left, rebuilt byte for byte from a published dump of real SPT tickets. |
| **Demo 07 GWR Touch** | The shapes a real GWR Touch card carried and no other demo did: 160-byte sectors, a revision 1 ID with nothing optional, revision 2 season tickets with and without CPICC and with no value records, the two revision 4 records a rail gate writes (a check-in carrying only the entry operator, a check-out with no fare or entry), each reader's InstanceID, and a Directory InstanceID with an extended-range ISAM. |
| **Demo 08 Reading Buses** | A full ITSO shell on an NFC Type 2 tag (CMD9, NTAG215): 64-byte sectors at fixed pages, the chip pages saved as a Tag block, an Abacus with ten uses left, and a journey ticket whose value records alternate between two anti-tear copies of the group - its history is only whole with both. |
| **Demo 09 MyXplore** | The same layout on an Ultralight EV1 (CMD10): 128-byte sectors, a shell carrying an MCRN (so the rotated first byte is 0x20), a period ticket with value records in both copies, and a log whose Record Offset makes the first slot the newer: two journeys on Xplore Dundee's route 22, between stops named by their NaptanCodes. |
| **Demo 10 SPT coupons** | A paper book of coupons (TYP 29 in coupon form) whose area is a location - a LOC3 zone map - rather than a fare code, and whose backup counts four coupons a bit, so it can only say "up to". Hypothetical: no real ticket of this shape has been seen. |
| **Demo 11 The Key Sussex** | A Brighton commuter's Southern Key, issued under Southern's own OID. A revision 1 season Brighton - London Victoria that Southern has blocked, a revision 3 Gatwick Express return in the return mode that revision adds, across two sectors, a purse spent past zero, and a business travel account by value (TYP 4), which no other card carries. |
| **Demo 12 Bee Card** | A Manchester commuter's card from the Bee Network. A Metrolink season valid in zones 1 to 3 (a zone map, LocDefType 204), and a bus return from Piccadilly Gardens whose far end is a route and a stop together (216): route 36 at Swinton Civic Centre. Metrolink stops and bus stops by NaptanCode in the log, and an older tram journey recorded only by fare zone (207). |

## Adding one

Every card read is a chance to find a shape these do not cover. Save it in the
app, pull it, and ask:

    tools/demo/new_encodings.py card.flipso

It lists the structures (geometry, each product's type, revision, bitmap and
value records, each journey record's revision and groups) and the screen lines
the card has that no demo card does. Anything it lists goes into a demo card,
built with the structures in `tools/test/itso_build.py`: extend an existing card
when the new shape fits its story, or add one when it is a different kind of
card, as Demo 07 is. Then:

- keep the shapes and invent the values - serial, dates, prices, stations,
  ISAMs - so nothing from the real card, least of all its route, is copied;
- check it with `tools/test/screens.py` and `tools/test/replay.py`, and against
  the real card's screens, which should differ only in the values;
- pin the lines only it produces in `tools/test/test_format.c`, as `demo_seven()`
  and `demo_type2_full()` do, and bump the card count there;
- run `new_encodings.py` on the real card again: it should say nothing is new.

## What they are not

Every card number is in ITSO's registered issuer range with an invented serial,
every holder is invented, and every seal is filler - a seal is a MAC over a key
in an ISAM, so nothing without that key can tell a real one from padding
anyway. The operator numbers *are* real, because the branding and the product
lines are part of what is being demonstrated.

Station names resolve from the packaged table. Bus stops resolve only where
`naptan.dat` has been installed on the SD card (see `data/README.md`); without
it a stop shows as the folded code the card actually stores, which is what a
real card does too.

The structures are built by `tools/test/itso_build.py`, shared with the host
test suite's card builder, so a field offset is stated once for both.
