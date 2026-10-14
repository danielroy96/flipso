# Name the train operator and supplements on a reserved journey

## Goal

Two TYP 24 elements are codes from the rail industry's own data, and Flipso
shows them as codes:

- **OperatorSpecificity** - "Used to indicate that product is only valid on
  the services of a specific operator ... Derive from carrier in timetable
  information for product/flow sold" (RSPS3002 section 3.8.3): a two-letter
  TOC code, `GW`, `SN`, `SW`. The time-band group's OperatorApplicability is
  the same kind of code. Flipso shows `Only on operator: GW`
  (`format/product/flipso_product_reservation.c`). An "operator-only" ticket
  is one of the commonest ways to buy the wrong ticket, so the name matters.
- **AssociatedSupplementCode** - "the 3 character supplement code for the
  rail supplements priced (as defined within the RSP fares data)" (section
  3.8.3): seat reservations, sleeper berths, bikes, car parking. Flipso shows
  `Supplement: ABC` under Technical.

Name both.

Size: small to medium, a day.

## The sources

Both are in RSPS5045, *Fares and Associated Data Feed Interface Specification*
(P-02-04, April 2026, public at rspaccreditation.org), whose data comes from
the National Rail Data Portal and needs a registration:

- **TOCS** (section 4.21), record type `T`: `TOC_ID` (2), `TOC_NAME` (30),
  `ACTIVE_INDICATOR`. "Rate of change: infrequent." A few dozen rows.
- **SUPPLEMENTS** (section 4.13), record type `S`: `SUPPLEMENT_CODE` (3),
  `DESCRIPTION` (20), `SHORT_DESC` (12) - "#" or "$" in either means "£" -
  and `SUPPL_TYPE` (`SEA` seat, `SLE` sleeper, `BIK` bike, `EXA` extras such
  as car parks, `QUO` quota-controlled). Records are dated; keep the live
  ones.

For TOC codes there is a source that needs no registration: railwaycodes.org.uk
lists the two-letter business codes, and Flipso already has that site's
agreement to redistribute its data (README, "NLC codes"). Check the TOC list
there against RSPS5045's before choosing; the timetable's carrier codes are
what RSPS3002 says to use.

Check the fares data's licence allows redistribution before committing any of
it; if it does not, the TOC names can still come from railwaycodes.

## What to build

1. **TOC names.** A few dozen two-letter codes and names is about a kilobyte
   - as a file asset it costs no RAM, and the station table's builder
   (`tools/stations/`) could carry them in the same `.dat` under a separate
   key range; or a separate small table through `lookup/flipso_table.c`.
   Not a `const` array (CLAUDE.md: constant data reaches RAM).
   Show `Only on operator: Great Western Railway`, the code under Technical.
2. **Supplement names.** A table of the live supplement codes, built like
   `tools/ticket_types/`, opened per lookup. Show
   `Supplement: Seat reservation` (sentence case from `DESCRIPTION`, `#`/`$`
   turned into `£`) where the supplements are listed, the code under
   Technical. If a supplement's type is `SEA`, `SLE` or `BIK`, it explains the
   reservation leg beside it - consider saying so on that page.
3. Wire each new lookup into `FlipsoFormat`, `application.fam`,
   `tools/test/sources.py`, and `tools/test/screens.py`'s parts and stub
   assets.

## Tests

- Lookup tests against small built tables, as `test_ticket_types.c` does.
- `tools/test/screen_text/test_reservation_screen.c`: Demo 01's reserved
  journey (or a synthetic one) with an operator code and two supplements shows
  their names; unknown codes still show as codes.

## Done when

- A reserved journey names the operator it is limited to and the supplements
  it was sold with.
- PROTOCOL.md's "National Rail's profile" names the sources; the README's
  licensing section covers any new data.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
