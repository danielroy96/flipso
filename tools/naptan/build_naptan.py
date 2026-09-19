#!/usr/bin/env python3
"""Build Flipso's packed bus stop table.

ITSO carries bus locations as one of two national codes, both of which come from
NaPTAN, the Department for Transport's register of public transport access
points. This script turns those codes into stop names and writes the packed,
sorted table that the app binary-searches on the device.

The data is the NaPTAN download, Crown copyright, published under the Open
Government Licence v3.0:

    https://beta-naptan.dft.gov.uk/download

which this script fetches from the API behind that page, or reads from a local
Stops.csv given with --csv.

The whole register is what data/naptan.dat holds: around 390,000 active stops
packing to about 20 MB. That file ships ready built, and is copied to the SD
card rather than packaged into the .fap - data/README.md says why - so there is
no need to run this script unless you want a fresher or a narrower table.

A narrower one is worth building if you only ever use one area:

    python3 build_naptan.py --area 180 -o naptan.dat   # Greater Manchester
    tools/flipper/flipctl push naptan.dat /ext/apps_data/flipso/naptan.dat

Either way, a stop the table does not hold shows as its bare code.

Usage:
    python3 build_naptan.py -o ../../data/naptan.dat    # what we ship
    python3 build_naptan.py --csv Stops.csv -o out.dat  # from a local download
    python3 build_naptan.py --area 180,490 -o out.dat   # Manchester and London
    python3 build_naptan.py --list-areas                # what the area codes are
    python3 build_naptan.py --stop-type BCT -o out.dat  # on-street buses only
    python3 build_naptan.py --no-locality --no-atco     # the smallest useful table

See FORMAT.md for the layout this writes, and SOURCES.md for the licensing.
"""
import argparse
import csv
import re
import struct
import sys
import urllib.request

NAPTAN_CSV = "https://naptan.api.dft.gov.uk/v1/access-nodes?dataFormat=csv"

MAGIC = b"FNPT"
VERSION = 1
HEADER_LEN = 24
STOP_ENTRY_LEN = 8
ATCO_ENTRY_LEN = 16
ATCO_MAX = 12
# Matches FLIPSO_NAPTAN_NAME_MAX. The screen wraps, so this is about keeping the
# blob small rather than about fitting a line.
NAME_MAX = 40
# The name blob is addressed by a 24-bit offset.
BLOB_MAX = 0xFFFFFF

# ITSO TS 1000-1 table 28: a NaptanCode's letters are folded onto a telephone
# keypad so that its eight characters pack into four bytes of BCD. Case is not
# distinguished, and the fold is lossy - see FORMAT.md.
KEYPAD = {
    "2": "ABC",
    "3": "DEF",
    "4": "GHI",
    "5": "JKL",
    "6": "MNO",
    "7": "PQRS",
    "8": "TUV",
    "9": "WXYZ",
}
FOLD = {str(d): str(d) for d in range(10)}
for _digit, _letters in KEYPAD.items():
    for _letter in _letters:
        FOLD[_letter] = _digit
        FOLD[_letter.lower()] = _digit

# Four bytes of BCD hold eight digits and no more.
NAPTAN_DIGITS_MAX = 8


def fold_naptan(code):
    """Fold a NaptanCode to the digits an ITSO card stores, or None."""
    digits = []
    for char in code:
        mapped = FOLD.get(char)
        if mapped is None:
            return None
        digits.append(mapped)
    if not digits or len(digits) > NAPTAN_DIGITS_MAX:
        return None
    # Leading zeros are insignificant: the spec right-justifies a short code in
    # the field, and the app parses the field back to a number.
    return int("".join(digits))


def clean(text):
    """Collapse whitespace and drop anything that will not render on the screen."""
    text = re.sub(r"\s+", " ", text or "").strip()
    return "".join(c if 0x20 <= ord(c) <= 0x7E else "-" for c in text)


def stop_name(row, locality):
    """The name to show for a stop: what it is called, which one it is, and where.

    The indicator is what distinguishes the six stops called "Temple Meads Stn",
    and the locality is what distinguishes the four hundred called "High Street",
    so both earn their bytes. --no-locality drops the last part, which is about a
    third of the blob.

    The locality is also the part that gets dropped when the whole will not fit
    in NAME_MAX, because a name cut mid-word - "Ashton-under-Lyne Interchange,
    Ashton-un" - reads worse than one that simply does not say where it is.
    """
    name = clean(row["CommonName"])
    indicator = clean(row["Indicator"])
    if indicator:
        name = f"{name} ({indicator})"
    if locality:
        where = clean(row["LocalityName"])
        if where and len(name) + len(where) + 2 <= NAME_MAX:
            name = f"{name}, {where}"
    return name.encode("ascii", "replace")[:NAME_MAX]


def read_rows(handle):
    # utf-8-sig: the published download carries a byte order mark.
    return csv.DictReader(handle)


