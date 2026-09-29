# TYP 25 Travel Related Voucher: decode the dataset

## Goal

A voucher (car parking with a rail ticket, a meal on a train) is reported today
from its directory entry and its value record alone: how many uses are left and
whether it auto-renews. Its IPE dataset — when it was issued, when it runs out
each day, what it is for, what it is worth and what was paid — is not read at
all. Decode it and show it.

Size: small, about two to three hours including tests.

## The specification

TS 1000-5 clause 2.12, IPE Format Revision 1 only. Table 36 is the dataset,
table 37 the bitmap, table 38 the value record, table 39 the flags. Read them
with `tools/spec/itso_spec.py page 5 104` to `107`. Offsets are absolute from
the start of the IPE data group, header included.

| Offset | Bits | Element | Notes |
| --- | --- | --- | --- |
| 2 | 16-23 | RemoveDate | Already read by `itso_parse_ipe_common()` |
| 3 | 24-39 | ProductRetailer | Already read |
| 5 | 40-47 | TYP25Flags | Bit 5 PrintTicket, bit 6 PrintReceipt; the rest RFU |
| 6.25 | 50-55 | PassbackTime | Minutes; 0 is "the POST's own rule" |
| 7.25 | 58-71 | IssueDate | DATE |
| 9 | 72-95 | ValidityStartDTS | DTS |
| 12.625 | 101-111 | ExpiryTime | TIME; 1440 and over is the next day, as TYP 22 |
| 14 | 112-119 | ServiceID | Owner-defined: which car park, which meal |
| 15 | 120-135 | MaxValue25 | VALI: the most the voucher buys |
| 17 | 136-139 | MaxValueCurrencyCode | VALC, high nibble |
| 17.5 | 140-143 | AmountPaidCurrencyCode | VALC, low nibble |
| 18 | 144-159 | AmountPaid | VALI |
| 20 | 160-163 | AmountPaidMethodOfPayment | EN1545 PaymentMeansCode |
| 20.5 | 164-175 | AmountPaidVATSalesTax | 0.01% steps |
| 22 | 176-183 | UserDefined | Owner-defined |
| 23 | 184-191 | AutoRenewQuantity2 | Optional, bitmap bit 1: uses added per renewal |

The mandatory part ends at byte 23. Bitmap bit 0 is the IIN, appended as the
last three bytes of the dataset like every other type, and already handled.

The value record (table 38) is already decoded by `itso_decode_value_record()`
and `itso_parse_value_records()`. CountUsesAvailable is record byte 10 and
TYP25ValueFlags bit 0 (auto-renew) is record byte 11. Nothing to change there.

## Where the code is

- `itso/itso_parse.c`: `itso_parse_ipe()` dispatches by type. Add a case
  calling a new `itso_parse_voucher_ipe()`. `itso_parse_journey_terms()` is the
  closest model: the same kind of fixed offsets, filling `ItsoTicketTerms`.
- `itso/itso.h`: `ItsoTicketTerms` already has `issue_date`, `valid_from_dts`,
  `expiry_time`, `amount_paid`, `paid_mop`, `vat` and `renew_quantity`. Reuse
  them, and set `t->valid` so `flipso_cat_ticket_terms()` shows them.
  `ItsoProduct` has `max_value` for MaxValue25 (priced in MaxValueCurrencyCode,
  not in the value record's currency) and `print_defined` / `print_flags` for
  the two print flags. ServiceID and UserDefined need two new bytes. Every byte
  in `ItsoProduct` is paid for per product, so keep them to `uint8_t`.
- `itso/itso_names.c`: `itso_count_name()`. A voucher's counter is
  "CountUsesAvailable", but `itso_decode_value_record()` gives it
  `ItsoCountRides`, so it shows as "Rides left". Add an `ItsoCountUses`
  ("Uses left") and use it for TYP 25. Check the Summary and product list
  still read naturally.
- `format/flipso_format_product.c`: `flipso_cat_ticket_terms()` shows the terms once
  `t->valid` is set. Its "Renewal adds" line picks passes or days. A voucher
  adds uses, so give it a third wording. MaxValue25 fits the "Spending limit"
  line in `flipso_cat_purse_terms()`, or a line of its own ("Worth up to").
  ServiceID and UserDefined belong under Technical, beside the validity and
  promotion codes.

## Design notes

- House style as `format/flipso_format.h` sets it out: `Label: Value`, values
  capitalised, details indented and labelled, money as `£`. Show every element
  the dataset holds, defaults included (see the "show default values" rule).
  PassbackTime 0 is "Set by the operator", as for every other type.
- A zero AmountPaid with a zero currency code is the spec's "not used". Follow
  what TYP 22 and 23 already do with it.
- ServiceID and UserDefined mean nothing without the owner's tables. Show them
  as numbers, as `Validity code` and `Promotion code` are.

## Tests

- `tools/test/build_card.py`: add a `voucher_group` with every element set,
  including bitmap bit 1, and a value group. Emit it in the `card_data.h` list
  at the bottom.
- `tools/test/test_parse.c`: decode it with `parse_group()` (see
  `spec_review_fields()`) and check every field. Add it to the truncation loop
  there: every length from 1 to the full group, each an exact-length
  allocation, so ASan sees an over-read.
- `tools/test/test_format.c`: pin the new lines in `spec_review()` with
  `product_screen()`.
- `tools/demo/build_demo_cards.py`: Demo 01's E7 voucher has a 16-byte dataset,
  too short for table 36 (23 bytes mandatory). Rebuild it as a real one, then
  update the Demo 01 row in `tools/demo/README.md`.

## Done when

- A voucher's product screen shows its terms, value, price and print flags,
  and "Uses left" rather than "Rides left".
- `tools/test/run.sh` passes, and `ufbt lint` is clean.
- The TYP 25 row in `docs/PROTOCOL.md`'s product coverage table lists what is
  decoded, and the Limitations entry no longer names TYP 25.
- This brief and its row in `README.md` here are deleted.
