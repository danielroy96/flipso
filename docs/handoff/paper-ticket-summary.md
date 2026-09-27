# A Summary for paper tickets

## Goal

A paper ticket's Summary is thin today:

```
Ticket: Active
Paper period ticket: Until 21/09/2026
```

It should answer what a holder of a paper ticket asks: is it valid, how many
rides are left, and when and where was it last used:

```
Ticket: Active
Multi-use ticket: Until 21/09/2026
  Rides left: 1
Last used: Hillhead
  When: 21/09/2026 17:47
Price paid: £3.30
```

## Where the code is

- `flipso_format_summary()` in `flipso_format.c`; `card->shell_compact` marks a
  paper ticket (a CMD4 Type 2 tag), whose one product is `card->products[0]`.
- The Space Saving IPE's facts are in `card->space` (`ItsoSpaceSaving` in
  `itso/itso.h`): `has_last_use` / `last_use_dts` (TYP 27, 28, 29 rev 2),
  `usage_alighted`. For TYP 29 revision 1, where it was last used is
  `product->from` (an SPT Subway station — see `itso_space_usage_place()` in
  `itso_parse.c`) and there is no timestamp.
- `flipso_cat_space_saving()` in `flipso_format_product.c` already renders all of
  these on the product screen — reuse its logic rather than re-deriving it.
- Price: `product->ticket.amount_paid`.
- The menu already has a "Ticket" row for paper tickets (`FlipsoMenuItemTicket`
  in `scenes/flipso_scene_menu.c`), so the Summary need not repeat everything.

## Design notes

- "Last used: Never" when the ticket has not been through a gate — say it; it is
  information on a ticket.
- `last_use_dts` of zero means never used, not a 2028 timestamp.
- Keep the full-card Summary unchanged.

## Tests

- `test_format.c` has paper-ticket fixtures (`cmd4_pages`, `cmd4_return`,
  `cmd4_spent`, …) — pin the new Summary lines for a day ticket and a return.
- Demo cards 5 and 6 are paper tickets.

## Done when

- Demo 5 and 6 Summaries show state, what is left, last use and price.
- Screenshots of both on the device.
