#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The Pi's compiled scripts on the 68030 (make bc68k): aerajsc, built
for this machine (as for the Pi), compiles tests/js/kept/lib.js; the
68030's jsbench (QEMU) compiles the same and keeps it (-k), and it must
be the very same bytes (bytecode and regular expressions big-endian on
both: third_party/quickjs/patches/...-0002); then the 68030 runs the
Pi's code and says what the host's run says.

And with a second AERAJSC (tools/jsc's own build, as AERA-Gateway's jsc/
builds it on the Pi): its tag and its bytecode are the same as EWeb's.

    tests/js/bc68k.py AERAJSC JSBENCH68K EWRENDER EWEB.FNT [AERAJSC2]"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
QEMU = os.environ.get("QEMU_M68K", "qemu-m68k")


def main():
    jsc, bench, ewr, fnt = sys.argv[1:5]
    work = tempfile.mkdtemp(prefix="bc68k")
    bad = 0
    try:
        src = open(os.path.join(HERE, "kept", "lib.js"), "rb").read()
        r = subprocess.run([jsc], input=b"EWJQ\1" + len(src).to_bytes(4, "big") + src, capture_output=True)
        pi = r.stdout.partition(b"\n")[2]
        if len(sys.argv) > 5:
            r2 = subprocess.run([sys.argv[5]], input=b"EWJQ\1" + len(src).to_bytes(4, "big") + src,
                                capture_output=True)
            if r2.stdout != r.stdout or not pi.startswith(b"EWQJS "):
                bad += 1
                print("bc68k: FAIL tools/jsc's aerajsc isn't EWeb's: %r and %r" % (r2.stdout[:60], r.stdout[:60]))
        # the 68030's own, kept by jsbench -k
        keep = os.path.join(work, "keep")
        os.mkdir(keep)
        page = os.path.join(work, "own.html")
        with open(page, "w") as f:
            f.write('<!DOCTYPE html><title>own</title><script src="%s"></script>' % os.path.join(HERE, "kept", "lib.js"))
        own = subprocess.run([QEMU, "-cpu", "m68030", bench, "-k", keep, page], capture_output=True, text=True)
        kept = [open(os.path.join(keep, n), "rb").read() for n in os.listdir(keep)]
        if len(kept) != 1 or kept[0] != pi:
            bad += 1
            print("bc68k: FAIL the Pi's code isn't the 68030's (%d kept, %d and %d bytes)" %
                  (len(kept), len(pi), len(kept[0]) if kept else 0))
        # the Pi's code run on the 68030
        with open(os.path.join(work, "lib.qjs"), "wb") as f:
            f.write(pi)
        page = os.path.join(work, "pi.html")
        with open(page, "w") as f:
            f.write('<!DOCTYPE html><title>pi</title><script src="%s"></script>' % os.path.join(work, "lib.qjs"))
        ran = subprocess.run([QEMU, "-cpu", "m68030", bench, page], capture_output=True, text=True)
        host = subprocess.run([ewr, "-J", "-d", "-f", fnt, os.path.join(HERE, "kept.html")], capture_output=True,
                              text=True).stdout
        want = re.findall(r"^console: (kept .*)$", host, re.M)
        for what, out in (("its own code", own.stdout), ("the Pi's code", ran.stdout)):
            got = re.findall(r"^(kept .*)$", out, re.M)
            if not want or got != want:
                bad += 1
                print("bc68k: FAIL the 68030 running %s says otherwise\n  68030: %s\n  host:  %s" %
                      (what, got, want))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("bc68k: %d failed" % bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
