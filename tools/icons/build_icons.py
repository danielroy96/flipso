#!/usr/bin/env python3
"""
Regenerate the 1-bit PNG icons in images/ from the pixel art below.

The icons are small enough that keeping them as art here, rather than as opaque
binaries, is what makes them reviewable: a wrong pixel is visible in the diff.
The build turns each PNG into an `I_<name>` symbol (see application.fam's
fap_icon_assets), and treats black as "on", so '#' is an ink pixel.

Run with any Python 3 that has Pillow, e.g. the ufbt toolchain's:
    ~/.ufbt/toolchain/*/bin/python3 tools/icons/build_icons.py
"""
import os
import struct
import zlib

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "images")

# --- 10x10: one per menu row -------------------------------------------------

ICONS = {}

ICONS["card_10px"] = """
..........
.########.
.#......#.
.#.##...#.
.#.##..##.
.#.....##.
.#..#####.
.#......#.
.########.
..........
"""

# A coin: the purse holds money, and a coin reads at 10px where a "£" does not.
ICONS["purse_10px"] = """
..######..
.##....##.
##..##..##
#..####..#
#....##..#
#..######.
#....##..#
##..##..##
.##....##.
..######..
"""

# Head and shoulders: the ITSO ID and entitlement products name a person.
ICONS["id_10px"] = """
...####...
..#....#..
..#....#..
...####...
..........
.########.
##......##
#........#
#........#
#........#
"""

# The contactless mark, which is what a tap actually is.
ICONS["taps_10px"] = """
.......#..
...#...#..
..#.#..#..
.#...#.#..
.#.#..##..
.#.#..##..
.#...#.#..
..#.#..#..
...#...#..
.......#..
"""

# Two tickets, offset: the products list is a stack of them.
ICONS["products_10px"] = """
..#######.
..#.....#.
.##.....#.
.#.######.
.#......#.
##......#.
#.#######.
#.......#.
#.......#.
#########.
"""

# --- 10x10: one per product type in the products list ------------------------

# A single ticket with its tear-off perforation.
ICONS["ticket_10px"] = """
..........
##########
#...#....#
#...#....#
#........#
#........#
#...#....#
#...#....#
##########
..........
"""

# A calendar: a period ticket or pass is bounded by dates.
ICONS["pass_10px"] = """
..#....#..
##########
#........#
##########
#.#.#.#..#
#........#
#.#.#.#..#
#........#
##########
..........
"""

# A star for loyalty points.
ICONS["star_10px"] = """
....##....
....##....
...####...
##########
.########.
..######..
..######..
.##....##.
.#......#.
..........
"""

# A luggage tag: the fallback for a product type Flipso reports from its
# directory entry alone.
ICONS["tag_10px"] = """
...#######
..##.....#
.#.##....#
##...#...#
#.....#..#
#......#.#
##......##
.#......#.
..#....#..
...####...
"""

# --- 10x10: saving and deleting a card ---------------------------------------

# A floppy disk, which has meant "save" for longer than it has existed as
# hardware, and reads at 10px where a downward arrow into a tray does not.
ICONS["save_10px"] = """
##########
#.#....#.#
#.#....#.#
#.#....#.#
#........#
#.######.#
#.#....#.#
#.#....#.#
#.######.#
##########
"""

# A pencil, point down-left: the standard mark for editing, and the name is the
# only part of a saved card there is anything to edit.
ICONS["rename_10px"] = """
.......###
......###.
.....###..
....###...
...###....
..###.....
.####.....
.###......
.##.......
.#........
"""

# A waste bin with its lid: the saved copy goes, the card does not.
ICONS["delete_10px"] = """
...####...
.########.
..........
.########.
.#.#..#.#.
.#.#..#.#.
.#.#..#.#.
.#.#..#.#.
.########.
..######..
"""

