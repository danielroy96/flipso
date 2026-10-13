#!/usr/bin/env python3
"""Build Flipso's packed rail ticket restriction table.

A reserved rail journey (ITSO TYP 24) carries the two-character restriction
code its fare is sold under (RestrictionCode, TS 1000-5 table 136) - "OP" -
and nothing on the card says what that code allows. The Rail Delivery Group's
Knowledgebase Ticket Restrictions feed does, in the words National Rail
Enquiries prints: "Not valid on trains timed to depart before 09.30". This
turns that feed into the packed, sorted table the app binary-searches on the
device.

For each code it keeps:

  Name             only where it says more than the code; for 632 of the
                   644 codes the feed's name is the code again
  DetailsOutward   the restriction on the outward journey
  DetailsReturn    and on the return

The details are HTML, and run to 3.5 KB for the longest. They are reduced to
plain text and cut at a word to MAX_TEXT bytes, saying where the rest is: a
text screen keeps its longest page in memory, and the reserved journey is
already Flipso's longest screen. A text of a character or two - there are a
few, "C" - is a placeholder, not a restriction, and is left out.

Usage:
    python3 build_ticket_restrictions.py                    # what we ship
    python3 build_ticket_restrictions.py -i feed.xml -o /tmp/ticket_restrictions.dat

The app reads, in order:
    /ext/apps_data/flipso/ticket_restrictions.dat    your own build, if present
    /ext/apps_assets/flipso/ticket_restrictions.dat  the packaged table

See FORMAT.md for the layout this writes and SOURCES.md for the data.
"""
import argparse
import html
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "TicketRestrictions_v4.xml")
OUTPUT = os.path.join(HERE, "..", "..", "assets", "ticket_restrictions.dat")

NS = {"t": "http://nationalrail.co.uk/xml/ticketrestriction"}

MAGIC = b"FRST"
VERSION = 1
HEADER_LEN = 16
CODE_LEN = 2
# The code, then the name, outward and return texts: each a 3-byte offset into
# the blob and a length byte.
INDEX_LEN = CODE_LEN + 3 * 4
# Matches FLIPSO_TICKET_RESTRICTION_TEXT_MAX. A length is one byte, so 255 is
# the most the format allows; this leaves a text at about twelve screen lines.
MAX_TEXT = 250
# Where the rest of a text cut short can be read.
MORE = " ... More at nationalrail.co.uk"
# Shorter than this is a placeholder ("C"), not a restriction.
MIN_TEXT = 4

# The typography the feed uses that the device's font has no glyph for. The
# pound sign stays: Flipso draws its own.
PLAIN = {
    "‐": "-", "‑": "-", "–": "-", "—": "-",
    "‘": "'", "’": "'", "“": '"', "”": '"',
    "•": "-", " ": " ", "​": "",
}


def plain(markup):
    """HTML to one line of text the device can draw."""
    text = re.sub(r"<[^>]+>", " ", markup or "")
    text = html.unescape(text)
    text = "".join(PLAIN.get(c, c) for c in text)
    text = " ".join(text.split())
    for c in text:
        if ord(c) > 126 and c != "£":
            raise SystemExit(f"no glyph for {c!r} in {text[:60]!r}")
    return text


def cut(text):
    """At most MAX_TEXT bytes, cut at a word and saying where the rest is."""
    if len(text.encode("utf-8")) <= MAX_TEXT:
        return text
    room = MAX_TEXT - len(MORE)
    while len(text[:room].encode("utf-8")) > MAX_TEXT - len(MORE):
        room -= 1
    head = text[:room]
    if " " in head:
        head = head[: head.rindex(" ")]
    return head.rstrip(" ,;:") + MORE


def read(path):
    """The code -> (name, outward, return) table from the feed's XML."""
    table = {}
    root = ET.parse(path).getroot()
    for restriction in root.findall("t:TicketRestriction", NS):
        code = (restriction.findtext("t:RestrictionCode", namespaces=NS) or "").strip()
        name = " ".join((restriction.findtext("t:Name", namespaces=NS) or "").split())
        if len(code) != CODE_LEN or not code.isascii():
            raise SystemExit(f"{code!r}: not a two-character code")
        if code in table:
            raise SystemExit(f"{code}: listed twice")

        def details(tag):
            text = plain(restriction.findtext(f".//t:{tag}", namespaces=NS))
            return cut(text) if len(text) >= MIN_TEXT else ""

        table[code] = (
            name if name and name != code else "",
            details("DetailsOutward"),
            details("DetailsReturn"),
        )
    return table


def pack(table):
    """Sorted index of fixed-width entries, then each distinct text once."""
    codes = sorted(table, key=lambda c: c.encode("ascii"))
    blob = bytearray()
    placed = {}
    index = bytearray()
    longest = 0
    for code in codes:
        index += code.encode("ascii")
        for text in table[code]:
            data = text.encode("utf-8")
            assert len(data) <= MAX_TEXT, f"{code}: {len(data)} bytes"
            longest = max(longest, len(data))
            # Many codes share their words, and a restriction that is the same
            # both ways is the same text twice: each is stored once.
            if data and data not in placed:
                placed[data] = len(blob)
                blob += data
            index += (placed[data] if data else 0).to_bytes(3, "little")
            index += bytes([len(data)])
    names_offset = HEADER_LEN + len(index)
    header = MAGIC + struct.pack("<BBHII", VERSION, longest, 0, len(codes), names_offset)
    assert len(header) == HEADER_LEN
    return bytes(header + index + blob)


def verify(packed, table):
    """Binary-search the packed bytes the way the device does, for every code."""
    magic, version, longest, _, count, names = struct.unpack("<4sBBHII", packed[:HEADER_LEN])
    assert magic == MAGIC and version == VERSION, "bad header"
    assert count == len(table), "count mismatch"
    assert longest <= MAX_TEXT, "text longer than the app's buffer"

    def lookup(wanted):
        wanted = wanted.encode("ascii")
        low, high = 0, count
        while low < high:
            mid = (low + high) // 2
            at = HEADER_LEN + mid * INDEX_LEN
            code = packed[at : at + CODE_LEN]
            if code == wanted:
                texts = []
                for field in range(3):
                    ptr = at + CODE_LEN + field * 4
                    offset = int.from_bytes(packed[ptr : ptr + 3], "little")
                    length = packed[ptr + 3]
                    texts.append(packed[names + offset : names + offset + length].decode("utf-8"))
                return tuple(texts)
            if code < wanted:
                low = mid + 1
            else:
                high = mid
        return None

    for code, texts in table.items():
        got = lookup(code)
        assert got == texts, f"{code}: packed as {got!r}, expected {texts!r}"
    return lookup


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("-i", "--input", default=SOURCE, help="the feed's XML")
    parser.add_argument("-o", "--output", default=OUTPUT)
    args = parser.parse_args()

    table = read(args.input)
    if not table:
        print("no restrictions - nothing written", file=sys.stderr)
        return 1
    packed = pack(table)
    lookup = verify(packed, table)
    with open(args.output, "wb") as handle:
        handle.write(packed)

    print(f"wrote {os.path.relpath(args.output)}: {len(table)} restrictions, {len(packed)} bytes")
    for probe in ("OP", "O1", "SR", "1A"):
        name, outward, back = lookup(probe)
        print(f"  {probe} -> {name or '(no name)'} | {outward[:70]} | {back[:40]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
