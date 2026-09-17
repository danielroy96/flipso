# Station table format

`stations.dat` maps a four-digit National Location Code to a station name. It is
written by `build_stations.py` and read by `flipso_stations.c`.

The shape is dictated by where it has to live. A Flipper has 256 KB of RAM and a
`.fap` is loaded into it whole, so the table cannot be a C array: 2,500-odd names
as `.rodata` would be a larger allocation than the rest of Flipso put together,
and the app would simply fail to load. Instead the table stays a file on the SD
card and is searched in place, which costs one file handle and a 47-byte scratch
buffer no matter how many entries it holds.

That rules out any format you have to parse from the front. What is left is an
index of fixed-width entries — so entry *i* is at a computable offset and a
binary search is a dozen seeks — followed by the names packed end to end, since
padding every name to the longest would waste about a third of the file.

## Layout

All integers are little-endian, matching the device.

```
offset  size  field
 0      4     magic, "FSTN"
 4      1     version, currently 1
 5      1     longest name in this table, in bytes
 6      2     reserved, zero
 8      4     entry count
12      4     offset of the name blob, absolute

16      6 x count   index, ascending by code:
        +0  2       code, the NLC as a number (0000-9999)
        +2  3       offset of the name, relative to the blob
        +5  1       length of the name, in bytes

        ...        name blob: ASCII, no separators or terminators
```

A lookup is therefore: parse the NLC to a `uint16`, binary-search the index
reading 6 bytes per step, then one more read for the name itself.

## Notes

- Names are ASCII and at most `FLIPSO_STATION_NAME_MAX` (40) bytes. The reader
  rejects a table whose header declares anything longer, so the two constants
  have to move together.
- The name blob is addressed by a 24-bit offset, which caps it at 16 MB — about
  a million entries, so the limit is theoretical.
- Codes are unique and sorted; the writer sorts, and the reader assumes it.
- NLCs that are not four digits (ITSO allows any four ASCII characters in the
  field) never match, which is correct: the published register is numeric.
- The reader range-checks every header field against the file size. The table
  lives on a removable card, so a truncated or unrelated file has to fail
  cleanly rather than read off the end.
