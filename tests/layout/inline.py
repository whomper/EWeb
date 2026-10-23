#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of inline blocks beyond their list (inline.txt), on
tests/layout/inline_cases.html through ewrender:

- a click on a label's text ticks its checkbox, for a label that is an
  inline-block, a block or an inline (-K);
- a hidden inline-block draws nothing (no fill), its line kept;
- the text in an inline-block inside a translucent span is as faint;
- a box positioned absolutely in an inline-block that isn't positioned
  is where its containing block puts it (here the page's top left);
- baselines: an inline table's is its first row's, an inline flex's its
  first line's after align-items, an inline-block's its last line's
  whatever floats come after it or a flex inside it does;
- an address's #name in an inline-block is where the block went;
- a box positioned in a static inline-block with text after it leaves
  that text where it was, drawn once; a fixed box in a positioned box in
  an inline-block is drawn; a label's box positioned in it is the
  label's (the review's second round).

    tests/layout/inline.py EWRENDER EWEB.FNT"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "inline_cases.html")


def fail(msg):
    print("inline: " + msg)
    sys.exit(1)


def run(ewrender, font, *args):
    p = subprocess.run([ewrender, "-f", font, "-d"] + list(args), capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        fail("ewrender %s failed:\n%s" % (" ".join(args), p.stderr[-800:]))
    return p.stdout


def texts(out):
    """each text item's (x, y, colour) by its words"""
    t = {}
    for line in out.splitlines():
        m = re.match(r'text (-?\d+),(-?\d+) \d+x\d+ (#[0-9a-f]+) \S+ link \d+ "(.*)"', line)
        if m:
            t[m.group(4).strip()] = (int(m.group(1)), int(m.group(2)), m.group(3))
    return t


def main():
    ewrender, font = sys.argv[1], sys.argv[2]
    for i, (id_, name) in enumerate((("ib", "an inline-block"), ("bl", "a block"), ("il", "an inline"),
                                     ("lpos", "an inline-block's positioned box in its"))):
        out = run(ewrender, font, "-K", id_, PAGE)
        ticks = [l for l in out.splitlines() if l.startswith("control ") and " checkbox " in l]
        if len(ticks) != 4:
            fail("four checkboxes expected, %d drawn" % len(ticks))
        for k, l in enumerate(ticks):
            if ('"✓"' in l) != (k == i):
                fail("a click on the text of %s label didn't tick its own box only:\n%s" % (name, "\n".join(ticks)))
    out = run(ewrender, font, PAGE)
    t = texts(out)
    if any(l.startswith("fill") and ("#ff0000" in l or "#0000ff" in l) for l in out.splitlines()):
        fail("a hidden inline-block was drawn")
    if t["Hidden"][1] != t["box"][1]:
        fail("the line round a hidden inline-block broke")
    if not t["INFADED"][2].endswith("4c"):
        fail("an inline-block in a span of opacity .3 isn't faint: " + t["INFADED"][2])
    if t["PINNED"][0] != 0 or t["PINNED"][1] > 20:
        fail("a box positioned in an inline-block was moved with it: %r" % (t["PINNED"],))
    lines = out.splitlines()
    for word in ("AFTER", "TAILWORD", "FIXEDBOX"):
        n = sum(1 for l in lines if l.startswith("text ") and word in l)
        if n != 1:
            fail("%s drawn %d times" % (word, n))
    if t["AFTER"][1] != t["DROPDOWN"][1]:
        fail("the text after an inline-block with a positioned box in it moved: %r %r" % (t["DROPDOWN"], t["AFTER"]))
    for a, b, inside, what in (("DEEP1", "DEEP2", "DEEPIN", "an inline-block's in a centred flex in one"),
                               ("TBL1", "TBL2", "ROW1", "an inline table's first row"),
                               ("FLX1", "FLX2", "FLXIN", "an inline flex's centred line"),
                               ("FLT1", "FLT2", "FLTIN", "an inline-block's last line before a float"),
                               ("NST1", "NST2", "NSTIN", "a flex's centred line in an inline-block")):
        if not t[a][1] == t[b][1] == t[inside][1]:
            fail("the line isn't on %s: %s %r, %s %r" % (what, a, t[a], inside, t[inside]))
    top = [l for l in run(ewrender, font, PAGE + "#inner").splitlines() if l.startswith("top:")]
    if not top or abs(int(top[0].split()[1]) - (t["INNER"][1] - 13)) > 4:
        fail("#inner isn't where its inline-block went: %s, its text at %d" % (top, t["INNER"][1]))
    print("inline: labels, hidden, opacity, positioned boxes, baselines and an address in inline blocks as they should be")


main()
