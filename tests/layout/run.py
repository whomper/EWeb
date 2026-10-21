#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's layout check: ewrender's display list (-d) of each page in
tests/layout and of tests/bench/sample.html, against the one kept beside
it (PAGE.txt), with the repository's folder taken out of the links; and
the Hebrew test page laid out the same in each of its encodings
(tests/hebrew: UTF-8, windows-1255, ISO-8859-8 logical and -I).

    tests/layout/run.py EWRENDER EWEB.FNT [--update]

--update writes the lists as they are now: look at the pages (ewrender
without -d draws them) before keeping them."""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ewrender, font = sys.argv[1], sys.argv[2]
update = "--update" in sys.argv[3:]


def dump(page):
    p = subprocess.run([os.path.abspath(ewrender), "-f", os.path.abspath(font), "-d", os.path.abspath(page)],
                       capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("layout: %s: ewrender failed\n%s" % (page, p.stderr))
    # the list's lines only: Hubbub prints its states too, with its
    # assertions on (make test's build)
    keep = [line for line in p.stdout.splitlines() if line.split(" ", 1)[0] in ("page", "fill", "text", "link", "image", "background")]
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

# one page, five encodings: the same words in the same places
heb = {}
for page in ("UTF8.HTM", "W1255.HTM", "I88598I.HTM", "LOGICAL.HTM"):
    heb[page] = "\n".join(line for line in dump(os.path.join(ROOT, "tests/hebrew", page)).splitlines()
                          if line.startswith("text "))
if len(set(heb.values())) != 1:
    fails += 1
    print("layout: the Hebrew page differs between its encodings")

if update:
    print("layout: %d lists written" % len(pages))
else:
    print("layout: %s" % ("all %d pages as kept, Hebrew the same in 4 encodings" % len(pages) if not fails
                          else "%d failed" % fails))
sys.exit(1 if fails else 0)
