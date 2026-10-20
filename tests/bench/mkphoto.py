#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes PHOTO.JPG: a made-up landscape, 480 x 320, for timing
PICBENCH.TTP on a picture like a page's photo.

    tests/bench/mkphoto.py OUTDIR

A sky going lighter to the horizon, a sun, hills one behind another,
trees, a lake with the sky in it and a grain over all, drawn with a fixed
seed by the Pillow in tools/requirements.txt (its libjpeg makes the file:
baseline, 4:2:0, quality 85, no EXIF), so every run writes the same bytes
and the file is ours to hand out.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

W, H = 480, 320


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    rnd = random.Random(1966)
    im = Image.new("RGB", (W, H))
    px = im.load()
    horizon = 190
    for y in range(horizon):
        t = y / horizon
        for x in range(W):
            px[x, y] = (int(70 + 120 * t), int(120 + 90 * t), int(200 + 40 * t))
    d = ImageDraw.Draw(im)
    d.ellipse((340, 40, 400, 100), fill=(255, 236, 170))
    # hills, the far ones paler
    for k, (base, amp, col) in enumerate([(150, 25, (120, 150, 140)), (170, 30, (80, 125, 80)),
                                          (195, 20, (60, 105, 50))]):
        pts = [(0, H)]
        for x in range(0, W + 8, 8):
            pts.append((x, base + amp * math.sin(x / (60.0 + 25 * k) + k) + 6 * math.sin(x / 13.0 + k)))
        pts.append((W, H))
        d.polygon(pts, fill=col)
    # the lake, with the sky in it
    lake = (0, 235, W, H)
    for y in range(lake[1], lake[3]):
        t = (y - lake[1]) / (lake[3] - lake[1])
        d.line((0, y, W, y), fill=(int(150 - 60 * t), int(185 - 50 * t), int(225 - 40 * t)))
    # trees on the near hill
    for _ in range(40):
        x = rnd.randrange(0, W)
        y = rnd.randrange(185, 232)
        r = rnd.randrange(6, 16)
        g = rnd.randrange(60, 110)
        d.rectangle((x - 1, y, x + 1, y + r), fill=(70, 50, 30))
        d.ellipse((x - r // 2 - 2, y - r, x + r // 2 + 2, y + r // 3), fill=(30, g, 35))
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    # a grain, as a camera's
    px = im.load()
    for y in range(H):
        for x in range(W):
            n = rnd.randrange(-6, 7)
            r, g, b = px[x, y]
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))
    os.makedirs(out, exist_ok=True)
    im.save(os.path.join(out, "PHOTO.JPG"), "JPEG", quality=85, subsampling=2, optimize=False)


if __name__ == "__main__":
    main()
