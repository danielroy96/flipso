#!/usr/bin/env python3
"""
Print every screen of a saved card as the Flipper would draw it.

    tools/test/screens.py card.flipso [--now YYYY-MM-DD]

replay.py shows what the decoder made of a card's bytes; this shows what the
holder would read. The text comes from the real flipso_format*.c, and station
and stop names from the real tables - the packaged assets/stations.dat and
data/naptan.dat - through the real readers, so it is the device's text without
the scrolling. It ends by listing every operator number on the card, which is
the first thing to look at when a card comes up as "ITSO card".

Pull a card to render with:

    tools/flipper/flipctl pull /ext/apps_data/flipso/cards/NAME.flipso card.flipso

--now sets the date expiry is judged against (default: today), so a ticket can
be checked as it looked on the day it was valid.
"""

import argparse
import datetime
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

SOURCES = [
    "tools/test/screens.c",
    "flipso_format.c", "flipso_format_product.c", "flipso_format_card.c",
    "flipso_format_journeys.c", "flipso_capture.c", "flipso_media.c",
    "flipso_stations.c", "flipso_naptan.c",
    "itso/itso_parse.c", "itso/itso_util.c", "itso/itso_names.c",
    "itso/itso_operators.c",
]

# What the storage stub in screens.c opens for each table - the device's
# APP_ASSETS_PATH, mapped by tools/test/stub/storage/storage.h - and the file
# in the tree that holds it.
TABLES = {
    "stub_assets_stations.dat": "assets/stations.dat",
    "stub_assets_naptan.dat": "data/naptan.dat",
}


def build() -> str:
    """Compile the renderer, and return the path of the binary."""
    binary = os.path.join(HERE, "screens")
    cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
           "-Wno-unused-parameter", "-fsanitize=address,undefined",
           "-I", ROOT, "-I", os.path.join(ROOT, "itso"), "-I", HERE,
           "-I", os.path.join(HERE, "stub")]
    cmd += [os.path.join(ROOT, s) for s in SOURCES]
    cmd += ["-o", binary]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0:
        print(done.stdout + done.stderr, file=sys.stderr)
        sys.exit("screens: build failed")
    return binary


def midnight(day: datetime.date) -> int:
    """Local midnight, as the device's clock would have it."""
    return int(datetime.datetime.combine(day, datetime.time()).timestamp())


def render(binary: str, card: str, now: int, capture: bool = False):
    """Run the renderer over one card, with the real tables linked in.

    Returns the CompletedProcess; with @p capture its stdout is the text.
    """
    with tempfile.TemporaryDirectory() as work:
        for stub, real in TABLES.items():
            path = os.path.join(ROOT, real)
            if os.path.exists(path):
                os.symlink(path, os.path.join(work, stub))
            else:
                print(f"screens: no {real}, so its names show as codes", file=sys.stderr)
        return subprocess.run([binary, os.path.abspath(card), str(now)], cwd=work,
                              capture_output=capture, text=True)


def main():
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    p.add_argument("card", help="a saved card (.flipso)")
    p.add_argument("--now", help="judge expiry as of this date, YYYY-MM-DD")
    args = p.parse_args()

    if not os.path.exists(args.card):
        sys.exit(f"{args.card}: not found. Pull one with:\n"
                 "  tools/flipper/flipctl pull /ext/apps_data/flipso/cards/NAME.flipso card.flipso")
    day = (datetime.date.fromisoformat(args.now) if args.now
           else datetime.date.today())
    sys.exit(render(build(), args.card, midnight(day)).returncode)


if __name__ == "__main__":
    main()
