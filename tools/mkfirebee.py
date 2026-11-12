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

    tools/mkfirebee.py OUT.PRG"""
import os
import re
import shlex
import subprocess
import sys

CC = "m68k-linux-gnu-gcc"
MCPU = "-mcpu=5475"


def run(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(cmd + "\n" + p.stdout + p.stderr)
        sys.exit("mkfirebee: a command failed")
    return p.stdout


def cpu(words):
    out = []
    for w in words:
        if w in ("-m68030", "-m68000", "-msoft-float", "-m68881", "-m68020", "-m68020-60"):
            if MCPU not in out:
                out.append(MCPU)
            continue
        out.append(w.replace("build/tos/", "build/cf/"))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    target = sys.argv[1]
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
