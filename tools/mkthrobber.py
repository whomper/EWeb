#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkthrobber.py - EWeb's throbber, the AERA mark's pyramid A with a star
going round it over a field of faint stars that pulse slowly (Erez's
choice, 20 Oct 2026, "Orbit"). Drawn here 8 times bigger and reduced, then
put on the Falcon's own colours: TOS 4.04's 256-colour palette
(tools/falcon256.txt, read in Hatari) for 256 colours and true colour, its
16 system colours, and black and white for monochrome and 4 colours.

Each edition is 16 frames of 30 x 30 pixels, the height of EWeb's toolbar
(its buttons' frames, 21 Oct 2026; drawn in a 40-unit square, as at
first), and 30 x 15 for the screens of 200 lines (their pixels are twice
as tall), as a byte a pixel into its own list of colours, each frame
deflated on its own (EWeb inflates the one it shows). Written as
ui/throbber.h; the ST edition has no 256-colour or 16-colour 480-line
one (its screens are monochrome, or have 200 lines).

    python3 tools/mkthrobber.py ui/throbber.h [preview]
      preview: preview.gif (the editions moving, x4) and preview.png (every frame)
"""
import math
import os
import random
import sys
import zlib

from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
S = 8                           # drawn 8 times bigger
N = 40                          # the square it is drawn in, in units
OUT = 30                        # the throbber, square, in pixels
FRAMES = 16

PAL256 = [tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) for h in open(os.path.join(HERE, "falcon256.txt")).read().split()]
PAL16 = PAL256[:16]

BG = (0, 0, 0)                  # the mark's night
# the bronze, light to dark, and the copper of the AERA star; the
# 16-colour edition's own: gold as yellow and dark yellow, the shadow
# side dark red, the glint white
BRONZE = dict(HI=(255, 236, 200), LIGHT=(236, 190, 130), MID=(196, 132, 78), DARK=(122, 74, 40), DEEP=(66, 38, 20),
              COPPER=(222, 140, 88))
SYSTEM16 = dict(HI=(255, 255, 255), LIGHT=(255, 255, 0), MID=(170, 170, 0), DARK=(170, 170, 0), DEEP=(170, 0, 0),
                COPPER=(170, 170, 0))
C = dict(BRONZE)

APEX, BASE = (20, 4.5), 32.5
L_OUT, L_IN, R_IN, R_OUT = 3.5, 11.0, 29.0, 36.5
APEX_IN = 15.5                  # where the strokes' inner edges meet


def P(pts):
    return [(x * S, y * S) for x, y in pts]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def canvas():
    return Image.new("RGB", (N * S, N * S), BG)


def pyramid(d):
    """the A: two strokes, each in two bevelled faces lit from the top left,
    the edge the light catches"""
    ax, ay = APEX

    def leg(xo, xi, faces):
        mid = (xo + xi) / 2
        inner = [(xi, BASE), (xi + (ax - xi) * (BASE - APEX_IN) / (BASE - APEX_IN), APEX_IN)]
        d.polygon(P([(xo, BASE), (ax, ay), (ax, ay), (mid, BASE)]), fill=faces[0])
        d.polygon(P([(mid, BASE), (ax, ay), inner[1], inner[0]]), fill=faces[1])
    leg(L_OUT, L_IN, (C["LIGHT"], C["DARK"]))
    leg(R_OUT, R_IN, (C["MID"], C["DEEP"]))
    d.line(P([(L_OUT + 0.4, BASE), (ax, ay + 0.4)]), fill=C["HI"], width=S)


def base_line(d):
    """the wordmark's line under the letters, copper in the middle"""
    for i in range(18 * S):
        c = lerp(C["DEEP"], C["COPPER"], 1 - i / (18 * S))
        for x in (20 * S + i, 20 * S - i):
            d.line([(x, 35.5 * S), (x, 35.5 * S + S * 0.9)], fill=c)


def star(d, cx, cy, r, colour, rot=0.0):
    """the AERA four-pointed star"""
    pts = []
    for k in range(8):
        a = rot + k * math.pi / 4
        rr = r if k % 2 == 0 else r * 0.32
        pts.append((cx + rr * math.sin(a), cy - rr * math.cos(a)))
    d.polygon(P(pts), fill=colour)


def glow(cx, cy, r, colour, strength):
    g = Image.new("RGB", (N * S, N * S), (0, 0, 0))
    gd = ImageDraw.Draw(g)
    for k in range(10, 0, -1):
        t = k / 10
        rr = r * t * S
        c = tuple(max(0, min(255, int(v * strength * (1 - t) ** 1.2))) for v in colour)
        gd.ellipse([cx * S - rr, cy * S - rr, cx * S + rr, cy * S + rr], fill=c)
    return g


# ---------------------------------------------------------------- the orbit

TILT = math.radians(-14)
RX, RY, CX, CY = 15.0, 5.0, 20.0, 19.5


