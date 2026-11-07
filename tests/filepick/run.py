#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG (make) on one of the family's machines: the family's file
dialog opened on a folder of music, a file, folders, a playlist and a text
file, with EPlay's types (Music files first). The keys select (Down,
Shift+Down), the mask narrows (*.W*) and is cleared, Control+A takes
every file, Return adds them. Checks: the dialog came, each step changed
the screen, the files taken were the music files in name order and not the
text file, the dialog called idle(), and no CRASH.LOG. A screenshot each
step in WORK/shots.

    run.py WORK MACHINE [--nvdi] [--old]

--old also runs FPOLD.PRG (make old EPLAY=...: EPlay's own Add files) the
same way and compares every screenshot, pixel for pixel: the move into
common must change none (the hub, 5 Nov 2026). On rows of 8 pixels (ST
low and medium) the folders' shapes are the one change since: they no
longer reach into the next row. There a difference within one column of
folder shapes (16 pixels wide at most) is a NOTE; anything more fails.
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

# C:\MUSIC: (name, bytes); folders end in '\'. Fixed dates, so a shot is
# the same each run (3 Nov 2026, 14:00)
FILES = [("01_NIGHT.MP3", 6123456), ("02_RAIN.WAV", 52000000), ("03_ECHO.AIF", 9000), ("LIVE.M3U", 812),
         ("README.TXT", 1200), ("04_LAST.MP3", 700000), ("LIVE\\", 0), ("BONUS\\", 0), ("LIVE\\X.MP3", 1000)]
WHEN = time.mktime((2026, 11, 3, 14, 0, 0, 0, 0, -1))
WANT = ["C:\\MUSIC\\01_NIGHT.MP3", "C:\\MUSIC\\02_RAIN.WAV", "C:\\MUSIC\\03_ECHO.AIF", "C:\\MUSIC\\04_LAST.MP3",
        "C:\\MUSIC\\LIVE.M3U"]
STEPS = [("1-open", []), ("2-select", ["Down", "shift+Down"]), ("3-mask", ["type:*.W*"]),
         ("4-cleared", ["BackSpace", "BackSpace", "BackSpace", "BackSpace"]), ("5-all", ["ctrl+a"])]


def session(work, machine, nvdi, prg, display):
    r = speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails, shots = [], {}
    try:
        music = os.path.join(r.hd, "MUSIC")
        for name, size in FILES:
            p = os.path.join(music, *name.rstrip("\\").split("\\"))
            if name.endswith("\\"):
                os.makedirs(p, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.truncate(size)
        for d, ds, fs in os.walk(music):
            for n in ds + fs:
                os.utime(os.path.join(d, n), (WHEN, WHEN))
        # under one name, FPTEST.PRG: the desktop shows it at the top
        shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        if "picture add" not in r.console():
            fails.append("the dialog never came")
            return fails, shots
        prev = None
        for name, keys in STEPS:
            for k in keys:
                if k.startswith("type:"):
                    r.type(k[5:])
                else:
                    r.key(k)
            r.still(most=20)
            shots[name] = r.shot(name)
            if prev and same(prev, shots[name]):
                fails.append("%s: the screen didn't change" % name)
            prev = shots[name]
        r.key("Return")
        t_end = time.time() + 60
        while time.time() < t_end and "FPT dir" not in r.console():
            time.sleep(1)
        con = r.console()
        took = [l[9:] for l in con.splitlines() if l.startswith("FPT took ")]
        if took != WANT:
            fails.append("took %s, wanted %s" % (took, WANT))
        if "idle called" not in con:
            fails.append("idle() never called")
        if "FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\" not in con:
            fails.append("the folder and the recent ones: %s" %
                         [l for l in con.splitlines() if l.startswith("FPT dir")])
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("CRASH.LOG in %s" % d)
        print("  %s on %s: %s" % (os.path.basename(prg), r.describe(),
                                  "; ".join(l for l in con.splitlines() if " picture " in l)))
    finally:
        r.stop()
    return fails, shots


def diff_box(a, b):
    """the box the two pictures differ in, (w, h, x, y), or None"""
    p = subprocess.run(["convert", a, b, "-compose", "difference", "-composite", "-trim", "info:"],
                       capture_output=True, text=True)
    m = re.search(r" (\d+)x(\d+) \d+x\d+\+(\d+)\+(\d+) ", p.stdout)
    return tuple(int(g) for g in m.groups()) if m else None


def same(a, b):
    p = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(p.stderr.split()[0])) == 0
    except (ValueError, IndexError):
        return False


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    work, machine, nvdi = os.path.abspath(a[0]), a[1], "--nvdi" in sys.argv
    print("the file dialog on %s%s" % (machine, " with NVDI" if nvdi else ""))
    fails, shots = session(os.path.join(work, "new"), machine, nvdi,
                           os.path.join(HERE, "build", "atari", "FPTEST.PRG"), ":92")
    if "--old" in sys.argv:
        f2, old = session(os.path.join(work, "old"), machine, nvdi,
                          os.path.join(HERE, "build", "old", "FPOLD.PRG"), ":92")
        fails += ["EPlay's: " + f for f in f2 if not f.startswith("took")]
        small = machine.split(":")[-1] in ("med", "low")
        for name in shots:
            if name not in old:
                continue
            if same(shots[name], old[name]):
                print("ok    %s the same as EPlay's own, pixel for pixel" % name)
                continue
            box = diff_box(shots[name], old[name])
            if small and box and box[0] <= 16:
                print("NOTE  %s: the folders' shapes only (%dx%d at %d,%d), as on rows of 8 they should" %
                      ((name,) + box))
            else:
                fails.append("%s differs from EPlay's own (%s, %s; %s)" % (name, shots[name], old[name], box))
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
