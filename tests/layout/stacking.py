#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the order things are drawn in (CSS 2.1, Appendix
E): positioned boxes over what isn't, by z-index, as drawn and as
clicked (tests/layout/stacking.html's list has the order, "drawn:").

- demozoo.org's login menu (position: absolute, z-index 10) is drawn
  over the column that follows it, its background where the column's red
  heading would show (Erez's photo, 24 Oct 2026), in the Falcon's and
  the ST edition's builds;
- of two boxes, the one of the higher z-index is drawn over the other,
  whatever their order; a positioned box with no z-index over a block
  that follows it; a box with z-index 2 over a context of z-index 1 and
  the box of z-index 100 in it; a box of z-index below 0 under the
  block that follows;
- a click where a link drawn over another covers it follows the one
  drawn over (-J -K);
- a click on a link laid out as a block, beside its words, follows it
  (its box is the link's, as in a browser), and on its padding too.

    tests/layout/stacking.py EWRENDER EWEB.FNT [ST-EWRENDER]"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
font = os.path.abspath(sys.argv[2])
fails = 0


def fail(what):
    global fails
    fails += 1
    print("stacking: " + what)


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
    return [[tuple(raw[y * (1 + 3 * w) + 1 + 3 * x:y * (1 + 3 * w) + 4 + 3 * x]) for x in range(w)] for y in range(h)]


def items(ewrender, page):
    """the page's list (-d): fill and text lines, by their colour or words"""
    p = subprocess.run([ewrender, "-f", font, "-n", "-d", page], capture_output=True, text=True, timeout=60)
    return p.stdout.splitlines()


def fills(lines, colour):
    """the fills of that colour (#rrggbb): x, y, w, h each"""
    got = []
    for line in lines:
        f = line.split()
        if len(f) == 4 and f[0] == "fill" and f[3] == colour:
            x, y = f[1].split(",")
            w, h = f[2].split("x")
            got.append((int(x), int(y), int(w), int(h)))
    return got


def where(lines, colour):
    """the first fill of that colour, or None"""
    f = fills(lines, colour)
    return f[0] if f else None


page = os.path.join(HERE, "stacking.html")
with tempfile.TemporaryDirectory() as tmp:
    for ewrender in [os.path.abspath(e) for e in sys.argv[1:2] + sys.argv[3:4]]:
        name = os.path.basename(os.path.dirname(ewrender))
        out = os.path.join(tmp, "out.png")
        r = subprocess.run([ewrender, "-f", font, "-n", "-o", out, page], capture_output=True, text=True, timeout=60)
        if r.returncode != 0:
            fail("%s: ewrender failed\n%s" % (name, r.stderr))
            continue
        rows = png(out)
        lines = items(ewrender, page)

        def at(x, y):
            return rows[y][x]

        def near(got, want):
            """(the PNG is in the Falcon's true colour, 5, 6 and 5 bits)"""
            return all(abs(a - b) <= 8 for a, b in zip(got, want))

        # the menu's body (the tallest of its grey) over the column's red
        # heading (the one on the right: the logo is red too)
        menu = max(fills(lines, "#e8e8e8"), key=lambda r: r[3], default=None)
        head = ([r for r in fills(lines, "#cc0033") if r[0] > 300] or [None])[0]
        if menu is None or head is None:
            fail("%s: the menu or the heading isn't in the list" % name)
        else:
            x, y = max(menu[0], head[0]) + 2, max(menu[1], head[1]) + 2
            if not (x < menu[0] + menu[2] and y < menu[1] + menu[3] and x < head[0] + head[2] and
                    y < head[1] + head[3]):
                fail("%s: the menu %s doesn't cover the heading %s" % (name, menu, head))
            elif not near(at(x, y), (0xe8, 0xe8, 0xe8)):
                fail("%s: at %d,%d the menu (z-index 10) isn't over the column's heading: %s" % (name, x, y, at(x, y)))
        # z-index 2 over 1, in that order in the document
        a, b = where(lines, "#ff8800"), where(lines, "#0088ff")
        if a is None or b is None or not b[0] < a[0] + a[2]:
            fail("%s: the boxes of z-index 2 and 1 aren't side by side, overlapping" % name)
        else:
            if not near(at(b[0] + 2, b[1] + b[3] // 2), (0xff, 0x88, 0x00)):
                fail("%s: z-index 2 isn't over z-index 1: %s" % (name, at(b[0] + 2, b[1] + b[3] // 2)))
        # position: relative over the block pulled up over it
        rel, pulled = where(lines, "#88cc88"), where(lines, "#cc88cc")
        if rel is None or pulled is None or not near(at(pulled[0] + 100, pulled[1] + 2), (0x88, 0xcc, 0x88)):
            fail("%s: a positioned box isn't over the block that follows it" % name)
        # z-index 2 over a context of 1 and its 100
        hi, over = where(lines, "#ff0000"), where(lines, "#0000ff")
        if hi is None or over is None or not near(at(over[0] + 2, over[1] + 2), (0, 0, 0xff)):
            fail("%s: z-index 2 isn't over the context of z-index 1 and its z-index 100" % name)
        # below 0: under the block that follows
        under, nxt = where(lines, "#ffff00"), where(lines, "#00cccc")
        if under is None or nxt is None or not near(at(nxt[0] + 100, nxt[1] + 2), (0, 0xcc, 0xcc)):
            fail("%s: z-index below 0 isn't under the block that follows it" % name)

    # a click where a link of a higher z-index covers another: that one
    click = os.path.join(tmp, "click.html")
    with open(click, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0'>"
                "<a id=top href='#top' style='position:absolute; left:0; top:0; z-index:2'>TOP TOP TOP TOP</a>"
                "<a id=low href='#low' style='position:absolute; left:0; top:0; z-index:1'>low</a></body></html>")
    p = subprocess.run([os.path.abspath(sys.argv[1]), "-f", font, "-J", "-K", "low", "-d", click],
                       capture_output=True, text=True, timeout=60)
    got = p.stdout.split("followed:", 1)[1].split("\n", 1)[0] if "followed:" in p.stdout else ""
    if "#top" not in got:
        fail("a click on a link covered by one of a higher z-index didn't follow that one:\n" +
             p.stdout[-800:])

    # a link as a block: a click in its box, beside its word
    card = os.path.join(tmp, "card.html")
    with open(card, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0'><a id=card href='#card' style='display:block;"
                "padding:20px;width:300px;background:#eef'>Card</a></body></html>")
    p = subprocess.run([os.path.abspath(sys.argv[1]), "-f", font, "-J", "-K", "card", "-d", card],
                       capture_output=True, text=True, timeout=60)
    got = p.stdout.split("followed:", 1)[1].split("\n", 1)[0] if "followed:" in p.stdout else ""
    if "#card" not in got:
        fail("a click in a block link's box, beside its word, didn't follow it:\n" + p.stdout[-800:])

if fails:
    sys.exit(1)
print("stacking: all passed")
