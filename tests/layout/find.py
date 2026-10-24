#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of Edit > Find (src/layout/find.c, the canvas's
mark): tests/layout/find.html through ewrender's -G and -U, in both
editions.

- words found in order, going round after the last; back from the top
  to the last;
- case aside (ASCII and Latin-1: "ångström" finds ÅNGSTRÖM), a space for
  any white space and a line's end;
- a word in two runs (a bold "Ata" and "ri") found as one;
- Hebrew in the order it is read: two words that follow each other, the
  end of one and the start of the next, across runs drawn right to left;
- text no one sees (opacity 0) not found;
- words in a box that scrolls: the box brought to them;
- the words marked where they are drawn: the run's part, from its right
  in a right-to-left run.

    tests/layout/find.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "find.html")
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("find: FAIL %s %s" % (name, detail))


def finds(ewr, fnt, args, png=None):
    cmd = [ewr, "-f", fnt] + (["-o", png] if png else ["-d"]) + args + [PAGE]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("find: ewrender failed\n" + p.stderr)
    return [line[len("found: "):] for line in p.stdout.splitlines() if line.startswith("found: ")]


def nth(line):
    m = re.match(r"(\d+) of (\d+)", line or "")
    return (int(m.group(1)), int(m.group(2))) if m else None


def where(line):
    m = re.search(r"item (\d+) (\d+) to item (\d+) (\d+) at (-?\d+),(-?\d+) (\d+)x(\d+)", line or "")
    return tuple(int(g) for g in m.groups()) if m else None


def png_rows(path):
    """an RGB PNG (ewrender's, 8 bits, no interlace) as rows of (r, g, b)"""
    with open(path, "rb") as f:
        data = f.read()
    pos, w, h, idat = 8, 0, 0, b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 2, (depth, ctype)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, rows, prev, stride = zlib.decompress(idat), [], bytearray(3 * w), 3 * w
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b, c = prev[i], prev[i - 3] if i >= 3 else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[i:i + 3]) for i in range(0, stride, 3)])
        prev = line
    return rows


def yellow_span(rows, y):
    xs = [x for x, px in enumerate(rows[y]) if px[0] > 200 and px[1] > 200 and px[2] < 80]
    return (min(xs), max(xs)) if xs else None


def edition(ewr, fnt, label):
    seven = finds(ewr, fnt, ["-G", "atari"] * 7)
    check(label + "in order, round after the last", [nth(f) for f in seven] ==
          [(1, 6), (2, 6), (3, 6), (4, 6), (5, 6), (6, 6), (1, 6)], str(seven))
    back = finds(ewr, fnt, ["-U", "atari", "-U", "atari"])
    check(label + "back: the last, then the one before", [nth(f) for f in back] == [(6, 6), (5, 6)], str(back))
    two = where(seven[1])
    check(label + "a word in two runs", two is not None and two[0] + 1 == two[2] and two[3] == 2, seven[1])
    check(label + "in a box that scrolls: brought to it", "(a box scrolled)" in seven[4], seven[4])
    case = finds(ewr, fnt, ["-G", "ÅNGSTRÖM", "-G", "ångström"])
    check(label + "case aside, Latin-1 too", [nth(f) for f in case] == [(1, 2), (2, 2)], str(case))
    words = finds(ewr, fnt, ["-G", "line   and\tthe START", "-G", "amet, the end"])
    check(label + "a space for any white space", nth(words[0]) == (1, 1), str(words))
    check(label + "over a line's end", nth(words[1]) == (1, 1) and where(words[1])[0] != where(words[1])[2],
          str(words))
    hidden = finds(ewr, fnt, ["-G", "hidden"])
    check(label + "not what no one sees", hidden == ["none (0)"], str(hidden))
    he = finds(ewr, fnt, ["-G", "שלום עולם", "-G", "עולם, שלום", "-G", "ם עו"])
    check(label + "Hebrew as it is read", [nth(f) for f in he] == [(1, 1), (1, 1), (1, 1)] and
          all(where(f)[0] == where(f)[2] + 1 for f in he), str(he))
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "f.png")
        one = finds(ewr, fnt, ["-G", "Falcon"], png)
        w = where(one[0])
        rows = png_rows(png)
        span = yellow_span(rows, w[5] + w[7] // 2) if w else None
        check(label + "marked where the words are drawn", span is not None and w[4] < span[0] and
              span[1] < w[4] + w[6] and span[1] - span[0] > 20, "%s %s" % (one, span))
        part = finds(ewr, fnt, ["-G", "שלו"], png)
        w = where(part[0])
        rows = png_rows(png)
        span = yellow_span(rows, w[5] + w[7] // 2) if w else None
        check(label + "a right-to-left run's first letters marked from its right",
              span is not None and span[1] >= w[4] + w[6] - 2 and span[0] > w[4] + 2, "%s %s" % (part, span))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    edition(sys.argv[1], sys.argv[2], "")
    if len(sys.argv) > 3:
        edition(sys.argv[3], sys.argv[2], "ST: ")
    if fails:
        print("find: %d failed" % fails)
        sys.exit(1)
    print("find: words found in order and round, back, case aside, across runs and lines, Hebrew as it is read, "
          "not hidden text, in a box that scrolls, marked where they are drawn%s" %
          (", in 2 editions" if len(sys.argv) > 3 else ""))


main()
