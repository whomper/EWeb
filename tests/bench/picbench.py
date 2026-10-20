#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PICBENCH.TTP (tests/bench/picbench.c, `make binary/PICBENCH.TTP`) on one of
the family's machines in Hatari (common/tools/machines.py: Erez's TOS when
~/.aera/tos has it, else EmuTOS), cycle-exact: a picture decoded as
EWEBST.PRG decodes it, and dithered, each step timed.
    tests/bench/picbench.py [--machine ST:mono] PICTURE WIDTH HEIGHT [COLOURS [TIMES]]
It prints what picbench printed (its times are the emulated machine's).
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
    spec = "ST:mono"
    if args[:1] == ["--machine"]:
        spec = args[1]
        args = args[2:]
    if len(args) < 3:
        sys.exit(__doc__)
    pic = args[0]
    name = "PIC" + os.path.splitext(pic)[1].upper()[:4]
    work = os.path.join(ROOT, "build", "picbench")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copy(os.path.join(ROOT, "binary", "PICBENCH.TTP"), work)
    shutil.copy(pic, os.path.join(work, name))
    with open(os.path.join(work, "PICBENCH.CMD"), "w") as f:
        f.write(" ".join([name] + args[1:]))
    log = os.path.join(work, "hatari.log")
    m = machines.Machine(spec, work)
    cmd = ["hatari"] + m.args() + ["--auto", "C:\\PICBENCH.TTP", "--fast-boot", "yes", "--natfeats", "yes",
                                   "--cpu-exact", "yes", "--fast-forward", "yes", "--statusbar", "no",
                                   "--log-level", "fatal"]
    print(m.describe())
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
