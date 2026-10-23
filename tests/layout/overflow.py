#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of boxes that hide what overflows them (CSS Overflow
3: overflow hidden and clip), beyond their list (overflow.txt), on
tests/layout/overflow.html through ewrender:

- the red text below each such box's foot, or in an inner one, isn't
  drawn (no red pixel in the picture), in EWEB.PRG's edition and the
  ST's;
- a link hidden so isn't followed when clicked (-K);
- a carousel's 2000-pixel track in such a box doesn't make the page
  wider than the window (no "wider:" line);
- a box positioned in such a box: within it when the box is its
  containing block, drawn past its foot when its containing block is
  outside (the green under each);
- and the review's (23 Oct 2026), each a page of its own: a code block
  with overflow-x auto and a max-height keeps it and scrolls (the
  paragraph after it below the box); the body's overflow-x hidden with a height
  doesn't cut the page; a control wider than its box keeps its width;
  a script's click goes to what shows, not to what a box hides; a row's
  overflow clips nothing; italic text in a box hiding its overflow is
  drawn as in one that doesn't.

    tests/layout/overflow.py EWRENDER FNT [ST_EWRENDER]"""
import os
import re
import subprocess
import sys
import tempfile
import zlib
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "overflow.html")


def fail(msg):
    print("overflow: " + msg)
    sys.exit(1)


def png_rgb(path):
    """a PNG's width, height and rows of (r, g, b), as ewrender writes them"""
    data = open(path, "rb").read()
    pos, chunks = 8, b""
    w = h = 0
    ctype = 2
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, _, ctype = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            chunks += body
        pos += 12 + n
    raw = zlib.decompress(chunks)
    bpp = 4 if ctype == 6 else 3
    rows, prev, stride = [], bytearray(w * bpp), w * bpp
    i = 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows


def main():
    ewrender, font = sys.argv[1], sys.argv[2]
    editions = [ewrender] + sys.argv[3:4]
    with tempfile.TemporaryDirectory() as tmp:
        for ed in editions:
            out = os.path.join(tmp, "o.png")
            p = subprocess.run([ed, "-f", font, "-o", out, PAGE], capture_output=True, text=True, timeout=120)
            if p.returncode != 0:
                fail("%s failed: %s" % (ed, p.stderr[-500:]))
            w, h, rows = png_rgb(out)
            red = [(x, y) for y in range(h) for x, (r, g, b) in enumerate(rows[y]) if r > 150 and g < 110 and b < 110]
            if red:
                fail("%s: red text that a box hides was drawn, at %s" % (os.path.basename(ed), red[:5]))
    d = subprocess.run([ewrender, "-f", font, "-d", PAGE], capture_output=True, text=True, timeout=120).stdout
    if re.search(r"^wider:", d, re.M):
        fail("a carousel's track made the page wider than the window")
    clips = [tuple(int(v) for v in m.groups()) for m in
             re.finditer(r"^clip x (\d+)\+(\d+) y (\d+)\+(\d+) in \d+$", d, re.M)]
    greens = [tuple(int(v) for v in m.groups()) for m in
              re.finditer(r"^fill (\d+),(\d+) (\d+)x(\d+) #00aa00$", d, re.M)]
    if len(greens) != 2 or len(clips) < 4:
        fail("the positioned boxes' or the clips' items aren't there:\n" + d[:2000])
    out = os.path.join(tempfile.gettempdir(), "ewr_overflow_%d.png" % os.getpid())
    subprocess.run([ewrender, "-f", font, "-o", out, PAGE], capture_output=True, timeout=120)
    w, h, rows = png_rgb(out)
    os.unlink(out)

    def green_below(fillr, clipr):
        x, y, fw, fh = fillr
        foot = clipr[2] + clipr[3] + 2		# past the box's border
        return sum(1 for yy in range(foot, min(y + fh, h)) for xx in range(x, x + fw)
                   if rows[yy][xx] == (0, 170, 0))
    # the clips of the two boxes the positioned ones are in: the third and fourth
    if green_below(greens[0], clips[2]) != 0:
        fail("a box positioned in a box that is its containing block was drawn past its foot")
    if green_below(greens[1], clips[3]) == 0:
        fail("a box positioned in a box but not its containing block was cut at its foot")
    k = subprocess.run([ewrender, "-f", font, "-d", "-K", "hidlink", PAGE], capture_output=True, text=True,
                       timeout=120).stdout
    if "followed:" in k:
        fail("a link a box hides was followed when clicked")
    review(ewrender, font)
    print("overflow: what boxes hide not drawn%s nor clicked, no wider page, positioned boxes by their "
          "containing block, and the review's 6 cases" % (" in 2 editions" if len(editions) > 1 else ""))