def orbit_at(t):
    """where the star is at t (0..1), and whether it is behind the A"""
    a = 2 * math.pi * t
    x, y = RX * math.sin(a), -RY * math.cos(a)
    return (CX + x * math.cos(TILT) - y * math.sin(TILT), CY + x * math.sin(TILT) + y * math.cos(TILT),
            math.cos(a) > 0.15)


def scene(f):
    """frame f at 8x, in the colours C has now"""
    t = f / FRAMES
    x, y, behind = orbit_at(t)
    im = canvas()
    d = ImageDraw.Draw(im)
    if behind:
        star(d, x, y, 2.6, C["DARK"], t * math.pi)
    pyramid(d)
    base_line(d)
    if not behind:
        # the A's lit edge catches the star as it passes
        near = max(0.0, 1 - abs(x - 12) / 10)
        if near > 0:
            im = ImageChops.add(im, glow(11.5, 20, 7, (255, 200, 130), 0.55 * near))
        d = ImageDraw.Draw(im)
        for k, (tx, ty, tb) in enumerate(orbit_at(t - j * 0.022) for j in (3, 2, 1)):
            if not tb:          # its trail, fading
                r = 0.55 + 0.25 * k
                d.ellipse(P([(tx - r, ty - r), (tx + r, ty + r)]), fill=lerp(C["DEEP"], C["COPPER"], (k + 1) / 4))
        im = ImageChops.add(im, glow(x, y, 6.5, (255, 190, 120), 0.75))
        d = ImageDraw.Draw(im)
        star(d, x, y, 4.6, C["HI"], t * math.pi)
    return im.resize((OUT, OUT), Image.LANCZOS)


# the field: 22 stars, a fixed seed (the same every time); each pulses once
# or twice a round between two of its edition's darkest greys, never
# brighter than the A's shadow side; four become a small plus at their
# brightest
random.seed(7)
FIELD = []
while len(FIELD) < 22:
    x, y = random.randrange(1, 39), random.randrange(1, 38)
    if all(abs(x - a) + abs(y - b) >= 5 for a, b, _, _ in FIELD):
        FIELD.append((x, y, random.choice((1, 1, 2)), random.random()))
