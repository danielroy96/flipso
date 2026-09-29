#!/usr/bin/env python3
"""
Replay bytes captured from a real card through the decoder on this machine.

Reflashing the Flipper and re-tapping a card to test one hypothesis takes a
minute and needs someone holding the card. Running the same bytes through the
host build takes a second and needs nobody, so capture once and iterate here.

Two kinds of file work, because both hold the same thing - the raw blocks a
read produced. Either can be pulled off the device and replayed:

  A card the user saved in the app. This needs no instrumentation and no
  special build: save the card on the Flipper, then

    tools/flipper/flipctl pull /ext/apps_data/flipso/cards/<name>.flipso card.flipso
    tools/test/replay.py card.flipso

  A dump from the opt-in instrumentation in tools/debug/flipso_dump.c, which
  also records reads that failed before there was a card worth saving:

    tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt
    tools/test/replay.py dump.txt

A saved card is a Flipper key-value file:

    Filetype: Flipso card
    Version: 1
    Read at: 1758400000
    Shell: 9E 00 ...
    Directory: ...
    Product 1: ...
    Log: ...

A dump is a header line naming the block and its length, then one line of hex:

    SHELL 47
    9E00...
    DIR 128
    ...
    GROUP1 128
    ...

Transcribing that hex by hand is how a phantom one-byte offset gets introduced,
so the C array is always generated from the file, never typed.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


SAVED_KEYS = {"shell": "SHELL", "directory": "DIR", "log": "LOG", "type 2": "TYPE2", "tag": "TAG"}


def parse_saved(path):
    """Read a saved card (Filetype: Flipso card) into the same block list."""
    blocks = []
    with open(path) as fh:
        for lineno, raw in enumerate(fh, 1):
            key, _, value = raw.partition(":")
            key, value = key.strip(), value.strip()
            if not key or not value:
                continue

            index = 0
            product = re.fullmatch(r"[Pp]roduct\s+(\d+)", key)
            if product:
                label, index = "GROUP", int(product.group(1))
            elif key.lower() in SAVED_KEYS:
                label = SAVED_KEYS[key.lower()]
            else:
                continue  # Filetype, Version, Read at, or a key from a later build.

            if not re.fullmatch(r"[0-9A-Fa-f]{2}(\s+[0-9A-Fa-f]{2})*", value):
                sys.exit(f"{path}:{lineno}: {key} is not a hex byte array")
            data = bytes.fromhex(value.replace(" ", ""))
            blocks.append([label, index, len(data), data])
    return blocks


def looks_saved(path):
    """True for a file whose first meaningful line is our Filetype header."""
    with open(path) as fh:
        for raw in fh:
            if raw.strip():
                return raw.strip().lower().startswith("filetype:")
    return False


def parse_dump(path):
    """Read a dump file into {label: [(index, bytes), ...]}."""
    blocks = []
    label = None
    declared = 0
    with open(path) as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.strip()
            if not line or line.startswith("---"):
                continue
            head = re.match(r"^([A-Za-z_]+)(\d*)\s+(\d+)$", line)
            if head:
                label, index, declared = head.group(1).upper(), head.group(2), int(head.group(3))
                blocks.append([label, int(index) if index else len(
                    [b for b in blocks if b[0] == label]) + 1, declared, None])
                continue
            if label is None:
                sys.exit(f"{path}:{lineno}: hex line before any block header")
            if not re.fullmatch(r"[0-9A-Fa-f]*", line):
                sys.exit(f"{path}:{lineno}: expected hex, got {line[:40]!r}")
            data = bytes.fromhex(line)
            if len(data) != declared:
                print(f"{path}:{lineno}: {label} declared {declared} bytes but carries "
                      f"{len(data)} - using what is there", file=sys.stderr)
            blocks[-1][3] = data
            label = None
    missing = [b[0] for b in blocks if b[3] is None]
    if missing:
        sys.exit(f"{path}: block(s) {', '.join(missing)} have a header but no hex line")
    return blocks


def c_array(name, data):
    body = ",\n    ".join(
        ", ".join(f"0x{b:02X}" for b in data[i:i + 12]) for i in range(0, len(data), 12))
    if not data:
        return f"static const uint8_t {name}[1] = {{0}};\nstatic const size_t {name}_len = 0;\n"
    return (f"static const uint8_t {name}[{len(data)}] = {{\n    {body}\n}};\n"
            f"static const size_t {name}_len = {len(data)};\n")


def write_header(blocks, path):
    shell = next((b[3] for b in blocks if b[0] == "SHELL"), b"")
    directory = next((b[3] for b in blocks if b[0] == "DIR"), b"")
    log = next((b[3] for b in blocks if b[0] in ("LOG", "LOGGROUP")), b"")
    type2 = next((b[3] for b in blocks if b[0] == "TYPE2"), b"")
    tag = next((b[3] for b in blocks if b[0] == "TAG"), b"")
    groups = [b for b in blocks if b[0] in ("GROUP", "IPE")]

    # Generated, so exempt from ufbt lint: clang-format would split every array.
    out = ["/* Generated by tools/test/replay.py - do not edit. */",
           "/* clang-format off */",
           "#pragma once", "", "#include <stddef.h>", "#include <stdint.h>", "",
           "typedef struct {", "    uint8_t index;", "    const uint8_t* data;",
           "    size_t len;", "} ReplayBlock;", ""]
    out.append(c_array("replay_shell", shell))
    out.append(c_array("replay_dir", directory))
    out.append(c_array("replay_log", log))
    out.append(c_array("replay_type2", type2))
    out.append(c_array("replay_tag", tag))
    for _, index, _, data in groups:
        out.append(c_array(f"replay_group_{index}", data))
    out.append("static const ReplayBlock replay_groups[] = {")
    for _, index, _, _ in groups:
        out.append(f"    {{{index}, replay_group_{index}, sizeof(replay_group_{index})}},")
    out.append("    {0, NULL, 0},")
    out.append("};")
    out.append(f"static const size_t replay_group_count = {len(groups)};")
    with open(path, "w") as fh:
        fh.write("\n".join(out) + "\n")
    return shell, directory, log, groups, type2


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    dump = sys.argv[1]
    if not os.path.exists(dump):
        sys.exit(f"{dump}: not found. Pull a saved card, or a dump, with:\n"
                 "  tools/flipper/flipctl pull /ext/apps_data/flipso/cards/NAME.flipso card.flipso\n"
                 "  tools/flipper/flipctl pull /ext/apps_data/flipso/dump.txt dump.txt")

    blocks = parse_saved(dump) if looks_saved(dump) else parse_dump(dump)
    if not blocks:
        sys.exit(f"{dump}: no card blocks in it")
    header = os.path.join(HERE, "replay_data.h")
    shell, directory, log, groups, type2 = write_header(blocks, header)
    if type2:
        print(f"{dump}: Type 2 tag, {len(type2)}B of page memory -> "
              f"{os.path.relpath(header, ROOT)}")
    else:
        print(f"{dump}: shell {len(shell)}B, directory {len(directory)}B, "
              f"{len(groups)} product group(s), log {len(log)}B -> "
              f"{os.path.relpath(header, ROOT)}")

    binary = os.path.join(HERE, "replay")
    # Same sanitiser flags as run.sh: a decoder bug in card data is usually an
    # over-read, and ASan is the only thing that reliably catches those.
    cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
           "-Wno-unused-parameter", "-fsanitize=address,undefined",
           "-I", os.path.join(ROOT, "itso"), "-I", HERE,
           os.path.join(HERE, "replay.c"),
           os.path.join(ROOT, "itso", "itso_parse.c"),
           os.path.join(ROOT, "itso", "itso_util.c"),
           os.path.join(ROOT, "itso", "itso_names.c"),
           "-o", binary]
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr, file=sys.stderr)
        sys.exit("replay: build failed")
    print()
    sys.exit(subprocess.run([binary]).returncode)


if __name__ == "__main__":
    main()