def open_source(path):
    if path:
        return open(path, newline="", encoding="utf-8-sig")
    print(f"Fetching {NAPTAN_CSV}", file=sys.stderr)
    response = urllib.request.urlopen(NAPTAN_CSV, timeout=300)
    return open_response(response)


def open_response(response):
    import io

    return io.TextIOWrapper(response, encoding="utf-8-sig", newline="")


class Table:
    """Stops gathered from the register, keyed both ways over shared names."""

    def __init__(self):
        self.stops = {}  # folded NaptanCode -> name bytes
        self.atcos = {}  # padded AtcoCode -> name bytes
        self.skipped_filter = 0
        self.skipped_unfoldable = 0
        self.collisions = 0
        self.duplicate_atco = 0

    def add(self, row, locality, want_atco):
        name = stop_name(row, locality)
        if not name:
            return

        naptan = (row["NaptanCode"] or "").strip()
        if naptan:
            key = fold_naptan(naptan)
            if key is None:
                # Either a character outside the keypad map, or a code longer
                # than eight characters. Neither can be stored on a card, so
                # neither can ever be looked up.
                self.skipped_unfoldable += 1
            elif key in self.stops:
                # Two distinct codes folded together, or the register listed the
                # same code twice. First one wins; see FORMAT.md.
                self.collisions += 1
            else:
                self.stops[key] = name

        if want_atco:
            atco = (row["ATCOCode"] or "").strip().upper()
            if atco and len(atco) <= ATCO_MAX and re.fullmatch(r"[0-9A-Z]+", atco):
                padded = atco.encode("ascii").ljust(ATCO_MAX, b"\0")
                if padded in self.atcos:
                    self.duplicate_atco += 1
                else:
                    self.atcos[padded] = name


def gather(handle, args):
    table = Table()
    areas = set(args.area) if args.area else None
    types = set(args.stop_type) if args.stop_type else None

    for row in read_rows(handle):
        if not args.include_inactive and row["Status"] != "active":
            table.skipped_filter += 1
            continue
        # The first three digits of an AtcoCode are its ATCO area - 180 Greater
        # Manchester, 490 London - which is the number people know, and is the
        # one visible in the code itself. NaPTAN's own AdministrativeAreaCode is
        # a separate numbering that agrees with it but appears nowhere on a card.
        if areas is not None and row["ATCOCode"][:3] not in areas:
            table.skipped_filter += 1
            continue
        if types is not None and row["StopType"] not in types:
            table.skipped_filter += 1
            continue
        table.add(row, args.locality, not args.no_atco)

    return table


def list_areas(handle):
    """Print the ATCO areas in the register, so --area can be given a number."""
    counts = {}
    for row in read_rows(handle):
        if row["Status"] != "active":
            continue
        area = row["ATCOCode"][:3]
        stops, where = counts.get(area, (0, ""))
        counts[area] = (stops + 1, where or clean(row["LocalityName"]))
    for area in sorted(counts):
        stops, where = counts[area]
        print(f"  {area}  {stops:>7} stops   e.g. {where}")
    return 0


def pack(table):
    """Return the packed table described in FORMAT.md."""
    blob = bytearray()
    offsets = {}
    name_max = 0

    def place(name):
        """Intern a name into the blob, so the two indexes and the many stops
        that share a name all point at one copy."""
        nonlocal name_max
        at = offsets.get(name)
        if at is None:
            at = len(blob)
            if at > BLOB_MAX:
                raise ValueError(
                    "name blob is larger than the 24-bit offset field; "
                    "build fewer areas, or pass --no-locality"
                )
            offsets[name] = at
            blob.extend(name)
            name_max = max(name_max, len(name))
        return at

    stop_index = bytearray()
    for code in sorted(table.stops):
        name = table.stops[code]
        at = place(name)
        stop_index += struct.pack("<I", code) + at.to_bytes(3, "little")
        stop_index += struct.pack("<B", len(name))

    atco_index = bytearray()
    for code in sorted(table.atcos):
        name = table.atcos[code]
        at = place(name)
        atco_index += code + at.to_bytes(3, "little") + struct.pack("<B", len(name))

    atco_offset = HEADER_LEN + len(stop_index)
    names_offset = atco_offset + len(atco_index)
    header = MAGIC + struct.pack(
        "<BBHIIII",
        VERSION,
        name_max,
        0,
        len(table.stops),
        len(table.atcos),
        atco_offset,
        names_offset,
    )
    assert len(header) == HEADER_LEN
    return bytes(header + stop_index + atco_index + blob)


