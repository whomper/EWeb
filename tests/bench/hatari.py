#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""NSBENCH.TTP on an emulated Falcon: Hatari, EmuTOS, a 68030 at 32 MHz
(Hatari's fastest) with a 68882, 14 MB of ST-RAM and 64 MB of TT-RAM, and
cycle-exact timing; or with --stock, a stock Falcon: 16 MHz, no FPU, no TT-RAM.

    tests/bench/hatari.py [--stock] ETOS1024K.IMG PAGE.HTML [SHEET.CSS ...] [-- NSBENCH-ARGS]

It prints what nsbench printed (its times are the emulated machine's).
"""
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    args = sys.argv[1:]
    extra = []
    stock = "--stock" in args
    if stock:
        args.remove("--stock")
    if "--" in args:
        extra = args[args.index("--") + 1:]
        args = args[:args.index("--")]
    tos, page, sheets = os.path.abspath(args[0]), args[1], args[2:]
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
    cmd = ["hatari", "--machine", "falcon", "--tos", tos, "--harddrive", work,
           "--auto", "C:\\NSBENCH.TTP", "--fast-boot", "yes", "--natfeats", "yes",
           "--cpuclock", "16" if stock else "32", "--fpu", "none" if stock else "68882",
           "--addr24", "off", "--memsize", "14", "--ttram", "0" if stock else "64",
           "--cpu-exact", "yes",
           "--fast-forward", "yes", "--sound", "off", "--confirm-quit", "no",
           "--statusbar", "no", "--monitor", "vga", "--log-level", "fatal"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(cmd, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
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
