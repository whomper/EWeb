#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mktbicons.py - the toolbar's icons (Back, Forward, Reload, Home, Search,
Stop) in the platinum look (Erez's choice, 24 Oct 2026, C of three drawn):
graphite shading, light at the top, a dark edge, and red for Stop alone.
Drawn in a 30 x 25 grid 16 times bigger and reduced: 30 x 25 pixels for
the screens with the 16-pixel font (TB_TALL), 30 x 12 for the 200-line
ones (TB_SHORT, their pixels twice as tall).

Three editions, as the throbber's: 256 colours (true colour too) shaded
and kept smooth; the 16 system colours (white, the two greys, black, red
and dark red), so that 16 colours look the same; and black and white
(monochrome, 4 colours): a white fill and a black edge. Each has the six
icons and the six greyed (a button that can't be used), each pixel a
byte into the edition's colours, 0 a hole (the button shows through), and
each picture deflated on its own. Written as ui/tbicons.h; the ST
edition has no 256-colour or 16-colour 25-row one (its screens are
monochrome, or have 200 lines).

    python3 tools/mktbicons.py ui/tbicons.h [preview.png]
"""
import os
import sys
import zlib

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OW, OT, OS = 30, 25, 12         # wide; tall (TB_TALL); on 200 lines (TB_SHORT)
S = 16                          # drawn 16 times bigger
BG = (0xbb, 0xbb, 0xbb)         # the toolbar's paper, VDI's pen 8
ICONS = ["back", "forward", "reload", "home", "search", "stop"]
HOLE = 0.35                     # a pixel less covered than this is a hole

PAL256 = [tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) for h in open(os.path.join(HERE, "falcon256.txt")).read().split()]
# the 16 system colours the icons take (VDI's pens 0, 8, 9, 1, 2, 10, 6, 14)
SYS16 = [(255, 255, 255), (0xbb, 0xbb, 0xbb), (0x88, 0x88, 0x88), (0, 0, 0), (255, 0, 0), (0xaa, 0, 0),
         (255, 255, 0), (0xaa, 0xaa, 0)]


def own256():
    """EWeb's own palette (ui/screen.c, own_colour) after the 16 system colours"""
    grey = [13, 26, 38, 64, 77, 89, 115, 128, 140, 166, 179, 191, 212, 220, 226, 231, 235, 238, 241, 244, 247, 250, 253]
    out = list(PAL256[:16])
    for k in range(239):
        if k < 216:
            out.append((k // 36 * 51, k // 6 % 6 * 51, k % 6 * 51))
        else:
            out.append((grey[k - 216],) * 3)
    return out


def nearest(c, pal):
    best, bd = pal[0], 1 << 30
    for p in pal:
        d = (p[0] - c[0]) ** 2 * 2 + (p[1] - c[1]) ** 2 * 4 + (p[2] - c[2]) ** 2 * 3
        if d < bd:
            best, bd = p, d
    return best


def P(x, y):
    return (x * S, y * S)


def arrow(d, v, right):
    pts = [(4, 12.5), (13, 4), (13, 8.5), (26, 8.5), (26, 16.5), (13, 16.5), (13, 21)]
    d.polygon([P(30 - x if right else x, y) for x, y in pts], fill=v)


def ring(d, v, short=False):
    """Reload: a ring round (15, 12.5), open at the top right, its end an
    arrowhead going on round it (on 200 lines a little bolder)"""
    import math
    r, w, head = (7.5, 3.6, 5.5) if not short else (8, 4.4, 6.5)
    d.arc([P(15 - r, 12.5 - r), P(15 + r, 12.5 + r)], -45, 255, fill=v, width=int(w * S))
    a = math.radians(-45)
    cx, cy = 15 + r * math.cos(a), 12.5 + r * math.sin(a)
    tx, ty = math.sin(a), -math.cos(a)
    nx, ny = math.cos(a), math.sin(a)
    d.polygon([P(cx + tx * head, cy + ty * head), P(cx + nx * head * 0.9, cy + ny * head * 0.9),
               P(cx - nx * head * 0.9, cy - ny * head * 0.9)], fill=v)


def house(d, v):
    d.polygon([P(3, 13), P(15, 3), P(27, 13)], fill=v)
    d.rectangle([P(7, 11), P(23, 22)], fill=v)


def door(d, v):
    d.rectangle([P(13, 15), P(17.5, 22)], fill=v)


def rim(d, v):
    d.ellipse([P(5, 3), P(20, 18)], fill=v)


def glass(d, v):
    d.ellipse([P(7.4, 5.4), P(17.6, 15.6)], fill=v)


def handle(d, v):
    d.line([P(17.5, 15.5), P(25, 23)], fill=v, width=int(3.6 * S))


OCT = [(10, 2.2), (20, 2.2), (27, 9.2), (27, 16.2), (20, 23.2), (10, 23.2), (3, 16.2), (3, 9.2)]


def octagon(d, v):
    d.polygon([P(*p) for p in OCT], fill=v)


def bar(d, v):
    d.rectangle([P(9, 11), P(21, 14.5)], fill=v)


# the parts of each icon, drawn in order: (shape, top colour, bottom colour,
# edge colour or None, the rows the shading goes over, its look in 16
# colours, its look in black and white). In 16 colours a part is shaded
# from one system colour to another (dithered, 2 x 2) with a 1-pixel edge
# or none; in black and white 'fill' is white with a black edge, 'black'
# and 'white' solid. Each icon three ways: "on", "dim" (greyed: it can't
# be used) and "lit" (under the mouse: it wakes, in AERA's copper)
HI, LO, ED = (245, 245, 245), (120, 120, 125), (55, 55, 60)
DHI, DLO, DED = (215, 215, 215), (165, 165, 165), (135, 135, 135)
CHI, CLO, CED = (250, 196, 112), (190, 105, 30), (90, 45, 10)
WHITE, LGREY, DGREY, BLACK, RED, DRED, YELLOW, DYELLOW = SYS16
PLAT16 = ([WHITE, LGREY, DGREY], BLACK)         # the graphite: white to dark grey, a black edge
DIM16 = ([], DGREY)                             # greyed: the dark grey edge alone
LIT16 = ([WHITE, YELLOW, DYELLOW], BLACK)       # lit: white to gold


def parts(kind, state, h=OT):
    hi, lo, ed = {"on": (HI, LO, ED), "dim": (DHI, DLO, DED), "lit": (CHI, CLO, CED)}[state]
    p16 = {"on": PLAT16, "dim": DIM16, "lit": LIT16}[state]
    dim, lit = state == "dim", state == "lit"
    if kind in ("back", "forward"):
        right = kind == "forward"
        return [(lambda d, v: arrow(d, v, right), hi, lo, ed, (4, 21), p16, "fill")]
    if kind == "reload":
        return [(lambda d, v: ring(d, v, h == OS), hi, lo, ed, (3, 22), p16, "fill")]
    if kind == "home":
        top, bot = ((DED, DED) if dim else ((70, 40, 15), (45, 25, 8)) if lit else ((90, 90, 95), (60, 60, 64)))
        return [(house, hi, lo, ed, (3, 22), p16, "fill"),
                (door, top, bot, None, (15, 22), ([DGREY] if dim else [BLACK], None), "black")]
    if kind == "search":
        if dim:
            return [(handle, DED, DED, None, (15, 23), ([DGREY], None), "black"),
                    (rim, DED, DED, None, (3, 18), ([], DGREY), "black"),
                    (glass, DHI, DLO, None, (5, 16), ([], None), "white")]
        if lit:
            return [(handle, (190, 105, 30), (90, 45, 10), None, (15, 23), ([DYELLOW, BLACK], None), "black"),
                    (rim, (250, 170, 70), (150, 75, 15), None, (3, 18), ([DYELLOW], BLACK), "black"),
                    (glass, (255, 250, 240), (240, 215, 175), None, (5, 16), ([WHITE, WHITE, YELLOW], None), "white")]
        return [(handle, (120, 120, 125), (50, 50, 55), None, (15, 23), ([DGREY, BLACK], None), "black"),
                (rim, (170, 170, 175), (70, 70, 75), None, (3, 18), ([DGREY], BLACK), "black"),
                (glass, (250, 250, 250), (200, 205, 212), None, (5, 16), ([WHITE, WHITE, LGREY], None), "white")]
    if kind == "stop":
        if dim:
            return [(octagon, hi, lo, ed, (2, 23), DIM16, "black"),
                    (bar, (255, 255, 255), (235, 235, 235), None, (11, 15), ([WHITE], None), "white")]
        if lit:
            return [(octagon, (255, 125, 105), (205, 45, 35), (120, 25, 20), (2, 23), ([RED], BLACK), "black"),
                    (bar, (255, 255, 255), (245, 245, 245), None, (11, 15), ([WHITE], None), "white")]
        return [(octagon, (230, 90, 80), (160, 30, 30), (100, 20, 20), (2, 23), ([RED, RED, DRED], DRED), "black"),
                (bar, (255, 255, 255), (235, 235, 235), None, (11, 15), ([WHITE], None), "white")]
    raise ValueError(kind)


def big_mask(shape):
    m = Image.new("L", (OW * S, OT * S), 0)
    shape(ImageDraw.Draw(m), 255)
    return m


def edge_of(m, w):
    """the mask's outline, w units wide"""
    inner = m.filter(ImageFilter.MinFilter(int(w * S) | 1))
    return Image.composite(Image.new("L", m.size, 0), m, inner)


def gradient(top, bottom, box):
    g = Image.new("RGBA", (OW * S, OT * S), top + (255,))
    d = ImageDraw.Draw(g)
    y0, y1 = int(box[0] * S), int(box[1] * S)
    for y in range(OT * S):
        t = min(1.0, max(0.0, (y - y0) / max(1, y1 - y0 - 1)))
        d.line([(0, y), (OW * S, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    return g


def shaded(kind, state, h):
    """the icon h rows high: [(r, g, b) or None (a hole)] a row"""
    big = Image.new("RGBA", (OW * S, OT * S), (0, 0, 0, 0))
    for shape, top, bot, ed, box, _, _ in parts(kind, state, h):
        m = big_mask(shape)
        big.paste(gradient(top, bot, box), (0, 0), m)
        if ed is not None:
            # 1.1 units wide; on 200 lines a row is two units high, so the
            # edge as high as on the tall ones
            big.paste(Image.new("RGBA", m.size, ed + (255,)), (0, 0), edge_of(m, 1.1 if h == OT else 1.6))
    cover = big.getchannel("A").resize((OW, h), Image.BOX)
    over = Image.new("RGBA", big.size, BG + (255,))
    over.alpha_composite(big)
    col = over.convert("RGB").resize((OW, h), Image.LANCZOS)
    return [[col.getpixel((x, y)) if cover.getpixel((x, y)) >= HOLE * 255 else None for x in range(OW)]
            for y in range(h)]


def small_mask(shape, h):
    """the shape h rows high: the pixels it covers half of or more"""
    m = big_mask(shape).resize((OW, h), Image.BOX)
    return [[m.getpixel((x, y)) >= 128 for x in range(OW)] for y in range(h)]


def rim_of(m):
    """a mask's pixels with a neighbour (of four) outside it"""
    h = len(m)
    def at(x, y):
        return 0 <= x < OW and 0 <= y < h and m[y][x]
    return [[m[y][x] and not (at(x - 1, y) and at(x + 1, y) and at(x, y - 1) and at(x, y + 1)) for x in range(OW)]
            for y in range(h)]


BAYER = [[0, 2], [3, 1]]


def sys16(kind, state, h):
    """16 colours: each part's ramp (white to dark grey, say) as the rows go
    down its box, dithered between the two steps nearest, and its edge"""
    out = [[None] * OW for _ in range(h)]
    for shape, _, _, _, box, (ramp, edge), _ in parts(kind, state, h):
        m = small_mask(shape, h)
        e = rim_of(m) if edge is not None else None
        y0, y1 = box[0] * h / OT, box[1] * h / OT
        for y in range(h):
            t = min(1.0, max(0.0, (y + 0.5 - y0) / max(0.5, y1 - y0)))
            for x in range(OW):
                if not m[y][x]:
                    continue
                if e is not None and e[y][x]:
                    out[y][x] = edge
                elif ramp:
                    f = t * (len(ramp) - 1)
                    i = min(int(f), len(ramp) - 2) if len(ramp) > 1 else 0
                    frac = f - i if len(ramp) > 1 else 0
                    out[y][x] = ramp[i + 1] if frac * 4 > BAYER[y % 2][x % 2] + 0.5 else ramp[i]
    return out


def mono(kind, state, h):
    """black and white: each part white with a black edge, or solid; the
    greyed ones their edges alone, every other dot; the lit ones inverted
    within their outline, as GEM shows a chosen object"""
    dim = state == "dim"
    out = [[None] * OW for _ in range(h)]
    for shape, _, _, _, _, _, how in parts(kind, "on", h):
        m = small_mask(shape, h)
        e = rim_of(m)
        for y in range(h):
            for x in range(OW):
                if not m[y][x]:
                    continue
                if dim:
                    out[y][x] = (0, 0, 0) if how != "white" and e[y][x] else None
                elif how == "fill":
                    out[y][x] = (0, 0, 0) if e[y][x] else (255, 255, 255)
                else:
                    out[y][x] = (0, 0, 0) if how == "black" else (255, 255, 255)
    if dim:
        # the edges dotted: a dot where none of its neighbours has one
        # yet, so that diagonals are dotted as well as the straight lines
        for y in range(h):
            for x in range(OW):
                if out[y][x] is not None and any(
                        0 <= x + dx < OW and y + dy >= 0 and out[y + dy][x + dx] == (1, 1, 1)
                        for dx, dy in ((-1, 0), (-1, -1), (0, -1), (1, -1))):
                    out[y][x] = None
                elif out[y][x] is not None:
                    out[y][x] = (1, 1, 1)
        out = [[(0, 0, 0) if c is not None else None for c in row] for row in out]
    if state == "lit":
        whole = rim_of([[c is not None for c in row] for row in out])
        out = [[None if c is None else (0, 0, 0) if whole[y][x] or c == (255, 255, 255) else (255, 255, 255)
                for x, c in enumerate(row)] for y, row in enumerate(out)]
    return out


def edition(name, h):
    pics = []
    for state in ("on", "dim", "lit"):
        for kind in ICONS:
            if name == "mono":
                pics.append(mono(kind, state, h))
                continue
            pics.append(sys16(kind, state, h) if name == "16" else shaded(kind, state, h))
    return pics


def reduce256(pics, n=64):
    """the colours of the 256-colour edition, at most n of them: the
    screen's nearest is taken for each when the toolbar is first drawn"""
    cols = [c for p in pics for row in p for c in row if c is not None]
    strip = Image.new("RGB", (len(cols), 1))
    strip.putdata(cols)
    q = strip.quantize(colors=n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:3 * n]
    idx = list(q.get_flattened_data() if hasattr(q, 'get_flattened_data') else q.getdata())
    it = iter(idx)
    out = [[[tuple(pal[3 * k:3 * k + 3]) if c is not None else None for c, k in
             ((c, next(it) if c is not None else 0) for c in row)] for row in p] for p in pics]
    return out


def colours_of(pics):
    cols = []
    for p in pics:
        for row in p:
            for c in row:
                if c is not None and c not in cols:
                    cols.append(c)
    return cols


def c_array(lines, ctype, name, vals, per, fmt):
    lines.append("static const %s %s[%d] = {" % (ctype, name, len(vals)))
    for i in range(0, len(vals), per):
        lines.append("\t" + " ".join(fmt % v + "," for v in vals[i:i + per]))
    lines.append("};")


EDITIONS = [("256", OT, "tall"), ("256", OS, "short"), ("16", OT, "tall"), ("16", OS, "short"),
            ("mono", OT, "tall"), ("mono", OS, "short")]


def main(argv):
    lines = ["/* SPDX-License-Identifier: GPL-2.0-or-later",
             " * Copyright (C) 2026 AERA | Erez Yaary */",
             "/* tbicons.h - the toolbar's icons, made by tools/mktbicons.py: Back,",
             "   Forward, Reload, Home, Search and Stop, then the same six greyed,",
             "   then lit (under the mouse),",
             "   %d pixels wide and %d rows (TB_TALL) or %d (TB_SHORT, for the 200-line" % (OW, OT, OS),
             "   screens), each pixel a byte into its edition's colours (0xRRGGBB), 0 a",
             "   hole, the pictures one after another and deflated together (off[1]",
             "   bytes: one Huffman table for all; an 8 MHz ST took 70 ms a picture to",
             "   build one each): 256 colours (true colour too), 16 colours, and black",
             "   and white (monochrome, 4 colours). */",
             "#define TB_W %d" % OW, "#define TB_TALL %d" % OT, "#define TB_SHORT %d" % OS,
             "#define TB_PICS %d" % (3 * len(ICONS)),
             "typedef struct { short ncol, h; const unsigned long *col; const unsigned short *off; "
             "const unsigned char *data; } TB_ED;", ""]
    shown = {}
    for name, h, tag in EDITIONS:
        pics = edition(name, h)
        if name == "256":
            pics = reduce256(pics)
        cols = colours_of(pics)
        assert len(cols) < 256
        raw = b"".join(bytes(cols.index(c) + 1 if c is not None else 0 for row in p for c in row) for p in pics)
        z = zlib.compressobj(9, zlib.DEFLATED, -15)
        data = z.compress(raw) + z.flush()
        off = [0, len(data)]
        st = name == "mono" or (name == "16" and h == OS)
        if not st:
            lines.append("#ifndef EW_ST")
        sfx = "%s_%s" % (name, tag)
        c_array(lines, "unsigned long", "tb_col_" + sfx, [0] + [c[0] << 16 | c[1] << 8 | c[2] for c in cols], 6, "0x%06XUL")
        c_array(lines, "unsigned short", "tb_off_" + sfx, off, 10, "%d")
        c_array(lines, "unsigned char", "tb_data_" + sfx, list(data), 12, "0x%02X")
        lines.append("static const TB_ED tb_%s = { %d, %d, tb_col_%s, tb_off_%s, tb_data_%s };" % (
            sfx, len(cols) + 1, h, sfx, sfx, sfx))
        if not st:
            lines.append("#endif")
        lines.append("")
        shown[(name, h)] = pics
    open(argv[1], "w").write("\n".join(lines))
    if len(argv) > 2:
        preview(shown, argv[2])


def preview(shown, out):
    """each edition on the toolbar's grey, as the screen shows it (the
    256-colour one in TOS's palette and in EWeb's own; the short ones
    twice as tall), three times bigger: as they are, greyed, and lit"""
    from PIL import ImageFont
    rows = [("256 colours, EWeb's palette", "256", own256()), ("256 colours, TOS's palette", "256", PAL256),
            ("16 colours", "16", None), ("monochrome, 4 colours", "mono", None)]
    Z, bw = 3, 36
    strip_w = 6 * bw + 8
    cell_h = OT + 6
    sheet = Image.new("RGB", (8 + 3 * (strip_w * Z + 16) + 8, len(rows) * 2 * (cell_h * Z + 18) + 8), (255, 255, 255))
    d = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    y = 6
    for label, name, pal in rows:
        for h in (OT, OS):
            pics = shown[(name, h)]
            d.text((8, y), "%s, %s" % (label, "25 rows" if h == OT else "12 rows (200 lines, shown twice as tall)"),
                   fill=(0, 0, 0), font=font)
            for half in (0, 1, 2):
                im = Image.new("RGB", (strip_w, cell_h), BG if name != "mono" else (255, 255, 255))
                for i in range(6):
                    p = pics[half * 6 + i]
                    ic = Image.new("RGB", (OW, h), BG)
                    for yy in range(h):
                        for xx in range(OW):
                            c = p[yy][xx]
                            if c is not None:
                                ic.putpixel((xx, yy), nearest(c, pal) if pal else c)
                    mk = Image.new("L", (OW, h), 0)
                    for yy in range(h):
                        for xx in range(OW):
                            if p[yy][xx] is not None:
                                mk.putpixel((xx, yy), 255)
                    if h == OS:
                        ic = ic.resize((OW, 2 * h), Image.NEAREST)
                        mk = mk.resize((OW, 2 * h), Image.NEAREST)
                    im.paste(ic, (4 + i * bw + 3, 3), mk)
                sheet.paste(im.resize((strip_w * Z, cell_h * Z), Image.NEAREST), (8 + half * (strip_w * Z + 16), y + 14))
            y += cell_h * Z + 18
    sheet.save(out)


if __name__ == "__main__":
    main(sys.argv)
