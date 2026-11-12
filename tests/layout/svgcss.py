#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of an inline <svg> coloured and sized by its page
(CSS Fill and Stroke 3; SVG 2, 5.1.1 and 6.4): small pages through
ewrender in true colour, their pixels read back from the PNG.

- a page rule's fill on a class of the <svg> (inherited by its paths), on
  a path's class and through a descendant selector from outside the
  picture (".bar svg path"), its stroke and stroke-width;
- the more specific rule wins, and the later of two alike; the path's
  own style attribute wins over both; a rule beats a presentation
  attribute (fill="red");
- fill: currentColor from a page rule takes the <svg>'s CSS colour;
- what is meant for the box (display: none on a class the picture's
  paths share, width) isn't the picture's;
- a rule inside @media that doesn't hold is left out;
- width="100%" fills the box (its height by its viewBox), width="2em" is
  its font's;
- under UBSan and AddressSanitizer (make test's ewrender), no undefined
  behaviour.

    tests/layout/svgcss.py EWRENDER EWEB.FNT"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ewrender, font = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
fails = 0
checks = 0

RED, GREEN, BLUE, WHITE = (255, 0, 0), (0, 128, 0), (0, 0, 255), (255, 255, 255)
SQ = "<svg %s viewBox='0 0 10 10' width='40' height='40'><rect %s x='0' y='0' width='10' height='10'/></svg>"


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


def draw(tmp, body, width=200, height=80):
    path = os.path.join(tmp, "page.html")
    with open(path, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0; background:#fff'>" + body + "</body></html>")
    out = os.path.join(tmp, "out.png")
    p = subprocess.run([ewrender, "-f", font, "-w", str(width), "-z", "1024", "-H", str(height), "-o", out, path],
                       capture_output=True, text=True, timeout=60)
    if p.returncode != 0 or "runtime error:" in p.stderr or "ERROR: AddressSanitizer" in p.stderr:
        sys.exit("svgcss: ewrender failed\n" + p.stderr)
    return png(out)


def mean(rows, x0, y0, x1, y1):
    px = [rows[y][x] for y in range(y0, y1) for x in range(x0, x1)]
    return tuple(sum(p[k] for p in px) / float(len(px)) for k in range(3))


def check(what, ok, got=""):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("svgcss: %s%s" % (what, (": " + got) if got else ""))


def near(got, want, tol=12):
    return all(abs(g - v) <= tol for g, v in zip(got, want))


with tempfile.TemporaryDirectory() as tmp:
    def case(what, css, body, want, x=20, y=20, r=4):
        rows = draw(tmp, "<style>%s</style>%s" % (css, body))
        got = mean(rows, x - r, y - r, x + r, y + r)
        check(what, near(got, want), "%s, not %s" % (tuple(int(v) for v in got), want))

    case("fill on the svg's class, inherited", ".ic { fill: blue }", SQ % ("class=ic", ""), BLUE)
    case("fill on the path's class", ".sq { fill: green }", SQ % ("", "class=sq"), GREEN)
    case("a descendant selector from outside the picture", ".bar svg rect { fill: blue }",
         "<div class=bar>" + SQ % ("", "") + "</div>", BLUE)
    case("the descendant selector not met: its default black", ".bar svg rect { fill: blue }",
         "<div class=other>" + SQ % ("", "") + "</div>", (0, 0, 0))
    case("the more specific rule wins", "svg .sq { fill: green } .sq { fill: red }", SQ % ("", "class=sq"), GREEN)
    case("the later of two alike wins", ".sq { fill: red } .sq { fill: blue }", SQ % ("", "class=sq"), BLUE)
    case("the path's own style wins", ".sq { fill: red }", SQ % ("", "class=sq style='fill: green'"), GREEN)
    case("a rule beats a presentation attribute", ".sq { fill: blue }", SQ % ("", "class=sq fill=red"), BLUE)
    case("fill: currentColor takes the svg's CSS colour", ".ic { color: green } .ic rect { fill: currentColor }",
         SQ % ("class=ic", ""), GREEN)
    case("display: none on a shared class is the box's, not the picture's", ".ic { display: block } "
         ".hidden-sm { fill: blue }", SQ % ("class='ic hidden-sm'", ""), BLUE)
    case("a rule in @media that doesn't hold is left out", "@media (max-width: 10px) { .sq { fill: red } }",
         SQ % ("", "class=sq fill=green"), GREEN)
    # a stroke: the square's edge drawn 2 wide in blue, its fill none
    case("stroke and stroke-width", ".sq { fill: none; stroke: blue; stroke-width: 2 }", SQ % ("", "class=sq"),
         BLUE, x=2, y=20, r=1)
    case("stroke's inside left unfilled", ".sq { fill: none; stroke: blue; stroke-width: 2 }",
         SQ % ("", "class=sq"), WHITE, x=20, y=20)

    # width="100%": the box's width, its height by the viewBox (2:1)
    rows = draw(tmp, "<div style='width:160px'><svg width='100%' viewBox='0 0 20 10'>"
                   "<rect width='20' height='10' fill='blue'/></svg></div>", width=200, height=120)
    check("width=100% fills its box", near(mean(rows, 150, 70, 156, 76), BLUE) and
          near(mean(rows, 166, 40, 172, 46), WHITE), str(mean(rows, 150, 70, 156, 76)))
    check("width=100%: its height by its viewBox", near(mean(rows, 20, 84, 26, 90), WHITE) and
          near(mean(rows, 20, 72, 26, 78), BLUE), str(mean(rows, 20, 72, 26, 78)))
    # width="2em" at a 30px font: 60 wide
    rows = draw(tmp, "<div style='font-size:30px'><svg width='2em' height='1em' "
                   "viewBox='0 0 20 10'><rect width='20' height='10' fill='green'/></svg></div>",
                   width=200, height=80)
    check("width=2em is its font's", near(mean(rows, 52, 10, 56, 14), GREEN) and
          near(mean(rows, 66, 10, 70, 14), WHITE), str(mean(rows, 66, 10, 70, 14)))

print("svgcss: %d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
