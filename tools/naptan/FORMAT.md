# Stop table format

`naptan.dat` maps a bus stop code to a stop name. It is written by
`build_naptan.py` and read by `flipso_naptan.c`.

It is the station table's design applied to a table a hundred times the size,
and for the same reason: a Flipper has 256 KB of RAM and a `.fap` is loaded into
it whole, so the names live in a file on the SD card that is searched in place.
A lookup costs one file handle, a 16-byte scratch buffer and about twenty short
reads, whether the table holds a county or the country.

The one structural difference is that this table has **two indexes over one blob
of names**, because an ITSO card can name a stop two ways and the two codes want
different keys.

## The two codes

NaPTAN gives every stop both an `AtcoCode` and, usually, a `NaptanCode`. ITSO
carries either:

- **AtcoCode** (LocDefType 211) is stored whole — up to twelve ASCII characters,
  `0100BRP90310`. It indexes directly.

- **NaptanCode** (LocDefTypes 206, 212 and 216) is stored *folded*. A NaptanCode
  is eight characters such as `cumfatda`, and TS 1000-1 table 28 maps its letters
  onto a telephone keypad — `ABC`→2, `DEF`→3, … `WXYZ`→9 — so that eight
  characters pack into four bytes of BCD. Case is not preserved either. The card
  therefore holds a number, not the code, and **the folding cannot be undone**.

  So the builder folds the register the same way and keys the index on the
  result. A code shorter than eight characters is right-justified with leading
  zeros, which falls out of treating the folded digits as a number.

  The folding is nearly injective — across the full register about 770 of
  408,000 folded keys collide, roughly one stop in five hundred — and a colliding
  key keeps the first stop and is counted in the builder's output.

## Layout

All integers are little-endian, matching the device.

```
offset  size  field
 0      4     magic, "FNPT"
 4      1     version, currently 1
 5      1     longest name in this table, in bytes
 6      2     reserved, zero
 8      4     NaptanCode entry count
12      4     AtcoCode entry count
16      4     offset of the AtcoCode index, absolute
20      4     offset of the name blob, absolute

24      8 x naptan count   NaptanCode index, ascending by code:
        +0  4       folded NaptanCode, 0-99999999
        +4  3       offset of the name, relative to the blob
        +7  1       length of the name, in bytes

        16 x atco count    AtcoCode index, ascending by code:
        +0  12      AtcoCode, ASCII upper case, zero padded
        +12 3       offset of the name, relative to the blob
        +15 1       length of the name, in bytes

        ...        name blob: ASCII, no separators or terminators
```

A NaptanCode lookup is: parse the digits to a `uint32`, binary-search the first
index reading 8 bytes per step, then one more read for the name. An AtcoCode
lookup pads the code to twelve bytes and `memcmp`s its way down the second
index, which is sorted in exactly that order.

Either index may be empty, and the builder can be told to omit the AtcoCode one;
a table with neither is refused.

## Notes

- Names are ASCII and at most `FLIPSO_NAPTAN_NAME_MAX` (40) bytes. The reader
  rejects a table whose header declares anything longer, so the two constants
  have to move together.
- **Names are shared between the two indexes and deduplicated.** Both codes for
  a stop point at the same bytes, and so do the several thousand stops called
  `High Street (adj)`. On the full register this saves about a third of the blob.
- The name blob is addressed by a 24-bit offset, which caps it at 16 MB. Unlike
  the station table that limit is reachable: the full register with localities is
  around 12 MB, so the builder fails cleanly rather than wrapping.
- AtcoCodes are upper-cased by both the builder and the reader, since the
  register publishes them upper case but nothing obliges a card to.
- Codes are unique and sorted in both indexes; the writer sorts, and the reader
  assumes it.
- The reader range-checks every header field against the file size, and
  recomputes both index offsets from the counts rather than trusting them. The
  table lives on a removable card, so a truncated or unrelated file has to fail
  cleanly rather than read off the end.
