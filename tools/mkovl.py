#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEBJS.OVL from the overlay's ELF (atari/js_ovl.h, atari/ovl.ld), and
the C file of the addresses EWEB.PRG gives it:

    mkovl.py OVL.ELF EWEBJS.OVL JS_IMPORTS.C

The ELF is linked at 0 with --emit-relocs and its imports left undefined.
The file is a TOS program's: the header (flags 7: in TT RAM when there is
some), text and data, TOS's fixups for the places that hold an address
in the overlay, then the imports' part:

    "EWJI", the CRC of the imports' names, how many places, and for each
    its offset (a long) and the import's number (a word) and kind (a
    word: 0 the place holds an address, 1 a distance from the place)

as each place that names something of EWEB.PRG's gets its address there
(R_68K_32) or the distance to it (R_68K_PC32, PLT32) added when it is
loaded (atari/js_ovl.c). JS_IMPORTS.C lists their addresses in the same
order, with the same CRC, so an overlay from another build is refused.
"""
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("/", 2)[0] + "/common/tools")
from elf2tos import read_elf  # noqa: E402

R_68K_32, R_68K_16, R_68K_8, R_68K_PC32, R_68K_PLT32 = 1, 2, 3, 4, 13
PC_KINDS = (R_68K_PC32, R_68K_PLT32)
SHN_ABS = 0xFFF1
SHF_ALLOC = 2
HEAD = """/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Made by tools/mkovl.py: what EWEBJS.OVL takes from EWEB.PRG, in its order. */
"""


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    data, secs = read_elf(sys.argv[1])
    by = {s["name"]: s for s in secs}
    text, dat, bss = by[".text"], by.get(".data"), by.get(".bss")
    tsize = text["size"]
    dsize = dat["size"] if dat else 0
    bsize = bss["size"] if bss else 0
    if text["addr"] != 0:
        sys.exit(".text must be linked at 0")
    if dat and dsize and dat["addr"] != tsize:
        sys.exit(".data must follow .text directly")
    if bss and bsize and bss["addr"] != tsize + dsize:
        sys.exit(".bss must follow .data directly")
    # nothing else may be loaded (a .got or .init_array would be lost)
    for s in secs:
        if s["flags"] & SHF_ALLOC and s["size"] and s["name"] not in (".text", ".data", ".bss"):
            sys.exit("%s: a loaded section mkovl.py doesn't know" % s["name"])
    image = bytearray(data[text["off"]:text["off"] + tsize])
    if dsize:
        image += data[dat["off"]:dat["off"] + dsize]

    symtab = by[".symtab"]
    strtab = secs[symtab["link"]]

    def sym(i):
        name, value, _, info, _, shndx = struct.unpack_from(">IIIBBH", data, symtab["off"] + i * 16)
        end = data.index(b"\0", strtab["off"] + name)
        return data[strtab["off"] + name:end].decode(), value, info >> 4, shndx

    fixups, places = [], []
    for s in secs:
        if s["type"] != 4:  # SHT_RELA
            continue
        if secs[s["info"]]["name"] not in (".text", ".data", ".ovlhead"):
            continue
        for i in range(s["size"] // 12):
            r_off, r_info, _ = struct.unpack_from(">IIi", data, s["off"] + i * 12)
            kind, si = r_info & 0xFF, r_info >> 8
            name, _, bind, shndx = sym(si) if si else ("", 0, 0, 1)
            if r_off & 1:
                sys.exit("odd relocation at %x" % r_off)
            if shndx == 0 and si:
                if bind == 2:       # weak, and nothing defines it: stays 0
                    continue
                if kind == R_68K_32:
                    places.append((r_off, name, 0))
                elif kind in PC_KINDS:
                    places.append((r_off, name, 1))
                else:
                    sys.exit("%s: relocation %d at %x can't reach EWEB.PRG" % (name, kind, r_off))
            elif kind == R_68K_32:
                # an address in the overlay (not a number: no symbol, or
                # an absolute one)
                if si and shndx != SHN_ABS:
                    fixups.append(r_off)
            elif kind in (R_68K_16, R_68K_8):
                sys.exit("%s: a 16- or 8-bit address at %x can't be moved" % (name, r_off))
    names = sorted(set(n for _, n, _ in places))
    index = {n: i for i, n in enumerate(names)}
    crc = zlib.crc32("\n".join(names).encode()) & 0xFFFFFFFF

    fixups.sort()
    reloc = bytearray()
    if fixups:
        reloc += struct.pack(">I", fixups[0])
        prev = fixups[0]
        for a in fixups[1:]:
            d = a - prev
            if d == 0:
                continue
            while d > 254:
                reloc.append(1)
                d -= 254
            reloc.append(d)
            prev = a
        reloc.append(0)
    else:
        reloc += b"\0\0\0\0"
    imp = bytearray(b"EWJI" + struct.pack(">II", crc, len(places)))
    for off, n, k in sorted(places):
        imp += struct.pack(">IHH", off, index[n], k)

    hdr = struct.pack(">HIIIIIIH", 0x601A, tsize, dsize, bsize, 0, 0, 7, 0)
    with open(sys.argv[2], "wb") as f:
        f.write(hdr + image + reloc + imp)
    with open(sys.argv[3], "w") as f:
        f.write(HEAD + "\n")  # (no headers: each is named here as bytes, functions too)
        for n in names:
            f.write("extern char %s[];\n" % n)
        f.write("\nconst void *const js_imports[] = {\n")
        for n in names:
            f.write("\t%s,\n" % n)
        f.write("};\nconst unsigned long js_imports_n = %d, js_imports_crc = 0x%08xUL;\n" % (len(names), crc))
    print("%s: text %d, data %d, bss %d, %d fixups, %d imports at %d places" %
          (sys.argv[2], tsize, dsize, bsize, len(fixups), len(names), len(places)))


if __name__ == "__main__":
    main()