CASES = {
    "auto": '<body style="margin:0;font:16px/20px sans-serif"><pre style="overflow-x:auto;max-height:60px;'
            'margin:0">line 1\nline 2\nline 3\nline 4\nline 5\nline 6</pre><p style="margin:0">AFTER</p>',
    "body": '<style>body{overflow-x:hidden;height:100vh}</style>'
            + "".join("<p>Paragraph %d of a long page.</p>" % i for i in range(60)),
    "ctl": '<body style="margin:0"><div style="width:200px"><button>A very long button label that is wider '
           'than the box</button></div>',
    "js": '<body style="margin:0;font:16px/16px sans-serif"><div style="height:16px;overflow:hidden">first '
          'line<br><span onclick="console.log(\'HIDDEN got it\')">hiddenhiddenhiddenhiddenhidden</span></div>'
          '<div id=v onclick="console.log(\'VISIBLE got it\')" style="height:16px;width:300px"></div>',
    "tr": '<style>tr{overflow:hidden}</style><table><tr><td rowspan=2>one<br>two<br>three<br>four</td>'
          '<td>B</td></tr><tr><td>C</td></tr></table>',
    "italic": '<body style="margin:0;font:italic 24px/40px serif"><div style="overflow:%s;height:200px;'
              'padding:20px">fjord Wolf jjj gyp ffff <b>fjfj</b> Jfj</div>',
}


def review(ewrender, font):
    with tempfile.TemporaryDirectory() as tmp:
        def page(name, html):
            path = os.path.join(tmp, name + ".html")
            with open(path, "w") as f:
                f.write("<!DOCTYPE html><meta charset=utf-8>" + html)
            return path

        def dump(name, *args):
            return subprocess.run([ewrender, "-f", font, "-d"] + list(args) + [page(name, CASES[name])],
                                  capture_output=True, text=True, timeout=120).stdout

        def text_y(d, t):
            m = re.search(r'^text \d+,(\d+) \d+x(\d+) .*"%s"$' % re.escape(t), d, re.M)
            return (int(m.group(1)), int(m.group(2))) if m else None
        d = dump("auto")
        after = text_y(d, "AFTER")
        box = re.search(r"^clip x \S+ y (\d+)\+(\d+) in 0$", d, re.M)
        if after is None or box is None or after[0] < int(box.group(1)) + int(box.group(2)) or \
                not re.search(r"^bar y ", d, re.M):
            fail("a code block with overflow-x auto and a max-height didn't scroll in its box:\n" + d)
        d = dump("body")
        m = re.search(r"^page \d+ x (\d+)", d, re.M)
        if m is None or int(m.group(1)) < 1500 or text_y(d, "Paragraph 59 of a long page.") is None:
            fail("the body's overflow-x hidden and height cut the page:\n" + d[:300])
        d = dump("ctl")
        m = re.search(r"^fill 0,0 (\d+)x", d, re.M)
        if m is None or int(m.group(1)) <= 200:
            fail("a button wider than its box was narrowed to it:\n" + d[:500])
        d = dump("js", "-J", "-K", "v")
        if "VISIBLE got it" not in d or "HIDDEN" in d:
            fail("a script's click went to what a box hides:\n" + d[-500:])
        d = dump("tr")
        if re.search(r"^clip ", d, re.M):
            fail("a row's overflow clipped its cells:\n" + d)
        shots = []
        for o in ("visible", "hidden"):
            out = os.path.join(tmp, o + ".png")
            subprocess.run([ewrender, "-f", font, "-o", out, page("italic_" + o, CASES["italic"] % o)],
                           capture_output=True, timeout=120)
            shots.append(png_rgb(out))
        if shots[0] != shots[1]:
            fail("italic text in a box hiding its overflow was cut at its words' edges")


main()
