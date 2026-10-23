#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of boxes that scroll (overflow auto and scroll, CSS
Overflow 3), beyond their list (scroll.txt), on tests/layout/scroll.html
through ewrender:

- each box with more than it shows has its bars, and one whose content
  fits has none;
- the link at the foot of a box isn't followed when clicked (-K) until
  the box is scrolled to it (-O), then it is;
- a box scrolled stays so when the page is laid out again (-L);
- a box in a box moves with the outer one's scrolling;
- every box scrolled to its end, the page drawn in EWEB.PRG's edition and
  the ST's (under the sanitizers in make test's build).

    tests/layout/scroll.py EWRENDER FNT [ST_EWRENDER]"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "scroll.html")


def fail(msg):
    print("scroll: " + msg)
    sys.exit(1)


def run(ed, font, *args):
    p = subprocess.run([ed, "-f", font] + list(args) + [PAGE], capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        fail("%s failed: %s" % (os.path.basename(ed), p.stderr[-500:]))
    return p.stdout


def bars(d):
    return [(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6)))
            for m in re.finditer(r"^bar (x|y) \d+,(\d+) \S+ box (\d+) reach (\d+),(\d+) at \d+,(\d+)", d, re.M)]


def main():
    ewrender, font = sys.argv[1], sys.argv[2]
    d = run(ewrender, font, "-d")
    b = bars(d)
    # the code block across, the tall box down, both ways, the box hiding
    # overflow-x down, the inner box and the outer: 7 bars in 6 boxes
    if len(b) != 7 or len({x[2] for x in b}) != 6:
        fail("the boxes' bars aren't as they should be:\n" + "\n".join(map(str, b)))
    if re.search(r'"just one line"', d) is None or any(x[2] > 6 for x in b):
        fail("a box whose content fits has a bar")
    k = run(ewrender, font, "-d", "-K", "deep")
    if "followed:" in k:
        fail("a link the box doesn't show yet was followed when clicked")
    reach = [x for x in b if x[2] == 2][0][4]
    k = run(ewrender, font, "-d", "-O", "2:0:%d" % reach, "-K", "deep")
    if "followed:" not in k:
        fail("the link a box was scrolled to wasn't followed when clicked:\n" + k[-800:])
    k = run(ewrender, font, "-d", "-O", "2:0:30", "-L", "500")
    if not re.search(r"^bar y \S+ \S+ box 2 reach \d+,\d+ at 0,30 ", k, re.M):
        fail("a box scrolled wasn't kept so when the page was laid out again:\n" + k[:1500])
    inner = [x for x in b if x[2] == 5][0][1]
    k = run(ewrender, font, "-d", "-O", "6:0:20")
    if [x for x in bars(k) if x[2] == 5][0][1] != inner - 20:
        fail("a box in a box didn't move with the outer one's scrolling")
    ends = []
    for x in b:
        ends += ["-O", "%d:%d:%d" % (x[2], x[3], x[4])]
    with tempfile.TemporaryDirectory() as tmp:
        for ed in [ewrender] + sys.argv[3:4]:
            run(ed, font, "-o", os.path.join(tmp, "s.png"), *ends)
            run(ed, font, "-B", "-o", os.path.join(tmp, "s.png"), *ends)
    print("scroll: boxes with bars as they should be, their links clicked once scrolled to, kept when laid "
          "out again, nested, drawn scrolled%s" % (" in 2 editions" if len(sys.argv) > 3 else ""))


main()
