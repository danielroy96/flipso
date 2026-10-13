# Sum up what a purse has been spent on

## Goal

The Pay as you go screen's History and Off card pages list each transaction
with its signed amount (`itso_value_change()`, `itso/ipe/itso_value.c`), and a
saved card keeps transactions the card has rolled off. Nothing adds them up.
"How much have I spent on fares this month?" is the question a holder brings
to a pay-as-you-go card, and Flipso has the numbers.

Size: medium, a day.

## What to build

1. **The sum**, a pure function over one purse's `value_history`
   (newest first, `ItsoProduct::value_history_count`):
   - fares: the negative changes, and how many;
   - top-ups: the positive changes, split into top-ups and refunds by the
     record's EventTypeCode (`ItsoValueRecord::txn`; the History page already
     names them through `itso_transaction_name()`);
   - the span: the oldest and newest record that contributed.
2. **Honesty about gaps.** `itso_value_change()` only gives an amount across
   two consecutive writes (TS# one apart), so records that rolled off between
   two reads leave transactions uncounted. Count the records with no amount
   and say so: "Spent: At least £23.40". A history of one record says nothing.
3. **Where.** CLAUDE.md: a new line goes on the page that answers its
   question, never on a page of its own. The History page answers "what has
   happened to it", so the totals open that page, above the first
   transaction:

   ```
   Spent: £23.40
     Fares: 6
     Since: 01/09/2026
   Topped up: £35.00
   ```

   When the Off card page holds older records, the totals cover both pages and
   say "Since" the oldest - they are the holder's spending, not the card's.
   Decide by looking at it on the device whether the totals should instead
   sit on the purse's first page under the balance.
4. **Charge to account** (TYP 4, `balance_is_spend`) counts spend up; the
   signs are already turned round by `itso_value_change()`, so the same sum
   works. TYP 5 has no amounts (counts reset per period) and gets nothing.

## Constraints

- Do not add journey fares from the log to this: the log's AMT groups and the
  purse's value records describe the same fares, and adding both counts each
  twice. The value records are the purse's own ledger; use them alone.
- Money in the record's own currency (`ItsoMoney`); records in different
  currencies are not summed together (`itso_value_change()` already refuses a
  change across currencies - skip, and count, those).
- Built in `format/product/flipso_product_history.c`; no new heap beyond the
  page string.

## Tests

- `tools/test/screen_text/test_value_history.c`: totals for Demo 04's purse
  (on-card and off-card records), a history with a gap shows "At least", a
  single record shows no totals, a TYP 4 account sums the right way round.
- House style on every demo.

## Done when

- A purse with history says what it has spent and been topped up with, since
  when, and never claims more certainty than its records allow.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
