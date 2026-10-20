#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mktbicons.py - the toolbar's icons (Back, Forward, Reload, Home, Search,
Stop) as layers of one colour each, 24 pixels wide: 20 high for screens
with the 16-pixel font, 10 for the 200-line ones (their pixels are twice as
tall). Drawn here as shapes, and written as ui/tbicons.h: each layer a
VDI pen and a 1-bit mask, drawn in that order (the outline last), so that
any palette, monochrome too, shows them.

    python3 tools/mktbicons.py ui/tbicons.h [preview.png]
"""
import math
import sys

from PIL import Image, ImageDraw

W, H = 24, 20
SS = 8                          # drawn 8 times bigger, then each pixel at half cover or more

# VDI pens: 0 white, 1 black, 2 red, 3 green, 4 blue, 6 yellow, 9 dark grey (VDI has no brown;
# with 4 colours win.c makes blue black and yellow white)
BLACK, RED, GREEN, BLUE, YELLOW, DARK, WHITE = 1, 2, 3, 4, 6, 9, 0


def shapes(kind):
    """[(pen, draw(d, s))] for one icon, s the scale: the fills, then the outline"""
    out = []
    if kind in ("back", "forward"):
        pts = [(1.5, 10), (10, 1.5), (10, 6), (22, 6), (22, 14), (10, 14), (10, 18.5)]
        if kind == "forward":
            pts = [(24 - x, y) for x, y in pts]
        out.append((GREEN, lambda d, s, p=pts: d.polygon([(x * s, y * s) for x, y in p], fill=255)))
        out.append((BLACK, lambda d, s, p=pts: d.polygon([(x * s, y * s) for x, y in p], outline=255, width=s)))
    elif kind == "reload":
        def ring(d, s, w):
            d.arc([(12 - 8.5) * s, (10 - 8.5) * s, (12 + 8.5) * s, (10 + 8.5) * s], 0, 290, fill=255, width=int(w * s))
        def head(d, s):
            c = lambda a, r: ((12 + r * math.cos(math.radians(a))) * s, (10 + r * math.sin(math.radians(a))) * s)
            d.polygon([c(290, 2), c(290, 12), c(338, 7)], fill=255)
        out.append((BLUE, lambda d, s: (ring(d, s, 4.5), head(d, s))))
        def outline(d, s):
            for r in (8.5, 4):
                d.arc([(12 - r) * s, (10 - r) * s, (12 + r) * s, (10 + r) * s], 0, 290, fill=255, width=s)
            c = lambda a, r: ((12 + r * math.cos(math.radians(a))) * s, (10 + r * math.sin(math.radians(a))) * s)
            d.polygon([c(290, 2), c(290, 12), c(338, 7)], outline=255, width=s)
        out.append((BLACK, outline))
    elif kind == "home":
        roof = [(12, 1), (22.5, 10.5), (1.5, 10.5)]
        out.append((RED, lambda d, s: d.polygon([(x * s, y * s) for x, y in roof], fill=255)))
        out.append((YELLOW, lambda d, s: d.rectangle([5 * s, 10.5 * s, 19 * s, 19 * s], fill=255)))
        out.append((DARK, lambda d, s: d.rectangle([10 * s, 13 * s, 14 * s, 19 * s], fill=255)))
        def outline(d, s):
            d.polygon([(x * s, y * s) for x, y in roof], outline=255, width=s)
            d.rectangle([5 * s, 10.5 * s, 19 * s, 19 * s], outline=255, width=s)
            d.rectangle([10 * s, 13 * s, 14 * s, 19 * s], outline=255, width=s)
        out.append((BLACK, outline))
    elif kind == "search":
        out.append((WHITE, lambda d, s: d.ellipse([3.5 * s, 2.5 * s, 13.5 * s, 12.5 * s], fill=255)))
        out.append((DARK, lambda d, s: d.line([(14 * s, 13 * s), (21 * s, 19 * s)], fill=255, width=int(2.5 * s))))
        def outline(d, s):
            d.ellipse([2 * s, 1 * s, 15 * s, 14 * s], outline=255, width=int(1.6 * s))
            d.line([(13 * s, 12 * s), (21.5 * s, 19.5 * s)], fill=255, width=int(1.2 * s))
        out.append((BLACK, outline))
    elif kind == "stop":
        oc = [(8, 1), (16, 1), (22, 6.5), (22, 13.5), (16, 19), (8, 19), (2, 13.5), (2, 6.5)]
        out.append((RED, lambda d, s: d.polygon([(x * s, y * s) for x, y in oc], fill=255)))
        out.append((BLACK, lambda d, s: d.polygon([(x * s, y * s) for x, y in oc], outline=255, width=s)))
        out.append((WHITE, lambda d, s: d.rectangle([7 * s, 8.5 * s, 17 * s, 11.5 * s], fill=255)))
    return out


ICONS = ["back", "forward", "reload", "home", "search", "stop"]


def mask(draw, h):
    big = Image.new("L", (W * SS, H * SS), 0)
    draw(ImageDraw.Draw(big), SS)
    small = big.resize((W, h), Image.BOX)
    return [[1 if small.getpixel((x, y)) >= 100 else 0 for x in range(W)] for y in range(h)]


def words(m):
    """each row as two 16-bit words (24 pixels and 8 of nothing)"""
    out = []
    for row in m:
        v = 0
        for x in range(32):
            v = (v << 1) | (row[x] if x < W else 0)
        out += [v >> 16, v & 0xFFFF]
    return out


def main(argv):
    hdr = argv[1]
    lines = ["/* SPDX-License-Identifier: GPL-2.0-or-later",
             " * Copyright (C) 2026 AERA | Erez Yaary */",
             "/* tbicons.h - the toolbar's icons, made by tools/mktbicons.py: each a",
             "   list of layers (a VDI pen, the pen on a monochrome screen, and a",
             "   mask, 2 words a row), drawn in order, 24 pixels wide,",
             "   20 rows (TB_TALL) or 10 (TB_SHORT, for the 200-line screens). */",
             "#define TB_W 24", "#define TB_TALL 20", "#define TB_SHORT 10",
             "typedef struct { short pen, mono; const unsigned short *bits; } TB_LAYER;	/* mono: the pen on a monochrome screen, -1 none */",
             "typedef struct { short n; const TB_LAYER *layer; } TB_ICON;", ""]
    previews = []
    for h, tag in ((20, "tall"), (10, "short")):
        for kind in ICONS:
            layers = shapes(kind)
            names = []
            for k, (pen, draw) in enumerate(layers):
                m = mask(draw, h)
                ws = words(m)
                name = "tb_%s_%s_%d" % (kind, tag, k)
                names.append((pen, name))
                lines.append("static const unsigned short %s[%d] = {" % (name, len(ws)))
                for i in range(0, len(ws), 8):
                    lines.append("\t" + ", ".join("0x%04X" % v for v in ws[i:i + 8]) + ",")
                lines.append("};")
            def mono(pen):
                return pen if pen in (0, 1) else (1 if kind == "stop" and pen == RED else -1)
            lines.append("static const TB_LAYER tb_%s_%s[] = { %s };" % (
                kind, tag, ", ".join("{ %d, %d, %s }" % (p, mono(p), n) for p, n in names)))
            previews.append((kind, h, layers))
        lines.append("static const TB_ICON tb_%s[%d] = { %s };" % (
            tag, len(ICONS), ", ".join("{ %d, tb_%s_%s }" % (len(shapes(k)), k, tag) for k in ICONS)))
        lines.append("")
    open(hdr, "w").write("\n".join(lines))
    if len(argv) > 2:
        pens = {0: (255, 255, 255), 1: (0, 0, 0), 2: (228, 0, 43), 3: (0, 160, 0), 4: (0, 64, 200),
                6: (255, 214, 0), 9: (100, 100, 100)}
        im = Image.new("RGB", (len(ICONS) * 32 * 4, 46 * 4), (192, 192, 192))
        for i, kind in enumerate(ICONS):
            for row, h in ((0, 20), (1, 10)):
                for pen, draw in shapes(kind):
                    m = mask(draw, h)
                    for y in range(h):
                        for x in range(W):
                            if m[y][x]:
                                y0 = (2 + row * 22 + y * (2 if h == 10 else 1)) * 4
                                for yy in range(2 if h == 10 else 1):
                                    im.paste(pens[pen], ((i * 32 + 4 + x) * 4, y0 + yy * 4,
                                                         (i * 32 + 5 + x) * 4, y0 + yy * 4 + 4))
        im.save(argv[2])


if __name__ == "__main__":
    main(sys.argv)
