#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""NSBENCH.TTP on an emulated Falcon, the family's (common/tools/machines.py:
Erez's TOS 4.04 when ~/.aera/tos has it, else EmuTOS): a 68030 at 32 MHz
(Hatari's fastest) with a 68882, 14 MB of ST-RAM and 128 MB of TT-RAM, and
cycle-exact timing; or with --stock, a stock Falcon: 16 MHz, no FPU, no TT-RAM.

    tests/bench/hatari.py [--stock] PAGE.HTML [SHEET.CSS ...] [-- NSBENCH-ARGS]

It prints what nsbench printed (its times are the emulated machine's).
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
    extra = []
    stock = "--stock" in args
    if stock:
        args.remove("--stock")
    if "--" in args:
        extra = args[args.index("--") + 1:]
        args = args[:args.index("--")]
    page, sheets = args[0], args[1:]
    work = os.path.join(ROOT, "build", "hatari")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copy(os.path.join(ROOT, "binary", "NSBENCH.TTP"), work)
    shutil.copy(page, os.path.join(work, "PAGE.HTM"))
    names = []
    for i, s in enumerate(sheets):
        n = "SHEET%d.CSS" % (i + 1)
        shutil.copy(s, os.path.join(work, n))
        names.append(n)
    with open(os.path.join(work, "NSBENCH.CMD"), "w") as f:
        f.write(" ".join(extra + ["PAGE.HTM"] + names))
    log = os.path.join(work, "hatari.log")
    m = machines.Machine("Falcon:16", work)
    cmd = ["hatari"] + m.args() + ["--auto", "C:\\NSBENCH.TTP", "--fast-boot", "yes", "--natfeats", "yes",
                                   "--cpu-exact", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                   "--log-level", "fatal"]
    if stock:		# a stock Falcon: its own clock, no FPU, no TT-RAM
        for opt, val in (("--cpuclock", "16"), ("--fpu", "none"), ("--ttram", "0")):
            cmd[cmd.index(opt) + 1] = val
    print(m.describe() + (", stock" if stock else ""))
    h = subprocess.Popen(cmd, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    end = time.time() + 3000
    try:
        while time.time() < end and h.poll() is None:
            time.sleep(1)
            if "done (" in open(log, errors="replace").read():
                break
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
    text = open(log, errors="replace").read()
    print(text.strip())
    sys.exit(0 if "done (0)" in text else 1)


if __name__ == "__main__":
    main()
