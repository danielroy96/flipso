#!/usr/bin/env python3
"""Build Flipso's packed station table.

ITSO carries rail locations as a four-character National Location Code. This
script turns those codes into names and writes the packed, sorted table that
the app binary-searches on the device.

Two sources are supported:

  orr            Office of Rail and Road, "Estimates of station usage"
                 (table 1410). Crown copyright, Open Government Licence v3.0,
                 so it can be redistributed. Covers every currently open GB
                 station under its National Rail Enquiries name.

  railwaycodes   The CRS/NLC/TIPLOC tables at railwaycodes.org.uk, compiled and
                 maintained by Phil Deaves. Much wider: closed stations, older
                 codes, and the fare groups ("London Zone R1256") that season
                 tickets use as an endpoint. Published without a licence grant
                 - see the note in README.md before redistributing a table
                 built from it.

The default merges both, which is what assets/stations.dat holds. ORR names win
where the two disagree: they are the ones National Rail Enquiries publishes and
they are kept current.

Usage:
    python3 build_stations.py -o ../../assets/stations.dat   # what we ship
    python3 build_stations.py --source orr                   # OGL data only
    python3 build_stations.py -o /tmp/stations.dat

The app reads, in order:
    /ext/apps_data/flipso/stations.dat    your own build, if present
    /ext/apps_assets/flipso/stations.dat  the packaged table, deployed by the
                                          firmware when the .fap is installed

so a local build only has to be copied to the first path to take over.

See FORMAT.md for the layout this writes.
"""
import argparse
import html
import re
import string
import struct
import sys
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from io import BytesIO

UA = "Mozilla/5.0 (compatible; Flipso station table builder)"

MAGIC = b"FSTN"
VERSION = 1
HEADER_LEN = 16
INDEX_LEN = 6
# Matches FLIPSO_STATION_NAME_MAX. The screen wraps, so this is about keeping
# the record small rather than about fitting a line.
NAME_MAX = 40

ORR_PAGE = "https://dataportal.orr.gov.uk/statistics/usage/estimates-of-station-usage/"
# Used when the page cannot be scraped; the media id changes with each release.
ORR_FALLBACK = (
    "https://dataportal.orr.gov.uk/media/1907/"
    "table-1410-passenger-entries-and-exits-and-interchanges-by-station.ods"
)
ODS_NS = {
    "table": "urn:oasis:names:tc:opendocument:xmlns:table:1.0",
    "text": "urn:oasis:names:tc:opendocument:xmlns:text:1.0",
}

RC_BASE = "https://www.railwaycodes.org.uk/crs"
# Fare groups appear as ticket origins and destinations just as stations do.
RC_GROUP_PATTERNS = (
    re.compile(r"^London Zone", re.I),
    re.compile(r"^London Stations$", re.I),
    re.compile(r"\bStations$", re.I),
)
# A rail ticket's ProductRetailer is the NLC of the office that sold it
# (RSPS3002 section 3.6.3), which for a ticket bought online or by phone is the
# operator's sales channel rather than a station: 7175 is GWR's web sales, 8385
# SWR's web ticket issuing system. Ticket machines are left out: there are two
# thousand of them, which would more than double the table.
RC_RETAILER = re.compile(
    r"\b(web|webtis|web ?sales|telesales|online|internet|mobile|apps?|digital|"
    r"travel cent(re|er)|booking office|ticket office|ticket line|on train sales|"
    r"business travel|call centre|portal|kiosk|itso|smartcards?)\b",
    re.I,
)
# A travel agent's code ends in its ABTA-style branch number ("Pole Travel
# S658"). Agents' codes have been reissued to operators' own channels - 8385 was
# that agent's before it was SWR's - so where a code has both, the channel wins.
RC_AGENT = re.compile(r"\s[A-Z]{1,2}\d{2,3}[A-Z]?$")
RC_MACHINE = re.compile(r"ticket machine|self.?service", re.I)


