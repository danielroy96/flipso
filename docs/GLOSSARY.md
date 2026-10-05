# Flossary

UK public transport smart ticketing comes with a lot of jargon - some of it from
the railway, some from the ITSO specification, some from NFC chips and some
from the Flipper itself - and this is a reference for all of it.

Each section is in alphabetical order. Where a term comes from a particular part
of the ITSO specification, the part is named (`TS 1000-2` and so on); the
[README](../README.md#itso-specification) lists the parts and links to them, and
[`PROTOCOL.md`](PROTOCOL.md) explains how Flipso uses each one.

Contents:

- [Tickets, fares and travel](#tickets-fares-and-travel)
- [Organisations, schemes and cards](#organisations-schemes-and-cards)
- [How an ITSO card is laid out](#how-an-itso-card-is-laid-out)
- [ITSO acronyms and field names](#itso-acronyms-and-field-names)
- [Product types (TYP)](#product-types-typ)
- [Locations (LocDefType)](#locations-locdeftype)
- [Chips, NFC and card hardware](#chips-nfc-and-card-hardware)
- [Flipso's screens and features](#flipsos-screens-and-features)
- [Flipper and development](#flipper-and-development)

## Tickets, fares and travel

The words a passenger, a ticket office or a railway timetable would use, as they
appear on Flipso's screens.

| Term | Meaning |
| --- | --- |
| **Airline style** | A seat that faces the back of the seat in front, rather than one at a table. One of the seat attributes on a reserved journey. |
| **Alighting** | Getting off a bus or train. Paper Subway tickets record whether their last use was boarding or alighting, shown as *Last got on* / *Last got off*. |
| **Anytime / off-peak** | An *Anytime* ticket may be used at any time of day. An *off-peak* ticket can only be used outside the busiest times, which the operator defines (on the railway, by the ticket's restriction code). Shown as *Off-peak only*. |
| **Area** | Where a ticket is valid, when it covers a network or a set of zones rather than a journey between two places. *Set by the operator* means the card holds the operator's own code for the area, not a list of places. |
| **Auto-renew** | A ticket that tops itself up with more passes or rides when it runs out, without the holder visiting a ticket office. *Renewal adds* says how many. |
| **Auto top-up** | Pay as you go that adds money to itself when the balance falls below a threshold (*When below*), charged to a payment method set up in advance. |
| **Balance limit** | The most a pay-as-you-go purse may hold (the purse's *ceiling*). |
| **Berth** | A bed on a sleeper train, booked like a seat. *Upper* or *Lower* bunk. *Cabin shared* is RSPS3002's TogetherFlag, which records whether the sleeper cabin is shared. |
| **Boarding** | Getting on a bus or train. See *alighting*. |
| **Booking reference** | The reference for a reservation made in advance, as printed on an e-ticket or confirmation email. |
| **Break of journey** | Stopping partway along a rail journey - leaving the station - and carrying on later on the same ticket. Some tickets allow it, some do not. |
| **Capping** | A limit on what pay as you go charges over a period: once the fares in a day (or week, or other period) reach the cap, further journeys are free. Shown on the *Fare capping* page. See *accumulator* and *Complex Capping*. |
| **Carnet** | A book of tickets bought together, often at a discount, and used one at a time. Flipso calls a carnet product a *Book of tickets*. |
| **Charge period** | The period over which a charge-to-account product counts its use - say a month - after which the count resets. |
| **Charge to account** | A product that does not hold money: journeys are recorded and billed to an account afterwards (TYP 4 and TYP 5). |
| **Class** | The standard of accommodation: *First* or *Standard* on the railway. The coding ITSO takes from EN 1545 also has *Business*, *Economy*, *Club*, *Small* and *Large* (TS 1000-5 annex A.1). |
| **Closed system** | A network where the passenger presents their ticket both on the way in and on the way out (TS 1000-1 table 2), usually at ticket gates - the Glasgow Subway, or gated railway stations. A tap made inside one shows *Inside ticket gates*. |
| **Coach** | On the railway, a carriage of a train - not a long-distance road coach. A reserved seat names its coach (a letter) and its seat number. |
| **Companion** | Someone who may travel with the holder on the same ticket or pass, usually free - typically the carer of a disabled pass holder. *Companion allowed* / *With a companion*. |
| **Concession, concessionary travel** | Free or reduced-price travel for a group of people, such as those over the state pension age, disabled people or young people. A concessionary bus pass in England is an ENCTS pass. The *Concession* line names the group (Adult, Child, Pensioner, Disabled, Student, Staff and so on). |
| **Coupon** | One ride of a book of single tickets. A paper Subway carnet shows *Coupons left*. |
| **Day pass** | A ticket valid for unlimited travel within its area for one day. |
| **Deposit** | Money paid for the card itself (*Card deposit*) or held against a product, which may be refundable when it is returned. |
| **Discount code** | On the railway, the three- or five-character code of the railcard or other discount a ticket was priced with, such as `DIS` (Disabled Persons Railcard), `YNG` (16-25 Railcard) or `SRN` (Senior Railcard). `XXXXX` means the discount came from an entitlement stored on the card (RSPS3002). |
| **Entitlement** | A right to travel free or at a discount, stored on the card on its own (TYP 14) or as part of an ID (TYP 16): *Free travel*, *Half fare*, *Flat fare*, *Capped fare*, *Warrant* and so on. |
| **Fare** | The price of a journey. *Fare collected* is what a tap took; *Fare so far* is what a journey with several legs has cost up to now. |
| **Fare group** | A set of stations priced as one for rail fares, such as *London Stations* (printed on tickets as London Terminals), *Colchester Stations* or a London zone group like *London Zone R1256*. The station table names fare groups as well as stations. |
| **Fare rounding** | How a reader rounds a half or proportional fare for this holder - up, down or to the nearest unit. |
| **Fare stage** | A point along a bus route where the fare changes. Bus fares are often priced from one stage to another, and older ticket machines record the stage number rather than a stop. The Glasgow Subway records its 15 stations as fare stages. |
| **Fare type** | See *FTOT*. |
| **Follow-on renewal** | A season ticket bought to start when the current one ends. One of the reserved journey's flags. |
| **Gates, ticket gates** | The barriers at a station that open for a valid ticket. See *closed system*. |
| **Interchange** | Changing from one train, bus or line to another. An *out-of-station interchange* is a change between two nearby stations that needs the passenger to leave one and walk to the other, permitted on the same ticket within a time limit (*Change stations at*). |
| **Journey** | One trip from an origin to a destination, which may have several *legs* if it involves changing. |
| **Journey ticket** | A ticket for a number of rides, rather than for a period of time (TYP 23). *Rides left* counts down as it is used. |
| **Leg** | One part of a journey, on one train or bus. A reserved journey lists its *Reserved legs*. |
| **Multi-use ticket** | A paper ticket good for more than one ride: a carnet of single tickets, a set of coupons or a journey of several legs (TYP 29, TS 1000-5 clause 2.16). The Glasgow Subway's paper singles and returns are this type. |
| **Off-peak** | See *Anytime / off-peak*. |
| **Operator** | The company or authority that runs the service, or that sold or owns a ticket. Flipso names operators from their OID. |
| **Outward / Return** | The two halves (*portions*) of a return ticket: the journey out and the journey back. Each may have its own dates of validity. |
| **Overdraft limit** | How far below zero a pay-as-you-go balance may go before the card is refused. |
| **Party size, travellers** | How many people a ticket covers - adults and children counted separately. |
| **Pass** | A ticket for unlimited travel over a period. A period ticket can hold several stored passes (*Passes left*), used one after another. |
| **Passback** | The time a reader waits before accepting the same card again (*Passback timeout*). It stops one ticket being passed back over the barrier to a second person. A value of 0 means the reader's own rule applies. |
| **Pay as you go** | Money stored on the card and spent on fares as they are travelled, rather than a ticket bought in advance. ITSO calls it *Stored Travel Rights* (TYP 2); it is also called a *purse* or *e-purse*. |
| **Period ticket** | A ticket for unlimited travel between two places or within an area for a period - anything from a day to a year. One of a week or longer is a *season ticket*. TYP 22 on a smartcard, TYP 27 on a paper ticket. |
| **Photocard** | A card bearing the holder's photo that must be carried with a ticket or railcard. *Photocard number* identifies it. |
| **Portion** | On a ticket, one half of a return: see *Outward / Return*. Not the railway's other sense of a portion, the part of a train that divides from the rest. |
| **Promotion code** | An operator's code for the offer or promotion a ticket was sold under. |
| **Public holidays** | Bank holidays, which some tickets treat differently from ordinary weekdays (*Special days*). |
| **Purse** | See *Pay as you go*. |
| **Railcard** | A National Rail discount card - typically a third off fares - for a group of people: the 16-25, Senior, Disabled Persons, Two Together, Family & Friends Railcards and others. A ticket bought with one is only valid when the railcard is carried, hence *Valid only with*. |
| **Reservation** | A seat, berth, wheelchair space or bicycle space booked on a particular train. |
| **Reserved journey** | A rail ticket with reservations, carrying what a paper ticket would: dates, route, railcard, seats (TYP 24). |
| **Restriction code** | A rail code that refers to a published set of conditions on when a ticket may be used, typically which trains count as off-peak. |
| **Retail service ID** | The eight-character code (RSID) that identifies a train on a given day for rail retailing and seat reservations: the operator's two letters, a four-digit number and two digits for the portion of a train that splits. Recorded on a reservation. |
| **Return** | A ticket for a journey and the journey back. *Return, journeys in pairs* is a journey ticket whose rides are counted as outward-and-return pairs. |
| **Rides** | Journeys a journey ticket or carnet allows. *Rides left* counts them down. |
| **Route** | On a rail ticket, the route the passenger must take: a five-digit *Route code* from the fares data, such as `00000` for *Any Permitted*, which Flipso shows as the code itself. On a bus, the service number. |
| **Routing point** | A station a rail ticket's route is defined by - a place the journey must, or must not, pass through. |
| **Season ticket** | See *Period ticket*. |
| **Single** | A ticket for one journey in one direction. |
| **Sleeper** | An overnight train with berths - not the beam the rails are laid on. |
| **Stored pass** | One of several passes a period ticket holds in reserve, activated one at a time. *Unused passes until* is when the stock expires. |
| **Supplement** | An additional charge on top of a fare, for example for a seat reservation or a sleeper berth. |
| **Tap, tap in, tap out** | Holding a card against a reader. On a gated network the passenger taps in at the start and out at the end, and the fare is worked out from both. *Last tap* is the most recent. |
| **Test ticket** | A ticket issued for testing equipment, not for travel. |
| **Top-up** | Adding money to pay as you go, or passes or rides to a ticket. |
| **Transfer** | Changing to another service partway through a journey without paying again. *Transfers left* counts how many changes remain; *Changes allowed* sets the limit. |
| **Transferable** | A ticket that may be used by someone other than the person who bought it. |
| **Valid days** | The days of the week a ticket may be used, such as *Weekdays only*. |
| **Validity code** | An operator's code for the conditions a ticket is valid under. |
| **Via / Not via** | A route restriction: the journey must (*Via*) or must not (*Not via*) pass through a given place. |
| **Voucher** | A *Travel Related Voucher* (TYP 25): a product for something that goes with travel rather than the travel itself, such as car parking with a rail ticket or a meal on the train (TS 1000-5 clause 2.12). It can carry a value or a number of uses; *Worth up to* is its maximum value. |
| **Warrant** | A travel warrant: an authority from an employer or public body, such as the armed forces, for a ticket the body pays for. An *Unfulfilled warrant* flag marks a ticket issued against one. |
| **Zone** | An area of a network priced as one, as on London's Travelcard zones. A zonal ticket is valid within a set of zones (*Zones 1,2,3*) or from one zone to another. |

## Organisations, schemes and cards

| Term | Meaning |
| --- | --- |
| **ATCO** | Association of Transport Co-ordinating Officers: the local-authority transport officers who maintain bus stop data. Gives its name to the *AtcoCode*. |
| **Bee Card** | The smartcard of Greater Manchester's Bee Network. |
| **DfT** | Department for Transport. Publishes the NaPTAN register of bus stops. |
| **ENCTS** | English National Concessionary Travel Scheme: free off-peak local bus travel in England for older and disabled people. Its passes are ITSO cards issued by local councils. |
| **Freedom Pass** | London's concessionary pass for older and disabled residents, issued by London Councils. Besides buses it is valid on the Tube and, after 09:30 on weekdays and all day at weekends, on most National Rail services in London. |
| **Glasgow Tripper** | The multi-operator bus smartcard of Glasgow's bus companies, run through Glasgow Smartzone Ticketing. |
| **Go! Smart** | The ITSO smartcard of McGill's, the Scottish bus operator, which styles it *GoSmart*. |
| **ITSO** | The UK's national standard for interoperable smart ticketing, and ITSO Ltd, the not-for-profit body that maintains it. Originally the *Integrated Transport Smartcard Organisation*. A card with the ITSO logo can carry products from many operators and be read by any operator's ITSO-certified reader. |
| **London Councils** | The body representing London's boroughs, which issues the Freedom Pass. |
| **MCard** | West Yorkshire's multi-operator smartcard. |
| **National Rail** | The passenger rail network of Great Britain, run by many train operating companies under common fares and ticketing rules. |
| **NRS** | National Reservation System: rail's seat reservation system, run by Rail Settlement Plan. Its reference data (RSPS5048, not published) defines the four-letter seat attribute codes; only a few, such as `SEAT`, are in public view, so Flipso's reading of the others (`QUIE` as quiet coach, for example) is a best guess. |
| **Oyster** | Transport for London's own smartcard. A MIFARE DESFire card but **not** an ITSO card: its data is in TfL's own application under TfL's keys, so Flipso can describe the chip but not read the balance. See [Oyster cards](PROTOCOL.md#oyster-cards). |
| **RDG** | Rail Delivery Group: the industry body for Britain's train operators, which publishes the rail ticketing specifications. |
| **RSP** | Rail Settlement Plan: the rail industry company that shares out ticket revenue between operators, and publishes the RSPS standards. |
| **RSPS3002** | RSP's *ITSO in National Rail Specification*: what National Rail does with the parts of ITSO left to the operator, particularly TYP 24. Flipso uses it for railcard codes, seat attributes and retailer NLCs. RSPS3008, which it cites for ID document types, is not published. |
| **SEFT** | South East Flexible Ticketing: the Department for Transport programme, delivered by Rail Settlement Plan, that brought ITSO smartcards to rail in south-east England. |
| **SPT** | Strathclyde Partnership for Transport, which runs the Glasgow Subway and issues its smartcards and paper NFC tickets. |
| **STNR** | Smart Ticketing on National Rail: the rail industry's name for its ITSO smartcard programme. |
| **Subway** | The Glasgow Subway: a 15-station underground loop run by SPT. Its stations are numbered anticlockwise from Govan. |
| **SWR Touch, GWR Touch, The Key, c2c Smart, ScotRail Smartcard, TPE Smartcard, Chiltern Smartcard, CrossCountry Smartcard** | Train operators' own brands for their ITSO smartcards. *The Key* is used by Govia Thameslink Railway (Southern, Thameslink and Great Northern) and by Southeastern. |
| **TfL** | Transport for London. Runs Oyster, which is not ITSO, but also accepts ITSO concessionary passes. |
| **UIC** | Union Internationale des Chemins de fer, the international union of railways. A *UIC country code* prefixes a station code in an international location (LocDefType 208). Great Britain's is 70. |

## How an ITSO card is laid out

The structure the ITSO specification gives every card, from the outside in. See
[How it reads the card](PROTOCOL.md#how-it-reads-the-card) for how Flipso walks it.

| Term | Meaning |
| --- | --- |
| **Abacus** | On a CMD9 card, a count of bits in the chip's one-time-programmable page. One bit is set per value record written, so an old copy of the card cannot be written back, and sixteen bits set means the card is *retired*. Flipso shows the uses left. |
| **Anti-tear** | Protection against a card being pulled away from a reader mid-write ("torn"). A DESFire does it in hardware with backup files; CMD2, CMD9 and CMD10 do it in software by keeping two copies of everything that changes and switching between them (TS 1000-10 annex A). |
| **Block** | Nothing to do with the railway's signalling blocks: ITSO measures many lengths in blocks of a few bytes (ShellLength, IPELength). In Flipso's saved-card file a *block* is one key's worth of raw bytes - Shell, Directory, a Product, the Log. |
| **Blocked** | Withdrawn from use by the issuer. A *product* is blocked through its Sector Chain Table, retiring that ticket alone; the whole *card* (shell) is blocked by a bit in the directory. See [Blocking](PROTOCOL.md#blocking). |
| **Compact shell** | The three-byte shell a CMD4 paper ticket carries in place of a full one (TS 1000-2 clause 4.2). Everything else is implied by the CMD - OID 8189, serial number 0 - so every compact-shell ticket shares the same card number. |
| **Customer media** | ITSO's term for whatever the holder carries - a smartcard, a paper ticket, an NFC tag. Each kind is defined by a *CMD*. |
| **Cyclic log** | A small ring of records on the card (four on a DESFire) that remembers the most recent taps, each new one overwriting the oldest. Its records are *Transient Ticket Records*. |
| **Data group** | A unit of related data on the card - the Shell Environment Data Group, the Directory Data Group, an IPE Data Group, a Value Record Data Group - usually with its own seal. |
| **Dataset** | The fields of an IPE that are particular to its type, after the elements every IPE shares. |
| **Directory** | The Directory Data Group: the card's table of contents. One entry per product says who owns it, its type, its expiry and where its data starts; the Sector Chain Table says where it continues. |
| **Directory copy A / B** | The two copies of the Directory kept by media with software anti-tear. The one with the newer DIRS# is live. |
| **Directory entry** | One product's line in the Directory: owner (OID), TYP, subtype, the VGP flag and expiry, five bytes in all. Shown as *Directory slot*. Entries are reused when a product is removed. |
| **Geometry** | The size and shape of the card's ITSO area: the number of sectors (*S*), the bytes in each (*B*), the number of directory entries (*E#*) and the Sector Chain Table's entry width (*SCTL*). Flipso reads it from the shell rather than assuming the defaults. |
| **IPE** | ITSO Product Entity: a product on the card - a ticket, pass, purse, entitlement or ID. Its *type* is the TYP. |
| **IPE Data Group** | The stored form of an IPE: the elements every IPE shares, its type's dataset, and its seal. |
| **Log Directory Entry** | The directory entry that describes the cyclic log rather than a product, including which product was used on the last tap. |
| **Logical sector** | A fixed-size unit of the card's ITSO storage, numbered by the specification. On a DESFire each is a file; on CMD2 a file under its own directory; on a Type 2 tag a run of pages. |
| **Orphan IPE Data Group** | A record that belongs to no directory entry. Each journey record in the cyclic log is one, which is why it carries its own InstanceID. |
| **Private product** | A directory entry with TYP 0: a private application that sits in the ITSO shell but whose data ITSO does not define (TS 1000-2 clause 6.1). |
| **Product** | Flipso's word for an IPE as shown to the user - a ticket, a pass, pay as you go or an ID. |
| **Retired** | A CMD9 card whose Abacus has run out; readers reject it. |
| **Seal** | A cryptographic signature (a MAC) over a data group, made with a key held in an ISAM. It is what makes a ticket unforgeable. Flipso cannot check seals without the keys; a CMD4 ticket's seal set to all zeros means it is blocked. See [Integrity](PROTOCOL.md#integrity). |
| **Sector chain** | The sectors one product occupies, linked one to the next by the Sector Chain Table. |
| **Sector Chain Table (SCT)** | A table in the Directory with one entry per sector, giving the next sector of the chain. The last entry marks the end and the product's state: its own sector number for a product never used, S-2 for a blocked one, S-1 for one used at least once (TS 1000-2 clause 5.1.5.2). |
| **Shell** | The ITSO Shell Environment: the card's identity and layout - card number, expiry, geometry, key details - in the *Shell Environment Data Group* that every ITSO card starts with. The *shell owner* is the operator that issued the card, which gives the card its brand. |
| **Space Saving IPE** | A compact product format for small media such as CMD4 paper tickets: a fixed sequence of fields rather than a bitmap of optional ones (TYP 27, 28 and 29; TS 1000-5 clauses 2.14-2.16). |
| **Transient Ticket Record (TTR)** | One record of the cyclic log: what happened at a tap - where, when, which product, what it cost. Shown on the *Journeys* screen. |
| **Value record** | A small record of the part of a product that changes as it is used: a purse's balance, a journey ticket's rides left. A product keeps a few in a cyclic *Value Record Data Group*; the one with the highest TS# is live, the rest are its history. |
| **Value Group Extension** | Extra data after a product's value records, identified by its VGXRef: fare capping accumulators (1, 2) or a reserved journey's reservations (3). |

## ITSO acronyms and field names

Abbreviations and element names from the ITSO specification as they appear in
the source, the documentation and the *Technical* pages. Element names are given
as the specification spells them.

| Term | Meaning |
| --- | --- |
| **Accumulator** | In complex capping, a running total of what has been spent towards a cap, with its rule (one day, *n* days) and what the fares would have been uncapped. A card holds four sets. |
| **AMT group** | The optional group of a journey record holding the amount paid, its currency, how it was paid and its VAT (TS 1000-5 table 59). |
| **Annex** | An appendix to a part of the specification. TS 1000-5's annexes define the code tables (A.10 HalfDayOfWeek, A.21 currency, A.24 language and so on). |
| **B** | Bytes per logical sector, part of the card's geometry. The CMD sets the default: 64 on a DESFire (which may also use 80 to 240), 48 on CMD2, 64 on an NTAG215 and 128 on an NTAG216 (CMD9), 128 on CMD10. SPT's CMD2 cards use 80. |
| **BCD** | Binary-coded decimal: each decimal digit stored in four bits. ITSO stores card numbers, dates of birth and NaptanCodes this way. |
| **BL** | Block length: the unit, in bytes, a data group measures its own length in. The shell's is 4, so a ShellLength of 8 is 32 bytes. |
| **CIPE group** | *Candidate IPEs*: in a revision 3 or 4 journey record, the products the reader considered using for the tap. |
| **Clause** | A numbered section of a specification part, such as *TS 1000-2 clause 5.1.4*. |
| **CMD** | Customer Media Definition: ITSO's definition of how its data sits on one kind of chip (TS 1000-10). Flipso reads **CMD2** (any ISO 7816 smartcard), **CMD4** (compact paper tickets on Type 2 tags), **CMD7** (MIFARE DESFire), **CMD9** (NTAG215/216), **CMD10** (MIFARE Ultralight EV1) and **CMD12** where its layout matches CMD7. **CMD11** replaces the file system with ITSO's own command set and is not supported. CMD1 and CMD3 (MIFARE Classic) are obsolete. |
| **Complex Capping** | The fare capping extension a purse can carry, as a Value Group Extension with VGXRef 1 (*reduced*, type 1) or 2 (*full*, type 2) (TS 1000-5 clause 4.1). |
| **CPICC** | Concessionary Pass Issuer Cost Centre: the code of the authority that issued a concessionary pass (*Pass issuer code*). |
| **CRC, CRC_B** | Cyclic redundancy check: a checksum that catches accidental corruption. CRC_B is the variety ISO 14443 defines, used for the SECRC. |
| **DATE** | ITSO's date type: a 14-bit count of days from 1 January 1997. Zero means the top of the range (10/11/2041), which schemes use for *no expiry*. |
| **DIRBitMap** | Flags at the start of the Directory, including the bit that blocks the whole card and the log's configuration. |
| **DIRS#** | The Directory's sequence number, incremented on each write. Decides which of two Directory copies is newer, counting FF to 00 as a step forward. |
| **Directory InstanceID** | The key ID, the shell iteration number (INS#) and the ISAM ID of the last machine to change the Directory - shown as *Last updated by machine* (TS 1000-2 clause 5.2). Unlike an IPE's InstanceID it has no sequence number. |
| **DTS** | Date Time Stamp: ITSO's date-and-time type, a 24-bit two's complement count of minutes from 24 November 2028 20:16. Its resolution is one minute. |
| **E#** | The number of directory entries, part of the geometry. |
| **E(n), E1, E2** | Directory entry number *n*. A saved card's `Product n` block is keyed by it. |
| **EEI** | Entry/exit indicator in the log: whether the holder is inside a closed system. |
| **EN 1545** | The European standard for the data elements of fare collection. ITSO uses its codes for travel class (AccommodationClassCode), payment method (PaymentMeansCode), entitlement (EntitlementTypeCode) and concession (ProfileCodeIOP). |
| **ENTRY group** | In a revision 4 journey record, the ISAM and sequence number of the tap-in, so a tap out names the gate the holder came in through (*Tapped in with*). |
| **EventTypeCode** | What a transaction was: Sale, Top-up, Tap in, Tap out, Fare paid, Refunded, Ticket activated and so on. |
| **EXP** | The shell's expiry date: when the card itself expires. |
| **Format revision** | The version of a product type's layout. Several types have more than one (TYP 22 has revisions 1-3) and the fields move between them. |
| **FTOT** | Fares Type Of Ticket: rail's three-character code for the kind of ticket (single, return, off-peak and so on) in the fares data (*Fare type*). |
| **FVC** | Format Version Code: which CMD the shell is laid out for. On a Type 2 tag it is what tells CMD4, CMD9 and CMD10 apart. |
| **HalfDayOfWeek** | Which half-days a product is valid: two periods per day, defined by the network (annex A.10). |
| **HolderID** | The issuer's number for the cardholder or their photo (*Holder number*); *SecondaryHolderID* is a second one. |
| **Hotlist** | A list, sent to readers, of cards or products that must be refused - lost, stolen or cancelled. A hotlist names a shell by its ISRN and INS#. |
| **IDFlags** | Flags on an ITSO ID: gender, companion allowed, photo on card, and whether another application on the card should be used instead. |
| **IIN** | Issuer Identification Number: the six-digit number that starts a card number and names the scheme. **633597** is ITSO's. |
| **IINL** | A flag saying a product's operator belongs to a different network from the card's IIN. |
| **INP#** | A product's iteration number, increased to bring a hotlisted product back into use. |
| **INS#** | The shell's iteration number, increased to bring a stopped card back into use. |
| **InstanceID** | The structure after a data group's dataset that names the key its seal uses and the machine behind it. An *IPE InstanceID* gives the ISAM that created the product and that ISAM's sequence number, which together identify one particular ticket (*Created by machine*); value records and journey records carry the same shape. The Directory's names only the ISAM - see *Directory InstanceID*. |
| **IPEBitMap** | Flags at the start of an IPE saying which of its type's optional elements are present. |
| **IPELength** | The length of an IPE, in blocks. |
| **ISAM** | ITSO Secure Application Module: the secure chip inside every ITSO ticket machine, gate and bus reader that holds the keys and makes the seals. Its *ISAM ID* encodes the OID of the operator it is registered to, which is how Flipso names whose machine sold a ticket or took a tap. |
| **ISAMS#** | The sequence number of something an ISAM created, part of an InstanceID. |
| **ISAMIDModifier** | In a value record, the ISAM of the machine that last wrote it (*Last updated by machine*). |
| **ISRN** | ITSO Shell Reference Number: the 18-digit card number - IIN (6 digits), OID (4), ISSN (7) and a Luhn check digit. |
| **ISSN** | ITSO Shell Serial Number: the seven digits of the card number that the issuing operator assigns. |
| **KID** | Key ID: the version of the key a seal is made with (*Seal key version*). |
| **KSC** | Key Strategy Code: which security algorithm the card's keys use. |
| **KVC** | Key-set Version Code: which version of that strategy's keys. Shown together with the KSC as *Key set*. |
| **LOC1, LOC2, LOC3, LOC4, LOCE** | ITSO's location formats (TS 1000-1 clause 4.2.4). A **LOC1** has a length byte and can hold any encoding; a **LOC2** is a fixed-length form; a **LOC3** packs an origin and a destination, and a **LOC4** adds a via, into a fixed size for media short of space such as Space Saving IPEs; a **LOCE** is the body of one place, whose length depends on the LocDefType (at most four bytes inside a LOC3 or LOC4). Each starts by giving its LocDefType. |
| **LocDefType** | Location Definition Type: a number from 200 that says how a location is encoded - a rail station code, a bus stop, a zone and so on. See [Locations](#locations-locdeftype). |
| **LPF** | A flag in the Log Directory Entry saying whether it points at a journey record in the log or the machine wrote none. |
| **Luhn** | The check-digit formula used by bank cards and by the last digit of the ISRN, which catches a mistyped digit. |
| **MAC** | Message Authentication Code: a cryptographic checksum only a key holder can make. An ITSO seal is one; a *Transaction MAC* is one over a single transaction. |
| **MCRN** | Multi-application Card Reference Number: on a card that carries ITSO alongside other applications, a copy of the number that card's own issuer gave it. |
| **NDoIE, NDoEE** | Flags on a carnet of day passes: whether a pass may be used on the day of issue and on the day of expiry. |
| **OID** | Operator ID: ITSO's number for an operator, authority or other organisation. It is in the card number of every card an operator issues and on every product it owns. ITSO does not publish the register, so Flipso's table is built from cards that have been read; an unknown one shows as *Unknown (1234)* - see [Operator names](PROTOCOL.md#operator-names-and-card-branding). |
| **OTP** | One-time programmable: chip memory whose bits can be set but never cleared, used for counters that must only go one way. |
| **POST** | Point of Service Terminal: any ITSO machine that reads or writes cards - a ticket gate, a bus ticket machine, a ticket office terminal, a validator. |
| **ProductRetailer** | Who sold a product (*Sold by*): an OID, or on the railway the selling station's NLC. |
| **QtyRemaining** | On a paper multi-use ticket, the rides left - stored counting up from 8191 less the rides bought. |
| **RDATE** | How many days past expiry before a product may be removed from the card (the *remove date*). |
| **Record Offset (RO)** | Which slot of the cyclic log will be written next, so which is newest. |
| **RFU** | Reserved for future use: bits the specification leaves unused. |
| **S** | The number of logical sectors, part of the geometry. A Sector Chain Table value of S-1 or S-2 ends a chain. |
| **ScaledQtyBackup** | On a paper multi-use ticket, a backup count of rides in the OTP page, one bit per ride (or per *ScalingFactor* rides). Flipso checks it *Agrees with rides left*. |
| **SCTL** | The length of the Sector Chain Table in bytes, part of the geometry (TS 1000-2 clause 4.1.12). The width of each entry, Ψ, is not stored: it is the number of bits needed to count to S. |
| **SECRC** | Shell Environment CRC: a two-byte checksum at the end of the shell, the one integrity check Flipso can make without keys (*Checksum*). |
| **ShellBitMap, ShellLength, ShellFormatRevision** | The shell's first elements: which optional elements it carries, how long it is, and the version of its layout. |
| **SNCODE, SNCODE2** | ITSO's compact codings of a bus service number: four 5-bit characters from a reduced alphabet, or four 6-bit characters covering all the digits and letters (TS 1000-1 tables 41 and 42b). |
| **Subtype** | A second number in a directory entry that the product's owner uses to tell its own kinds of a TYP apart. |
| **TLV** | Tag, length, value: a self-describing way to store an optional field, used inside some IPE datasets. |
| **TS 1000** | The ITSO technical specification, in parts: TS 1000-1 (data types), -2 (the shell and data groups), -5 (product types), -7 (security) and -10 (the customer media). Flipso is written against version 2.1.5. |
| **TS#** | Transaction sequence number: how many times a value record has been written (*Times updated*). The highest is the live record, not the latest timestamp. |
| **TYP** | The type of an IPE - what kind of product it is. See [Product types](#product-types-typ). |
| **TYP22Flags, TYP24Flags and so on** | A product type's own set of flags - off-peak, transferable, auto-renew, test ticket and the like. |
| **UD** | User-defined: an element the specification leaves the product's owner to define. RSPS3002 defines many of TYP 24's for rail. |
| **UsageRec** | On a paper multi-use ticket, where it was last used - a fare stage or a station (LocDefType 202 or 203) - and whether that was boarding or alighting (TS 1000-5 table 58). |
| **VALC** | ITSO's four-bit currency code, which travels beside an amount (a VALI or VALS) and says what it is counted in: two bits of currency (0 the scheme's local currency - sterling in the UK - 1 its global one, the euro, 2 and 3 tokens) and two of scaling factor (TS 1000-1 clause 4.2.6, TS 1000-5 annex A.21). |
| **VALS** | A signed value, as a balance is - it can go overdrawn. |
| **VAT** | Value Added Tax. Some products record the VAT rate on what was paid. |
| **VGBitMap** | Flags at the start of a Value Record Data Group, including whether a Value Group Extension follows. |
| **VGP** | Value Group Present: a flag in the directory entry saying the product has value records. |
| **VGXRef** | Value Group Extension reference: which extension a value group carries - 1 or 2 complex capping, 3 a reserved journey's reservations. |

## Product types (TYP)

The ITSO product types Flipso knows (TS 1000-5), with the name each is shown
under. [Product coverage](PROTOCOL.md#product-coverage) lists what is decoded
from each.

| TYP | ITSO name (TS 1000-5) | Shown as | What it is |
| --- | --- | --- | --- |
| 0 | Private application | Private product | Data ITSO does not define, owned by the OID in its directory entry. |
| 2 | Stored Travel Rights | Pay as you go | Money on the card, spent on fares. |
| 3 | Loyalty type 1 | Loyalty | A points balance. |
| 4 | Charge To Account Mode 1 | Charge to account | Journeys billed afterwards, with a credit limit. |
| 5 | Charge To Account Mode 2 | Charge to account | A number of charged journeys allowed per charge period. |
| 14 | Entitlement | Entitlement | A right to free or discounted travel on its own. |
| 16 | ITSO ID | ITSO ID | The holder's identity - name, date of birth, concession, photo - usually with an entitlement, as on a bus pass. |
| 17 | Loyalty Type 2 | Loyalty | Named only; its dataset is not decoded. |
| 22 | Pre-defined Ticket (Area Based) | Period ticket | Unlimited travel over a period: a day ticket or a season ticket. |
| 23 | Pre-defined Specific Journey Ticket | Journey ticket | A number of rides. |
| 24 | Pre-defined Specific Journey Ticket Including Reservations | Reserved journey | A rail ticket with reservations, railcard and route. |
| 25 | Travel Related Voucher | Voucher | Something that goes with travel, such as parking or a meal. |
| 26 | Open System Tolling Ticket | Toll pass | A bridge, tunnel or ferry crossing whose fee does not depend on distance: crossings left, vehicle class, dates. |
| 27 | Period Ticket (space saving) | Paper period ticket | A paper day ticket. |
| 28 | Carnet Ticket (space saving) supporting day passes | Book of tickets | Up to eight paper day passes. |
| 29 | Multi-Use Ticket (space saving) | Multi-use ticket | A paper carnet of singles, coupons or a journey of several legs. |

## Locations (LocDefType)

How a card says where something happened or where a ticket is valid
(TS 1000-1 table 6), and how Flipso shows each.

| Term | Meaning |
| --- | --- |
| **AtcoCode** | A bus stop's full identifier in NaPTAN, up to twelve characters such as `1800ALTRNHM0` (LocDefType 211). Looked up whole in the stop table. |
| **NaPTAN** | National Public Transport Access Nodes: the Department for Transport's register of every bus stop and other public transport access point in Great Britain. Flipso's optional `naptan.dat` is built from it. |
| **NaptanCode** | A bus stop's short code, eight characters such as `cumfatda`, the kind printed on stops for text-message services. ITSO packs it into four bytes by mapping its letters onto a telephone keypad, so two stops can share a number (LocDefTypes 206, 212, 216). Without the stop table it shows as `Stop 28632832`. |
| **NLC** | National Location Code: the four-character code the railway gives every station, and some non-station locations, for fares and accounting (LocDefType 203, or 208 with a UIC country code). Flipso names it from the station table, or shows `Station 1234`. Rail also uses an NLC for the station that sold a ticket. |
| **Null location** | LocDefType 255: no location recorded (*Not recorded*). |
| **Stage** | See *fare stage*. LocDefTypes 202, 209 and 217 are bus fare stages, with the machine or service number they belong to. |
| **Zonal bit map** | A set of zones as one bit per zone (LocDefTypes 204, *valid within zone*, and 205, *zone to zone*). Shown as `Zones 1,2,3`. |
| **Zone number** | A single zone (LocDefType 207). |

| LocDefType | Encoding | Shown as |
| --- | --- | --- |
| 202 | Bus fare stage: machine number and stage | `Fare stage 3 (…)`, or a Subway station |
| 203 | Rail NLC | Station name, or `Station 1234` |
| 204, 205 | Zonal bit map | `Zones 1,2,3` |
| 206 | NaptanCode | Stop name, or `Stop 28632832` |
| 207 | Zone number | `Zone 4` |
| 208 | UIC country code and NLC | Station name |
| 209, 217 | Bus fare stage with OID and service number | `Route 42, stage 3` |
| 210, 218 | Service numbers | `Route 42` |
| 211 | AtcoCode | Stop name, or `Stop 1800ALTRNHM0` |
| 212 | Several NaptanCodes | The first, and how many more |
| 216 | OID, service number and NaptanCode | `Route 42 at High Street` |
| 255 | Null | `Not recorded` |

## Chips, NFC and card hardware

| Term | Meaning |
| --- | --- |
| **AID** | Application Identifier: the number a chip's application is selected by. ITSO's on a DESFire is sent as `16 02 A0` (least significant byte first, as DESFire takes it; TS 1000-10 clause 8.8.2); Oyster's is `4F 59 31` ("OY1"). |
| **APDU** | Application Protocol Data Unit: one command or response in the ISO 7816 command set, as CMD2 cards use. |
| **Batch** | The manufacturing batch number a DESFire reports about itself. |
| **BCC0** | A check byte in a Type 2 tag's serial number. |
| **CDC** | USB Communications Device Class: how the Flipper's USB serial port appears to a computer. |
| **DESFire** | NXP's MIFARE DESFire, a secure smartcard chip with a file system of applications and files (EV1, EV2 and EV3 are its generations). Most ITSO smartcards are DESFire cards (CMD7). |
| **DF, EF** | Dedicated File and Elementary File: a directory and a data file in an ISO 7816 card's file system. A CMD2 card keeps each ITSO sector as an EF in its own DF. |
| **FCI** | File Control Information: what an ISO 7816 card returns when an application is selected. Many CMD2 cards include the ITSO shell in it. |
| **GetVersion, GetFreeMemory** | DESFire commands that report the chip's hardware and software versions, UID, batch and date of manufacture, and the free memory. Shown on the *Chip* page. |
| **Infineon my-d** | The Ultralight-compatible chip the Subway's paper tickets are made on. |
| **ISO 14443** | The international standard for contactless cards. **-3** covers how a card is woken up and selected (*Type A* is the variety MIFARE cards use); **-4** the protocol smart cards speak once activated. |
| **ISO 7816** | The standard for smart card file systems and commands, shared by contact and contactless cards. |
| **Lock bits, lock bytes** | Bits on a Type 2 tag that make pages permanently read-only. *Locked pages* lists which are; *Lock bits frozen* means the lock bits themselves can no longer change. |
| **MIFARE Classic** | An older NXP card family. ITSO's media definitions for it (CMD1 for the 1K, CMD3 for the 4K) are obsolete and Flipso does not read them; it reports one as *Unsupported card*. |
| **MIFARE Ultralight EV1** | A small, low-cost NXP tag. CMD10 puts a full ITSO shell on one. |
| **NAK** | A negative acknowledgement: a tag refusing a command. |
| **NFC** | Near-Field Communication: the short-range radio a contactless card and reader talk over, at 13.56 MHz. |
| **NFC Forum Type 2 tag** | A simple memory tag read in four-byte *pages*, with no files and no applications: NTAG and Ultralight chips. ITSO puts CMD4, CMD9 and CMD10 on them. |
| **NTAG215, NTAG216** | NXP's NFC tag chips, 504 and 888 bytes, used for CMD9. |
| **NXP** | NXP Semiconductors, maker of DESFire, MIFARE and NTAG chips. A tag serial number starting `04` is NXP's, `05` Infineon's. |
| **Page** | Four bytes of a Type 2 tag's memory, the unit it is read and locked in. |
| **PICC** | Proximity Integrated Circuit Card: the card, as opposed to the reader. DESFire commands that address the chip rather than an application are *PICC level*. |
| **Poller** | The Flipper firmware's driver for talking to one kind of card over NFC. |
| **RATS** | Request for Answer To Select: the command that activates an ISO 14443-4 card. A Type 2 tag cannot answer it. |
| **READ (0x30), READ_CNT** | Type 2 tag commands: read four pages, and read a one-way counter. |
| **UID** | Unique Identifier: a chip's serial number, usually 7 bytes. Not the same as the ITSO card number. |

## Flipso's screens and features

| Term | Meaning |
| --- | --- |
| **Brand** | The name a card is sold under, such as *The Key* or *Freedom Pass*, which titles the card's menu. Taken from the shell owner's OID. |
| **Card** | The screen describing the card itself rather than its products: card number, expiry, issuer, the chip, and whether the shell's checksum matches. |
| **Card details** | What Flipso can say about a DESFire that is not an ITSO card, such as an Oyster. |
| **Demo cards** | Synthetic cards, built from the specification, that ship with the app (*About > Demo cards*) so every screen can be seen without a real card. |
| **History** | The page of a product's earlier transactions, from the value records behind the live one. |
| **ID** | The screen for an ITSO ID (TYP 16): the holder, their concession and entitlement. |
| **Journeys** | The screen of the cyclic log: recent taps and what each one did. |
| **Last tap** | The newest journey record, which the log points at. |
| **Off card** | Records only a saved file remembers - journeys, transactions or whole products that have since rolled off the card. A saved card accumulates them each time it is re-saved. |
| **Operators file** | `/ext/apps_data/flipso/operators.txt` on the SD card, where users can add or correct operator names and brands without rebuilding. |
| **Pay as you go** | The screen for a TYP 2 purse. |
| **Products** | The list of products on the card, then any it has dropped. |
| **Saved card** | A `.flipso` file of the raw bytes a read produced, in `/ext/apps_data/flipso/cards/`. Loading one runs the current decoder over the bytes, so it is also a test case. See [Saved cards](PROTOCOL.md#saved-cards). |
| **Shell owner** | The operator whose OID is in the card number: the card's issuer. |
| **Summary** | The first screen of a card: whether it and each product is still good, until when and with what. |
| **Technical** | The last page of every screen: codes, flags and identifiers that answer no passenger's question. |

## Flipper and development

| Term | Meaning |
| --- | --- |
| **ASan, UBSan** | AddressSanitizer and UndefinedBehaviorSanitizer: compiler checks that the host tests run under to catch memory errors and undefined behaviour. |
| **Apps Catalog** | Flipper's official store of third-party apps. |
| **.fap** | Flipper Application Package: an app as installed on the Flipper. The whole of it is loaded into the heap before the app starts. |
| **.fapassets** | Files packaged with a `.fap` that the firmware unpacks to the SD card rather than loading into memory, such as the station table and demo cards. |
| **flipctl** | Flipso's tool (`tools/flipper/flipctl`) for deploying to, driving and debugging a Flipper over USB. |
| **Furi** | The Flipper firmware's core library. A *furi_check failure* is a failed assertion that halts the device. |
| **Heap** | The Flipper's working memory, about 190 KB in all, which the app and the firmware share. |
| **Host tests** | The decoder and screen-text tests that run on a computer rather than the Flipper (`tools/test/run.sh`). |
| **Loader** | The firmware service that starts and stops apps. |
| **Momentum** | A popular community firmware for the Flipper Zero. Flipso runs on it and on the official firmware. |
| **RPC** | Remote procedure call: the protocol a computer uses to control the Flipper over USB - screenshots, file transfer, key presses. |
| **Synthetic card** | A card built by a script rather than read from a real one, for the tests (`tools/test/build_card.py`) and the demo cards (`tools/demo/`). |
| **ufbt** | micro Flipper Build Tool: builds, formats, lints and installs Flipper apps without the full firmware source. |
