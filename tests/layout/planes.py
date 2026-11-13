#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the ST edition's monochrome drawing (DESIGN.md, The
ST edition's speed): every test page, the sample page and the news page
drawn by ewrender -p 2 -1, each band of 16 rows at many scroll places and
parts across both a byte a pixel (its pens' low bit, as ui/screen.c turns
it into the plane) and, where canvas.c's cv_page_planes takes the band,
straight into one plane: the same bits in every band, under UBSan and
AddressSanitizer (make test's ewrenders: the host's, and the ST edition's,
-DEW_ST, whose gradients are runs of fills drawn in the plane too). And the
planes must be used: on the sample and news pages most bands are.

    tests/layout/planes.py EWRENDER EWEB.FNT [EWRENDER_ST]"""
import glob
import os
import re
import subprocess
import sys

font = os.path.abspath(sys.argv[2])
ewrenders = [os.path.abspath(sys.argv[1])] + [os.path.abspath(a) for a in sys.argv[3:4]]
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
pages = sorted(glob.glob(os.path.join(here, "*.html")))
pages.append(os.path.join(root, "tests", "bench", "sample.html"))
news = os.path.join(root, "build", "news", "NEWS.HTM")
if os.path.exists(news):
    pages.append(news)
fails = 0
bands = planar = 0
for ewrender in ewrenders:
    for page in pages:
        p = subprocess.run([ewrender, "-f", font, "-p", "2", "-1", page], capture_output=True, text=True, timeout=1200)
        m = re.search(r"planes: (\d+) bands, (\d+) in one plane, (\d+) differ", p.stdout)
        if p.returncode != 0 or m is None or m.group(3) != "0":
            fails += 1
            print("planes: FAIL %s (%s): %s" % (os.path.basename(page), ewrender, (p.stdout + p.stderr)[-1500:].strip()))
            continue
        bands += int(m.group(1))
        planar += int(m.group(2))
        name = os.path.basename(page)
        if name in ("sample.html", "NEWS.HTM") and int(m.group(2)) * 10 < int(m.group(1)) * 9:
            fails += 1
            print("planes: FAIL %s: only %s of %s bands in one plane" % (name, m.group(2), m.group(1)))
print("planes: %d pages, %d ewrenders, %d bands, %d in one plane, %d failed" % (len(pages), len(ewrenders), bands,
                                                                              planar, fails))
sys.exit(1 if fails else 0)
