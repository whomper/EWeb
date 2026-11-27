#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEBTW.OVL, Tailwind's utilities in a file of their own (atari/tw_ovl.h;
decided 27 Nov 2026): tests/layout/tailwind.html (a page whose head loads
Tailwind's compiler) as the start page. Cases:

  overlay   EWEB.PRG with ewebtw.ovl beside it: the overlay loaded (its
            "EWEB tw: EWEBTW.OVL loaded" line) and the page styled: its
            height as ewrender's styled page's, not the unstyled one's;
  missing   no ewebtw.ovl: the log says it is missing, the page shows
            unstyled, nothing else goes wrong.

On Falcon-4 (EWEB.PRG, the 68030) and a Falcon with a 68060 ("060": the
scripts rig's, Falcon:16 with Hatari's 68060 and its FPU; EWEB60.PRG with
the same overlay, 68020 to 68060 code).

    make && python3 tests/hatari/twovl.py WORK [MACHINE...]
"""
import os, shutil, subprocess, sys, time, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

work = os.path.abspath(sys.argv[1])
specs = sys.argv[2:] or ["Falcon-4", "060"]
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def run(w, spec, overlay):
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    m = machines.Machine("Falcon:16" if spec == "060" else spec, hd)
    prg = "EWEB60.PRG" if spec == "060" else "EWEB.PRG"
    shutil.copy(os.path.join(ROOT, "binary", prg), os.path.join(hd, "EWEB.PRG"))
    shutil.copy(os.path.join(ROOT, "binary", "EWEB.FNT"), os.path.join(hd, "eweb.fnt"))
    shutil.copy(os.path.join(ROOT, "tests", "layout", "tailwind.html"), os.path.join(hd, "tw.htm"))
    if overlay:
        shutil.copy(os.path.join(ROOT, "binary", "EWEBTW.OVL"), os.path.join(hd, "ewebtw.ovl"))
    with open(os.path.join(hd, "eweb.inf"), "w", newline="") as f:
        f.write("HOME=file:///C:/tw.htm\r\nNATFEATS=1\r\n")
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + m.args() + (["--cpulevel", "6", "--fpu", "internal"] if spec == "060" else []) + ["--natfeats", "yes", "--log-level", "fatal", "--fast-forward", "yes",
                                    "--auto", "C:\\EWEB.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    shown = None
    for _ in range(600):
        time.sleep(0.5)
        lines = [l.strip() for l in open(log, errors="replace") if l.startswith("EWEB ")]
        shown = next((l for l in lines if l.startswith("EWEB shown file:///C:/") and "tw.htm" in l.lower()), None)
        if shown or p.poll() is not None:
            break
    time.sleep(1)
    lines = [l.strip() for l in open(log, errors="replace") if l.startswith("EWEB ")]
    p.kill()
    p.wait()
    tw = [l for l in lines if l.startswith("EWEB tw: ")]
    h = re.search(r" height (\d+)", shown or "")
    print("%s %s on %s: %s; %s" % ("overlay" if overlay else "missing", prg, m.describe(),
                                   (shown or "(not shown)")[:70], tw[0] if tw else "(no tw line)"), flush=True)
    return (int(h.group(1)) if h else None), tw


os.makedirs(work, exist_ok=True)
for spec in specs:
    w = os.path.join(work, spec.replace(":", "-"))
    h1, tw1 = run(os.path.join(w, "overlay"), spec, True)
    h2, tw2 = run(os.path.join(w, "missing"), spec, False)
    check(any("EWEBTW.OVL loaded at" in l for l in tw1), "%s: EWEBTW.OVL loaded for the Tailwind page" % spec)
    check(h1 is not None and h2 is not None and h1 != h2,
          "%s: the page styled with it (height %s), unstyled without it (%s)" % (spec, h1, h2))
    check(any("is missing" in l for l in tw2), "%s: without it, the log says it is missing" % spec)
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
