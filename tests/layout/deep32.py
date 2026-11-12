#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the page drawn at 32 bits (a byte a part, as on a
24- or 32-bit screen: the FireBee's, a Falcon's or TT's graphics card),
through ewrender --deep, its pixels read back from the PNG:

- a gradient black to white across 256 px is every level, each pixel
  within 2 of its place on the line (no pattern: the 5-6-5 screen's 16
  values a step apart aren't needed);
- a translucent fill (white at 50% on black) and a gradient of alpha are
  mixed at 8 bits;
- a picture (5-6-5 kept, widened as drawn) and text;
- the test pages drawn at 32 bits and at 16 differ by no more than the
  16-bit screen's steps and its pattern;
- the window's bands (ewrender -B) at 8, 16 and 32 bits under UBSan and
  AddressSanitizer (make test's ewrender): no undefined behaviour.

    tests/layout/deep32.py EWRENDER EWEB.FNT"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ewrender, font = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
here = os.path.dirname(os.path.abspath(__file__))
fails = 0
checks = 0


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


def run(args):
    p = subprocess.run([ewrender, "-f", font] + args, capture_output=True, text=True, timeout=300)
    if p.returncode != 0 or "runtime error:" in p.stderr or "ERROR: AddressSanitizer" in p.stderr:
        sys.exit("deep: ewrender %s failed\n%s" % (" ".join(args), p.stderr))


def draw(tmp, page, deep=True, width=300, zoom=True):
    out = os.path.join(tmp, "out.png")
    run(["-w", str(width)] + (["-z", "1024"] if zoom else []) + (["--deep"] if deep else []) +
        ["-o", out, page])
    return png(out)


def check(what, ok, got=""):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("deep: %s%s" % (what, (": " + got) if got else ""))


with tempfile.TemporaryDirectory() as tmp:
    page = os.path.join(tmp, "page.html")
    with open(page, "w") as f:
        f.write("<!DOCTYPE html><html><body style='margin:0; background:#fff'>"
                "<div style='width:256px; height:8px; background:linear-gradient(to right, #000, #fff)'></div>"
                "<div style='width:256px; height:8px; background:#000'>"
                "<div style='height:8px; background:rgba(255, 255, 255, 0.5)'></div></div>"
                "<div style='width:256px; height:8px; background:linear-gradient(to right, "
                "rgba(255, 0, 0, 0), #f00)'></div>"
                "<div style='width:256px; height:8px; background:#00f; opacity:0.25'></div>"
                "</body></html>")
    rows = draw(tmp, page)
    g = [rows[3][x] for x in range(256)]
    check("a gradient across 256 px is every level", len(set(g)) >= 250, "%d levels" % len(set(g)))
    worst = max(abs(g[x][0] - (2 * x + 1) * 255 / 512.0) for x in range(256))
    check("each pixel within 2 of its place on the line", worst <= 2, "%.1f off" % worst)
    check("grey (no part another's)", all(p[0] == p[1] == p[2] for p in g), str(g[:4]))
    check("white at 50% on black", all(abs(rows[11][x][0] - 128) <= 1 and rows[11][x][0] == rows[11][x][2]
                                       for x in range(256)), str(rows[11][0]))
    a = [rows[19][x] for x in range(256)]
    worst = max(abs(a[x][1] - (255 - (2 * x + 1) * 255 / 512.0)) for x in range(256))
    check("a gradient of alpha, red over white", all(p[0] == 255 and p[1] == p[2] for p in a) and worst <= 2,
          "%s, %.1f off" % (a[128], worst))
    check("opacity 0.25 of blue on white", abs(rows[27][100][0] - 191) <= 1 and rows[27][100][2] == 255,
          str(rows[27][100]))

    # the test pages at 32 bits and at 16: the 16-bit screen's steps (8
    # levels of red and blue, 4 of green) and its pattern (a step more)
    for name in ("gradients", "alpha", "pictures", "svg", "text", "transform", "cards", "forms"):
        path = os.path.join(here, name + ".html")
        d32, d16 = draw(tmp, path, True, 640, False), draw(tmp, path, False, 640, False)
        if len(d32) != len(d16):
            check("%s: the same height at 32 bits" % name, False, "%d, not %d" % (len(d32), len(d16)))
            continue
        worst = max(abs(p[k] - q[k]) for r32, r16 in zip(d32, d16) for p, q in zip(r32, r16) for k in range(3))
        check("%s at 32 bits as at 16" % name, worst <= 16, "%d levels apart" % worst)

    # every band of the window at 8, 16 and 32 bits
    run(["-w", "640", "-B", os.path.join(here, "gradients.html")])

print("deep: %d of %d checks passed" % (checks - fails, checks))
sys.exit(1 if fails else 0)
