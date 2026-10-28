#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWeb started by another program, as EMail starts it from a link (Erez,
28 Oct 2026: EWeb didn't find its fonts): it must take its files from its
own folder (common's app_dir), not from the current one. AERA-Atari's
common/tests/appdir setup: C:\\LAUNCH.PRG makes A:\\APP current and starts
A:\\APP\\PROBE.PRG with shel_write, and TOS 1.04 and 4.04 start it in the
desktop's folder whatever the caller set. A: is a real FAT floppy
(mtools), since Hatari's emulated C: keeps one current folder for all
programs. EWeb is the probe (PROBE.PRG, the launcher's name) with
EWEB.FNT and EWEB.INF (HOME=about:bookmarks) beside it; it must start,
draw its start page (EWEB.FNT found) and show about:bookmarks (EWEB.INF
read). Where it looks elsewhere it alerts "fonts are missing" or shows its
own home page (EWeb 0.48 on the ST's TOS 1.04). Two cases on each machine:

  floppy  EWeb in A:\\APP, with a small EWEB.FNT (DejaVu Sans at 13 pixels,
          as a floppy holds): EWEBST.PRG on an 800 KB floppy (10 sectors a
          track), on a Falcon EWEB.PRG on a 1.44 MB one.
  C:      EWeb in C:\\APP with the whole EWEB.FNT (the launcher made to
          start C:\\APP\\PROBE.PRG), A:\\APP still the current folder: EWeb
          started on C:, whose folder (the root) isn't EWeb's.

Not on a Falcon with Erez's TOS 4.04: there the program started after
Hatari's autostart one runs in ST low (before the desktop has set its
mode from NEWDESK.INF), where EWeb can't run, and Hatari's floppies give
it only small files. common/tests/appdir shows TOS 4.04 too starting it
in the desktop's folder, and app_dir() right there.

    make && python3 tests/hatari/appdir.py WORK [MACHINE...] [--prg DIR]
    (ST and Falcon; --prg: EWEB.PRG and EWEBST.PRG from DIR, as a release)

Erez's TOS when ~/.aera/tos has it, else EmuTOS (machines.py);
AERA_TOS=emutos for EmuTOS.
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

argv = sys.argv[1:]
prgdir = os.path.join(ROOT, "binary")
if "--prg" in argv:
    i = argv.index("--prg")
    prgdir = os.path.abspath(argv[i + 1])
    del argv[i:i + 2]
work = os.path.abspath(argv[0])
specs = argv[1:] or ["ST", "Falcon"]
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def mtools(*args):
    subprocess.run(list(args), check=True, capture_output=True)


def run(w, spec, prg, fnt, on_c):
    """EWeb (prg) as A:\\APP\\PROBE.PRG or C:\\APP\\PROBE.PRG; its "EWEB shown" line"""
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(os.path.join(hd, "APP"))
    m = machines.Machine(spec, hd)
    code = open(os.path.join(launch, "LAUNCH.PRG"), "rb").read()
    if on_c:
        code = code.replace(b"A:\\APP\\PROBE.PRG", b"C:\\APP\\PROBE.PRG")
    open(os.path.join(hd, "LAUNCH.PRG"), "wb").write(code)
    files = [(prg, "PROBE.PRG"), (fnt, "EWEB.FNT"), (inf, "EWEB.INF")]
    fifo = os.path.join(w, "hatari.fifo")
    args = ["hatari", "--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn", "--cmd-fifo", fifo] + m.args()
    big = not on_c and m.name.startswith("Falcon")
    img = os.path.join(w, "a.st")
    mtools("mformat", "-i", img, "-t", "80", "-h", "2", "-s", "18" if big else "10", "-C", "::")
    mtools("mmd", "-i", img, "::/APP")
    for src, name in files:
        if on_c:
            shutil.copy(src, os.path.join(hd, "APP", name))
        else:
            mtools("mcopy", "-i", img, src, "::/APP/" + name)
    args += ["--disk-a", img, "--auto", "C:\\LAUNCH.PRG"]
    log = os.path.join(w, "hatari.log")
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    shown = ""
    for _ in range(600):
        time.sleep(0.5)
        shown = next((l for l in open(log, errors="replace") if l.startswith("EWEB shown ")), "")
        if shown:
            break
    if not shown and os.path.exists(fifo):
        with open(fifo, "w") as f:             # what the screen shows, for the report
            f.write("hatari-debug screenshot %s\n" % os.path.join(w, "screen.png"))
        time.sleep(2)
    p.kill()
    p.wait()
    print("%s on %s: %s" % (os.path.basename(prg), m.describe(),
                            shown[:60].strip() or "(no page shown: %s)" % os.path.join(w, "screen.png")), flush=True)
    return shown


launch = os.path.join(work, "launch")
subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "common", "tests", "appdir"), "B=" + launch],
               check=True)
small = os.path.join(work, "EWEB.FNT")
if not os.path.exists(small):
    subprocess.run([sys.executable, "-c", "import sys; sys.path.insert(0, %r); import mkfont; "
                    "mkfont.FAMILIES = mkfont.FAMILIES[:1]; mkfont.SIZES = [13]; "
                    "sys.argv = ['mkfont', %r]; mkfont.main()"
                    % (os.path.join(ROOT, "tools"), small)], check=True)
inf = os.path.join(work, "EWEB.INF")
with open(inf, "w", newline="") as f:
    f.write("HOME=about:bookmarks\r\n")

for spec in specs:
    w = os.path.join(work, spec.replace(":", "-"))
    falcon = spec.startswith("Falcon")
    prg = os.path.join(prgdir, "EWEB.PRG" if falcon else "EWEBST.PRG")
    if falcon and machines.Machine(spec, w).real:
        print("%s: skipped (Erez's TOS 4.04: see above)" % spec, flush=True)
        continue
    shown = run(os.path.join(w, "floppy"), spec, prg, small, False)
    check(shown.startswith("EWEB shown about:bookmarks "),
          "started by another program from A:\\APP: EWEB.FNT and EWEB.INF found there")
    shown = run(os.path.join(w, "c"), spec, prg, os.path.join(ROOT, "binary", "EWEB.FNT"), True)
    check(shown.startswith("EWEB shown about:bookmarks "),
          "started by another program from C:\\APP, A:\\APP current: EWEB.FNT and EWEB.INF found there")
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
