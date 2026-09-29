# Transaction amounts in the balance history

## Goal

A purse's history currently shows the balance *after* each transaction:

```
## Earlier on card
Fare paid
  When: 18/09/2026 08:12
  Balance: £27.65
Top-up
  When: 12/08/2026 18:05
  Balance: £31.20
```

Nobody wants the balance; they want what the fare cost, or how much a top-up
added. It can be worked out from consecutive balances, so show it:

```
Fare paid
  When: 18/09/2026 08:12
  Amount: -£3.55
  Balance: £27.65
```

## Where the code is

- `flipso_format_product.c`: `flipso_cat_value_record()` renders one record,
  `flipso_cat_value_history()` walks them (newest first, split into
  "Earlier on card" and "Off card" sections). The live record is
  `product->value_history[0]` and is shown as the headline balance, not in the
  history — its amount belongs on the "Last transaction" line
  (`flipso_cat_last_transaction()`).
- `itso/itso.h`: `ItsoValueRecord` — `amount` (balance after, `ItsoMoney`),
  `count` / `has_count` (rides, passes, points), `ts` (the 12-bit TS#), `dts`.
- `product->balance_is_spend` is set for TYP 4 (charge to account): the amount
  counts *up*, so a positive difference is spend, not a top-up.

## Design notes

- Difference = this record's value minus the next-older record's. Only show it
  when the two records are **consecutive writes**: TS# of the newer equals the
  older's + 1 modulo 4096 (see `itso_ts_newer()` in `itso_parse.c`). A saved
  card's history can have gaps (records that rolled off between reads), and a
  difference across a gap would be the sum of several transactions presented as
  one. When they are not consecutive, show nothing rather than a wrong number.
- Only across the same currency (`ItsoMoney.currency`); both must be `valid`.
- The oldest record has nothing to compare with: no amount line.
- Counters work the same way and are worth doing at the same time:
  `Rides left: 8` → `  Change: -1`. Label choice is yours; keep it
  `Label: Value` and capitalised.
- Sign: always show it (`-£3.55`, `+£20.00`); `itso_format_money()` already
  writes a leading `-`; add `+` for positive.
- Consider the Summary's "Last tap" / PAYG headline: "Last transaction: Fare paid
  -£3.55" is the single most useful line on the purse screen.

## Tests

- `tools/test/test_format.c`: pin a history with consecutive records showing the
  amount, a gap showing none, and TYP 4 showing spend with the right sign.
- Demo card 4 ("past reads") has a long purse history with off-card records —
  use it to check the gap rule on real-looking data.

## Done when

- Every consecutive pair shows its amount; gaps and the oldest record show none.
- House-style checks pass for every demo card.
- Screenshot of Demo 01's Pay as you go screen on the device.
