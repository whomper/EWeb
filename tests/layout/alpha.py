#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of translucency (CSS Color 4's alpha and opacity) as
drawn: small pages through ewrender in true colour and at 8 bits in 2, 4,
16 and 256 colours (-p), their pixels read back from the PNG.

- opacity 0 draws nothing at all (its link is still in the list);
- a fill of half black on white, half white on black and blue at opacity
  .5 come out half way (true colour), a grey (16 and 256 colours), a
  pattern of about half the pixels (monochrome);
- text of half alpha is no darker than half, transparent text (and its
  underline) isn't drawn, and text under a translucent cover shows
  through it;
- a picture at opacity .5 is the opaque one mixed half with white (true
  colour), or about half of its pixels (8 bits: an ordered pattern);
- text of a transparent colour the page shows another way (a gradient
  through it, its shadow, its outline) is drawn in that colour, the
  gradient's or the outline's over a faint shadow's, a gradient EWeb
  can't compute in the text's own colour, solid; one a picture replaces
  isn't;
- a checkbox's tick is drawn whatever the text's colour; one of opacity
  0 is drawn (a custom control EWeb can't draw would leave none), one in
  a group of opacity 0 isn't;
- a translucent root background is one colour all down the window (the
  canvas's, painted once);
- a click on a link under a veil of opacity 0 and pointer-events: none
  follows that link, not the veil's (-J -K);
- tests/layout/alpha.html drawn in bands at 8 and 16 bits (-B), under
  AddressSanitizer in make test.

    tests/layout/alpha.py EWRENDER EWEB.FNT"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ewrender, font = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
MODES = (0, 2, 4, 16, 256)	# 0: true colour
fails = 0


def fail(what):
    global fails
    fails += 1
    print("alpha: " + what)


def png(path):
    """the PNG ewrender wrote (8-bit RGB, unfiltered): width, height, rows of (r, g, b)"""
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
    rows = []
    for y in range(h):
        r = raw[y * (1 + 3 * w) + 1:(y + 1) * (1 + 3 * w)]
        rows.append([tuple(r[3 * x:3 * x + 3]) for x in range(w)])
    return w, h, rows


def draw(tmp, html, mode, height=80, whole=False):
    page = os.path.join(tmp, "page.html")
    out = os.path.join(tmp, "out.png")
    with open(page, "w") as f:
        f.write(html if whole else "<!DOCTYPE html><html><body style='margin:0; background:#fff; "
                "font-family:sans-serif'>" + html + "</body></html>")
    args = [ewrender, "-f", font, "-w", "200", "-z", "1024", "-H", str(height), "-o", out]
    if mode:
        args += ["-p", str(mode)]
    p = subprocess.run(args + [page], capture_output=True, text=True, timeout=60)
    if p.returncode != 0:
        sys.exit("alpha: ewrender failed\n" + p.stderr)
    return png(out)[2]


def dump(tmp, html):
    page = os.path.join(tmp, "page.html")
    with open(page, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0'>" + html + "</body></html>")
    p = subprocess.run([ewrender, "-f", font, "-w", "200", "-z", "1024", "-d", page], capture_output=True, text=True, timeout=60)
    return p.stdout


def area(rows, x0, y0, x1, y1):
    return [rows[y][x] for y in range(y0, y1) for x in range(x0, x1)]


def grey(p):
    return p[0] == p[1] == p[2]


def name(mode):
    return "%d colours" % mode if mode else "true colour"


with tempfile.TemporaryDirectory() as tmp:
    # opacity 0: nothing drawn, in every mode; its text and link kept
    gone = ("<div style='height:30px; background:#c00; border:3px solid #000; color:#000; opacity:0'>"
            "Gone <a href='#x'>link</a> <input value=x> <input type=checkbox checked></div>")
    for mode in MODES:
        rows = draw(tmp, gone, mode, 40)
        if any(p != (255, 255, 255) for p in area(rows, 0, 0, 200, 40)):
            fail("%s: opacity 0 drew something" % name(mode))
    lst = dump(tmp, gone)
    if "link 1 \"link\"" not in lst or "#0000ee00" not in lst:
        fail("opacity 0: its link's text isn't in the list, unseen:\n" + lst)

    # fills: half black on white, half white on black, blue at opacity .5
    fills = ("<div style='height:20px; background:rgba(0,0,0,.5)'></div>"
             "<div style='height:20px; background:#000'><div style='height:20px; background:#ffffff80'></div></div>"
             "<div style='height:20px; background:#00f; opacity:.5'></div>")
    for mode in MODES:
        rows = draw(tmp, fills, mode, 60)
        a, b, c = rows[10][10], rows[30][10], rows[50][10]
        if mode == 0:
            for got, want in ((a, (128, 128, 128)), (b, (128, 128, 128)), (c, (128, 128, 255))):
                if any(abs(g - w) > 8 for g, w in zip(got, want)):
                    fail("true colour: a fill mixed to %s, not about %s" % (got, want))
        elif mode in (16, 256):
            for got in (a, b):
                if not grey(got) or not 60 < got[0] < 200:
                    fail("%s: half black on white or half white on black is %s, not a grey" % (name(mode), got))
        elif mode == 2:
            for y0 in (0, 20):
                dark = sum(p == (0, 0, 0) for p in area(rows, 0, y0, 200, y0 + 20)) / 4000.0
                if not 0.35 < dark < 0.65:
                    fail("monochrome: a half grey fill is %d%% black, not about half" % (dark * 100))

    # text: half alpha, transparent (underlined), and under a cover of half white
    text = ("<div style='font-size:24px; height:30px; color:rgba(0,0,0,.5)'>MMMM</div>"
            "<div style='font-size:24px; height:30px; color:transparent; text-decoration:underline'>MMMM</div>"
            "<div style='font-size:24px; height:30px; position:relative'>MMMM"
            "<div style='position:absolute; left:0; top:0; width:200px; height:30px; background:rgba(255,255,255,.5)'>"
            "</div></div>")
    for mode in MODES:
        rows = draw(tmp, text, mode, 90)
        half, clear, under = area(rows, 0, 0, 200, 30), area(rows, 0, 30, 200, 60), area(rows, 0, 60, 200, 90)
        if any(p != (255, 255, 255) for p in clear):
            fail("%s: transparent text drew something" % name(mode))
        if mode == 0:
            for what, px in (("half alpha text", half), ("text under a half white cover", under)):
                darkest = min(min(p) for p in px)
                if not 110 <= darkest <= 145:
                    fail("true colour: %s is at darkest %d, not about 128" % (what, darkest))
        elif mode in (16, 256):
            for what, px in (("half alpha text", half), ("text under a half white cover", under)):
                if all(p == (255, 255, 255) for p in px) or any(p == (0, 0, 0) for p in px):
                    fail("%s: %s isn't drawn grey" % (name(mode), what))

    # a picture at opacity .5 against the same opaque
    one = "file://" + os.path.join(HERE, "img", "one.png")
    pics = ("<img src='%s' style='display:block'><img src='%s' style='display:block; opacity:.5'>" % (one, one))
    for mode in (0, 16, 256):
        rows = draw(tmp, pics, mode, 60)
        full, half = area(rows, 0, 0, 60, 30), area(rows, 0, 30, 60, 60)
        if mode == 0:
            off = max(abs((f + 255) / 2 - h) for p, q in zip(full, half) for f, h in zip(p, q))
            if off > 8:
                fail("true colour: a picture at opacity .5 is %d off half way to white" % off)
        else:
            kept = sum(p == q for p, q in zip(full, half) if p != (255, 255, 255))
            shown = sum(p != (255, 255, 255) for p in full)
            if shown == 0 or not 0.35 < kept / float(shown) < 0.65:
                fail("%s: a picture at opacity .5 kept %d of %d pixels" % (name(mode), kept, shown))

    # text of a transparent colour: a gradient through it (its first
    # colour), its shadow, its outline; not one a picture replaces. The
    # gradient or the outline rather than a faint shadow it also has (one
    # inherited here); a gradient EWeb can't compute (a var() of nothing,
    # as Tailwind 4's without @property) in the text's own colour, solid
    shown = ("<div style='font-size:24px; height:30px; background:linear-gradient(to right, transparent, #f00, #00f);"
             " -webkit-background-clip:text; background-clip:text; color:transparent'>MMMM</div>"
             "<div style='font-size:24px; height:30px; color:transparent; text-shadow:0 0 0 #00f'>MMMM</div>"
             "<div style='font-size:24px; height:30px; color:transparent; -webkit-text-stroke:1px #f00'>MMMM</div>"
             "<div style='font-size:24px; height:30px'><span style='color:transparent;"
             " background:url(no.png)'>MMMM</span></div>"
             "<div style='font-size:24px; height:30px; text-shadow:0 2px 4px rgba(0,0,0,.1)'><span style='background:"
             "linear-gradient(90deg, #00f, #f00); -webkit-background-clip:text; color:transparent'>MMMM</span></div>"
             "<div style='font-size:24px; height:30px; background-image:linear-gradient(to right, var(--from), var(--to));"
             " -webkit-background-clip:text; background-clip:text; color:transparent'>MMMM</div>"
             "<div style='font-size:24px; height:30px; color:transparent; -webkit-text-stroke:1px #00f;"
             " text-shadow:0 0 30px rgba(255,255,255,.15)'>MMMM</div>")
    for mode in (0, 16, 256):
        rows = draw(tmp, shown, mode, 210)
        for y0, want in ((0, (255, 0, 0)), (30, (0, 0, 255)), (60, (255, 0, 0)), (120, (0, 0, 255)),
                         (150, (0, 0, 0)), (180, (0, 0, 255))):
            if want not in area(rows, 0, y0, 200, y0 + 30):
                fail("%s: text of a transparent colour shown another way (at %d) isn't drawn in %s"
                     % (name(mode), y0, want))
        if any(p != (255, 255, 255) for p in area(rows, 0, 90, 200, 120)):
            fail("%s: text a picture replaces is drawn" % name(mode))

    # a checkbox's tick, whatever the text's colour; at opacity 0 its own
    # (a custom control's), not a group's
    boxes = ("<div style='height:30px; color:transparent'><input type=checkbox checked></div>"
             "<div style='height:30px'><input type=checkbox checked style='opacity:0'></div>"
             "<div style='height:30px; opacity:0'><input type=checkbox checked></div>")
    for mode in MODES:
        rows = draw(tmp, boxes, mode, 90)
        for y0, what in ((0, "in a transparent colour"), (30, "of opacity 0")):
            if not any(p[0] < 100 for p in area(rows, 3, y0 + 3, 16, y0 + 18)):
                fail("%s: a checkbox's tick %s isn't drawn" % (name(mode), what))
        if any(p != (255, 255, 255) for p in area(rows, 0, 60, 200, 90)):
            fail("%s: a checkbox in a group of opacity 0 is drawn" % name(mode))

    # a translucent root background: the canvas's, one colour all down
    rows = draw(tmp, "<!DOCTYPE html><html style='background:rgba(255,0,0,.5)'><body style='margin:0'>"
                "<p style='margin:0; height:20px'></p></body></html>", 0, 80, whole=True)
    if rows[10][100] != rows[70][100] or any(abs(a - b) > 8 for a, b in zip(rows[10][100], (255, 128, 128))):
        fail("true colour: a translucent root background is %s in the root's box and %s below it, not (255, 128, 128)"
             % (rows[10][100], rows[70][100]))

    # a click through a veil of opacity 0 and pointer-events: none
    page = os.path.join(tmp, "veil.html")
    with open(page, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0'><a id=under href='#under'>Under</a>"
                "<div style='position:absolute; left:0; top:0; width:200px; height:40px; background:#fff;"
                " opacity:0; pointer-events:none'><a href='#veil' style='display:block; height:30px'>Veil</a>"
                "<input name=q></div></body></html>")
    p = subprocess.run([ewrender, "-f", font, "-J", "-K", "under", "-d", page], capture_output=True, text=True,
                       timeout=60)
    if "followed:" not in p.stdout or "#under" not in p.stdout.split("followed:", 1)[1].split("\n", 1)[0]:
        fail("a click under a veil of pointer-events: none didn't follow the link under it:\n" + p.stdout[-800:])

# the test page in bands, at 8 and 16 bits (each into a buffer of its size)
p = subprocess.run([ewrender, "-f", font, "-B", os.path.join(HERE, "alpha.html")], capture_output=True, text=True,
                   timeout=300)
if p.returncode != 0 or "bands:" not in p.stdout:
    fail("alpha.html in bands failed\n" + p.stderr[-2000:])

print("alpha: %s" % ("opacity 0, fills, text, pictures, controls and clicks as they should be in 5 modes, and in bands"
                     if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
