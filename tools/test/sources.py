#!/usr/bin/env python3
"""
The sources each part of Flipso is built from, for the host builds.

run.sh, screens.py and replay.py all compile some of the app on this machine,
and each used to list its own files - so splitting one source in two meant
finding every list, and missing one only showed up as a link error in whichever
tool ran next. They take their lists from here instead. The firmware build
has its own list in application.fam, which ufbt reads.

    tools/test/sources.py itso format      # paths relative to the current directory

Each part is a list of globs from the project root, matched in sorted order so
that a build's command line is the same on every machine.
"""

import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PARTS = {
    # The decoder: see itso/itso.h for how it is laid out.
    "itso": ["itso/*.c", "itso/media/*.c", "itso/ipe/*.c"],
    # The text of every screen.
    "format": ["format/*.c", "format/*/*.c"],
    # A read's raw blocks, and the file they are saved as.
    "capture": ["cards/flipso_capture*.c"],
    "saved": ["cards/flipso_saved*.c"],
    "media": ["reader/flipso_media.c"],
    "scan_session": ["reader/flipso_scan_session.c"],
    "operators": ["lookup/flipso_operators.c"],
    # The SD card tables, each with the reader they share.
    "stations": ["lookup/flipso_stations.c", "lookup/flipso_table.c"],
    "naptan": ["lookup/flipso_naptan.c", "lookup/flipso_table.c"],
    "ticket_types": ["lookup/flipso_ticket_types.c", "lookup/flipso_table.c"],
    "views": ["views/flipso_glyphs.c"],
    "menu_view": ["views/flipso_menu_view.c"],
    "text_view": ["views/flipso_text_view.c"],
}


def sources(*parts: str) -> list[str]:
    """Absolute paths of every source in @p parts, in order and without repeats."""
    found = []
    for part in parts:
        if part not in PARTS:
            raise SystemExit(f"sources: no part called {part!r}; there are {', '.join(PARTS)}")
        for pattern in PARTS[part]:
            for path in sorted(glob.glob(os.path.join(ROOT, pattern))):
                if path not in found:
                    found.append(path)
    return found


if __name__ == "__main__":
    print(" ".join(os.path.relpath(p) for p in sources(*sys.argv[1:])))
