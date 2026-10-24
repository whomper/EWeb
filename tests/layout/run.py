#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's layout check: ewrender's display list (-d) of each page in
tests/layout and of tests/bench/sample.html, against the one kept beside
it (PAGE.txt), with the repository's folder taken out of the links; and
the Hebrew test page laid out the same in each of its encodings
(tests/hebrew: UTF-8, windows-1255, ISO-8859-8 logical and -I), in UTF-8
with no label (found by its bytes) and in windows-1255 with none, read
so by hand (View > Encoding, ewrender -e); a text file in UTF-8 with
no label shown as its lines, its "<" as it is; and a page in visual
Hebrew (ISO-8859-8) drawn as its letters are stored, the same as its text
in logical order (tests/hebrew/visual). A page's
<!-- args: ... --> line gives ewrender more (-L WIDTH: the window sized).

    tests/layout/run.py EWRENDER EWEB.FNT [--update]

--update writes the lists as they are now: look at the pages (ewrender
without -d draws them) before keeping them."""
import glob
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ewrender, font = sys.argv[1], sys.argv[2]
update = "--update" in sys.argv[3:]


def dump(page, more=()):
    args = list(more)
    with open(page, encoding="latin-1") as f:
        for m in re.finditer(r"<!-- args: (.*?) -->", f.read()):
            args += m.group(1).split()
    p = subprocess.run([os.path.abspath(ewrender), "-f", os.path.abspath(font), "-d"] + args + [os.path.abspath(page)],
                       capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("layout: %s: ewrender failed\n%s" % (page, p.stderr))
    # the list's lines only: Hubbub prints its states too, with its
    # assertions on (make test's build)
    keep = [line for line in p.stdout.splitlines()
            if line.split(" ", 1)[0] in ("page", "fill", "text", "link", "image", "background", "gradient", "summary",
                                         "clip", "wider:", "bar", "drawn:")]
    return "\n".join(keep).replace("file://" + ROOT + "/", "file://./") + "\n"


fails = 0
pages = sorted(glob.glob(os.path.join(HERE, "*.html"))) + [os.path.join(ROOT, "tests/bench/sample.html")]
for page in pages:
    got = dump(page)
    keep = os.path.join(HERE, os.path.basename(page).rsplit(".", 1)[0] + ".txt")
    if update:
        with open(keep, "w") as f:
            f.write(got)
        continue
    with open(keep) as f:
        want = f.read()
    if got != want:
        fails += 1
        g, w = got.splitlines(), want.splitlines()
        for i in range(max(len(g), len(w))):
            a = g[i] if i < len(g) else "(none)"
            b = w[i] if i < len(w) else "(none)"
            if a != b:
                print("layout: %s, line %d\n  now:  %s\n  kept: %s" % (os.path.basename(page), i + 1, a, b))
                break

# one page, its encodings: the same words in the same places
heb = {}
for page, more in (("UTF8.HTM", ()), ("W1255.HTM", ()), ("I88598I.HTM", ()), ("LOGICAL.HTM", ()),
                   ("unlabelled/UTF8.HTM", ()), ("unlabelled/W1255.HTM", ("-e", "windows-1255"))):
    heb[page] = "\n".join(line for line in dump(os.path.join(ROOT, "tests/hebrew", page), more).splitlines()
                          if line.startswith("text "))
if len(set(heb.values())) != 1:
    fails += 1
    print("layout: the Hebrew page differs between its encodings: %s" %
          ", ".join(k for k in heb if heb[k] != heb["UTF8.HTM"]))
# the windows-1255 page with no label, not read so by hand: windows-1252
# (a file has no address to tell the locale by), its Hebrew Latin letters
if "\u00f9\u00ec\u00e5\u00ed" not in dump(os.path.join(ROOT, "tests/hebrew/unlabelled/W1255.HTM")):
    fails += 1
    print("layout: the windows-1255 page with no label isn't read as windows-1252")
# a text file: its lines, "<" and "&" as they are, its UTF-8 found
txt = dump(os.path.join(ROOT, "tests/hebrew/unlabelled/UTF8.TXT"))
if not ('mono13 link 0 "SPDX-License-Identifier: GPL-2.0-or-later"' in txt and
        '"and English, <b>not bold</b> & "' in txt and 'rtl link 0 "\u05e9\u05dc\u05d5\u05dd"' in txt):
    fails += 1
    print("layout: the text file isn't shown as its lines\n" + txt)
# visual Hebrew: its lines not reordered, its words not reversed (the
# HTML Standard's rendering for ISO-8859-8), so it is drawn as the same
# text in logical order is
with tempfile.TemporaryDirectory() as tmp:
    drawn = []
    for page in ("LOGICAL.HTM", "VISUAL.HTM"):
        png = os.path.join(tmp, page + ".png")
        p = subprocess.run([os.path.abspath(ewrender), "-f", os.path.abspath(font), "-o", png,
                            os.path.join(ROOT, "tests/hebrew/visual", page)], capture_output=True, text=True, timeout=120)
        drawn.append(open(png, "rb").read() if p.returncode == 0 and os.path.exists(png) else page)
    vis = dump(os.path.join(ROOT, "tests/hebrew/visual/VISUAL.HTM"))
    if drawn[0] != drawn[1] or " rtl " in vis:
        fails += 1
        print("layout: visual Hebrew isn't drawn as it is stored\n" + vis)

if update:
    print("layout: %d lists written" % len(pages))
else:
    print("layout: %s" % ("all %d pages as kept, Hebrew the same in %d encodings and visual, a text file" %
                          (len(pages), len(heb))
                          if not fails
                          else "%d failed" % fails))
sys.exit(1 if fails else 0)
