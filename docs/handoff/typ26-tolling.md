# TYP 26 Open System Tolling Ticket: decode the dataset

## Goal

A tolling ticket (a bridge, tunnel or ferry crossing whose fee does not depend
on distance) is reported today from its directory entry and value record alone:
rides left and auto-renew. Its dataset — vehicle class, issue date, validity
start, passback and print flags — is not read. Decode it and show it.

Size: small, about two hours. No card, test fixture or demo card carries a TYP
26 today, so most of the work is building one.

## The specification

TS 1000-5 clause 2.13, IPE Format Revision 1 only. Table 40 is the dataset,
table 41 the bitmap, table 42 the value record, table 43 the flags. Read them
with `tools/spec/itso_spec.py page 5 108` to `111`. Offsets are absolute from
the start of the IPE data group.

| Offset | Bits | Element | Notes |
| --- | --- | --- | --- |
| 2 | 16-23 | RemoveDate | Already read by `itso_parse_ipe_common()` |
| 3 | 24-39 | ProductRetailer | Already read |
| 5.25 | 42-47 | PassbackTime | Earlier than most types: bits 40-41 are RFU |
| 6 | 48-55 | TYP26Flags | Bit 5 PrintTicket, bit 6 PrintReceipt |
| 7 | 56-63 | TYP26Class | Owner-defined class of vehicle or service, **not** the EN1545 AccommodationClassCode the other types carry |
| 8.25 | 66-79 | IssueDate | DATE |
| 10 | 80-103 | ValidityStartDTS | DTS |
| 13 | 104-159 | UserDefined | Seven owner-defined bytes |
| 20 | 160-167 | AutoRenewQuantity3 | Optional, bitmap bit 1: rides added per renewal |

The mandatory part ends at byte 20. There is no amount paid and no expiry time.

The value record (table 42) is already decoded: CountRemainingRidesJourneys is
record byte 10 and TYP26ValueFlags bit 0 (auto-renew) is record byte 11.

## Where the code is

- `itso/itso_parse.c`: add a case to the `itso_parse_ipe()` switch for a new
  `itso_parse_tolling_ipe()`. Fill `ItsoTicketTerms` (`issue_date`,
  `valid_from_dts`, `renew_quantity`, and set `t->valid`), `product->passback`
  / `has_passback`, and `print_defined` / `print_flags`.
- TYP26Class must **not** go into `ItsoTicketTerms::travel_class`.
  `flipso_cat_ticket_terms()` renders that through `itso_class_name()` as
  "Standard" or "First", which would be wrong here. Give it a field of its own.
  UserDefined is seven bytes: either keep it (7 bytes on every product) or
  keep nothing and read it from the capture when the product screen is drawn,
  as `flipso_decode_capping()` does for the capping extension. The second
  costs no memory, and is the better choice unless the bytes turn out to
  matter.
- `flipso_format_product.c`: the class as "Vehicle class: 3" under the terms,
  and UserDefined under Technical through `flipso_cat_code_bytes()` (text if
  printable, else hex). "Renewal adds: N rides" already works once
  `renew_quantity` is set, and `product->auto_renew` comes from the value
  record.

## Design notes

- The product title is "Toll pass" (`itso_typ_name()`). Keep it.
- Show every element the dataset holds, defaults included. PassbackTime 0 is
  "Set by the operator".
- Without an ExpiryTime, "Ends at" does not apply. Don't invent one.

## Tests

- `tools/test/build_card.py`: add a `tolling_group` (dataset with bit 1 set,
  and a value group using `voucher_tail()`, which is table 42's shape too).
  Emit it.
- `tools/test/test_parse.c`: check each field through `parse_group()`, and add
  the group to the exact-length truncation loop in `spec_review_fields()`.
- `tools/test/test_format.c`: pin "Vehicle class" and the print flags in
  `spec_review()`, and check that no "Class: Standard" line appears.
- `tools/demo/build_demo_cards.py`: no demo card has a TYP 26. Demo 1 is full
  (twelve entries), and Demo 2 has one free entry (E7) and free sectors 8, 10,
  12 and 13. Add a toll pass there, and describe it in `tools/demo/README.md`.

## Done when

- A toll pass shows its vehicle class, issue date, validity start, passback,
  renewal quantity and print flags.
- `tools/test/run.sh` passes, and `ufbt lint` is clean.
- `docs/PROTOCOL.md`'s TYP 26 row and Limitations entry are updated.
- This brief and its row in `README.md` here are deleted.
