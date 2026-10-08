# Ticket type table format

`ticket_types.dat` maps a rail Fares Type of Ticket code, "SOR", to the name a
passenger knows the ticket by, "Anytime Return". It is written by
`build_ticket_types.py` and read by `lookup/flipso_ticket_types.c`.

It is the station table's design (`tools/stations/FORMAT.md`), for the same
reason: three and a half thousand names would be over 50 KB of `.rodata`, which
the Flipper loads into RAM with the rest of the `.fap`. So the table is a file
asset, searched in place - a sorted index of fixed-width entries, so entry *i*
is at a computable offset and a lookup is a dozen short reads, followed by the
names packed end to end.

Unlike the station table, the reader does not keep the file open: only a
reserved journey (TYP 24) carries a ticket type, so it opens the file for the
lookup and closes it again rather than holding a handle, and its buffers, for
as long as the app runs.

## Layout

All integers are little-endian, matching the device.

```
offset  size  field
 0      4     magic, "FTKT"
 4      1     version, currently 1
 5      1     longest name in this table, in bytes
 6      2     reserved, zero
 8      4     entry count
12      4     offset of the name blob, absolute

16      7 x count   index, ascending by code as bytes:
        +0  3       code, ASCII, as the card holds it
        +3  3       offset of the name, relative to the blob
        +6  1       length of the name, in bytes

        ...        name blob: ASCII, no separators or terminators
```

## Notes

- Names are ASCII and at most `FLIPSO_TICKET_TYPE_NAME_MAX` (64) bytes. The
  reader rejects a table whose header declares anything longer, so the two
  constants have to move together.
- Over a thousand codes share a name - each operator has its own codes for an
  "Anytime Return" - so each distinct name is stored once and the index entries
  point at the same bytes. That takes the file from about 75 KB to 58 KB.
- Codes are unique and sorted by their bytes; the writer sorts, and the reader
  compares the same way.
- The reader range-checks every header field against the file size. The table
  lives on a removable card, so a truncated or unrelated file has to fail
  cleanly rather than read off the end.
