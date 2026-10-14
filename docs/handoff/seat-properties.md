# Name seat features from RDG's published list

## Goal

A TYP 24 reservation leg carries an AccommodationAttribute: four characters
"from NRS data feed" (RSPS3002 section 3.8.6, the TYP 24 value group and its
reservation extension), the National Reservation
System's reference data (RSPS5048), which is not published. Flipso names a
handful of codes it judged unambiguous (`itso_seat_attributes[]` in
`itso/itso_names.c`) and PROTOCOL.md calls the list "a best reading until a
real ticket, or RSPS5048, confirms it".

RSPS5080, *Coaches, Seats and Seat Properties Data Feed Specification*
(01-02, 18 Aug 2026, public at rspaccreditation.org), now publishes the
reservation system's codes. Its data comes from RARS, the Rail Availability
and Reservation Service that replaced NRS, and section 2.6 says of the
property codes: "Each TOC uses a limited set of these property codes, but the
code and description is the same for each TOC." Its examples confirm `AISL`
and add codes Flipso does not know: `LEGM` "MORE LEG ROOM", `COND`, `LOWR` and
`UPPR` (on a sleeper berth).

Replace the guessed list with RDG's.

Size: small to medium, a day.

## The source

`SeatPropertyDescriptions.csv` from the "Coaches, Seats and Seat Properties"
feed on the Rail Data Marketplace (RSPS5080 section 2.6): a header record,
then `"propertyCode","description"` - four characters and up to fifty. It is
refreshed nightly, but the code list changes rarely. Fetching it needs the
user's Marketplace account; check the product's licence allows redistribution
before committing the CSV.

The same feed has `InventoryClassDescriptions.csv` (section 2.5: a TOC's
inventory class codes - "1C" First Class, "9B" Bike). Read RSPS3002's TYP 24
reservation group to see whether any element carries an inventory class; if
one does, name it from the same feed in the same piece of work.

## What to build

1. **Which way to store it.** The list is likely tens of codes. Measure it:
   - small (a few hundred bytes of text) - generate a `const` table into
     `itso/itso_names.c` from the CSV with a builder in `tools/`, marked
     `/* clang-format off */` as generated headers are, and keep the builder
     and CSV in the repository;
   - larger - a file asset read through `lookup/flipso_table.c`, as the ticket
     type table is. Constant data reaches RAM (CLAUDE.md), so prefer the file
     past about half a kilobyte, and check with `flipctl size` either way.
2. **Wording.** RDG's descriptions are capitals ("SEAT POSITION AISLE").
   Present them in the house style - sentence case, short - either by a small
   override list for the common codes or by mechanical casing; keep the
   reservation leg's `Feature:` line (`format/product/flipso_product_reservation.c`).
3. **Codes RDG does not list.** `SEAT` and `HTMS` came from RDG's Guide to
   Rail Retailing (RSPS2100); keep them if the CSV lacks them. Anything else
   shows as the bare code, as now.
4. **PROTOCOL.md**: replace the "best reading" paragraph under "National
   Rail's profile" with the source, and update `docs/GLOSSARY.md` if it lists
   codes.

## Tests

- `tools/test/screen_text/test_reservation_screen.c`: a leg with `AISL`,
  one with `LEGM`, one with an unknown code.
- If generated, `run.sh` rebuilds the table from the CSV and fails when it is
  stale, as it does for the other generated data.

## Done when

- Every seat feature a reservation can carry is named from RDG's list.
- `tools/test/run.sh` passes and `ufbt lint` is clean.
- This brief and its row in `README.md` here are deleted.
