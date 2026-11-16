#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the narrow letters' choice (EWEB.INF's NARROW=,
Settings > General; Erez, 16 Nov 2026): tests/layout/narrow.html
through ewrender, its display list's faces.

- by default (NARROW=1, 13 Nov 2026's way) a page made for Arial, and
  the generic sans-serif, in DejaVu Sans Condensed ("cond");
- with --wide-sans (NARROW=0) both in DejaVu Sans ("sans"), wider;
- Verdana's kind and DejaVu Sans named, and a serif page, the same
  either way.

    tests/layout/narrow.py EWRENDER EWEB.FNT"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "narrow.html")
fails = 0


def check(what, ok, detail=""):
    global fails
    print("%s  narrow: %s%s" % ("ok  " if ok else "FAIL", what, "" if ok else " (%s)" % detail))
    if not ok:
        fails += 1


def runs(ewrender, fnt, *extra):
    """each text run's face and width, by its words"""
    out = subprocess.run([ewrender, "-f", fnt, "-d", *extra, PAGE], capture_output=True, text=True).stdout
    got = {}
    for w, face, words in re.findall(r'^text \S+ (\d+)x\d+ \S+ ([a-z]+)\d+ .*"(.*)"$', out, re.M):
        got[words] = (face, int(w))
    return got


def main():
    ewrender, fnt = sys.argv[1:3]
    narrow, wide = runs(ewrender, fnt), runs(ewrender, fnt, "--wide-sans")
    for words in ("Made for Arial", "The generic sans-serif"):
        check("%s: narrow by default" % words, narrow.get(words, ("",))[0] == "cond", narrow.get(words))
        check("%s: DejaVu Sans with NARROW=0" % words, wide.get(words, ("",))[0] == "sans", wide.get(words))
        check("%s: wider with NARROW=0" % words,
              words in narrow and words in wide and wide[words][1] > narrow[words][1],
              "%s %s" % (narrow.get(words), wide.get(words)))
    for words in ("Made for Verdana", "DejaVu Sans named", "A serif page"):
        check("%s: the same either way" % words, words in narrow and narrow.get(words) == wide.get(words),
              "%s %s" % (narrow.get(words), wide.get(words)))
    print("narrow: %d failed" % fails)
    sys.exit(1 if fails else 0)


main()
