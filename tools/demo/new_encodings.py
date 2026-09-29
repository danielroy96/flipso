#!/usr/bin/env python3
"""
What does this card encode that no demo card does?

    tools/demo/new_encodings.py card.flipso

The demo cards (build_demo_cards.py) exist to cover every shape of data Flipso
decodes, so each real card read is a chance to find one they miss. This lists
the structural features of a saved card - media geometry, each product's type,
revision, optional-field bitmap and whether it has value records, each journey
record's revision and groups, a Directory InstanceID - and every screen line
the card produces, then says which of them none of the demo cards has.

Anything it lists is a candidate for a demo card: build it with the structures
in tools/test/itso_build.py, as card_gwr_touch() in build_demo_cards.py was
built from what this reported for a real GWR Touch card. Values are the card's
business; only the shapes matter here, so a new price or date is not listed.

It needs no device. Exit 0 whether or not anything is new.
"""
import datetime
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "test"))
sys.path.insert(0, HERE)
import build_demo_cards  # noqa: E402
import screens  # noqa: E402

# TTBitMap2 groups, TS 1000-5 tables 59 and 66.
TT_GROUPS = {0: "AMT", 1: "DEST", 2: "IPEID", 3: "ORGN", 5: "RC", 7: "IIN",
             8: "CIPE", 9: "ENTRY", 10: "ENTRY_OID"}
TT_SLOT = 48


def blocks_of(path):
    """A saved card's blocks, as {key: bytes}."""
    out = {}
    with open(path) as fh:
        for line in fh:
            key, sep, value = line.rstrip("\n").partition(": ")
            if not sep or key in ("Filetype", "Version", "Read at"):
                continue
            try:
                out[key] = bytes.fromhex(value.replace(" ", ""))
            except ValueError:
                pass
    return out


def bits(data, offset, width):
    value = int.from_bytes(data, "big")
    total = len(data) * 8
    return (value >> (total - offset - width)) & ((1 << width) - 1)


def structure(blocks):
    """The set of shape descriptions for one card."""
    found = set()
    if "Type 2" in blocks:
        found.add("media: NFC Type 2 tag (CMD4), compact shell")
        return found

    shell = blocks.get("Shell", b"")
    if len(shell) < 20:
        return found
    sector, sectors, entries, sct_len = shell[16], shell[17], shell[18], shell[19]
    found.add(f"media: {sector}-byte sectors x {sectors}, {entries} directory entries")
    if "Tag" in blocks:
        found.add(f"media: NFC Type 2 tag (CMD{shell[11]}), full shell")
    if bits(shell, 6, 6) & 0b10:
        found.add("shell: carries an MCRN")

    directory = blocks.get("Directory", b"")
    dir_bitmap = bits(directory, 6, 6) if len(directory) >= 2 else 0
    log_index = entries if (dir_bitmap >> 1) & 0b11 else 0
    if dir_bitmap & 1:
        found.add("directory: shell blocked")
    instance = 2 + 5 * entries + sct_len + 1
    if len(directory) >= instance + 5 and any(directory[instance + 1:instance + 5]):
        found.add("directory: InstanceID (last writer's ISAM)")

    for i in range(1, entries + 1):
        entry = directory[2 + 5 * (i - 1):2 + 5 * i]
        if len(entry) < 5 or not any(entry) or i == log_index:
            continue
        typ, vgp = bits(entry, 14, 5), bits(entry, 24, 1)
        if bits(entry, 0, 1):
            found.add(f"TYP {typ}: owner OID in the extended range (EF set)")
        ipe = blocks.get(f"Product {i}")
        if not ipe or len(ipe) < 2:
            continue
        rev, bitmap = bits(ipe, 12, 4), bits(ipe, 6, 6)
        found.add(f"TYP {typ} rev {rev}: bitmap 0x{bitmap:02X}")
        found.add(f"TYP {typ} rev {rev}: {'with' if vgp else 'no'} value record group")

    log = blocks.get("Log", b"")
    for slot in range(0, len(log) - TT_SLOT + 1, TT_SLOT):
        record = log[slot:slot + TT_SLOT]
        if not any(record):
            continue
        length, rev = bits(record, 0, 6) * 4, bits(record, 12, 4)
        bitmap, txn = bits(record, 16, 12), bits(record, 28, 4)
        groups = "+".join(name for b, name in TT_GROUPS.items() if bitmap & (1 << b))
        writer = length and length + 5 <= TT_SLOT and any(record[length + 1:length + 5])
        found.add(f"journey record rev {rev}, type {txn}: {groups or 'no groups'}"
                  f"{', with reader InstanceID' if writer else ''}")
    return found


def screen_lines(binary, path, now):
    """The labels of every screen line, which is what a reader would see new."""
    text = screens.render(binary, path, now, capture=True).stdout
    labels = set()
    for line in text.split("\n==== Operators on this card")[0].splitlines():
        if line.startswith("==== ") or not line.strip():
            continue
        label = (line.split(": ")[0] if ": " in line else line).strip()
        # A bare value - the card number under its heading, a date - is the
        # card's business, not a shape, and printing it would put the card
        # number on screen besides.
        if not label or not (label[0].isalpha() or label[0] == "#"):
            continue
        labels.add(label)
    return labels


def main():
    if len(sys.argv) != 2 or not os.path.exists(sys.argv[1]):
        sys.exit(__doc__.strip())
    card = sys.argv[1]
    binary = screens.build()
    now = screens.midnight(datetime.date.today())

    demo_structure, demo_lines = set(), set()
    with tempfile.TemporaryDirectory() as out:
        for make in build_demo_cards.CARDS:
            name, read_at, blocks = make()
            path = build_demo_cards.write_card(os.path.join(out, name + ".flipso"),
                                               read_at, blocks)
            demo_structure |= structure(blocks_of(path))
            # As of the day it was read, so its products show as they were.
            demo_lines |= screen_lines(binary, path, read_at)

    mine = structure(blocks_of(card))
    lines = screen_lines(binary, card, now)
    new_structure = sorted(mine - demo_structure)
    new_lines = sorted(lines - demo_lines)

    print(f"{card}: {len(mine)} structural features, {len(lines)} distinct screen lines")
    if not new_structure and not new_lines:
        print("Nothing new: every shape on this card is already on a demo card.")
        return 0
    if new_structure:
        print("\nStructures no demo card has:")
        for item in new_structure:
            print(f"  {item}")
    if new_lines:
        print("\nScreen lines no demo card produces:")
        for item in new_lines:
            print(f"  {item}")
    print("\nA demo card that carries these belongs in tools/demo/build_demo_cards.py.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
