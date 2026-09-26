# Demo cards

Four synthetic ITSO cards, written as saved-card files and copied to the
Flipper, so that Flipso can be seen without owning the cards that carry the
features. Nobody has a wallet with a loyalty IPE, a charge-to-account product,
a blocked shell and a revision 1 period ticket in it; between them these four
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
| **Demo 1 The Key** | Eleven products - one of every IPE type the decoder names, plus the three states the list flags: blocked, expired and never used. Shell with an MCRN. A log with both tap record revisions, a bus journey, and a gate that flagged the holder. |
| **Demo 2 blocked pass** | A shell its issuer has stopped, which retitles the menu and banners the Card screen. The revision 1 ID, entitlement and period ticket layouts. A purse spent past zero. A log entry written in basic mode, so the card knows its last tap and has no journey record of it. |
| **Demo 3 Subway CMD2** | The other customer media: 80-byte sectors, 64 of them, 16 directory entries and a six-bit Sector Chain Table. A purse with no expiry date and a value record that has never been written. |
| **Demo 4 past reads** | What only a saved card holds: journeys and transactions that have rolled off the card, and four products the card no longer lists. Twelve taps, which is as many as the decoder keeps. |

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
