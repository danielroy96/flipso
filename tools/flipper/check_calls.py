#!/usr/bin/env python3
"""Fail when a .fap makes a short call into the firmware.

flipso_calls.h turns the app's own calls into Thumb BL and B.W, which reach
about 16 MB either way. The firmware's functions are in flash and the app is
loaded into RAM, hundreds of megabytes apart, so a short call to one of them
cannot be relocated and the app does not load. That happens when a firmware
header is first included after the pragma; this finds it in the build output,
before the device does.

    tools/flipper/check_calls.py [dist/flipso.fap]

Pure Python on purpose: it reads the ELF itself, so it runs wherever the .fap
was built, without the ARM toolchain on the PATH.
"""
import os
import struct
import sys

# Thumb relocations a BL or B.W carries (ARM ELF ABI, table 4-18).
SHORT_CALLS = {10: "R_ARM_THM_CALL", 30: "R_ARM_THM_JUMP24", 102: "R_ARM_THM_JUMP11"}
SHT_SYMTAB = 2
SHT_REL = 9
SHN_UNDEF = 0


def sections(elf):
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    out = []
    for i in range(shnum):
        out.append(struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize))
    names = out[shstrndx]
    named = []
    for s in out:
        start = names[4] + s[0]
        name = elf[start:elf.index(b"\0", start)].decode()
        named.append((name, s))
    return named


def short_calls_to_firmware(path):
    elf = open(path, "rb").read()
    if elf[:4] != b"\x7fELF" or elf[4] != 1:
        raise SystemExit(f"{path}: not a 32-bit ELF")
    secs = sections(elf)
    symtab = next(s for _, s in secs if s[1] == SHT_SYMTAB)
    strtab = secs[symtab[6]][1]

    def symbol(index):
        name, value, size, info, other, shndx = struct.unpack_from(
            "<IIIBBH", elf, symtab[4] + index * 16)
        start = strtab[4] + name
        return elf[start:elf.index(b"\0", start)].decode(), shndx

    bad = []
    for name, s in secs:
        if s[1] != SHT_REL:
            continue
        for off in range(s[4], s[4] + s[5], 8):
            r_offset, r_info = struct.unpack_from("<II", elf, off)
            kind = r_info & 0xFF
            if kind not in SHORT_CALLS:
                continue
            sym, shndx = symbol(r_info >> 8)
            if shndx == SHN_UNDEF:
                bad.append((name, r_offset, SHORT_CALLS[kind], sym))
    return bad


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "dist", "flipso.fap")
    bad = short_calls_to_firmware(path)
    for section, offset, kind, sym in bad:
        print(f"{section}+0x{offset:x}: {kind} to firmware symbol {sym}")
    if bad:
        print(f"{path}: {len(bad)} short call(s) into the firmware - include the header "
              "that declares them in flipso_calls.h, before the pragma")
        return 1
    print(f"{path}: no short calls into the firmware")
    return 0


if __name__ == "__main__":
    sys.exit(main())