def fetch(url, retries=3, binary=False):
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as response:
                data = response.read()
                return data if binary else data.decode("utf-8", "replace")
        except Exception as exc:  # noqa: BLE001 - a transient fetch failure is not fatal
            if attempt == retries - 1:
                print(f"  failed: {url}: {exc}", file=sys.stderr)
                return b"" if binary else ""
            time.sleep(2)
    return b"" if binary else ""


def clean(name):
    """Collapse whitespace and drop anything that will not render on the screen."""
    name = re.sub(r"\s+", " ", name).strip()
    name = "".join(c if 0x20 <= ord(c) <= 0x7E else "-" for c in name)
    return name[:NAME_MAX]


# --------------------------------------------------------------------------
# Office of Rail and Road
# --------------------------------------------------------------------------


def orr_ods_url():
    page = fetch(ORR_PAGE)
    # The newest table 1410 link on the page; the media id moves every year.
    hits = re.findall(r'href="([^"]*table-1410[^"]*\.ods)"', page, re.I)
    if not hits:
        print("  could not find table 1410 on the ORR page, using last known URL")
        return ORR_FALLBACK
    url = html.unescape(hits[0])
    return url if url.startswith("http") else "https://dataportal.orr.gov.uk" + url


def ods_rows(data, sheet_match):
    """Yield the rows of the first sheet whose name contains @p sheet_match."""
    root = ET.fromstring(zipfile.ZipFile(BytesIO(data)).read("content.xml"))
    name_attr = "{%s}name" % ODS_NS["table"]
    repeat_attr = "{%s}number-columns-repeated" % ODS_NS["table"]
    for sheet in root.iter("{%s}table" % ODS_NS["table"]):
        if sheet_match not in (sheet.get(name_attr) or ""):
            continue
        for row in sheet.findall("table:table-row", ODS_NS):
            cells = []
            for cell in row.findall("table:table-cell", ODS_NS):
                # A run of identical cells is stored once with a repeat count.
                # Trailing padding can claim thousands of columns, so cap it.
                repeat = min(int(cell.get(repeat_attr) or 1), 64)
                para = cell.find("text:p", ODS_NS)
                cells += ["".join(para.itertext()) if para is not None else ""] * repeat
            yield cells
        return


def scrape_orr():
    url = orr_ods_url()
    print(f"Fetching {url}")
    data = fetch(url, binary=True)
    if not data:
        return {}

    stations = {}
    name_col = None
    for row in ods_rows(data, "1410"):
        if name_col is None:
            # The table starts a few rows below the title and notes.
            for i, heading in enumerate(row):
                if "National Location Code" in heading:
                    name_col = i
            continue
        if len(row) <= name_col:
            continue
        nlc, name = row[name_col].strip(), row[0].strip()
        if re.fullmatch(r"\d{4}", nlc) and name:
            stations.setdefault(nlc, clean(name))

    if name_col is None:
        print("  no NLC column found - has the ORR table changed?", file=sys.stderr)
    print(f"  ORR: {len(stations)} stations")
    return stations


# --------------------------------------------------------------------------
# railwaycodes.org.uk
# --------------------------------------------------------------------------


def rc_cells(row):
    return [
        html.unescape(re.sub(r"<[^>]+>", " ", cell)).strip()
        for cell in re.findall(r"<td[^>]*>(.*?)</td>", row, re.S)
    ]