def searchers(packed):
    """Binary-search the packed bytes the way the device does."""
    magic, version, name_max, _, stops, atcos, atco_at, names = struct.unpack(
        "<4sBBHIIII", packed[:HEADER_LEN]
    )
    assert magic == MAGIC and version == VERSION, "bad header"
    assert name_max <= NAME_MAX, "name longer than the app's buffer"

    def name_at(entry, at):
        offset = int.from_bytes(entry[at : at + 3], "little")
        length = entry[at + 3]
        return packed[names + offset : names + offset + length]

    def by_naptan(wanted):
        low, high = 0, stops
        while low < high:
            mid = (low + high) // 2
            at = HEADER_LEN + mid * STOP_ENTRY_LEN
            entry = packed[at : at + STOP_ENTRY_LEN]
            code = struct.unpack("<I", entry[:4])[0]
            if code == wanted:
                return name_at(entry, 4)
            low, high = (mid + 1, high) if code < wanted else (low, mid)
        return None

    def by_atco(wanted):
        low, high = 0, atcos
        while low < high:
            mid = (low + high) // 2
            at = atco_at + mid * ATCO_ENTRY_LEN
            entry = packed[at : at + ATCO_ENTRY_LEN]
            code = entry[:ATCO_MAX]
            if code == wanted:
                return name_at(entry, ATCO_MAX)
            low, high = (mid + 1, high) if code < wanted else (low, mid)
        return None

    return by_naptan, by_atco


def verify(packed, table, sample):
    """Check the packed bytes read back, over a sample or in full.

    A sample is the default because the full register is 800,000 lookups of
    twenty steps each, which takes rather longer than the build.
    """
    by_naptan, by_atco = searchers(packed)

    def check(keys, lookup, source, what):
        keys = sorted(keys)
        if sample and len(keys) > sample:
            # The ends and an even spread through the middle: an off-by-one in
            # the search shows up at the boundaries first.
            step = len(keys) / sample
            chosen = {keys[0], keys[-1]}
            chosen.update(keys[int(i * step)] for i in range(sample))
            keys = sorted(chosen)
        for key in keys:
            got = lookup(key)
            assert got == source[key], f"{what} {key!r}: packed as {got!r}"
        return len(keys)

    checked = check(table.stops, by_naptan, table.stops, "NaptanCode")
    checked += check(table.atcos, by_atco, table.atcos, "AtcoCode")
    return checked, by_naptan, by_atco


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("-o", "--output", default="naptan.dat")
    parser.add_argument(
        "--csv",
        help="a Stops.csv downloaded from beta-naptan.dft.gov.uk (default: fetch it)",
    )
    parser.add_argument(
        "--area",
        type=lambda v: [a.strip().zfill(3) for a in v.split(",") if a.strip()],
        help="keep only these ATCO areas, e.g. 180 for Greater Manchester (see --list-areas)",
    )
    parser.add_argument(
        "--list-areas",
        action="store_true",
        help="print the ATCO area codes and how many stops each holds, then stop",
    )
    parser.add_argument(
        "--stop-type",
        type=lambda v: [t.strip().upper() for t in v.split(",") if t.strip()],
        help="keep only these NaPTAN stop types, e.g. BCT for on-street bus stops",
    )
    parser.add_argument(
        "--include-inactive",
        action="store_true",
        help="keep stops the register marks inactive (about 48,000 of them)",
    )
    parser.add_argument(
        "--no-locality",
        dest="locality",
        action="store_false",
        help="leave the locality off each name, which is about a third of the blob",
    )
    parser.add_argument(
        "--no-atco",
        action="store_true",
        help="omit the AtcoCode index, for schemes that only ever use NaptanCodes",
    )
    parser.add_argument(
        "--verify-all",
        action="store_true",
        help="read every entry back rather than a sample of 5,000 per index",
    )
    args = parser.parse_args()

    if args.list_areas:
        with open_source(args.csv) as handle:
            return list_areas(handle)

    with open_source(args.csv) as handle:
        table = gather(handle, args)

    if not table.stops and not table.atcos:
        print("no entries - nothing written", file=sys.stderr)
        return 1

    packed = pack(table)
    checked, by_naptan, by_atco = verify(packed, table, 0 if args.verify_all else 5000)
    with open(args.output, "wb") as out:
        out.write(packed)

    entries = len(table.stops) + len(table.atcos)
    print(
        f"wrote {args.output}: {len(table.stops)} NaptanCodes, {len(table.atcos)}"
        f" AtcoCodes, {len(packed)} bytes ({(len(packed) - HEADER_LEN) / entries:.1f}"
        f" per entry), {checked} verified"
    )
    if table.collisions:
        print(f"  {table.collisions} NaptanCodes folded onto a code already taken")
    if table.skipped_unfoldable:
        print(f"  {table.skipped_unfoldable} NaptanCodes cannot be held on a card")
    if table.duplicate_atco:
        print(f"  {table.duplicate_atco} repeated AtcoCodes")
    if table.skipped_filter:
        print(f"  {table.skipped_filter} rows filtered out")

    # A couple of entries read back through each index, so the output shows what
    # a lookup on the device will actually produce.
    for code in sorted(table.stops)[:2]:
        found = by_naptan(code)
        print(f"  NaptanCode {code:08d} -> {found.decode() if found else None}")
    for code in sorted(table.atcos)[:2]:
        found = by_atco(code)
        label = code.rstrip(b"\0").decode()
        print(f"  AtcoCode {label} -> {found.decode() if found else None}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
