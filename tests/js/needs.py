#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The note "This page needs its scripts" (src/browse's wants_scripts; Erez,
10 Nov 2026): on a page made without its scripts (ewrender -Jn: they could
run, the site isn't chosen) that looks empty or asks for JavaScript, and on
no other; never with the scripts run (-J) or where they can't run at all.
   tests/js/needs.py EWRENDER EWEB.FNT"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NOTE = "note: This page needs its scripts: click here to run this site's scripts."


def note(ew, fnt, page, *args):
    out = subprocess.run([ew, *args, "-f", fnt, "-d", os.path.join(HERE, "needs", page)],
                         capture_output=True, text=True).stdout
    return NOTE in out


def main():
    ew, fnt = sys.argv[1], sys.argv[2]
    cases = [("empty.html", ["-Jn"], True), ("asks.html", ["-Jn"], True), ("fine.html", ["-Jn"], False),
             ("empty.html", ["-J"], False), ("empty.html", [], False)]
    bad = 0
    for page, args, want in cases:
        got = note(ew, fnt, page, *args)
        if got != want:
            print("needs: %s %s: note %s, wanted %s" % (page, " ".join(args) or "(no FPU)", got, want))
            bad += 1
    print("needs: %d cases, %d failed" % (len(cases), bad))
    sys.exit(1 if bad else 0)


main()