def scrape_railwaycodes():
    stations, groups, retailers = {}, {}, {}

    for letter in string.ascii_lowercase:
        page = fetch(f"{RC_BASE}/crs{letter}.shtm")
        if not page:
            continue
        for row in re.findall(r"<tr>(.*?)</tr>", page, re.S):
            cell = rc_cells(row)
            if len(cell) < 3 or not cell[0]:
                continue
            name, crs, nlc = cell[0], cell[1], cell[2]
            is_station = bool(re.fullmatch(r"[A-Z]{3}", crs or ""))
            if not is_station and not any(p.search(name) for p in RC_GROUP_PATTERNS):
                # Ticket machines and accounting codes share this table but are
                # never named on a card. A sales office is, as the retailer;
                # only its location code, ending 00, is the four-character NLC
                # the card carries - 314244 is not location 3142.
                if RC_RETAILER.search(name) or RC_MACHINE.search(name):
                    for value in re.findall(r"\b(\d{4})00\b", nlc):
                        retailers.setdefault(value, set()).add(clean(name))
                continue
            # NLCs are published as six digits; ITSO carries the leading four.
            for value in re.findall(r"\b(\d{6})\b", nlc):
                target = stations if is_station else groups
                target.setdefault(value[:4], clean(name))
        print(f"  {letter}: {len(stations)} stations, {len(groups)} groups", flush=True)
        time.sleep(1.0)

    # The station groups page lists bare four-digit codes.
    page = fetch(f"{RC_BASE}/crs_group.shtm")
    for row in re.findall(r"<tr>(.*?)</tr>", page, re.S):
        cell = rc_cells(row)
        if len(cell) < 3:
            continue
        code, name = cell[1].strip(), cell[2]
        if re.fullmatch(r"\d{4}", code) and name:
            groups.setdefault(code, clean(name + " Stations"))

    # Codes are reissued and the table carries no dates, so a code two sales
    # offices share is left unnamed rather than given to the wrong one. Ticket
    # machines are collected only so that a code one shares is seen as shared.
    merged = {}
    for code, names in retailers.items():
        names = {name for name in names if not RC_AGENT.search(name)} or names
        if len(names) == 1 and not RC_MACHINE.search(next(iter(names))):
            merged[code] = names.pop()
    print(f"  {len(merged)} retailers named, {len(retailers) - len(merged)} ticket machines or ambiguous")
    merged.update(groups)
    merged.update(stations)  # a real station always wins the code
    return merged


# --------------------------------------------------------------------------
# Packing
# --------------------------------------------------------------------------


def pack(table):
    """Return the packed table described in FORMAT.md."""
    codes = sorted(table)
    blob = bytearray()
    index = bytearray()
    name_max = 0

    for code in codes:
        name = table[code].encode("ascii", "replace")[:NAME_MAX]
        offset = len(blob)
        if offset > 0xFFFFFF:
            raise ValueError("name blob is larger than the 24-bit offset field")
        index += struct.pack("<H", int(code)) + offset.to_bytes(3, "little")
        index += struct.pack("<B", len(name))
        blob += name
        name_max = max(name_max, len(name))

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
        low, high = 0, count
        while low < high:
            mid = (low + high) // 2
            at = HEADER_LEN + mid * INDEX_LEN
            code = struct.unpack("<H", packed[at : at + 2])[0]
            if code == wanted:
                offset = int.from_bytes(packed[at + 2 : at + 5], "little")
                length = packed[at + 5]
                return packed[names + offset : names + offset + length].decode("ascii")
            if code < wanted:
                low = mid + 1
            else:
                high = mid
        return None

    for code, name in table.items():
        got = lookup(int(code))
        assert got == name, f"{code}: packed as {got!r}, expected {name!r}"
    return lookup


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "-s",
        "--source",
        choices=("orr", "railwaycodes", "both"),
        default="both",
        help="where to get the codes (default: both; orr alone is the OGL subset)",
    )
    parser.add_argument("-o", "--output", default="stations.dat")
    args = parser.parse_args()

    table = {}
    if args.source in ("orr", "both"):
        table.update(scrape_orr())
    if args.source in ("railwaycodes", "both"):
        print("Scraping railwaycodes.org.uk ...")
        extra = scrape_railwaycodes()
        # ORR names are the ones National Rail Enquiries publishes and are kept
        # current, so they win; railwaycodes fills in everything ORR omits.
        for code, name in extra.items():
            table.setdefault(code, name)

    if not table:
        print("no entries - nothing written", file=sys.stderr)
        return 1

    packed = pack(table)
    lookup = verify(packed, table)
    with open(args.output, "wb") as handle:
        handle.write(packed)

    average = (len(packed) - HEADER_LEN) / len(table)
    print(
        f"wrote {args.output}: {len(table)} entries, {len(packed)} bytes"
        f" ({average:.1f} per entry)"
    )
    for probe in ("5685", "0035", "1444", "1072", "7175", "8385"):
        print(f"  {probe} -> {lookup(int(probe))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
