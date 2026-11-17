#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""profpad.py --ship SHIP.ELF --out OUT.ELF --cpu=CPU --dir DIR
   [--exclude OBJ ...] -- LINK...   (SHIP.ELF.map: the shipped link's map)

The timed build (build/prof: common's gem.c and ui/main.c with
AES_PROFILE) linked at the shipped build's places modulo 256, as the
68030's caches see them (DESIGN.md, Code layout and the 68030's caches):
otherwise the speed rig times a layout that isn't the one shipped (the
hub, from EPlay 1.45, 17 Nov 2026). The timed objects (the --exclude
ones, whose own symbols differ) are bigger, so is the one block the
linker merges every string into (the read-only data's start), and the
code they call that the shipped build leaves out is kept.

LINK is the link without its -o. It is linked with a map; the first
symbol both builds have (a unique name, not an --exclude object's) that
is not at its shipped place modulo 256 names its object, and a pad
(atari/codepad.S: code, read-only data, data and BSS, 1 to 256 bytes
each) goes after the object before it in the link, grown until it is;
then the next, until every one is. A symbol inside an object whose first
symbols are in place can't be put back by a pad: that fails the build,
as does any symbol still out of place. The pads are kept in
DIR/pads.txt, the next link's start.
"""
import argparse
import os
import re
import subprocess
import sys

KINDS = (("text", "t", ""), ("ro", "r", "_ro"), ("data", "d", "_data"), ("bss", "b", "_bss"))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def nm(path):
    """name -> (address, kind), and the names defined twice"""
    out = subprocess.run(["m68k-linux-gnu-nm", "-n", "--defined-only", path], capture_output=True, text=True,
                         check=True).stdout
    syms, twice = {}, set()
    for ln in out.splitlines():
        p = ln.split()
        if len(p) != 3 or p[1].lower() not in "trdb":
            continue
        if p[2] in syms:
            twice.add(p[2])
        syms[p[2]] = (int(p[0], 16), p[1].lower())
    return syms, twice


def defined(obj):
    out = subprocess.run(["m68k-linux-gnu-nm", "--defined-only", obj], capture_output=True, text=True).stdout
    return {ln.split()[-1] for ln in out.splitlines() if ln.split()}


def pieces(mapfile):
    """the map's input sections: (start, size, object), kept ones only"""
    out = []
    text = open(mapfile, errors="replace").read()
    text = text[text.find("Linker script and memory map"):]
    for m in re.finditer(r"^ \S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) (\S+\.o)\s*$", text, re.M):
        start, size = int(m.group(1), 16), int(m.group(2), 16)
        if size:
            out.append((start, size, m.group(3)))
    return sorted(out)


def ro_start(mapfile):
    """where the read-only data starts (it shares .text with the code, so
    nm calls it code)"""
    text = open(mapfile, errors="replace").read()
    m = re.search(r"\*\(\.rodata \.rodata\.\*\)\s*\n\s*\S*\s+0x([0-9a-f]+)", text)
    return int(m.group(1), 16) if m else None


def kinds(syms, ro):
    """each symbol's kind, read-only data apart from code"""
    return {n: (a, "r" if k == "t" and ro is not None and a >= ro else k) for n, (a, k) in syms.items()}


def object_at(parts, addr):
    """the object of the innermost piece holding addr (the merged strings'
    piece spans others)"""
    found = None
    for start, size, obj in parts:
        if start > addr:
            break
        if addr < start + size:
            found = obj
    return found


def link(a, link_cmd, pads, mapfile):
    cmd, us, after = [], [], {}
    for i, (obj, sizes) in enumerate(pads):
        name = "profpad_%d" % i
        o = os.path.join(a.dir, name + ".o")
        subprocess.run(["m68k-linux-gnu-gcc", a.cpu, "-DPAD_NAME=" + name] +
                       ["-DPAD_%s=%d" % (k.upper(), sizes[k]) for k, _, _ in KINDS] +
                       ["-c", os.path.join(ROOT, "atari", "codepad.S"), "-o", o], check=True)
        after[obj] = o
        us += ["-Wl,-u," + name + s for _, _, s in KINDS]
    for w in link_cmd:
        cmd.append(w)
        if w in after:
            cmd.append(after[w])
    subprocess.run(cmd[:1] + us + cmd[1:] + ["-o", a.out, "-Wl,-Map," + mapfile], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ship", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cpu", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--exclude", action="append", default=[])
    ap.add_argument("link", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    link_cmd = a.link[1:] if a.link[:1] == ["--"] else a.link
    objs = [w for w in link_cmd if w.endswith(".o")]
    os.makedirs(a.dir, exist_ok=True)
    kept = os.path.join(a.dir, "pads.txt")
    pads = []			# [(object it follows, {kind: size})], in link order
    try:
        for ln in open(kept):
            p = ln.split()
            if len(p) == 1 + len(KINDS) and p[0] in objs:
                pads.append((p[0], dict(zip([k for k, _, _ in KINDS], map(int, p[1:])))))
    except (OSError, ValueError):
        pads = []
    ship, ship2 = nm(a.ship)
    ship = kinds(ship, ro_start(a.ship + ".map"))	# (its link's map, beside it)
    skip = set().union(*(defined(o) for o in a.exclude)) if a.exclude else set()
    mapfile = a.out + ".map"
    why = None
    for it in range(60):
        link(a, link_cmd, pads, mapfile)
        prof, prof2 = nm(a.out)
        prof = kinds(prof, ro_start(mapfile))
        # (not the strings' labels, .LC: the merged block's order is the
        # linker's, with the timed build's strings in it; only where it
        # ends can be put back)
        common = sorted((prof[n][0], n) for n in prof
                        if n in ship and n not in skip and n not in ship2 and n not in prof2
                        and not n.startswith(("profpad_", ".L")) and ship[n][1] == prof[n][1])
        bad = [(addr, n) for addr, n in common if (ship[n][0] - addr) % 256]
        if not bad:
            break
        addr, n = bad[0]
        kind = prof[n][1]
        parts = pieces(mapfile)
        obj = object_at(parts, addr)
        # its object's first symbol of that kind: out of place too, or a pad
        # before the object can't help
        first = next((x for x in common if prof[x[1]][1] == kind and object_at(parts, x[0]) == obj), None)
        if obj not in objs or first is None or (ship[first[1]][0] - first[0]) % 256 == 0:
            why = "%s (%s) moved inside %s: a pad can't put it back" % (n, kind, obj)
            break
        before = objs[objs.index(obj) - 1]
        d = (ship[n][0] - addr) % 256
        k = next(k for k, letter, _ in KINDS if letter == kind)
        for o, sizes in pads:
            if o == before:
                sizes[k] = (sizes[k] + d - 1) % 256 + 1
                break
        else:
            sizes = {kk: 256 for kk, _, _ in KINDS}
            sizes[k] = d
            pads.append((before, sizes))
            pads.sort(key=lambda p: objs.index(p[0]))
    with open(kept, "w") as f:
        for o, sizes in pads:
            f.write(o + " " + " ".join(str(sizes[k]) for k, _, _ in KINDS) + "\n")
    print("profpad: %s: %d symbols at the shipped places modulo 256, %d not; pads after %s"
          % (a.out, len(common) - len(bad), len(bad),
             ", ".join("%s %s" % (os.path.basename(o), "/".join(str(s[k]) for k, _, _ in KINDS))
                       for o, s in pads) or "none"))
    if bad:
        if why:
            print("profpad:   " + why)
        for addr, n in bad[:8]:
            print("profpad:   %s shipped at %#x, timed at %#x" % (n, ship[n][0], addr))
        os.replace(a.out, a.out + ".bad")	# (make sees no ELF; kept to look at)
        sys.exit(1)


if __name__ == "__main__":
    main()
