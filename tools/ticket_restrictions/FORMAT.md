# Ticket restriction table format

`ticket_restrictions.dat` maps a rail restriction code, "OP", to what it
allows, in National Rail Enquiries' words: "Not valid on trains timed to depart
before 09.30". It is written by `build_ticket_restrictions.py` and read by
`lookup/flipso_ticket_restrictions.c`, through the reader the packed tables
share (`lookup/flipso_table.c`).

It is the ticket type table's design (`tools/ticket_types/FORMAT.md`): a file
asset searched in place, opened only for a lookup, since only a reserved
journey (TYP 24) carries a restriction code. Each entry points at three texts
rather than one: the restriction's name, where it has one beyond its code,
and its outward and return wording.

## Layout

All integers are little-endian, matching the device.

```
offset  size  field
 0      4     magic, "FRST"
 4      1     version, currently 1
 5      1     longest text in this table, in bytes
 6      2     reserved, zero
 8      4     entry count
12      4     offset of the text blob, absolute

16      14 x count  index, ascending by code as bytes:
        +0   2      code, ASCII, as the card holds it
        +2   3      name: offset of its text, relative to the blob
        +5   1            length in bytes; 0 for none
        +6   3      outward: offset
        +9   1               length; 0 for none
        +10  3      return: offset
        +13  1              length; 0 for none

        ...        text blob: UTF-8, no separators or terminators
```

## Notes

- Texts are ASCII but for the pound sign, which Flipso draws itself, and at
  most `FLIPSO_TICKET_RESTRICTION_TEXT_MAX` (250) bytes - the most a length
  byte can say, less a little. The feed's longest runs to 3.5 KB; the builder
  cuts at a word and ends " ... More at nationalrail.co.uk".
- A name is kept only where it says more than the code: for 632 of the 644
  codes the feed's name is the code itself.
- 263 restrictions say the same both ways, and many codes share wording, so
  each distinct text is stored once and entries point at the same bytes.
- The reader range-checks every header field and every text against the file
  size. The table lives on a removable card, so a truncated or unrelated file
  has to fail cleanly rather than read off the end.
