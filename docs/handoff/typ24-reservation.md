# TYP 24 Reservation ticket: decode the dataset and the reservations

## Goal

TYP 24 is the rail ticket that carries everything a paper rail ticket does:
single or return, outward and return validity, routes, railcard discounts,
supplements, permitted breaks of journey, time-band restrictions, and in its
value group the seat reservations for each leg. Flipso reports it today from
its directory entry, and from its value record as "Rides left". None of the
dataset and none of the reservations are read.

Size: large, two to three days. The dataset has five variable-length locations
in its fixed part and eight repeating optional groups after it, and the
reservations sit in a Value Group Extension of their own. No real TYP 24 has
been seen, so it all rests on the specification, which has several internal
inconsistencies (listed below). Consider decoding the fixed part and the
reservations first, and leaving the optional groups for a second pass.

## The specification

TS 1000-5 clause 2.11, IPE Format Revision **2** only (no revision 1 is
defined): table 136 is the dataset, 137 the bitmap, 138 the flags, 139 the
value record. Clause 4.1.3, table AD3, is the reservations extension, VGXRef 3.
Read them with `tools/spec/itso_spec.py page 5 93` to `103`, and `145` to `147`.

The offsets in table 136 assume **six-byte LOC1s** (note 1 of the table: "the
length of these LOC1 data elements is 6 for UK Rail applications"). A LOC1 is
variable length, so everything after the first location has to be walked with
`itso_parse_location()`'s return value, not read at a fixed offset. The same
goes for table AD3. The dataset may not exceed 256 bytes, and a value group
holds **one** record, not the usual two or more.

### Fixed part (table 136)

| Offset | Bits | Element | Notes |
| --- | --- | --- | --- |
| 2 | 16-23 | RemoveDate | Already read |
| 3 | 24-39 | ProductRetailer | Already read |
| 5 | 40-51 | TYP24Flags | 12 bits, table 138: 0 follow-on, 1 duplicate, 2 replacement, 3 unfulfilled warrant, 4 carnet, 5 test ticket, 6 passenger details present, 7 reservations mandatory, 8 companion permitted, 9 auto-renew, 10-11 RFU |
| 6.5 | 52-55 | ProductTypeEncoding | 0 n journeys one way, 1 n journeys in return pairs, 2 n journeys either way |
| 7 | 56-87 | TicketNumber | UD, the ticket's reference; personal data under GDPR |
| 11 | 88-89 | NumberOfAssociatedIPEs | Counts for the optional groups below, present only with bitmap bit 2 |
| 11.25 | 90-91 | NumberOfDiscounts | |
| 11.5 | 92-93 | NumberOfSupplements | |
| 11.75 | 94-95 | NumberOfTransferTypes | |
| 12 | 96-98 | NumberOfInterchanges | |
| 12.375 | 99-101 | NumberOfRestrictionTimeBands | |
| 12.75 | 102-104 | NumberOfVehicleSpecificRestrictions | |
| 13.125 | 105-107 | NumberOfRoutingPoints | |
| 13.5 | 108-110 | Class | EN1545 AccommodationClassCode, as TYP 22 and 23 |
| 13.875 | 111-116 | AutoRenewTimeAfterExpiry | Days |
| 14.625 | 117-125 | NumberOfJourneysSold | "n": 1 a single, 2 a return, 10 a carnet of singles |
| 15.75 | 126-134 | OutPortionPeriodOfValidity | Days from OutPortionValidFrom |
| 16.875 | 135-143 | RtnPortionPeriodOfValidity | Days from RtnPortionValidFrom |
| 18 | | OperatorSpecificity | UD 2: valid only on one operator's services |
| 20 | | FaresTypeOfTicket | UD 3: the rail FTOT code |
| 23-25 | | PartySizeAdult, Child, Concession | |
| 26 | | IdDocumentReference | UD 4: railcard or photocard number |
| 30 | | Origin | LOC1 — **from here on, offsets assume 6-byte LOC1s** |
| 36 | | Destination | LOC1 |
| 42 | | AlternativeOrigin | LOC1 |
| 48 | | AlternativeDestination | LOC1 |
| 54 | | Route | UD 5 |
| 59 | | OutPortionValidFrom | DTS |
| 62 | | RtnPortionValidFrom | DTS |
| 65 | | RestrictionCode | UD 2 |
| 67 | | DaysTravelPermitted | DOW |
| 68 | | DaysRestrictionApplies | DOW |
| 69 | | AmountPaidCurrencyCode | VALC, **high** nibble — the reverse of TYP 22/23's order |
| 69.5 | | AmountPaidMOP | Low nibble |
| 70 | | AmountPaid | VALI 4 |
| 74 | | VendorLoc | LOC1: where it was sold |

### Optional groups (bitmap bit 2), in the table's order

Each group repeats as many times as its count above says.

| Group | Bytes each (6-byte LOC1s) | Elements |
| --- | --- | --- |
| AssociatedIPE | 1 | Directory entry of another IPE that is part of this product |
| Discounts | 11 | DiscountCode UD 5, DiscountAmount VALI 4, DiscountPercentage 10 bits (tenths of a percent), DiscountCodeType 5 bits, RFU 1 bit |
| Supplement | 3 | AssociatedSupplementCode, ASCII |
| Interchange | 13 | Exit LOC1, entry LOC1, PermittedInterchangeTime 6 bits (minutes), RFU 2 bits |
| Transfers | 3 | TransferEntitlementType 8 bits, NumberOfTransfers 9 bits, RFU 1 bit, ExtendedValidityPeriod 6 bits (hours) |
| Restriction1 (time bands) | 12 | OperatorApplicability UD 2, SpecificLocationApplicability LOC1, TimeBandOnOutOrReturn 2 bits, TimeBandStart TIME, TimeBandEnd TIME, arrive/depart flag, include/exclude flag, RFU 6 bits |
| Restriction2 (vehicle) | 14 | SpecificVehicleDepartureLocation LOC1, SpecificServiceId UD 6, SpecificVehicleDepartureTime TIME, RestrictionOrEasementFlag, RFU |
| Route | 7 | RoutingLocation LOC1, ViaNotVia 2 bits, RFU 6 bits |
| PaxDetail (bitmap bit 1, not bit 2) | 21 | Name ASCII 20, Gender 2 bits, RFU 6 bits — personal data |

### Value record (table 139)

Already located by `itso_value_records()`. Record bytes, counting from the
common header's start:

| Record byte | Element | Notes |
| --- | --- | --- |
| 0 (high nibble) | TransactionType | 2 = outward leg used, 6 = single or return leg consumed |
| 10 | JourneysRemaining | Already decoded as the counter |
| 11-12.375 | TransfersRemaining | 11 bits |
| 12.375 | JourneyPartUsedFlag | 1 bit: part-way through a leg, e.g. out at an interchange |
| 12.5 | NumberOfReservations | 4 bits: how many reservations the extension holds (bitmap bit 3) |
| 13-14 | RFU | |

### Reservations extension, VGXRef 3 (table AD3)

After the value record, flagged by VGBitMap bit 0. `itso_vgx_ref()` already
finds it and returns 3, which lands in `product->vgx_ref`.

| Offset | Element |
| --- | --- |
| 0-1 | VGXLength (6 bits), VGXRef bits 9-8 (0), VGXRef bits 7-0 (3) |
| 2 | DTSOfLastValidation, DTS |
| 5 | LocationOfLastValidation, LOC1 |
| 11 | BookingReference, ASCII 8 |

then NumberOfReservations reservations, 32 bytes each with 6-byte LOC1s:
LegDepartureDateTime DTS, LegServiceId ASCII 6, LegOrigin LOC1, LegDestination
LOC1, Coach ASCII 2, SeatNumber ASCII 3, AccommodationAttribute ASCII 4,
SeatDirection 2 bits (EN1545 SeatPositionCode), BerthUpperLower 2 bits (01
lower, 10 upper), ReservationType 4 bits (seat, berth, bike, no place,
wheelchair; UD), TogetherFlag 1 bit, RFU 7 bits.

### Spec inconsistencies to settle before coding

- **Optional group order.** The counts are in the order Associated, Discounts,
  Supplements, Transfers, Interchanges…, but table 136 lists the Interchange
  group before Transfers. The table's order is the layout. Pin whichever you
  pick in a test, with the reason.
- **TransfersRemaining** is 11 bits, yet its comment allows "up to 3 transfer
  types each with up to 511 transfers", which would need 27. Read it as one
  11-bit count, and say so on screen as the total.
- **Restriction2's** offsets add up to 13.5 bytes plus an RFU "at 15.5", but its
  count says 14. Walk to 14 with 6-byte LOC1s, and treat the rest as RFU.
- **Table AD3's** byte count says 36 for the fixed part, but its elements total
  19 with a 6-byte LOC1. Trust VGXLength, not either figure.
- **LOC1 lengths.** Note 1 says rail uses 6-byte LOC1s. A bus operator's could
  differ (a LocDefType 211 AtcoCode is up to 14). Always walk; never assume.
- The clause numbers go from 2.11 to "2.12.1.1.1 IPEBitMap Definition". That
  is a numbering slip in the spec, not a second table.

## Where the code is

- `itso/itso_parse.c`: `itso_parse_ipe()` dispatches by type. Add a
  `itso_parse_reservation_ipe()` for the fixed part.
  `itso_parse_journey_ends()` and `itso_parse_route()` walk LOC1s and a
  RouteCode the way this needs. `ItsoTicketTerms` already holds class, party
  sizes, amount paid with its payment method, and a route code; reuse what
  fits. Take the currency from 69's **high** nibble.
- **Memory.** Do not put the optional groups, the passenger name or the
  reservations in `ItsoProduct`: it is 652 bytes on the device and paid for
  per product. Decode them when the product screen is drawn, from the capture,
  the way `flipso_decode_capping()` in `flipso_format_product.c` calls
  `itso_parse_capping()`. That means an `ItsoReservation` struct, a
  `itso_parse_reservations()` in `itso/itso_i.h`, and an allocate/free around
  the screen. Keep in `ItsoProduct` only what the Summary and product list
  need: the product type encoding, journeys sold, and the outward and return
  validity.
- `itso/itso_names.c`: the product title is "Reserved journey"; the counter
  label for JourneysRemaining is "Rides left" (`ItsoCountRides`). "Journeys
  left" is closer to the spec's wording, if a new `ItsoCountKind` is justified.
- `flipso_format_product.c`: a `flipso_cat_reservation()` alongside
  `flipso_cat_ticket_terms()` for the fixed part, and a "Reservations" heading
  section, like "Fare capping", listing each leg: departure, service, from, to,
  coach and seat. The flags that describe the ticket (duplicate, replacement,
  test ticket) are facts about it and belong in the main section. Put the
  discount and supplement codes under Technical, since they mean nothing
  without the rail industry's tables.

## Design notes

- House style: `Label: Value`, capitalised values, details indented and
  labelled, money as `£`. Show every element the card holds, defaults
  included.
- A test ticket (TYP24Flags bit 5) is worth a prominent line near the top,
  "Test ticket: Yes", since it is not valid for travel.
- Outward and return validity are a start DTS plus a period in days. Show both
  the start and the computed last day, as "Outward: 01/10/2026 to
  31/10/2026", and say when the return portion has no validity of its own.
- The passenger's name (PaxDetail) is personal data, like the ITSO ID's. Show
  it as the ID screen shows a name, and keep it out of logs and dumps.
- Seat and coach are ASCII: print them as they stand.

## Tests

- `tools/test/build_card.py`: a `reservation_group` with the fixed part (6-byte
  NLC LOC1s), bitmap bits 1, 2 and 3, one of each optional group, one value
  record, and a VGXRef 3 extension holding two reservations. A builder like
  `capping_vgx()` in `tools/test/itso_build.py` for the extension. A second
  group using a longer LOC1 (an AtcoCode, LocDefType 211) proves the parser
  walks rather than assumes 6-byte locations.
- `tools/test/test_parse.c`: every field, and every truncation from exact-length
  allocations (see the loop in `spec_review_fields()`), for the dataset and
  separately for the extension. Sweep the optional group counts at their
  maximums (3, 3, 3, 3, 7, 7, 7, 7). The 256-byte cap must hold, and a count
  that would run past the dataset must stop cleanly.
- `tools/test/test_format.c`: pin the reservation lines in `spec_review()`.
- `tools/demo/build_demo_cards.py`: Demo 1's E10 "reserved journey" has a
  16-byte dataset at revision 1, which the spec does not define. Replace it
  with a real revision 2 TYP 24 carrying a reservation or two. It needs more
  sectors than E10 has (64-byte sectors on that card), so chain it across
  several. Update `tools/demo/README.md`.

## Done when

- A TYP 24's product screen shows the ticket's terms, locations, validity
  portions and flags, and a Reservations section listing each reserved leg.
- The heap cost of opening one on the device is measured with `flipctl mem`
  and recorded in `CLAUDE.md` if it is noticeable.
- `tools/test/run.sh` passes, and `ufbt lint` is clean.
- `docs/PROTOCOL.md`'s TYP 24 row and Limitations entry are updated, and the
  spec inconsistencies you settled are written down there.
- This brief and its row in `README.md` here are deleted.
