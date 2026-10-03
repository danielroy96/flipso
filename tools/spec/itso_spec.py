#!/usr/bin/env python3
"""
Search the ITSO TS 1000 specification from the command line.

    tools/spec/itso_spec.py grep 'ProductRetailer'            # every part
    tools/spec/itso_spec.py grep 'OID numbering' --part 2 -C 30
    tools/spec/itso_spec.py page 5 51                          # part 5, page 51
    tools/spec/itso_spec.py grep 'Railcard' --part rsps3002    # rail's own rules

RSPS3002, RDG's "ITSO in National Rail Specification", is here too: it says
how rail fills the TS 1000-5 fields (retailer NLCs, railcards, reservations;
see docs/PROTOCOL.md). RDG no longer serves it, so it comes from the Wayback
Machine - a copy four separate sessions went looking for before it was named
here.

Every part except part 8 is published free under the Open Government Licence.
The first use of a part downloads its PDF and extracts the text, once, into
~/.cache/flipso/itso-spec (outside the tree: the PDFs are 4 MB each and the
text is for searching, not for committing). Page numbers are the PDF's own,
which match the page footers.

Extraction needs pypdf. The ufbt toolchain's Python has pip, so if the Python
running this has no pypdf, the toolchain's is used and pypdf installed into it.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import urllib.request

URL = "https://www.itso.org.uk/hubfs/TS_1000-{part}_V2_1_5_2025_03.pdf"
CACHE = os.path.expanduser("~/.cache/flipso/itso-spec")
# The parts the decoder is written against, searched when no --part is given:
# 1 data types, 2 shell / directory / IPE, 4 POST parameters (IIN index and
# friends), 5 per-type datasets, 10 customer media.
DEFAULT_PARTS = (1, 2, 4, 5, 10)
PAGE_MARK = re.compile(r"^=== PAGE (\d+) ===$")
# Documents searched by name rather than part number: (cache name, URL). The
# 2020 snapshot is the one that holds the PDF; RDG's own link now answers 200
# with an HTML page, which is why a download is checked for a PDF header.
NAMED = {
    "rsps3002": ("RSPS3002-02-01",
                 "https://web.archive.org/web/2020id_/https://www.raildeliverygroup.com"
                 "/files/Publications/services/rsp/"
                 "RSPS3002-02-01_ITSO_in_National_Rail_specification.pdf"),
}

EXTRACT = r"""
import sys
from pypdf import PdfReader
reader = PdfReader(sys.argv[1])
with open(sys.argv[2], "w") as out:
    for i, page in enumerate(reader.pages):
        out.write(f"=== PAGE {i + 1} ===\n" + (page.extract_text() or "") + "\n")
"""


def pdf_python() -> str:
    """A Python that can import pypdf, installing it into ufbt's if need be."""
    candidates = [sys.executable] + sorted(
        glob.glob(os.path.expanduser("~/.ufbt/toolchain/*/bin/python3.*[0-9]")))
    for py in candidates:
        if subprocess.run([py, "-c", "import pypdf"], capture_output=True).returncode == 0:
            return py
    for py in candidates[1:]:
        if subprocess.run([py, "-m", "pip", "install", "-q", "pypdf"]).returncode == 0:
            return py
    sys.exit("itso_spec: no Python here can install pypdf - "
             "`python3 -m pip install pypdf` and run again")


def part_arg(value: str):
    """A TS 1000 part number, or the name of a document in NAMED."""
    if value.lower() in NAMED:
        return value.lower()
    try:
        return int(value)
    except ValueError:
        raise argparse.ArgumentTypeError(
            f"not a part number or one of: {', '.join(NAMED)}")


def text_of(part) -> str:
    """Path of the extracted text of @p part, fetching it the first time."""
    os.makedirs(CACHE, exist_ok=True)
    name, url = NAMED[part] if part in NAMED else (f"TS_1000-{part}", URL.format(part=part))
    txt = os.path.join(CACHE, f"{name}.txt")
    if os.path.exists(txt):
        return txt
    pdf = os.path.join(CACHE, f"{name}.pdf")
    if not os.path.exists(pdf):
        print(f"itso_spec: fetching {name} from {url}", file=sys.stderr)
        try:
            urllib.request.urlretrieve(url, pdf + ".part")
        except Exception as exc:
            sys.exit(f"itso_spec: could not fetch {name}: {exc}"
                     + ("\nPart 8 is the one part ITSO does not publish." if part == 8 else ""))
        with open(pdf + ".part", "rb") as fh:
            if fh.read(5) != b"%PDF-":
                os.remove(pdf + ".part")
                sys.exit(f"itso_spec: {url} answered with a page, not a PDF - "
                         "the document has moved")
        os.replace(pdf + ".part", pdf)
    print(f"itso_spec: extracting {name} (once)", file=sys.stderr)
    subprocess.run([pdf_python(), "-c", EXTRACT, pdf, txt + ".part"], check=True)
    os.replace(txt + ".part", txt)
    return txt


def pages(part):
    """Yield (page number, list of lines) for @p part."""
    page, lines = 0, []
    with open(text_of(part), encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = PAGE_MARK.match(line.strip())
            if m:
                if lines:
                    yield page, lines
                page, lines = int(m.group(1)), []
            else:
                lines.append(line.rstrip("\n"))
    if lines:
        yield page, lines


def cmd_grep(args):
    pattern = re.compile(args.pattern, 0 if args.case else re.IGNORECASE)
    hits = 0
    for part in args.part or DEFAULT_PARTS:
        for page, lines in pages(part):
            for i, line in enumerate(lines):
                if not pattern.search(line):
                    continue
                hits += 1
                lo, hi = max(0, i - args.context), min(len(lines), i + args.context + 1)
                if args.context:
                    print(f"--- part {part}, page {page}")
                    for j in range(lo, hi):
                        print(f"{'>' if j == i else ' '} {lines[j]}")
                else:
                    print(f"part {part} p{page}: {line.strip()}")
    if not hits:
        print("no match", file=sys.stderr)
        return 1
    return 0


def cmd_page(args):
    for page, lines in pages(args.part):
        if page in range(args.page, args.page + args.count):
            print(f"=== part {args.part}, page {page} ===")
            print("\n".join(lines))
    return 0


def main():
    p = argparse.ArgumentParser(description="Search the ITSO TS 1000 specification.")
    sub = p.add_subparsers(dest="sub", required=True)
    s = sub.add_parser("grep", help="search the text for a regex")
    s.add_argument("pattern")
    s.add_argument("--part", type=part_arg, action="append",
                   help=f"part to search, repeatable (default: {DEFAULT_PARTS}); "
                        f"also {', '.join(NAMED)}")
    s.add_argument("-C", "--context", type=int, default=0,
                   help="lines of context either side, grouped by page")
    s.add_argument("--case", action="store_true", help="match case")
    s.set_defaults(func=cmd_grep)
    s = sub.add_parser("page", help="print a page of one part")
    s.add_argument("part", type=part_arg)
    s.add_argument("page", type=int)
    s.add_argument("--count", type=int, default=1, help="pages to print")
    s.set_defaults(func=cmd_page)
    args = p.parse_args()
    raise SystemExit(args.func(args))


if __name__ == "__main__":
    main()
