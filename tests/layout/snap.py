#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of a picture in the page's own colours (Erez, 16 Nov
2026: src/browse/img.c's snap, pagecol.h): tests/layout/snap/page.html
through ewrender at 256 colours with EWeb's own (-p 256 --own), a tile of
nearly the page's blue (#396ba5 on #3a6ea5) with an orange disc in it.

- the tile's blue drawn in the page's own colour, solid: the same pixels
  as the page's blue beside it, no other colour in it but the disc's;
- the disc dithered as before (not a page colour);
- at 256 colours with the system's (no page colours) the tile dithered as
  it was: more than one colour in its blue.

    tests/layout/snap.py EWRENDER EWEB.FNT"""
import os
import subprocess
import sys
import tempfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "snap", "page.html")
fails = 0


def check(what, ok, detail=""):
    global fails
    print("%s  snap: %s%s" % ("ok  " if ok else "FAIL", what, "" if ok else " (%s)" % detail))
    if not ok:
        fails += 1


def draw(ewrender, fnt, *more):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "p.png")
        subprocess.run([ewrender, "-f", fnt, "-w", "400", "-p", "256", *more, "-o", out, PAGE],
                       capture_output=True, text=True, timeout=120)
        return Image.open(out).convert("RGB")


def colours(im, box):
    """the colours in box"""
    return {c for _, c in im.crop(box).getcolors(65536)}


def main():
    ewrender, fnt = sys.argv[1:3]
    im = draw(ewrender, fnt, "--own")
    # the tile is 160 x 80 px (CSS px at 0.8), each of its 32 px squares a
    # disc in the middle: their corners, away from the discs
    corner = colours(im, (0, 0, 6, 6)) | colours(im, (32, 0, 38, 6)) | colours(im, (0, 70, 6, 76))
    plain = colours(im, (0, 90, 160, 160))
    check("the page's blue beside the tile drawn in one colour", len(plain) == 1, str(plain))
    check("the tile's blue drawn in the page's own colour, solid", corner == plain, "%s against %s" % (corner, plain))
    disc = colours(im, (13, 13, 19, 19))
    check("the disc in the tile drawn in its own colours", len(disc) >= 1 and not disc & plain, str(disc))
    sys_im = draw(ewrender, fnt)
    tile = colours(sys_im, (0, 0, 6, 6)) | colours(sys_im, (32, 0, 38, 6))
    check("with the system's colours the tile's blue dithered as before", len(tile) > 1, str(tile))
    print("snap: %d failed" % fails)
    sys.exit(1 if fails else 0)


main()