# A clock, hands at ten past eight. The product list draws this instead of the
# product's own icon for anything the card no longer carries: the row label is
# already the product type, so the icon is free to say which of the two lists a
# row is in - and that is the distinction the list exists to make.
ICONS["past_10px"] = """
..######..
.#......#.
#...#....#
#...#....#
#...####.#
#...#....#
#........#
#........#
.#......#.
..######..
"""

# --- 10x10: the menu header --------------------------------------------------

# A warning triangle, shown beside the header when the shell is blocked. Drawn
# as an outline rather than solid so the exclamation inside it survives at this
# size: a filled triangle would need white ink for the mark, and the header is
# drawn on both black and white backgrounds.
ICONS["warning_10px"] = """
....##....
....##....
...#..#...
...#..#...
..#.##.#..
..#.##.#..
.#..##..#.
.#......#.
#...##...#
##########
"""

# --- 14x14: the error screen -------------------------------------------------

# A card with a question mark: there is a card, but no ITSO application on it.
ICONS["not_itso_14px"] = """
..............
.############.
.#..........#.
.#...####...#.
.#..##..##..#.
.#......##..#.
.#.....##...#.
.#....##....#.
.#..........#.
.#....##....#.
.#..........#.
.############.
..............
..............
"""

# A card whose contents are damaged: the shell is there but will not decode.
ICONS["bad_shell_14px"] = """
..............
.############.
.#..........#.
.#...####...#.
.#..#....#..#.
.#.#..##..#.#.
.#.#.####.#.#.
.#.#..##..#.#.
.#..#....#..#.
.#...####...#.
.#..........#.
.############.
..............
..............
"""

# A scallop shell: the Oyster card, which is recognised rather than decoded.
ICONS["oyster_14px"] = """
..............
.....####.....
...##.##.##...
..##..##..##..
.##...##...##.
.#...#..#...#.
##...#..#...##
#...##..##...#
#...#....#...#
#..##....##..#
#..#......#..#
##.#......#.##
.############.
..............
"""

# A card leaving the field: the read did not finish.
ICONS["read_failed_14px"] = """
..............
.#########....
.#.......#..#.
.#.......#.#..
.#.......#.#..
.#.......##...
.#........#.#.
.#........#.#.
.#.......##...
.#.......#.#..
.#.......#.#..
.#########..#.
..............
..............
"""


# --- 10x10: the app icon -----------------------------------------------------

# application.fam's fap_icon, shown in the Flipper's app browser. It is not an
# I_ symbol, so it sits beside application.fam rather than in images/. A card,
# chip and all, laid at a tilt over the corner of a reader's ring. Its edges
# step two across for one down, the only slope that stays a solid line at this
# size, and the ring breaks a pixel short of the card so the two outlines do
# not run together.
APP_ICON_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "flipso.png")
APP_ICON = """
..####....
.#....#...
#......#..
#.........
#.....###.
#...##...#
.#.#.....#
..#.#..##.
..#..##...
...##.....
"""


def png(path, rows):
    height = len(rows)
    width = len(rows[0])
    # 8-bit greyscale: black is ink, which is what the build inverts into "on".
    raw = b"".join(
        b"\x00" + bytes(0x00 if c == "#" else 0xFF for c in row) for row in rows
    )

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    data = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    targets = [(name, os.path.join(OUT, name + ".png"), art) for name, art in sorted(ICONS.items())]
    targets.append(("flipso", APP_ICON_PATH, APP_ICON))
    for name, path, art in targets:
        rows = [line for line in art.strip("\n").split("\n") if line]
        widths = {len(row) for row in rows}
        if len(widths) != 1:
            raise SystemExit(f"{name}: rows are not all the same width: {sorted(widths)}")
        bad = set("".join(rows)) - set(".#")
        if bad:
            raise SystemExit(f"{name}: unexpected characters {sorted(bad)}")
        png(path, rows)
        print(f"{name}: {widths.pop()}x{len(rows)}")


if __name__ == "__main__":
    main()
