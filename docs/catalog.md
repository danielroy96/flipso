# Flipso

Tap a UK ITSO bus pass or rail smartcard on the back of your Flipper and see the tickets, passes, entitlements, journeys
and pay as you go balance stored on it.

**ITSO** is the UK's national standard for interoperable public transport ticketing. If you've got an ENCTS or Freedom
Pass, a season ticket on a bus or rail smartcard, or a local authority travel card, it's probably an ITSO card.

## What it reads

- **Tickets**: journey, period and carnet products, with operator, validity and the stations they cover
- **Pay as you go**: balance, owning operator and purse, and the most recent transactions
- **Cardholder**: name, date of birth, concession and entitlements
- **Journeys**: recent taps and completed journeys, with origin, destination and fare
- **Paper tickets**: ITSO's compact shell tickets, such as Glasgow Subway's paper NFC singles, returns and day tickets
- **NFC tag tickets**: ITSO shells on NTAG215/216 and MIFARE Ultralight EV1 tags

Flipso should be able to read almost any public transport smart card in the UK as long as it has the ITSO logo printed
somewhere on the card. If you have a card that won't read - save your card dump and create an issue on 
[Github](https://github.com/danielroy96/flipso) or create a PR to fix it!

## Bus stop names

Copy [naptan.dat](https://github.com/danielroy96/flipso/raw/main/data/naptan.dat) to Flipso's app data directory
on the SD card if you want Flipso to decode bus stop NaPTANs. There are over 300k bus stops in the UK (21 MB),
so this data set is shipped alongside the app rather than bundled into it.

## Data and licences

- Railway NLC (station codes/names) data courtesy of [railwaycodes.org.uk](https://www.railwaycodes.org.uk)
- Bus stops NaPTANs published by [Department for Transport](https://beta-naptan.dft.gov.uk) under the Open Government Licence
- Card specification ITSO TS 1000 published by [ITSO Ltd](https://www.itso.org.uk) under the Open Government Licence
