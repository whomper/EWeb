#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tests/bench/pagebench.c on the family's emulated 8 MHz ST (common/tools/
machines.py: Erez's TOS when ~/.aera/tos has it, else EmuTOS), cycle-exact:
a page parsed, styled, laid out and drawn on the monochrome screen as
EWEBST.PRG does it, each step timed (DESIGN.md, The ST edition's speed).
PAGEST.TTP is `make binary/PAGEST.TTP`.
    tests/bench/pagest.py [--machine ST:mono] [--profile OUT [--draw]] [PAGE.HTM [SHEET.CSS...] [-w WIDTH] [-r ROWS] [-n TIMES]]
Without a page, the news page (build/news/NEWS.HTM and NEWS.CSS, as make
hatarispeed loads it; tests/bench/mkpage.py makes them). -w and -r go to
pagebench. --profile has Hatari's profiler count the CPU's cycles from the
start to the end and names them, the program's functions, in OUT; with
--draw, from the drawing's start only.
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
sys.path.insert(0, HERE)
import machines  # noqa: E402
from picbench import name_profile  # noqa: E402


def main():
    args = sys.argv[1:]
    spec = "ST:mono"
    profile = None
    draw = "--draw" in args
    args = [a for a in args if a != "--draw"]
    while args[:1] in (["--machine"], ["--profile"]):
        if args[0] == "--machine":
            spec = args[1]
        else:
            profile = os.path.abspath(args[1])
        args = args[2:]
    if not args:
        news = os.path.join(ROOT, "build", "news")
        args = [os.path.join(news, "NEWS.HTM"), os.path.join(news, "NEWS.CSS")]
    opts = [a for a in args[1:] if a.startswith("-") or a.isdigit()]  # (-w 608, -r 400, -n 3)
    sheets = [a for a in args[1:] if a not in opts]
    work = os.path.join(ROOT, "build", "pagest")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in ("PAGEST.TTP", "EWEB.FNT"):
        shutil.copy(os.path.join(ROOT, "binary", f), os.path.join(work, f))
    shutil.copy(args[0], os.path.join(work, "PAGE.HTM"))
    names = []
    for k, sh in enumerate(sheets):
        names.append("SHEET%d.CSS" % k)
        shutil.copy(sh, os.path.join(work, names[-1]))
    with open(os.path.join(work, "PAGEST.CMD"), "w") as f:
        f.write(" ".join(["-f", "EWEB.FNT"] + opts + ["PAGE.HTM"] + names))
    log = os.path.join(work, "hatari.log")
    m = machines.Machine(spec, work)
    cmd = ["hatari"] + m.args() + ["--auto", "C:\\PAGEST.TTP", "--fast-boot", "yes", "--natfeats", "yes",
                                   "--cpu-exact", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                   "--log-level", "fatal"]
    raw = os.path.join(work, "profile.txt")
    if profile:
        with open(os.path.join(work, "save.ini"), "w") as f:
            f.write("profile save %s\nquit\n" % raw)
        with open(os.path.join(work, "on.ini"), "w") as f:
            f.write("profile on\n")
        with open(os.path.join(work, "prof.ini"), "w") as f:
            # the whole run, or from the drawing's start (pagebench's Tgettime)
            f.write("b GemdosOpcode = 0x2C :once :trace :file %s\n" % os.path.join(work, "on.ini") if draw else "profile on\n")
            f.write("b GemdosOpcode = 0x4C :file %s\n" % os.path.join(work, "save.ini"))
        cmd += ["--parse", os.path.join(work, "prof.ini")]
    print(m.describe())
    h = subprocess.Popen(cmd, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    end = time.time() + 3000
    size, still = -1, 0
    try:
        while time.time() < end and h.poll() is None:
            time.sleep(1)
            if not profile and "done (" in open(log, errors="replace").read():
                break
            if profile and os.path.exists(raw):
                # (saved: once it stops growing, Hatari is done with it,
                # whether or not its quit is taken)
                n = os.path.getsize(raw)
                still = still + 1 if n == size else 0
                size = n
                if still >= 20:
                    break
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
    text = open(log, errors="replace").read()
    print(text.strip())
    if profile:
        name_profile(raw, text, "PAGEST", profile, os.path.join(ROOT, "build", "st", "pagebench.elf"))
    sys.exit(0 if "done (0)" in text else 1)


if __name__ == "__main__":
    main()
