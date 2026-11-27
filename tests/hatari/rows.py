#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The gate table's rows that no other rig runs as a row of its own
(AERA-Atari's standards/TEST-MATRIX.md; the hub, 27 Nov 2026): EWeb
started on the machine as a user starts it, a page shown, shown again
(^R), and ^Q, with nothing else loaded beside it (the
guidelines' 1.2). EWEB.PRG on a Falcon or TT, EWEBST.PRG on an ST or STE.

  ROW[:MODE]       e.g. TT:ttlow, STE:low, ST-1:mono
  nonf:ROW[:MODE]  the same with NatFeats off (Hatari's and EWEB.INF's):
                   no event to read, so the screen is captured once the
                   page should be up, and Hatari must still be running

    make && python3 tests/hatari/rows.py WORK ROW...
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

CTRL, Q = 0x1D, 0x10
work = os.path.abspath(sys.argv[1])
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def events(log):
    return [l.strip() for l in open(log, errors="replace") if l.startswith("EWEB ")]


def wait(log, p, pred, secs):
    for _ in range(int(secs * 2)):
        ev = next((e for e in events(log) if pred(e)), None)
        if ev or p.poll() is not None:
            return ev
        time.sleep(0.5)
    return None


def row(arg):
    nonf = arg.startswith("nonf:")
    spec = arg[5:] if nonf else arg
    w = os.path.join(work, arg.replace(":", "-"))
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    m = machines.Machine(spec, hd)
    st = spec.startswith("ST")
    prg = "EWEBST.PRG" if st else "EWEB.PRG"
    shutil.copy(os.path.join(ROOT, "binary", prg), os.path.join(hd, prg))
    for f in ("EWEB.FNT", "EWEBJS.OVL", "EWEBTW.OVL", "TLS.OVL"):
        if not st or f == "EWEB.FNT":
            shutil.copy(os.path.join(ROOT, "binary", f), os.path.join(hd, f.lower()))
    shutil.copy(os.path.join(ROOT, "tests", "bench", "sample.html"), os.path.join(hd, "sample.htm"))
    with open(os.path.join(hd, "eweb.inf"), "w", newline="") as f:
        f.write("HOME=file:///C:/sample.htm\r\nNATFEATS=%d\r\n" % (0 if nonf else 1))
    fifo = os.path.join(w, "hatari.fifo")
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + m.args() + ["--natfeats", "no" if nonf else "yes", "--log-level", "fatal",
                                    "--fast-forward", "yes", "--cmd-fifo", fifo, "--auto", "C:\\" + prg]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(fifo):
            break
        time.sleep(0.1)
    ctl = open(fifo, "w")

    def send(c):
        ctl.write(c + "\n")
        ctl.flush()
        time.sleep(0.25)

    def key(scan, ctrl=False):
        if ctrl:
            send("hatari-event keydown 0x%x" % CTRL)
        send("hatari-event keypress 0x%x" % scan)
        if ctrl:
            send("hatari-event keyup 0x%x" % CTRL)

    desc = m.describe() + (", NatFeats off" if nonf else "")
    if nonf:
        time.sleep(90)
        send("hatari-debug screenshot %s" % os.path.join(w, "screen.png"))
        time.sleep(3)
        alive = p.poll() is None
        check(alive and os.path.exists(os.path.join(w, "screen.png")),
              "%s: %s ran (no event read; screen: %s)" % (desc, prg, os.path.join(w, "screen.png")))
        crash = os.path.exists(os.path.join(hd, "crash.log")) or os.path.exists(os.path.join(hd, "CRASH.LOG"))
        check(not crash, "%s: no crash.log" % desc)
        p.kill()
        p.wait()
        return
    shown = wait(log, p, lambda e: e.startswith("EWEB shown file:///C:/sample.htm"), 300)
    check(shown is not None, "%s: %s started and showed the start page" % (desc, prg))
    if shown:
        time.sleep(2)
        key(0x13, True)	# ^R: the page made again, from the disk
        again = wait(log, p, lambda e: e.startswith("EWEB shown") and e != shown, 120)
        check(again is not None, "%s: the page shown again (^R)" % desc)
        key(Q, True)
        time.sleep(5)
        evs = events(log)
        check(any("quit" in e for e in evs) or p.poll() is not None, "%s: ^Q quits" % desc)
    crash = os.path.exists(os.path.join(hd, "crash.log")) or os.path.exists(os.path.join(hd, "CRASH.LOG"))
    check(not crash, "%s: no crash.log" % desc)
    p.kill()
    p.wait()


os.makedirs(work, exist_ok=True)
for a in sys.argv[2:]:
    row(a)
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
