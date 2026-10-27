#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of CSS gradients as drawn (CSS Images 3 and 4): small
pages through ewrender in true colour and at 8 bits in 2, 4, 16 and 256
colours (-p), their pixels read back from the PNG.

- a linear gradient's ends are its first and last colours, its middle
  the two mixed (true colour, within a 5-6-5 step of dither); with a
  palette a 4 x 4 block's pens make the colour on average (16 and 256
  colours), or the share of light pixels its brightness (2 and 4
  colours);
- angles, to a side and to a corner, a sharp edge where it is, a hint
  bending the mix, positions in px and em, calc();
- a radial gradient's centre and corner, a circle's edge, one of no
  size its last colour; a repeating gradient's period, one of no length
  its stops' average; a conic gradient's quarters;
- a translucent gradient mixed with what is under it; a box's opacity;
  background-clip, -size, -position and -repeat; a box of no height
  stretched to its row's (a flex item, a table cell) with its gradient;
  the root's gradient over the whole page; an inline's gradient its
  average colour;
- a gradient whose stops can't be placed (min() of px and %) its
  stops' average, the colour before it in the shorthand kept; calc()
  angles; -webkit-linear-gradient() and -webkit-gradient(); a circle of
  no size with stops in px; the root's gradient on below a short page;
  small tiles (dots, checks) solid and translucent, each tile alike;
  translucent fades down and across;
- every page drawn in slices (ewrender -y, -H: as the window draws it, a
  band at a time) the same as drawn whole, in every mode: the patterns
  are fixed to the page;
- tests/layout/gradients.html drawn in bands at 8 and 16 bits (-B),
  under AddressSanitizer in make test;
- with EWRENDERST (ewrender built -DEW_ST), the ST edition's drawing:
  gradients to a side and at right angles as rows or columns of their
  colours, any other its average (one with min() inside calc() too),
  in true colour and 2, 4 and 16 colours, in slices the same as whole,
  and tall gentle gradients in bands of 32 rows from anywhere too;
