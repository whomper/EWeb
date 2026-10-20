#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tests/bench/picbench.c on one of the family's machines in Hatari
(common/tools/machines.py: Erez's TOS when ~/.aera/tos has it, else
EmuTOS), cycle-exact: a picture decoded as EWeb decodes it, and dithered,
each step timed. On an ST, PICBST.TTP (`make binary/PICBST.TTP`: as
EWEBST.PRG); on a Falcon, PICBENCH.TTP (`make binary/PICBENCH.TTP`: as
EWEB.PRG, on the 68030 and then with the DSP).
    tests/bench/picbench.py [--machine ST:mono] [--profile OUT] [PICTURE WIDTH HEIGHT [COLOURS [TIMES [WHICH]]]]
Without a picture, the release's run: PHOTO.JPG (tests/bench/mkphoto.py) at
its size and at 192 x 128. It prints what picbench printed (its times are
the emulated machine's). --profile has Hatari's profiler count the CPU's
cycles from the start to the end (picbench quitting) and save
them, with the program's own addresses named from its ELF, in OUT; WHICH 2
times the DSP's way alone, 1 the 68030's.
"""
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402


def main():
    args = sys.argv[1:]
    spec = "ST:mono"
    profile = None
    while args[:1] in (["--machine"], ["--profile"]):
        if args[0] == "--machine":
            spec = args[1]
        else:
            profile = os.path.abspath(args[1])
        args = args[2:]
    if len(args) not in (0, 3, 4, 5, 6):
        sys.exit(__doc__)
    work = os.path.join(ROOT, "build", "picbench")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    if args:
        pic = args[0]
        name = "PIC" + os.path.splitext(pic)[1].upper()[:4]
    else:
        subprocess.run([sys.executable, os.path.join(ROOT, "tests", "bench", "mkphoto.py"), work], check=True)
    prog = "PICBST" if spec.upper().startswith("ST") else "PICBENCH"
    shutil.copy(os.path.join(ROOT, "binary", prog + ".TTP"), work)
    if args:
        shutil.copy(pic, os.path.join(work, name))
        with open(os.path.join(work, prog + ".CMD"), "w") as f:
            f.write(" ".join([name] + args[1:]))
    log = os.path.join(work, "hatari.log")
    m = machines.Machine(spec, work)
    cmd = ["hatari"] + m.args() + ["--auto", "C:\\%s.TTP" % prog, "--fast-boot", "yes", "--natfeats", "yes",
                                   "--cpu-exact", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                   "--log-level", "fatal"]
    raw = os.path.join(work, "profile.txt")
    if profile:
        # profile from the start; at the end (Pterm) save it
        with open(os.path.join(work, "save.ini"), "w") as f:
            f.write("profile save %s\nquit\n" % raw)
        with open(os.path.join(work, "prof.ini"), "w") as f:
            f.write("profile on\nb GemdosOpcode = 0x4C :file %s\n" % os.path.join(work, "save.ini"))
        cmd += ["--parse", os.path.join(work, "prof.ini")]
    print(m.describe())
    h = subprocess.Popen(cmd, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    end = time.time() + 3000
    try:
        while time.time() < end and h.poll() is None:
            time.sleep(1)
            if not profile and "done (" in open(log, errors="replace").read():
                break
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
    text = open(log, errors="replace").read()
    print(text.strip())
    if profile:
        name_profile(raw, text, prog, profile)
    sys.exit(0 if "done (0)" in text else 1)


def name_profile(raw, log, prog, out):
    """Hatari's profile, the program's addresses as its functions: the
    cycles of each, most first"""
    import re
    base = int(re.search(r"program in \S+ at \$([0-9a-f]+)", log).group(1), 16)
    elf = os.path.join(ROOT, "build", "st" if prog == "PICBST" else "tos", "picbench.elf")
    syms = []
    for line in subprocess.run(["m68k-linux-gnu-nm", "-n", elf], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in "tT":
            syms.append((int(f[0], 16), f[2]))
    text0 = min(a for a, _ in syms)
    text1 = text0
    for line in subprocess.run(["m68k-linux-gnu-size", "-A", elf], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if f[:1] == [".text"]:
            text1 = int(f[2]) + int(f[1])
    cycles, total = {}, 0
    for line in open(raw, errors="replace"):
        # "ADDR bytes disassembly  pct% (instructions, cycles, i-misses, d-hits)"
        m = re.match(r"([0-9a-f]{8}) .*% \((\d+), (\d+),", line)
        if not m:
            continue
        addr, cyc = int(m.group(1), 16), int(m.group(3))
        total += cyc
        off = addr - base + text0
        name = "(TOS, at $%06x)" % (addr & ~0xff) if addr >= 0xe00000 else "(outside the program)"
        if text0 <= off < text1:
            lo, hi = 0, len(syms) - 1
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if syms[mid][0] <= off:
                    lo = mid
                else:
                    hi = mid - 1
            name = syms[lo][1]
        cycles[name] = cycles.get(name, 0) + cyc
    with open(out, "w") as f:
        for name, cyc in sorted(cycles.items(), key=lambda x: -x[1]):
            f.write("%12d %5.1f%%  %s\n" % (cyc, 100.0 * cyc / max(total, 1), name))
    print("profile: %s" % out)


if __name__ == "__main__":
    main()
