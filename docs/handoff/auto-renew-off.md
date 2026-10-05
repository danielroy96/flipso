# Auto-renew when it is off, and what a renewal would add

## Goal

A product whose value record carries an auto-renew flag shows it only when the
flag is set: "Auto-renew: On", with "Renewal adds: N ..." under it. When the
flag is clear the screen says nothing, and the renewal quantity the dataset
holds goes unshown with it. That breaks the house rule to show what the card
holds, defaults included: the flag is on the card either way, and so is the
quantity.

Show "Auto-renew: Off" wherever the value record carries the flag, and the
renewal quantity under it whenever the dataset carries one.

Size: small, about two hours, most of it in the tests.

## Which products

| TYP | Flag (TS 1000-5) | Quantity | Decoded in |
| --- | --- | --- | --- |
| 22 period | TYP22ValueFlags bit 0 (tables 29, 29a, 3.29) | AutoRenewQuantity1, mandatory: passes or days | `itso_ipe_period.c` |
| 23 journey | TYP23ValueFlags bit 0 (tables 33, 33a, 33b) | AutoRenewQuantity, revision 3 only: rides | `itso_ipe_journey.c` |
| 24 reserved | TYP24Flags bit 9, in the dataset (table 138) - already shown, clear, under Technical | how many days past expiry it renews to, from the dataset ("Renews until") | `itso_ipe_reservation.c` |
| 25 voucher | TYP25ValueFlags bit 0 (table 38) | AutoRenewQuantity2, IPEBitMap bit 1: uses | `itso_ipe_voucher.c` |
| 26 toll pass | TYP26ValueFlags bit 0 (table 42) | AutoRenewQuantity3, IPEBitMap bit 1: crossings | `itso_ipe_tolling.c` |

A product with no value record has no flag to show. TYP 24's flag is in its
dataset and already reads either way; what it lacks is its renewal period when
the flag is clear. A TYP 25 or 26 without bitmap bit 1 has no quantity.

## Where the code is

- `ItsoProduct::auto_renew` is a bool, false both when the flag is clear and
  when there is no record to carry it. The screens need to tell the two apart:
  a `has_auto_renew` beside it, set by each value decoder above (and by the
  TYP 24 dataset decoder), costs nothing - check `flipctl size` and the struct
  layout, as `ItsoTicketTerms::vehicle_class` was fitted into padding.
- `format/product/flipso_product_details.c`: the "Auto-renew: On" line and the
  voucher's and toll pass's "Renewal adds". `format/product/flipso_product_ticket.c`:
  the period and journey tickets' "Renewal adds". `format/product/flipso_product_reservation.c`:
  TYP 24's line.
- A quantity is a detail of the renewal, so it stays indented under the
  Auto-renew line, on whichever page that is.

## Tests

- `tools/test/screen_text/`: a product of each type with the flag clear shows
  "Auto-renew: Off", and the quantity under it where the type has one.
- Demo cards: check `tools/demo/new_encodings.py` and the demo README after
  any change to the builder, and that `tools/test/screen_text/` still passes
  every demo screen.

## Done when

- Every product that carries the flag says On or Off, and every renewal
  quantity on the card is shown.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
