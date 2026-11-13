#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEBFB.PRG, the FireBee's EWeb (ColdFire, -mcpu=5475; Erez, 10 Nov 2026:
a third file beside EWEB.PRG and EWEBST.PRG), made from the very rules
EWEB.PRG is made with: `make -n -B build/tos/eweb.elf` gives each command,
and each cross-compiler command is run again for the ColdFire:

- -m68030, -m68000, -msoft-float and -m68881 become -mcpu=5475 (the
  MCF5474 has an FPU: libgcc's soft float, like all of the cross
  compiler's libgcc, is 68020 code);
- build/tos/ becomes build/cf/;
- common's cpucheck.c (68000 code, which a ColdFire may not run) becomes
  cpucheck_cf.S, the same bytes for the 68000 and the ColdFire: the _CF_
  cookie, or ui/main.c's cpu_alert;
- the scripts' overlay, EWEBJS.OVL, is made for the ColdFire as well
  (EWEBJSFB.OVL beside OUT.PRG: QuickJS and musl's maths in the FPU's
  doubles, -mcpu=5475 in -m68881's place), from its own rules in the same
  output, with tools/mkovl.py, and EWEBFB.PRG's js_imports from it;
- -lgcc becomes common's atari/ll64.c (64-bit multiply and divide in C),
  and EWeb's atari/cfmath.c (64-bit integers to and from doubles, for
  the overlay).

The generated sources (build/gen) are the Falcon build's: run `make
binary/EWEB.PRG` first (the Makefile's binary/EWEBFB.PRG does).

With --68060 it makes EWEB60.PRG, the 68060's EWeb (Erez, 12 Nov 2026: the
68030's EWEB.PRG kept as it is for the stock Falcon) from the same rules:
-m68030 becomes -m68060 (no 64-bit mulu.l/divu.l, which the 68060 hasn't),
build/tos/ build/p060/, common's ll64.c goes ahead of libgcc (whose 64-bit
division has them), and none is left (objdump); the scripts' overlays and
their js_imports are EWEB.PRG's (EWEBJS60.OVL, EWEBJS.OVL).

    tools/mkfirebee.py [--68060] OUT.PRG"""
import os
import re
import shlex
import subprocess
import sys

CC = "m68k-linux-gnu-gcc"
MCPU = "-mcpu=5475"
OUT = "build/cf/"
# what the 68060 hasn't: a 64-bit multiply or divide, which objdump -m
# m68k:68030 writes with three operands (the 32-bit divull with two
# registers is the 68060's)
WIDE = re.compile(r"\s(mulul|mulsl|divul|divsl)\s+.*,%d[0-7],%d[0-7]\s*$")


def run(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(cmd + "\n" + p.stdout + p.stderr)
        sys.exit("mkfirebee: a command failed")
    return p.stdout


def cpu(words):
    out = []
    for w in words:
        if MCPU == "-m68060":
            if w in ("-m68030", "-m68020", "-m68020-60"):
                w = MCPU
        elif w in ("-m68030", "-m68000", "-msoft-float", "-m68881", "-m68020", "-m68020-60"):
            if MCPU not in out:
                out.append(MCPU)
            continue
        out.append(w.replace("build/tos/", OUT))
    return out


def rodata_start(path):
    """where the first read-only data goes in .text, from the link's map"""
    seen = False
    for line in open(path):
        if "*(.rodata .rodata.*)" in line:
            seen = True
        elif seen:
            m = re.match(r"\s*\.rodata\S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", line)
            if m and int(m.group(2), 16):
                return int(m.group(1), 16)
            m = re.match(r"\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s", line)	# (a long name: its address on the next line)
            if m and int(m.group(2), 16):
                return int(m.group(1), 16)
    sys.exit("mkfirebee: no read-only data in " + path)


def m68060(target, compiles, link):
    """EWEB60.PRG: EWEB.PRG's objects (not its overlays') for the 68060"""
    objs = set(w for w in link if w.endswith(".o"))
    n = 0
    for words in compiles:
        if words[words.index("-o") + 1] not in objs:
            continue
        words = cpu(words)
        os.makedirs(os.path.dirname(words[words.index("-o") + 1]), exist_ok=True)
        run(" ".join(shlex.quote(w) for w in words))
        n += 1
    os.makedirs(OUT + "rt", exist_ok=True)
    run(" ".join([CC, MCPU, "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-asynchronous-unwind-tables", "-c", "common/atari/ll64.c", "-o", OUT + "rt/ll64.o"]))
    words = cpu(link)
    words.insert(words.index("-lgcc"), OUT + "rt/ll64.o")
    elf = words[words.index("-o") + 1]
    words.insert(words.index("-lgcc"), "-Wl,-Map=" + elf + ".map")
    run(" ".join(shlex.quote(w) for w in words))
    # the code only: common's link.ld puts the read-only data in .text after
    # it, and a string's bytes can read as a divu.l (1.10's first build:
    # "LED\0WM_" of the window messages' names, there since the version's
    # name grew a byte)
    stop = rodata_start(elf + ".map")
    wide = [l for l in run("m68k-linux-gnu-objdump -d -m m68k:68030 --stop-address=0x%x %s" % (stop, elf)).splitlines()
            if WIDE.search(l)]
    if wide:
        sys.exit("mkfirebee: %d of the 68060's missing forms in %s, as\n%s" % (len(wide), elf, "\n".join(wide[:5])))
    env = dict(os.environ, PRGFLAGS="7")
    subprocess.run(["python3", "common/tools/elf2tos.py", elf, target], env=env, check=True)
    print("mkfirebee: %s, %d objects, %d bytes, no 64-bit multiply or divide" % (target, n, os.path.getsize(target)))


def main():
    global MCPU, OUT
    args = sys.argv[1:]
    if args[:1] == ["--68060"]:
        MCPU, OUT = "-m68060", "build/p060/"
        args = args[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    target = args[0]
    lines = run("make -n -B build/tos/eweb.elf").replace("\\\n", " ").splitlines()
    compiles, link, ovl_link, imports = [], None, None, None
    for line in lines:
        if not line.startswith(CC + " "):
            continue
        words = shlex.split(line)
        if "-o" in words and words[words.index("-o") + 1].startswith("build/t060/"):
            continue  # (the 68060's overlay: not the FireBee's)
        if "-c" in words:
            if "js_imports" in words[words.index("-c") + 1]:
                imports = words  # (made from the ColdFire's overlay, below)
            else:
                compiles.append(words)
        elif "-o" in words and words[words.index("-o") + 1] == "build/tos/eweb.elf":
            link = words
        elif "-o" in words and words[words.index("-o") + 1] == "build/tos/ewebjs.elf":
            ovl_link = words
    if link is None or ovl_link is None or imports is None:
        sys.exit("mkfirebee: make gave no link of build/tos/eweb.elf, of its overlay or its imports")
    if MCPU == "-m68060":
        return m68060(target, compiles + [imports], link)
    objs = []
    for words in compiles:
        src = words[words.index("-c") + 1]
        obj = words[words.index("-o") + 1]
        if src == "common/atari/cpucheck.c":
            words = [CC, MCPU, "-c", "common/atari/cpucheck_cf.S", "-o", obj]
        words = cpu(words)
        o = words[words.index("-o") + 1]
        os.makedirs(os.path.dirname(o), exist_ok=True)
        objs.append(o)
        run(" ".join(shlex.quote(w) for w in words))
    # the 64-bit arithmetic: ll64.c in libgcc's place
    os.makedirs("build/cf/rt", exist_ok=True)
    run(" ".join([CC, MCPU, "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-asynchronous-unwind-tables", "-c", "common/atari/ll64.c", "-o", "build/cf/rt/ll64.o"]))
    # the scripts' overlay, EWEBJSFB.OVL, and what EWEBFB.PRG gives it
    words = cpu([w for w in ovl_link if w != "-lgcc"]) + ["build/cf/rt/ll64.o"]
    run(" ".join(shlex.quote(w) for w in words))
    ovl = os.path.join(os.path.dirname(target) or ".", "EWEBJSFB.OVL")
    os.makedirs("build/cf/gen", exist_ok=True)
    run(" ".join(["python3", "tools/mkovl.py", "build/cf/ewebjs.elf", shlex.quote(ovl), "build/cf/gen/js_imports.c"]))
    src = imports[imports.index("-c") + 1]
    words = cpu([w if w != src else "build/cf/gen/js_imports.c" for w in imports])
    run(" ".join(shlex.quote(w) for w in words))
    # (and what EWEBJSFB.OVL takes from it beyond ll64.c: atari/cfmath.c)
    run(" ".join([CC, MCPU, "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-asynchronous-unwind-tables", "-c", "atari/cfmath.c", "-o", "build/cf/rt/cfmath.o"]))
    words = cpu([w for w in link if w != "-lgcc"])
    words += [o for o in ("build/cf/rt/ll64.o", "build/cf/rt/cfmath.o") if o not in words]
    run(" ".join(shlex.quote(w) for w in words))
    elf = words[words.index("-o") + 1]
    env = dict(os.environ, PRGFLAGS="7")
    subprocess.run(["python3", "common/tools/elf2tos.py", elf, target], env=env, check=True)
    # no 68k-only instruction in the code: disassembled as the ColdFire's,
    # nothing undecodable inside a function but its own tables
    print("mkfirebee: %s, %d objects, %d bytes" % (target, len(objs), os.path.getsize(target)))


if __name__ == "__main__":
    main()
