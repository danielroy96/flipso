# Days remaining on expiries

## Goal

Dates are exact but make the reader do arithmetic. Add how long is left:

```
Expires: 31/03/2027
  Days left: 185
```

and in the Summary, for a period ticket, show the date that matters day to day —
the current pass — rather than the whole product's expiry.

## Where the code is

- `flipso_format.c`: `flipso_cat_expiry()` writes every expiry line (label, a
  past-tense label once expired, and "No expiry" for the open dates —
  `itso_date_open()`). `FlipsoFormat.now` is the Unix time to measure from.
- `itso_date_to_unix()` / `itso_date_expired()` in `itso/itso_util.c`.
- Summary: `flipso_summary_product()` in `flipso_format.c`. Period tickets
  (TYP 22) carry `product->has_current_expiry` / `current_expiry` (the pass in
  use) and `stored_expiry` (the unused stock).

## Design notes

- Keep the date line exactly as it is and add an indented, labelled detail line,
  so the house style holds and line widths do not change: `  Days left: 185`.
  "Today" for zero; nothing once expired (the label already says "Expired").
- Decide which expiries get it: product expiry, current pass, card expiry are
  the useful ones; "Unused passes until" probably not. Do not add it to every
  call of `flipso_cat_expiry()` blindly — it is also used for entitlement dates.
- Summary for a TYP 22 with a current pass: `Period ticket: Until 20/10/2026`
  (the current pass) with `  Passes left: 5` as now. Fall back to the product
  expiry when there is no current pass.
- The Flipper's clock is local time and ITSO dates are UK dates; count whole
  calendar days, not 86,400-second blocks from "now" (a ticket expiring tomorrow
  at 00:01 is "1 day", not "0").

## Tests

- `test_format.c` already fixes `now`; add checks for 0, 1 and many days, the
  open date, and the Summary using the current pass. Note its "now" constant is
  **2025**-09-20 despite the comment saying 2026 — fix the comment while there.

## Done when

- Days left shows under product, card and current-pass expiries.
- The Summary shows the current pass for period tickets.
- House-style checks pass; screenshot of Demo 1's Summary and Period ticket.
