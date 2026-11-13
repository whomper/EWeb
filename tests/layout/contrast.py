#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of text on a screen of two pens (monochrome: canvas.c's
backdrops): a page's text drawn in the pen that stands out from the solid
box behind it, not by its own brightness (by which a light blue link, an
orange title or a grey caption came out white on a white page and was gone,
forum.atari-home.de, 13 Nov 2026), and text that hardly differs from its
box (white on white) left as hidden as a screen of colours shows it.
tests/layout/mono/contrast.html, drawn by ewrender -p 2: each line of id
"ink" must have pixels of the other pen than its box's in its text's box,
each of id "none" none.

    tests/layout/contrast.py EWRENDER EWEB.FNT"""
import os
import re
import subprocess
import sys
import tempfile

from PIL import Image

ewrender, font = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
here = os.path.dirname(os.path.abspath(__file__))
page = os.path.join(here, "mono", "contrast.html")
want = re.findall(r'<p id="(ink|none)"', open(page).read())
dl = subprocess.run([ewrender, "-f", font, "-d", page], capture_output=True, text=True, check=True).stdout
texts = []
for line in dl.splitlines():
    m = re.match(r"text (\d+),(\d+) (\d+)x(\d+) ", line)
    if m:
        x, base, w, h = map(int, m.groups())
        texts.append((x, base, w, h))
fills = [tuple(map(int, m.groups())) + (m2,) for line in dl.splitlines()
         for m in [re.match(r"fill (\d+),(\d+) (\d+)x(\d+) #(\w+)", line)] if m
         for m2 in [m.group(5)]]
fails = 0
with tempfile.TemporaryDirectory() as tmp:
    png = os.path.join(tmp, "c.png")
    subprocess.run([ewrender, "-f", font, "-p", "2", "-o", png, page], check=True, capture_output=True)
    im = Image.open(png).convert("L")
    # (one text item a line: each line's words are one run here)
    rows = {}
    for t in texts:
        rows.setdefault(t[1], []).append(t)
    lines = [rows[k] for k in sorted(rows)]
    if len(lines) != len(want):
        print("contrast: %d lines drawn, %d in the page" % (len(lines), len(want)))
        sys.exit(1)
    for kind, runs in zip(want, lines):
        x0 = min(r[0] for r in runs)
        x1 = max(r[0] + r[2] for r in runs)
        base, h = runs[0][1], runs[0][3]
        y0 = base - h + 4
        box = im.crop((x0, y0, x1, base + 2))
        px = list(box.tobytes())
        # the box's pen: what most of the line's pixels are
        bg = 255 if px.count(255) >= len(px) / 2 else 0
        ink = sum(1 for v in px if v != bg)
        ok = ink > 20 if kind == "ink" else ink == 0
        if not ok:
            fails += 1
            print("FAIL %s line at %d: %d pixels of the other pen" % (kind, base, ink))
print("contrast: %d lines, %d failed" % (len(want), fails))
sys.exit(1 if fails else 0)