FIELD = [(x * OUT // N, y * OUT // N, sp, ph) for x, y, sp, ph in FIELD]
BIG = {0, 5, 11, 17}


def stars(base, f, greys, edge):
    """the field on frame f where it is night (a black pixel with black all
    round)"""
    out = base.copy()
    px, src = out.load(), base.load()

    def night(x, y):
        return all(0 <= x + dx < OUT and 0 <= y + dy < OUT and src[x + dx, y + dy] == BG
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1))
    for i, (x, y, speed, ph) in enumerate(FIELD):
        u = 0.5 - 0.5 * math.cos(2 * math.pi * (speed * f / FRAMES + ph))
        g = greys[min(len(greys) - 1, int(u * len(greys)))]
        if g is None or not night(x, y):
            continue
        px[x, y] = g
        if i in BIG and u > 0.75 and edge is not None:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if night(x + dx, y + dy):
                    px[x + dx, y + dy] = edge
    return out


# ---------------------------------------------------------------- the editions

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def ordered(im, pal, spread):
    """to pens with an ordered dither: the same pattern every frame, so the
    still A doesn't shimmer while the star moves"""
    src = im.load()
    out = Image.new("RGB", im.size)
    px = out.load()
    near = {}
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            c = src[x, y]
            k = (BAYER[y % 4][x % 4] + 0.5) / 16 - 0.5
            want = c if c == BG else tuple(max(0, min(255, int(v + k * spread))) for v in c)
            if want not in near:
                near[want] = min(pal, key=lambda p: (p[0] - want[0]) ** 2 * 3 + (p[1] - want[1]) ** 2 * 4 +
                                 (p[2] - want[2]) ** 2 * 2)
            px[x, y] = near[want]
    return out


def warm(c):
    r, g, b = c
    return r >= g >= b and r > 0 and b <= r * 0.2


# 256 colours: the palette's golds, oranges and browns, black and white
# (its greys would make the lit face steel); the field in its darkest greys
PENS256 = sorted(set([c for c in PAL256 if warm(c)] + [(0, 0, 0), (255, 255, 255)]))
GREYS256 = [(17, 17, 17), (34, 34, 34), (34, 34, 34), (51, 51, 51), (51, 51, 51), (68, 68, 68)]
# 16: yellow, dark yellow, dark red, red, white, the greys and black; dark
# grey (888888) is the darkest grey there, so a star shows only near its
# brightest
PENS16 = [PAL16[i] for i in (0, 3, 11, 9, 1, 7, 8, 15)]
GREYS16 = [None, None, None, None, None, (136, 136, 136)]


def mono(im):
    """black and white: the A by its brightness, a dot white where it is
    lit at all (at 26 pixels a dither would leave no shape)"""
    src = im.load()
    out = Image.new("RGB", im.size)
    px = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b = src[x, y]
            # brightened (gamma 0.5): the bronze's shadow faces would be
            # a few dots, and the A must read as a shape
            lum = 255 * ((299 * r + 587 * g + 114 * b) / 255000) ** 0.5
            px[x, y] = (255, 255, 255) if lum > 110 else (0, 0, 0)
    return out


def half(im):
    """for the screens of 200 lines: every two rows as one"""
    return im.resize((OUT, OUT // 2), Image.BOX)


def editions():
    """{name: [frame RGB images]} for each edition and height"""
    out = {}
    for name in ("256", "16", "mono"):
        for tall in (True, False):
            fr = []
            for f in range(FRAMES):
                C.update(SYSTEM16 if name == "16" else BRONZE)
                im = scene(f)
                if not tall:
                    im = half(im)
                if name == "256":
                    im = ordered(im, PENS256, 28)
                    im = stars(im, f, GREYS256, (34, 34, 34)) if tall else im
                elif name == "16":
                    im = ordered(im, PENS16, 0)
                    im = stars(im, f, GREYS16, None) if tall else im
                else:
                    im = mono(im)
                fr.append(im)
            out[(name, tall)] = fr
    C.update(BRONZE)
    return out


def main(argv):
    hdr = argv[1]
    eds = editions()
    lines = ["/* SPDX-License-Identifier: GPL-2.0-or-later",
             " * Copyright (C) 2026 AERA | Erez Yaary */",
             "/* throbber.h - EWeb's throbber, made by tools/mkthrobber.py: the AERA",
             "   A with a star going round it, %d frames of %d x %d (THR_TALL) or" % (FRAMES, OUT, OUT),
             "   %d x %d (THR_SHORT, for the 200-line screens), each pixel a byte into" % (OUT, OUT // 2),
             "   its edition's colours (0xRRGGBB), each frame deflated on its own (at",
             "   off[f], off[f + 1] - off[f] bytes): 256 colours (true colour too),",
             "   16 colours, and black and white (monochrome, 4 colours). */",
             "#define THR_W %d" % OUT, "#define THR_TALL %d" % OUT, "#define THR_SHORT %d" % (OUT // 2),
             "#define THR_FRAMES %d" % FRAMES,
             "typedef struct { short ncol, h; const unsigned long *col; const unsigned short *off; const unsigned char *data; } THR_ED;",
             ""]
    for (name, tall), fr in eds.items():
        cols = []
        packed = b""
        off = [0]
        for im in fr:
            idx = bytearray()
            for c in im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata():
                if c not in cols:
                    cols.append(c)
                idx.append(cols.index(c))
            z = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed += z.compress(bytes(idx)) + z.flush()
            off.append(len(packed))
        tag = "%s_%s" % (name, "tall" if tall else "short")
        st = not (name == "256" or (name == "16" and tall))
        if not st:
            lines.append("#ifndef EW_ST")
        lines.append("static const unsigned long thr_col_%s[%d] = {" % (tag, len(cols)))
        for i in range(0, len(cols), 6):
            lines.append("\t" + ", ".join("0x%02X%02X%02XUL" % c for c in cols[i:i + 6]) + ",")
        lines.append("};")
        lines.append("static const unsigned char thr_data_%s[%d] = {" % (tag, len(packed)))
        for i in range(0, len(packed), 12):
            lines.append("\t" + ", ".join("0x%02X" % b for b in packed[i:i + 12]) + ",")
        lines.append("};")
        lines.append("static const unsigned short thr_off_%s[%d] = { %s };" % (tag, len(off), ", ".join(map(str, off))))
        lines.append("static const THR_ED thr_%s = { %d, %d, thr_col_%s, thr_off_%s, thr_data_%s };" % (
            tag, len(cols), OUT if tall else OUT // 2, tag, tag, tag))
        if not st:
            lines.append("#endif")
        lines.append("")
    open(hdr, "w").write("\n".join(lines))
    if len(argv) > 2:
        order = [("256", True), ("16", True), ("mono", True), ("256", False), ("16", False), ("mono", False)]
        gif = []
        for f in range(FRAMES):
            im = Image.new("RGB", (6 * 176 + 16, 176), (192, 192, 192))
            for k, key in enumerate(order):
                fr = eds[key][f]
                im.paste(fr.resize((160, 160 if key[1] else 80), Image.NEAREST), (16 + k * 176, 8))
            gif.append(im)
        gif[0].save(argv[2] + ".gif", save_all=True, append_images=gif[1:], duration=120, loop=0)
        sheet = Image.new("RGB", (FRAMES * 88 + 8, 6 * 88 + 8), (192, 192, 192))
        for r, key in enumerate(order):
            for f, fr in enumerate(eds[key]):
                sheet.paste(fr.resize((80, 80 if key[1] else 40), Image.NEAREST), (8 + f * 88, 8 + r * 88))
        sheet.save(argv[2] + ".png")
        print("%s: %s" % (hdr, ", ".join("%s %s" % k for k in order)))


if __name__ == "__main__":
    main(sys.argv)
