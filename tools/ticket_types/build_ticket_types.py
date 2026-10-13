#!/usr/bin/env python3
"""Build Flipso's packed rail ticket type table.

A reserved rail journey (ITSO TYP 24) carries its Fares Type of Ticket - the
three-character code the RSP fares data prices it under, "SOR" - and nothing
on the card says what that code is called. This turns the Rail Delivery
Group's ticket type reference data into the packed, sorted table the app
binary-searches on the device: "SOR" is an Anytime Return.

Each code is named by the first of these it has:

  OJPDisplayName   the name National Rail Enquiries shows a customer
  RSPDisplayName   the name RSP gives retailers
  Name             the short name printed on the ticket ("ANYTIME R")

Usage:
    python3 build_ticket_types.py                       # what we ship
    python3 build_ticket_types.py -i TicketTypes.xml -o /tmp/ticket_types.dat

The app reads, in order:
    /ext/apps_data/flipso/ticket_types.dat    your own build, if present
    /ext/apps_assets/flipso/ticket_types.dat  the packaged table, deployed by
                                              the firmware when the .fap is
                                              installed

See FORMAT.md for the layout this writes and SOURCES.md for the data.
"""
import argparse
import os
import struct
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "TicketTypesRefData_v1.2.xml")
OUTPUT = os.path.join(HERE, "..", "..", "assets", "ticket_types.dat")

MAGIC = b"FTKT"
VERSION = 1
HEADER_LEN = 16
CODE_LEN = 3
INDEX_LEN = 7
# Matches FLIPSO_TICKET_TYPE_NAME_MAX. The screen wraps, so this is about
# keeping the record small rather than about fitting a line.
NAME_MAX = 64


def read(path):
    """The code -> name table from RDG's TicketTypesReferenceData XML."""
    table = {}
    root = ET.parse(path).getroot()
    for ticket in root.iter("TicketType"):
        def field(tag):
            return " ".join((ticket.findtext(tag) or "").split())

        code = field("Code")
        name = field("OJPDisplayName") or field("RSPDisplayName") or field("Name")
        if len(code) != CODE_LEN or not name:
            continue
        if not (code.isascii() and name.isascii()):
            raise SystemExit(f"{code}: not ASCII, which the device's font cannot draw: {name!r}")
        if len(name) > NAME_MAX:
            raise SystemExit(f"{code}: {len(name)}-byte name, past NAME_MAX: {name!r}")
        if code in table:
            raise SystemExit(f"{code}: listed twice")
        table[code] = name
    return table


def pack(table):
    """Sorted index of fixed-width entries, then each distinct name once."""
    codes = sorted(table, key=lambda c: c.encode("ascii"))
    blob = bytearray()
    placed = {}
    index = bytearray()
    for code in codes:
        name = table[code].encode("ascii")
        # Over a thousand codes share a name with another - every operator's
        # own "Anytime Return" - so each name is stored once.
        if name not in placed:
            placed[name] = len(blob)
            blob += name
        index += code.encode("ascii")
        index += placed[name].to_bytes(3, "little")
        index += bytes([len(name)])
    name_max = max(len(table[c]) for c in codes)
    names_offset = HEADER_LEN + len(index)
    header = MAGIC + struct.pack("<BBHII", VERSION, name_max, 0, len(codes), names_offset)
    assert len(header) == HEADER_LEN
    return bytes(header + index + blob)


def verify(packed, table):
    """Binary-search the packed bytes the way the device does, for every code."""
    magic, version, name_max, _, count, names = struct.unpack("<4sBBHII", packed[:HEADER_LEN])
    assert magic == MAGIC and version == VERSION, "bad header"
    assert count == len(table), "count mismatch"
    assert name_max <= NAME_MAX, "name longer than the app's buffer"

    def lookup(wanted):
        wanted = wanted.encode("ascii")
        low, high = 0, count
        while low < high:
            mid = (low + high) // 2
            at = HEADER_LEN + mid * INDEX_LEN
            code = packed[at : at + CODE_LEN]
            if code == wanted:
                offset = int.from_bytes(packed[at + 3 : at + 6], "little")
                length = packed[at + 6]
                return packed[names + offset : names + offset + length].decode("ascii")
            if code < wanted:
                low = mid + 1
            else:
                high = mid
        return None

    for code, name in table.items():
        got = lookup(code)
        assert got == name, f"{code}: packed as {got!r}, expected {name!r}"
    return lookup


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("-i", "--input", default=SOURCE, help="RDG's TicketTypesRefData XML")
    parser.add_argument("-o", "--output", default=OUTPUT)
    args = parser.parse_args()

    table = read(args.input)
    if not table:
        print("no ticket types - nothing written", file=sys.stderr)
        return 1
    packed = pack(table)
    lookup = verify(packed, table)
    with open(args.output, "wb") as handle:
        handle.write(packed)

    print(f"wrote {os.path.relpath(args.output)}: {len(table)} ticket types, {len(packed)} bytes")
    for probe in ("SOR", "CDR", "SVR", "7DS", "0AA", "ZZZ"):
        print(f"  {probe} -> {lookup(probe)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
