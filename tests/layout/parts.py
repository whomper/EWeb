#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of a page drawn in parts (src/gfx/canvas.c's
cv_page_part, as ui/win.c makes the kept page's bands on a stock Falcon:
a band's items until its time is up, the rest at the next turns): every
page of tests/layout, and the news page, drawn by ewrender -k in bands of
8 rows, each stopped every 4 items, the same pixels as the page drawn
whole, in 16 colours and in true colour.

    tests/layout/parts.py EWRENDER EWEB.FNT [NEWS.HTM NEWS.CSS]"""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def draw(ewr, fnt, page, extra, out, parts):
    cmd = [ewr, "-f", fnt, "-o", out] + extra + (["-k"] if parts else []) + page
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=300)
    return r.returncode, r.stderr.decode(errors="replace")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ewr, fnt = sys.argv[1], sys.argv[2]
    pages = [[p] for p in sorted(glob.glob(os.path.join(HERE, "*.html")))]
    if len(sys.argv) >= 5:
        pages.append(sys.argv[3:5])
    bad = n = 0
    with tempfile.TemporaryDirectory() as tmp:
        a, b = os.path.join(tmp, "whole.png"), os.path.join(tmp, "parts.png")
        for page in pages:
            for extra in (["-p", "16"], []):
                ra, ea = draw(ewr, fnt, page, extra, a, False)
                rb, eb = draw(ewr, fnt, page, extra, b, True)
                n += 1
                name = "%s%s" % (os.path.basename(page[0]), " in 16 colours" if extra else "")
                if ra or rb:
                    print("FAIL  %s: ewrender %d/%d %s%s" % (name, ra, rb, ea[-300:], eb[-300:]))
                    bad += 1
                elif open(a, "rb").read() != open(b, "rb").read():
                    print("FAIL  %s: in parts, not the page drawn whole" % name)
                    bad += 1
    print("parts: %d of %d drawn alike" % (n - bad, n))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
