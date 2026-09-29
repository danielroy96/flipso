# Flipso

Read UK ITSO public transport smartcards with a Flipper Zero.

Tap a bus pass or rail smartcard on the back of your Flipper and see the tickets, passes, entitlements, journeys and pay as you go balance stored on it. Everything comes straight off the card: no account and no operator to log in to.

**ITSO** is the UK's national standard for interoperable public transport ticketing. ENCTS and Freedom Passes, bus and rail season tickets, and most local authority travel cards are ITSO cards.

## What it shows

- **Tickets**: journey, period and carnet products, with operator, validity and the stations they cover
- **Pay as you go**: balance, owning operator and purse, and the most recent transactions
- **Cardholder**: name, date of birth, concession and entitlements
- **Journeys**: recent taps and completed journeys, with origin, destination, fare and whether you were inside the gates
- **Paper tickets**: ITSO's compact paper tickets, such as the Glasgow Subway's singles, returns and day tickets
- **NFC tag tickets**: ITSO shells on NTAG215/216 and MIFARE Ultralight EV1 tags

Cards can be saved to the SD card and opened again later from the app.

Station and operator names are looked up on the Flipper from bundled reference data, so a card that stores only a code still shows a place you recognise.

## Bus stop names

Bus stops are identified by NaPTAN codes, and the full table of nearly half a million stops is 21 MB. That is too big to bundle into the app. To see bus stop names, download [naptan.dat](https://github.com/danielroy96/flipso/raw/main/data/naptan.dat) and copy it to apps_data/flipso/naptan.dat on the SD card. The About screen shows whether it is installed.

## What it cannot do

Flipso only reads what a card gives out without a key. It cannot check ITSO's security seals, which needs an operator's ISAM, and it cannot read TfL Oyster cards, which use Transport for London's own encrypted system rather than ITSO.

## Data and licences

- Railway station codes courtesy of [railwaycodes.org.uk](https://www.railwaycodes.org.uk), and station names from the Office of Rail and Road, Crown copyright, under the Open Government Licence v3.0
- Bus stops from NaPTAN, Department for Transport, Crown copyright and database right, under the Open Government Licence v3.0
- Card layouts from the ITSO TS 1000 specification, published by ITSO Ltd

Flipso is open source under the GPL-3.0. Source, documentation and issue tracker: [github.com/danielroy96/flipso](https://github.com/danielroy96/flipso)