- under UBSan (make test's ewrender), no undefined behaviour.

    tests/layout/gradients.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ewrender, font = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
ewrenderst = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
MODES = (0, 2, 4, 16, 256)	# 0: true colour
ST_MODES = (0, 2, 4, 16)
fails = 0
checks = 0


def fail(what):
    global fails
    fails += 1
    print("gradients: " + what)


def png(path):
    """the PNG ewrender wrote (8-bit RGB, unfiltered): rows of (r, g, b)"""
    with open(path, "rb") as f:
        d = f.read()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(d):
        n, kind = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    return [[tuple(raw[y * (1 + 3 * w) + 1 + 3 * x:y * (1 + 3 * w) + 4 + 3 * x]) for x in range(w)]
            for y in range(h)]


def page(tmp, html, whole=False):
    path = os.path.join(tmp, "page.html")
    with open(path, "w") as f:
        f.write(html if whole else "<!DOCTYPE html><html><body style='margin:0; background:#fff'>" + html +
                "</body></html>")
    return path


def draw(tmp, html, mode=0, height=80, top=0, whole=False, exe=None):
    path = page(tmp, html, whole)
    out = os.path.join(tmp, "out.png")
    args = [exe or ewrender, "-f", font, "-w", "200", "-z", "1024", "-y", str(top), "-H", str(height), "-o", out]
    if mode:
        args += ["-p", str(mode)]
    p = subprocess.run(args + [path], capture_output=True, text=True, timeout=60)
    if p.returncode != 0 or "runtime error:" in p.stderr:
        sys.exit("gradients: ewrender failed\n" + p.stderr)
    return png(out)


def mean(rows, x0, y0, x1, y1):
    px = [rows[y][x] for y in range(y0, y1) for x in range(x0, x1)]
    return tuple(sum(p[k] for p in px) / float(len(px)) for k in range(3))


def near(got, want, tol):
    return all(abs(g - w) <= tol for g, w in zip(got, want))


def light(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def name(mode):
    return "%d colours" % mode if mode else "true colour"


def box(css, h=40, extra=""):
    return "<div style='height:%dpx; %s'>%s</div>" % (h, css, extra)


def expect(rows, mode, what, x0, y0, x1, y1, want, tol_tc=10, tol_pal=40):
    """a block's mean colour: in true colour within tol_tc of want; with a
    palette (16 and 256 colours) within tol_pal; in 2 and 4 colours its
    brightness within 50"""
    global checks
    checks += 1
    got = mean(rows, x0, y0, x1, y1)
    if mode == 0:
        ok = near(got, want, tol_tc)
    elif mode in (16, 256):
        ok = near(got, want, tol_pal)
    else:
        ok = abs(light(got) - light(want)) <= 50
    if not ok:
        fail("%s: %s is %s, not about %s" % (name(mode), what, tuple(int(v) for v in got), want))


with tempfile.TemporaryDirectory() as tmp:
    # to bottom (the default), red to blue: rows of one colour
    for mode in MODES:
        rows = draw(tmp, box("background:linear-gradient(red, blue)", 64), mode, 64)
        expect(rows, mode, "to bottom's first rows", 0, 0, 200, 4, (255, 0, 0), 20, 40)
        expect(rows, mode, "to bottom's last rows", 0, 60, 200, 64, (0, 0, 255), 20, 40)
        expect(rows, mode, "to bottom's middle", 0, 30, 200, 34, (128, 0, 128), 12, 60)
        if mode == 0 and any(rows[10][x] != rows[10][0] and rows[10][x] != rows[10][1] and
                             rows[10][x] != rows[10][2] and rows[10][x] != rows[10][3] for x in range(200)):
            fail("true colour: a row of a gradient to bottom isn't one colour (a pattern of four)")

    # to right, a grey ramp; to left; to top; 90deg; to a corner
    for mode in MODES:
        rows = draw(tmp, box("background:linear-gradient(to right, #000, #fff)", 16) +
                    box("background:linear-gradient(to left, #000, #fff)", 16) +
                    box("background:linear-gradient(to top, #000, #fff)", 16) +
                    box("background:linear-gradient(90deg, #000, #fff)", 16), mode, 64)
        expect(rows, mode, "to right at a quarter", 48, 0, 52, 16, (64, 64, 64), 10, 30)
        expect(rows, mode, "to right at three quarters", 148, 0, 152, 16, (191, 191, 191), 10, 30)
        expect(rows, mode, "to left at a quarter", 48, 16, 52, 32, (191, 191, 191), 10, 30)
        expect(rows, mode, "to top's top", 0, 32, 200, 36, (240, 240, 240), 24, 40)
        expect(rows, mode, "to top's bottom", 0, 44, 200, 48, (16, 16, 16), 24, 40)
        expect(rows, mode, "90deg at a quarter", 48, 48, 52, 64, (64, 64, 64), 10, 30)
    rows = draw(tmp, box("background:linear-gradient(to bottom right, #000, #fff)", 100), 0, 100)
    # at right angles to the other diagonal: its corners are its ends,
    # and the other two corners are both half way
    for (x, y), want in (((1, 1), 0), ((198, 98), 255), ((198, 1), 128), ((1, 98), 128)):
        checks += 1
        if abs(rows[y][x][0] - want) > 12:
            fail("true colour: to bottom right at (%d, %d) is %d, not about %d" % (x, y, rows[y][x][0], want))
    rows = draw(tmp, box("background:linear-gradient(45deg, #000 50%, #fff 50%)", 200), 0, 200, 0)
    checks += 1
    if rows[150][40] != (0, 0, 0) or rows[40][150] != (255, 255, 255):
        fail("true colour: 45deg with a sharp edge has %s below it and %s above it" % (rows[150][40], rows[40][150]))

    # sharp edges where they are: at 50% of 200 px, at 30px and 2em
    for mode in (0, 16, 256, 2):
        rows = draw(tmp, box("background:linear-gradient(90deg, #000 50%, #fff 50%)", 10) +
                    box("background:linear-gradient(to right, #fff 30px, #000 30px)", 10) +
                    box("font-size:10px; background:linear-gradient(to right, #000 2em, #fff 2em calc(100% - 2em),"
                        " #000 calc(100% - 2em))", 10), mode, 30)
        for y, x, want in ((5, 99, (0, 0, 0)), (5, 100, (255, 255, 255)), (15, 29, (255, 255, 255)),
                           (15, 30, (0, 0, 0)), (25, 19, (0, 0, 0)), (25, 20, (255, 255, 255)),
                           (25, 179, (255, 255, 255)), (25, 180, (0, 0, 0))):
            checks += 1
            if rows[y][x] != want:
                fail("%s: a sharp edge's pixel (%d, %d) is %s, not %s" % (name(mode), x, y, rows[y][x], want))

    # a hint at 25% (CSS Images 4, 3.5.3): the half way grey there, and
    # past it the mix bent towards white
    rows = draw(tmp, box("background:linear-gradient(to right, #000, 25%, #fff)", 10), 0, 10)
    expect(rows, 0, "a hint's position", 48, 0, 52, 10, (128, 128, 128), 12)
    checks += 1
    if mean(rows, 98, 0, 102, 10)[0] < 170:
        fail("true colour: past a hint at 25%%, the middle is %d, not bent to white" % mean(rows, 98, 0, 102, 10)[0])

    # radial: the centre is the first colour, the corners the last; a
    # circle closest-side reaches the nearest side; one of no size is its
    # last colour
    for mode in (0, 16, 256, 2):
        rows = draw(tmp, box("background:radial-gradient(#fff, #000)", 100) +
                    box("background:radial-gradient(circle closest-side, #000 99%, #fff 100%)", 100) +
                    box("background:radial-gradient(closest-side at 0 50%, #fff, #000)", 20), mode, 220)
        expect(rows, mode, "a radial gradient's centre", 98, 48, 102, 52, (255, 255, 255), 16, 40)
        expect(rows, mode, "a radial gradient's corner", 0, 0, 4, 4, (0, 0, 0), 24, 40)
        if mode == 0:
            checks += 1
            # the circle: 50 px round its centre, (100, 150)
            if rows[150][52] != (0, 0, 0) or rows[150][47] != (255, 255, 255) or rows[102][100] != (0, 0, 0) \
                    or rows[100][100] != (255, 255, 255):
                fail("true colour: a closest-side circle's edge isn't 50 px from its centre: %s %s %s %s"
                     % (rows[150][52], rows[150][47], rows[102][100], rows[100][100]))
        expect(rows, mode, "a radial gradient of no size", 0, 200, 200, 220, (0, 0, 0), 4, 4)

    # repeating: every 20 px the same; a period of no length its average
    rows = draw(tmp, box("background:repeating-linear-gradient(to right, #f00 0 10px, #00f 10px 20px)", 10) +
                box("background:repeating-linear-gradient(red 10px, blue 10px)", 10) +
                box("background:repeating-radial-gradient(circle at 0 0, #000 0 5px, #fff 5px 10px)", 30), 0, 50)
    checks += 1
    if any(rows[5][x] != rows[5][x + 20] for x in range(150)) or rows[5][5] != (255, 0, 0) or \
            rows[5][15] != (0, 0, 255):
        fail("true colour: a repeating gradient's stripes aren't 10 px of red then of blue: %s" % rows[5][:24])
    expect(rows, 0, "a repeating gradient of no length", 0, 10, 200, 20, (128, 0, 128), 4)
    checks += 1
    if rows[20][2] != (0, 0, 0) or rows[20][7] != (255, 255, 255) or rows[20][12] != (0, 0, 0):
        fail("true colour: repeating rings at the corner are %s %s %s" % (rows[20][2], rows[20][7], rows[20][12]))

    # conic: a quarter red from the top, then green, then blue
    for mode in (0, 16, 256):
        rows = draw(tmp, box("background:conic-gradient(red 0 25%, lime 0 50%, blue 0)", 100), mode, 100)
        for what, x0, y0, want in (("top right", 140, 20, (255, 0, 0)), ("bottom right", 140, 70, (0, 255, 0)),
                                   ("bottom left", 40, 70, (0, 0, 255)), ("top left", 40, 20, (0, 0, 255))):
            expect(rows, mode, "a conic gradient's %s" % what, x0, y0, x0 + 8, y0 + 8, want, 2, 4)

    # translucent: half black over red; a box at opacity .5
    for mode in (0, 16, 256, 2):
        rows = draw(tmp, "<div style='background:#f00'>" +
                    box("background:linear-gradient(rgba(0,0,0,.5), rgba(0,0,0,.5))", 20) + "</div>" +
                    box("opacity:.5; background:linear-gradient(#00f, #00f)", 20), mode, 40)
        expect(rows, mode, "half black over red", 0, 0, 200, 20, (128, 0, 0), 10, 50)
        expect(rows, mode, "blue at opacity .5", 0, 20, 200, 40, (128, 128, 255), 10, 50)

    # background-size, -position, -repeat and -clip
    rows = draw(tmp, box("background:linear-gradient(#000, #000) center / 50% 50% no-repeat #fff", 40) +
                box("background:linear-gradient(#000 50%, #fff 50%) 0 0 / 100% 10px", 40) +
                box("border:10px solid transparent; background:linear-gradient(#000, #000) padding-box", 20) +
                box("border:10px solid transparent; background:linear-gradient(#000, #000)", 20), 0, 160)
    for (x, y), want in (((100, 20), (0, 0, 0)), ((10, 5), (255, 255, 255)), ((60, 15), (0, 0, 0)),
                         ((40, 15), (255, 255, 255)), ((5, 42), (0, 0, 0)), ((5, 47), (255, 255, 255)),
                         ((5, 72), (0, 0, 0)), ((5, 77), (255, 255, 255)), ((5, 85), (255, 255, 255)),
                         ((15, 95), (0, 0, 0)), ((5, 125), (0, 0, 0))):
        checks += 1
        if rows[y][x] != want:
            fail("true colour: background-size, -position, -repeat or -clip: (%d, %d) is %s, not %s"
                 % (x, y, rows[y][x], want))

    # a box of no height of its own stretched (a flex item, a table cell):
    # its gradient as tall as it is
    rows = draw(tmp, "<div style='display:flex; height:40px'><div style='width:50px; background:linear-gradient(#000,"
                " #fff)'></div><div style='width:50px; background:linear-gradient(#000, #fff) no-repeat'></div></div>"
                "<table style='border-spacing:0'><tr><td style='width:50px; padding:0; background:linear-gradient(#000,"
                " #fff)'></td><td style='height:40px; padding:0'>x</td></tr></table>", 0, 80)
    for x0, y0, what in ((0, 0, "a stretched flex item's"), (50, 0, "a stretched flex item's (no-repeat)"),
                         (0, 40, "a stretched table cell's")):
        expect(rows, 0, what + " top", x0, y0, x0 + 50, y0 + 2, (6, 6, 6), 10)
        expect(rows, 0, what + " foot", x0, y0 + 38, x0 + 50, y0 + 40, (249, 249, 249), 10)

    # the root's gradient over the whole page; an inline's, its average
    rows = draw(tmp, "<!DOCTYPE html><html style='background:linear-gradient(#000, #fff)'><body style='margin:0'>"
                "<p style='margin:0; height:100px'></p></body></html>", 0, 100, whole=True)
    expect(rows, 0, "the root's gradient at its top", 0, 0, 200, 2, (0, 0, 0), 8)
    expect(rows, 0, "the root's gradient at its foot", 0, 98, 200, 100, (255, 255, 255), 8)
    lst = subprocess.run([ewrender, "-f", font, "-w", "200", "-z", "1024", "-d",
                          page(tmp, "<p>a <span style='background:linear-gradient(red, blue)'>mark</span></p>")],
                         capture_output=True, text=True, timeout=60).stdout
    checks += 1
    if "#800080" not in lst:
        fail("an inline's gradient isn't its average colour:\n" + lst)

    # valid gradients whose stops can't be placed (a min() of px and %,
    # inside calc() too): their stops' average over the shorthand's
    # colour, never nothing; calc() angles worked out
    for mode in (0, 16, 2):
        rows = draw(tmp, box("background:#fff linear-gradient(red calc(100% - min(10px, 2em)), blue)", 20) +
                    box("background:#000 linear-gradient(rgba(0,0,0,0) min(10%, 5px), rgba(0,0,0,0))", 20) +
                    box("background:linear-gradient(calc(45deg * 2), #000, #fff)", 20) +
                    box("background:linear-gradient(max(10deg, 0.5turn), #000, #fff)", 20), mode, 80)
        expect(rows, mode, "an average of red and blue at its top", 0, 0, 200, 4, (128, 0, 128), 4, 60)
        expect(rows, mode, "an average of red and blue at its foot", 0, 16, 200, 20, (128, 0, 128), 4, 60)
        expect(rows, mode, "the shorthand's colour under a transparent average", 0, 20, 200, 40, (0, 0, 0), 4, 4)
        expect(rows, mode, "calc(45deg * 2)'s left", 0, 40, 8, 60, (0, 0, 0), 16, 40)
        expect(rows, mode, "calc(45deg * 2)'s right", 192, 40, 200, 60, (255, 255, 255), 16, 40)
        expect(rows, mode, "max(10deg, 0.5turn)'s top", 0, 60, 200, 63, (0, 0, 0), 30, 40)
    # -webkit-linear-gradient() from a side; -webkit-gradient()
    rows = draw(tmp, box("background:-webkit-linear-gradient(top, red, blue)", 20) +
                box("background:-webkit-linear-gradient(left, #000, #fff)", 20) +
                box("background:-webkit-gradient(linear, left top, left bottom, from(red), to(blue))", 20) +
                box("background:-webkit-gradient(linear, 0 0, 100% 0, color-stop(0.5, #fff), from(#000))", 20), 0, 80)
    expect(rows, 0, "-webkit-linear-gradient(top)'s top", 0, 0, 200, 2, (250, 0, 6), 12)
    expect(rows, 0, "-webkit-linear-gradient(top)'s foot", 0, 18, 200, 20, (6, 0, 250), 12)
    expect(rows, 0, "-webkit-linear-gradient(left)'s left", 0, 20, 4, 40, (0, 0, 0), 12)
    expect(rows, 0, "-webkit-linear-gradient(left)'s right", 196, 20, 200, 40, (255, 255, 255), 12)
    expect(rows, 0, "-webkit-gradient(linear, down)'s top", 0, 40, 200, 42, (250, 0, 6), 12)
    expect(rows, 0, "-webkit-gradient(linear, down)'s foot", 0, 58, 200, 60, (6, 0, 250), 12)
    expect(rows, 0, "-webkit-gradient(linear, across) at a quarter", 48, 60, 52, 80, (128, 128, 128), 12)
    expect(rows, 0, "-webkit-gradient(linear, across) past its middle", 120, 60, 200, 80, (255, 255, 255), 4)

    # a circle of no size (CSS Images 3, 3.2.3): as one a 256th of a px
    # round, its stops in px where they are; an ellipse of no width a
    # linear gradient across, mirrored
    rows = draw(tmp, "<div style='width:100px; height:100px; background:radial-gradient(circle closest-side at 0 0,"
                " red 20px, blue 40px)'></div>" +
                box("background:radial-gradient(closest-side at 50% 50%, #000 0, #fff 50px)", 1) +
                box("background:radial-gradient(closest-side at 0 50%, #000 0, #fff 100px)", 20), 0, 121)
    for (x, y), want in (((2, 2), (255, 0, 0)), ((2, 25), (198, 0, 74)), ((60, 60), (0, 0, 255))):
        checks += 1
        if not near(rows[y][x], want, 8):
            fail("true colour: a circle of no size at (%d, %d) is %s, not about %s" % (x, y, rows[y][x], want))
    expect(rows, 0, "an ellipse of no width, 50 px across", 48, 101, 52, 121, (128, 128, 128), 12)
    expect(rows, 0, "an ellipse of no width, at its centre", 0, 101, 2, 121, (0, 0, 0), 8)

    # the root's gradient on below a page shorter than the window: its
    # tiles (the page's height) go on down
    for mode in (0, 16):
        rows = draw(tmp, "<!DOCTYPE html><html style='background:linear-gradient(#000, #fff)'><body style='margin:0'>"
                    "<p style='margin:0; height:40px'></p></body></html>", mode, 120, whole=True)
        expect(rows, mode, "the root's gradient below the page, a tile on", 0, 40, 200, 42, (0, 0, 0), 16, 40)
        expect(rows, mode, "the root's gradient below the page, at the window's foot", 0, 117, 200, 120,
               (255, 255, 255), 16, 40)
        checks += 1
        if rows[85] != rows[45] or rows[110] != rows[30]:
            fail("%s: the root's gradient below the page doesn't repeat its tile" % name(mode))

    # small tiles: each tile alike (true colour: the 5-6-5 pattern comes
    # round every 4 px too), the dots where they are; solid, and
    # translucent over red
    for css, between in (("radial-gradient(#000 2px, #fff 3px) 1px 2px / 13px 11px", (255, 255, 255)),
                         ("radial-gradient(#000 2px, transparent 3px) 1px 2px / 13px 11px", (255, 0, 0)),
                         ("repeating-conic-gradient(#000 0 25%, transparent 0 50%) 1px 2px / 13px 11px", None)):
        for mode in MODES:
            rows = draw(tmp, "<div style='background:red'>" + box("background:" + css, 120) + "</div>", mode, 120)
            checks += 1
            if any(rows[y][x] != rows[y + 44][x + 52] for y in range(0, 44) for x in range(0, 148)):
                fail("%s: tiles of 13 x 11 (%s) aren't each alike" % (name(mode), css))
            if between and mode == 0:
                checks += 1
                if rows[7][7] != (0, 0, 0) or rows[7 + 33][7 + 26] != (0, 0, 0) or rows[2][2] != between:
                    fail("true colour: dots of %s aren't where they should be: %s %s %s"
                         % (css, rows[7][7], rows[7 + 33][7 + 26], rows[2][2]))

    # translucent fades over red: down (a row one colour), across
    for mode in (0, 16, 256, 2):
        rows = draw(tmp, "<div style='background:#f00'>" +
                    box("background:linear-gradient(rgba(0,0,0,0), #000)", 40) +
                    box("background:linear-gradient(to right, rgba(0,0,0,0), #000)", 20) + "</div>", mode, 60)
        expect(rows, mode, "a fade down's top", 0, 0, 200, 2, (250, 0, 0), 12, 40)
        expect(rows, mode, "a fade down's middle", 0, 18, 200, 22, (128, 0, 0), 12, 60)
        expect(rows, mode, "a fade down's foot", 0, 38, 200, 40, (6, 0, 0), 12, 40)
        expect(rows, mode, "a fade across at a quarter", 48, 40, 52, 60, (191, 0, 0), 12, 60)
        expect(rows, mode, "a fade across's right", 196, 40, 200, 60, (0, 0, 0), 12, 40)

    # a faint glow over a dark page (atarimania.com's: 8% teal over navy)
    # mixed with what is under it: with a palette no pixel the glow's own
    # pen, which drew it as a grid of bright dots (Erez's Falcon in 256
    # colours, 27 Oct 2026)
    for mode in (0, 4, 16, 256):
        rows = draw(tmp, box("background:#12142b radial-gradient(circle at 15% 0, rgba(95,168,160,.08), "
                             "transparent 40%)", 60), mode, 60)
        checks += 1
        hi = max(max(p) for r in rows[0:60] for p in r[0:200])
        if hi > 90:
            fail("%s: a faint glow over a dark page has a pixel as bright as %d" % (name(mode), hi))

    # drawn in slices as the window draws them, the same as whole
    slices = (box("background:linear-gradient(to right, #036, #fc0)", 30) +
              box("background:linear-gradient(170deg, #036, #fc0, #fff)", 30) +
              box("background:radial-gradient(circle at 30% 40%, #fff, #c00 40%, rgba(0,0,0,.3))", 30) +
              box("background:repeating-linear-gradient(-45deg, #fc0 0 6px, #333 6px 12px)", 30) +
              box("background:conic-gradient(from 30deg, red, lime, blue, red)", 30) +
              box("background:linear-gradient(#000 50%, #fff 50%) 0 0 / 7px 6px", 30) +
              box("background:#c33 radial-gradient(rgba(0,0,0,.6) 2px, transparent 3px) 3px 1px / 9px 7px", 30) +
              box("background:#c33 linear-gradient(to right, rgba(0,0,0,.1), #fff8) 0 0 / 11px 5px", 30))
    for mode in MODES:
        whole_rows = draw(tmp, slices, mode, 240)
        for top in range(0, 240, 13):
            part = draw(tmp, slices, mode, 13, top)
            n = min(13, 240 - top)
            checks += 1
            if part[:n] != whole_rows[top:top + n]:
                fail("%s: rows from %d drawn alone differ from the page drawn whole" % (name(mode), top))
                break

    # huge lengths (kept within the fixed point: tests/css/cases.txt)
    # drawn with no undefined behaviour (draw stops at UBSan's report)
    huge = (box("background:linear-gradient(red 1e9vw, blue)", 10) +
            box("background:radial-gradient(1e9vw 1e9vh at 1e9vmax -1e9vmin, red, blue)", 10) +
            box("background:linear-gradient(to right, red calc(-1e9vw - 1e9px), blue 1e9vh)", 10) +
            box("background:repeating-linear-gradient(red 0 1e9vw, blue 1e9vw 2e9vw)", 10))
    for exe, modes in ((ewrender, MODES),) + (((ewrenderst, ST_MODES),) if ewrenderst else ()):
        for mode in modes:
            draw(tmp, huge, mode, 40, exe=exe)
            checks += 1

# the test page in bands, at 8 and 16 bits (each into a buffer of its size)
for exe in (ewrender, ewrenderst) if ewrenderst else (ewrender,):
    p = subprocess.run([exe, "-f", font, "-B", os.path.join(HERE, "gradients.html")], capture_output=True, text=True,
                       timeout=300)
    if p.returncode != 0 or "bands:" not in p.stdout:
        fail("gradients.html in bands failed (%s)\n" % os.path.basename(os.path.dirname(exe)) + p.stderr[-2000:])

# the ST edition's drawing (EWEBST.PRG: src/gfx/canvas.c's EW_ST grad):
# a gradient to a side or at right angles a row or a column at a time,
# each in its colour as a fill is drawn (the grey pattern in 2 colours,
# the nearest pen in 4 and 16); any other its stops' average
st_checks = 0
if ewrenderst:
    checks0 = checks
    with tempfile.TemporaryDirectory() as tmp:
        for mode in ST_MODES:
            rows = draw(tmp, box("background:linear-gradient(red, blue)", 64), mode, 64, exe=ewrenderst)
            expect(rows, mode, "ST: to bottom's first rows", 0, 0, 200, 4, (255, 0, 0), 12, 40)
            expect(rows, mode, "ST: to bottom's last rows", 0, 60, 200, 64, (0, 0, 255), 12, 40)
            expect(rows, mode, "ST: to bottom's middle", 0, 30, 200, 34, (128, 0, 128), 12, 90)
            checks += 1
            if any(len(set(r)) != 1 for r in rows) and mode != 2:
                fail("ST %s: a row of a gradient to bottom isn't one colour" % name(mode))
            # -webkit-gradient(), the ST edition's too since its 1 MB ST
            # was dropped: drawn as the same gradient to bottom
            wk = draw(tmp, box("background:-webkit-gradient(linear, left top, left bottom, from(red), to(blue))",
                               64), mode, 64, exe=ewrenderst)
            checks += 1
            if wk != rows:
                fail("ST %s: -webkit-gradient(linear, down) isn't drawn as linear-gradient(red, blue)" % name(mode))
            # a grey ramp each way, at a tenth and nine tenths of it (in 4
            # colours: black and white, not the average's)
            rows = draw(tmp, box("background:linear-gradient(to right, #000, #fff)", 10) +
                        box("background:linear-gradient(270deg, #000, #fff)", 10) +
                        box("background:linear-gradient(to top, #000, #fff)", 40) +
                        box("background:linear-gradient(-180deg, #000, #fff)", 40), mode, 100, exe=ewrenderst)
            for what, x0, y0, x1, y1, want in (("to right at a tenth", 18, 0, 22, 10, 25),
                                               ("to right at nine tenths", 178, 0, 182, 10, 230),
                                               ("270deg at a tenth", 18, 10, 22, 20, 230),
                                               ("270deg at nine tenths", 178, 10, 182, 20, 25),
                                               ("to top at a tenth", 0, 22, 200, 26, 230),
                                               ("to top at nine tenths", 0, 54, 200, 58, 25),
                                               ("-180deg at a tenth", 0, 62, 200, 66, 25),
                                               ("-180deg at nine tenths", 0, 94, 200, 98, 230)):
                expect(rows, mode, "ST: " + what, x0, y0, x1, y1, (want, want, want), 12, 40)
            # stripes: repeating across, and tiles down
            rows = draw(tmp, box("background:repeating-linear-gradient(to right, #000 0 10px, #fff 10px 20px)", 10) +
                        box("background:linear-gradient(#000 50%, #fff 50%) 0 0 / 100% 10px", 20), mode, 30,
                        exe=ewrenderst)
            for what, x, y, want in (("a stripe across", 25, 5, (0, 0, 0)), ("the next stripe across", 35, 5,
                                     (255, 255, 255)), ("a stripe down", 5, 13, (0, 0, 0)),
                                     ("the next stripe down", 5, 18, (255, 255, 255)),
                                     ("a tile's stripe down", 5, 23, (0, 0, 0))):
                checks += 1
                if rows[y][x] != want:
                    fail("ST %s: %s (%d, %d) is %s, not %s" % (name(mode), what, x, y, rows[y][x], want))
            # any other: its stops' average, one colour all over (and a
            # hint mixes evenly); one whose stops can't be placed
            rows = draw(tmp, box("background:linear-gradient(45deg, #000, #fff)", 20) +
                        box("background:radial-gradient(#000, #fff)", 20) +
                        box("background:conic-gradient(#000, #fff)", 20) +
                        box("background:linear-gradient(to right, #000, 25%, #fff)", 20) +
                        box("background:#000 linear-gradient(red min(10%, 5px), blue)", 20) +
                        box("background:#000 linear-gradient(red calc(100% - min(10px, 2em)), blue)", 20), mode,
                        120, exe=ewrenderst)
            for k, what in enumerate(("45deg", "radial", "conic")):
                # (with a palette, the average's nearest pen)
                if mode in (0, 2):
                    expect(rows, mode, "ST: a %s gradient's average" % what, 0, 20 * k, 200, 20 * k + 20,
                           (128, 128, 128), 4, 40)
                if mode != 2:
                    checks += 1
                    if len(set(p for r in rows[20 * k:20 * k + 20] for p in r)) != 1:
                        fail("ST %s: a %s gradient isn't one colour all over" % (name(mode), what))
            expect(rows, mode, "ST: a hint's middle, mixed evenly", 98, 60, 102, 80, (128, 128, 128), 12, 40)
            expect(rows, mode, "ST: a gradient whose stops can't be placed", 0, 80, 200, 100, (128, 0, 128), 4, 90)
            # (the ST edition works out no min() inside calc(): such a
            # gradient is its average, not invalid with its colour)
            expect(rows, mode, "ST: a gradient with min() in calc()", 0, 100, 200, 120, (128, 0, 128), 4, 90)
            # the root's gradient on below a short page
            rows = draw(tmp, "<!DOCTYPE html><html style='background:linear-gradient(#000, #fff)'><body "
                        "style='margin:0'><p style='margin:0; height:40px'></p></body></html>", mode, 120, whole=True,
                        exe=ewrenderst)
            checks += 1
            if rows[85] != rows[45] or rows[110] != rows[30] or light(rows[117][0]) < 180:
                fail("ST %s: the root's gradient doesn't go on below the page" % name(mode))
        slices = (box("background:linear-gradient(to right, #036, #fc0)", 30) +
                  box("background:linear-gradient(#036, #fc0, #fff)", 30) +
                  box("background:repeating-linear-gradient(#fc0 0 6px, #333 6px 12px)", 30) +
                  box("background:radial-gradient(circle at 30% 40%, #fff, #c00 40%, rgba(0,0,0,.3))", 30) +
                  box("background:#c33 linear-gradient(rgba(0,0,0,.6), transparent) 3px 1px / 9px 7px", 30))
        for mode in ST_MODES:
            whole_rows = draw(tmp, slices, mode, 150, exe=ewrenderst)
            for top in range(0, 150, 13):
                part = draw(tmp, slices, mode, 13, top, exe=ewrenderst)
                n = min(13, 150 - top)
                checks += 1
                if part[:n] != whole_rows[top:top + n]:
                    fail("ST %s: rows from %d drawn alone differ from the page drawn whole" % (name(mode), top))
                    break
        # tall gentle gradients, many rows to each colour the screen
        # shows, and a ramp, drawn in bands of 32 rows from anywhere (as
        # a small machine draws its window, and a scroll the rows it shows)
        # the same as whole: rows alike are drawn in a colour of theirs,
        # not the colour of the first of them in the band
        tall = (box("background:linear-gradient(#2c3e50, #4ca1af)", 120) +
                box("background:linear-gradient(#f5f7fa, #c3cfe2)", 300) +
                box("background:linear-gradient(#000, #fff)", 200) +
                box("background:#c33 linear-gradient(rgba(0,0,0,.1), rgba(0,0,0,.6))", 100))
        for mode in ST_MODES:
            whole_rows = draw(tmp, tall, mode, 720, exe=ewrenderst)
            for top in range(7, 720, 32):
                part = draw(tmp, tall, mode, 32, top, exe=ewrenderst)
                n = min(32, 720 - top)
                checks += 1
                if part[:n] != whole_rows[top:top + n]:
                    fail("ST %s: tall gradients' rows from %d drawn as a band differ from the page drawn whole"
                         % (name(mode), top))
                    break
    st_checks = checks - checks0

print("gradients: %s" % ("%d checks of linear, radial, conic and repeating gradients as they should be in 5 modes, "
                         "in slices and in bands%s" % (checks - st_checks, ", and %d of the ST edition's in 4 modes"
                                                       % st_checks if st_checks else "")
                         if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
