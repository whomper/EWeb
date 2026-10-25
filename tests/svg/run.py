#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of EWeb's SVG pictures (src/svg): each case in
tests/svg/cases drawn by svgrender at its own proportions in 32 x 32 at
most, set against the same drawn by another renderer (librsvg's
rsvg-convert, kept in tests/svg/expect.txt as greys over white): no more
than MEAN apart on average.

    tests/svg/run.py SVGRENDER
    tests/svg/run.py SVGRENDER --update     (rsvg-convert and Pillow: the
                                             expectations made again)"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, "cases")
EXPECT = os.path.join(HERE, "expect.txt")
MEAN = 3.0		# of 255, a pixel's grey on average

render = sys.argv[1]
update = "--update" in sys.argv


def size_of(path):
    w, h = map(int, subprocess.run([render, "-z", path], capture_output=True, text=True, check=True).stdout.split())
    k = max(w, h)
    return max(1, round(w * 32 / k)), max(1, round(h * 32 / k))


def ours(path, w, h):
    out = subprocess.run([render, "-g", "-s", "%d,%d" % (w, h), path], capture_output=True, text=True, timeout=60)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip())
    rows = out.stdout.split("\n")[1:1 + h]
    return [int(r[i:i + 2], 16) for r in rows for i in range(0, 2 * w, 2)]


def theirs(path, w, h):
    from PIL import Image
    with tempfile.TemporaryDirectory() as t:
        png = os.path.join(t, "r.png")
        subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h), path, "-o", png], check=True)
        px = Image.open(png).convert("RGBA").tobytes()
        out = []
        for k in range(0, len(px), 4):
            r, g, b, a = px[k:k + 4]
            l = (r * 77 + g * 150 + b * 29) >> 8
            out.append((l * a + 255 * (255 - a) + 127) // 255)
        return out


names = sorted(f for f in os.listdir(CASES) if f.endswith(".svg"))
if update:
    with open(EXPECT, "w") as f:
        f.write("# tests/svg/run.py --update: each case as rsvg-convert draws it, greys over white\n")
        for n in names:
            p = os.path.join(CASES, n)
            w, h = size_of(p)
            g = theirs(p, w, h)
            f.write("%s %d %d\n" % (n, w, h))
            for y in range(h):
                f.write("".join("%02x" % v for v in g[y * w:(y + 1) * w]) + "\n")
    print("svg: %d cases' expectations made" % len(names))
    sys.exit(0)

want = {}
with open(EXPECT) as f:
    lines = [l.rstrip("\n") for l in f if not l.startswith("#")]
i = 0
while i < len(lines):
    n, w, h = lines[i].split()
    w, h = int(w), int(h)
    want[n] = (w, h, [int(r[k:k + 2], 16) for r in lines[i + 1:i + 1 + h] for k in range(0, 2 * w, 2)])
    i += 1 + h

fails = 0
for n in names:
    if n not in want:
        print("svg: %s: no expectation (--update)" % n)
        fails += 1
        continue
    w, h, g = want[n]
    try:
        got = ours(os.path.join(CASES, n), w, h)
    except Exception as e:
        print("svg: %s: %s" % (n, e))
        fails += 1
        continue
    mean = sum(abs(a - b) for a, b in zip(got, g)) / len(g)
    if mean > MEAN:
        print("svg: %s: %.1f apart on average (at most %.1f)" % (n, mean, MEAN))
        fails += 1
print("svg: %s" % ("all %d passed" % len(names) if not fails else "%d of %d failed" % (fails, len(names))))
sys.exit(1 if fails else 0)
