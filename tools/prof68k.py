#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Where a Linux 68030 program's instructions go, function by function:
QEMU's hotblocks plugin (tools/build-qemu.sh lists every block) and the
program's symbols (nm).

    tools/prof68k.py [-n 40] PROGRAM ARGS...

prints the total, then the functions that run the most instructions, each
with its share. The counts are exact for the code; a real 68030's time
also depends on its caches and memory."""
import bisect
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
Q = os.path.join(os.environ.get("QEMU_DIR", os.path.join(ROOT, "build/qemu")), "out")


def main():
    args = sys.argv[1:]
    top = 40
    if args[:1] == ["-n"]:
        top = int(args[1])
        args = args[2:]
    prog = args[0]
    syms = []
    for line in subprocess.run(["m68k-linux-gnu-nm", "-n", "--defined-only", prog], capture_output=True,
                               text=True).stdout.splitlines():
        a, t, name = line.split(" ", 2)
        if t.lower() in "tw":
            syms.append((int(a, 16), name))
    addrs = [a for a, _ in syms]
    with tempfile.NamedTemporaryFile("r", suffix=".log") as log:
        subprocess.run([os.path.join(Q, "qemu-m68k"), "-cpu", "m68030", "-plugin",
                        os.path.join(Q, "contrib/plugins/libhotblocks.so"), "-d", "plugin", "-D", log.name] + args,
                       stdout=subprocess.DEVNULL)
        per = {}
        total = 0
        for line in open(log.name):
            m = re.match(r"\s*0x([0-9a-f]+), (\d+), (\d+), (\d+)", line)
            if not m:
                continue
            pc, insns, execs = int(m.group(1), 16), int(m.group(3)), int(m.group(4))
            n = execs * insns
            i = bisect.bisect_right(addrs, pc) - 1
            name = syms[i][1] if i >= 0 else "?"
            per[name] = per.get(name, 0) + n
            total += n
    print("insns %d" % total)
    for name, n in sorted(per.items(), key=lambda x: -x[1])[:top]:
        print("%12d %5.1f%%  %s" % (n, 100.0 * n / total, name))


main()
