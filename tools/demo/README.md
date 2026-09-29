# Demo cards

Ten synthetic ITSO cards, written as saved-card files and copied to the
Flipper, so that Flipso can be seen without owning the cards that carry the
features. Nobody has a wallet with a loyalty IPE, a charge-to-account product,
a blocked shell and a revision 1 period ticket in it; between them these ten
have all of it.

A saved card is the raw blocks a read produced, not the decoded fields, so a
file built here goes through the same parsers a tap does. That is what makes
this honest: a demo card is not a mock-up of the screens, it is card bytes, and
what appears on the device is the decoder's reading of them.

    tools/demo/build_demo_cards.py <outdir>
    tools/flipper/flipctl push "<outdir>/Demo 1 The Key.flipso" \
        "/ext/apps_data/flipso/cards/Demo 1 The Key.flipso"

They then appear under **Saved cards** alongside real ones, which is the reason
each name starts with "Demo": the app has no way to tell a synthetic card from
a card that was tapped, and nor would anyone reading the screen.

## What each one is for

| Card | What it covers |
| --- | --- |
| **Demo 1 The Key** | Eleven products - one of every IPE type the decoder names, plus the three states the list flags: blocked, expired and never used. Shell with an MCRN. A log with both tap record revisions, a bus journey, and a gate that flagged the holder. A revision 3 period ticket naming the ID the holder must carry (IdentityDocumentID type 3) and keeping expired passes at a top-up; loyalty with owner data; a charge-to-account marked to be used first; an entitlement with its issuer, holder number, rounding rule and deposit. |
| **Demo 2 blocked pass** | A shell its issuer has stopped, which retitles the menu and banners the Card screen. The revision 1 ID (its language set aside by the URI flag), entitlement (with issuer, holder, rounding and deposit) and period ticket layouts. A revision 3 journey ticket in the return mode that revision adds, across two sectors. A purse spent past zero. A log entry written in basic mode, so the card knows its last tap and has no journey record of it. |
| **Demo 3 Subway CMD2** | The other customer media: 80-byte sectors, 64 of them, 16 directory entries and a six-bit Sector Chain Table. A purse with no expiry date and a value record that has never been written. |
| **Demo 4 past reads** | What only a saved card holds: journeys and transactions that have rolled off the card, and four products the card no longer lists. Twelve taps, which is as many as the decoder keeps. |
| **Demo 5 Subway paper** | An NFC Type 2 tag (CMD4): a compact shell and one TYP 27 day ticket at fixed page offsets, in the shape of a real SPT paper ticket. |
| **Demo 6 Subway return** | A TYP 29 multi-use return with one ride left, rebuilt byte for byte from a published dump of real SPT tickets. |
| **Demo 7 GWR Touch** | The shapes a real GWR Touch card carried and no other demo did: 160-byte sectors, a revision 1 ID with nothing optional, revision 2 season tickets with and without CPICC and with no value records, the two revision 4 records a rail gate writes (a check-in carrying only the entry operator, a check-out with no fare or entry), each reader's InstanceID, and a Directory InstanceID with an extended-range ISAM. |
| **Demo 8 Reading NTAG** | A full ITSO shell on an NFC Type 2 tag (CMD9, NTAG215): 64-byte sectors at fixed pages, the chip pages saved as a Tag block, an Abacus with ten uses left, and a journey ticket whose value records alternate between two anti-tear copies of the group - its history is only whole with both. |
| **Demo 9 Dundee EV1** | The same layout on an Ultralight EV1 (CMD10): 128-byte sectors, a shell carrying an MCRN (so the rotated first byte is 0x20), a period ticket with value records in both copies, and a log whose Record Offset makes the first slot the newer. |
| **Demo 10 zonal coupons** | A paper book of coupons (TYP 29 in coupon form) whose area is a location - a LOC3 zone map - rather than a fare code, and whose backup counts four coupons a bit, so it can only say "up to". Hypothetical: no real ticket of this shape has been seen. |

## Adding one

Every card read is a chance to find a shape these do not cover. Save it in the
app, pull it, and ask:

    tools/demo/new_encodings.py card.flipso

It lists the structures (geometry, each product's type, revision, bitmap and
value records, each journey record's revision and groups) and the screen lines
the card has that no demo card does. Anything it lists goes into a demo card,
built with the structures in `tools/test/itso_build.py`: extend an existing card
when the new shape fits its story, or add one when it is a different kind of
card, as Demo 7 is. Then:

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
