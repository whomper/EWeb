#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The round trip (the guidelines' 4.6, EWeb's half): a program that
starts EWeb for a link with --back=PATH after the address is started
again with " --resume" when the user quits EWeb, on a single-tasking AES.
tests/hatari/backtrip/caller.c stands in for the caller (EMail's own isn't
run beside EWeb: the guidelines' 1.2): started by Hatari, it starts
C:\\EWEB.PRG with "about:bookmarks --back=C:\\CALLER.PRG". Cases on each
machine:

  back    EWeb shows about:bookmarks (the address read without --back),
          ^Q, and the caller says it was started with --resume;
  gone    the same with --back naming a program that isn't there: EWeb
          quits, nothing is started, no alert;
  other   an argument EWeb doesn't know after the address (--zzzz=...):
          the address shown alone (EMail 2.31's --back reached EWeb 1.26 as
          part of the address), nothing started at the quit.

    make && python3 tests/hatari/backtrip.py WORK [MACHINE...]
    (Falcon-4, EWEB.PRG on TOS 4.04, and ST, EWEBST.PRG on TOS 1.04, by default)
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

CTRL, Q = 0x1D, 0x10
work = os.path.abspath(sys.argv[1])
specs = sys.argv[2:] or ["Falcon-4", "ST"]
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def lines(log, tag):
    return [l.strip() for l in open(log, errors="replace") if l.startswith(tag)]


def run(w, spec, gone, other=False):
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    m = machines.Machine(spec, hd)
    falcon = spec.startswith("Falcon")
    code = open(os.path.join(work, "caller", "CALLER.PRG"), "rb").read()
    if gone:
        code = code.replace(b"--back=C:\\CALLER.PRG", b"--back=C:\\NOTHER.PRG")
    if other:
        code = code.replace(b"--back=C:\\CALLER.PRG", b"--zzzz=C:\\CALLER.PRG")
    open(os.path.join(hd, "CALLER.PRG"), "wb").write(code)
    shutil.copy(os.path.join(ROOT, "binary", "EWEB.PRG" if falcon else "EWEBST.PRG"), os.path.join(hd, "EWEB.PRG"))
    shutil.copy(os.path.join(ROOT, "binary", "EWEB.FNT"), os.path.join(hd, "eweb.fnt"))
    with open(os.path.join(hd, "eweb.inf"), "w", newline="") as f:
        f.write("NATFEATS=1\r\n")
    fifo = os.path.join(w, "hatari.fifo")
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + m.args() + ["--natfeats", "yes", "--log-level", "fatal", "--cmd-fifo", fifo,
                                    "--fast-forward", "yes", "--auto", "C:\\CALLER.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    shown = None
    for _ in range(600):
        time.sleep(0.5)
        shown = next((l for l in lines(log, "EWEB shown ")), None)
        if shown or p.poll() is not None:
            break
    resumed = alerted = False
    if shown:
        time.sleep(3)
        with open(fifo, "w") as ctl:
            for c in ("hatari-event keydown 0x%x" % CTRL, "hatari-event keypress 0x%x" % Q,
                      "hatari-event keyup 0x%x" % CTRL):
                ctl.write(c + "\n")
                ctl.flush()
                time.sleep(0.25)
        for _ in range(120):
            time.sleep(0.5)
            if lines(log, "CALLER resumed") or lines(log, "EWEB quit"):
                time.sleep(3)
                break
        resumed = bool(lines(log, "CALLER resumed"))
        alerted = any("alert" in l for l in lines(log, "EWEB "))
    p.kill()
    p.wait()
    print("%s %s on %s: shown %s, resumed %s" % ("other" if other else "gone" if gone else "back", "EWEB.PRG" if falcon else "EWEBST.PRG",
                                                m.describe(), (shown or "-")[:40], resumed), flush=True)
    return shown, resumed, alerted


os.makedirs(work, exist_ok=True)
subprocess.run(["make", "-s", "-C", os.path.join(HERE, "backtrip"), "B=" + os.path.join(work, "caller")],
               check=True, capture_output=True)
for spec in specs:
    w = os.path.join(work, spec.replace(":", "-"))
    shown, resumed, _ = run(os.path.join(w, "back"), spec, False)
    check(shown is not None and shown.startswith("EWEB shown about:bookmarks"),
          "%s: started with an address and --back, the address shown" % spec)
    check(resumed, "%s: ^Q, the caller started again with --resume" % spec)
    shown, resumed, alerted = run(os.path.join(w, "gone"), spec, True)
    check(shown is not None and not resumed and not alerted,
          "%s: --back naming a program that isn't there: quits, nothing started, no alert" % spec)
    shown, resumed, alerted = run(os.path.join(w, "other"), spec, False, True)
    check(shown is not None and shown.startswith("EWEB shown about:bookmarks") and not resumed,
          "%s: an argument EWeb doesn't know after the address: the address alone shown" % spec)
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
