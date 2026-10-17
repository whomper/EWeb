#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Counts the 68030 instructions nsbench needs per phase, page by page.

    tests/bench/measure.py [-E libcss|ewcss] [build/pages] > results.csv

For every page in PAGES/pages it runs build/m68k/nsbench under QEMU
(tools/count68k.sh) once per step: an empty page (the program's own start),
the HTML alone (-P 1), and with each stylesheet in PAGES/css the CSS parse
(-P 2) and the selection (-P 3). The CSV has the page's size and counts,
then each step's instructions less the step before it. A summary goes to
stderr: the median and the 90th percentile, and instructions per byte. -E
picks the CSS engine: LibCSS (the default) or EWeb's own."""
import csv
import os
import re
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BENCH = os.path.join(ROOT, "build/m68k/nsbench")
COUNT = os.path.join(ROOT, "tools/count68k.sh")


def run(args):
    p = subprocess.run([COUNT, BENCH] + args, capture_output=True, text=True, cwd=ROOT)
    out = p.stdout
    m = re.search(r"^insns (\d+)", out, re.M)
    if p.returncode != 0 or m is None:
        raise RuntimeError("nsbench failed: %s\n%s%s" % (args, out, p.stderr))
    return int(m.group(1)), out


def stats(out):
    m = re.search(r"page (\d+) bytes: (\d+) elements, (\d+) text nodes \((\d+) bytes\), "
                  r"(\d+) attributes, depth (\d+)", out)
    return [int(x) for x in m.groups()]


ENGINE = ["-E", "libcss"]


def page_row(page, sheets, base):
    p1, out = run(ENGINE + ["-P", "1", page])
    row = [os.path.basename(page)] + stats(out) + [p1 - base]
    for sheet in sheets:
        p2, _ = run(ENGINE + ["-P", "2", page, sheet])
        p3, out3 = run(ENGINE + ["-P", "3", page, sheet])
        sel = re.search(r"styles: (\d+) elements, (\d+) inline", out3)
        row += [p2 - p1, p3 - p2]
    return row


def main():
    global ENGINE
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "-E":
        ENGINE = ["-E", args[1]]
        args = args[2:]
    pages_dir = args[0] if args else os.path.join(ROOT, "build/pages")
    pages = sorted(os.path.join(pages_dir, "pages", f) for f in os.listdir(os.path.join(pages_dir, "pages")))
    sheets = sorted(os.path.join(pages_dir, "css", f) for f in os.listdir(os.path.join(pages_dir, "css")))
    empty = os.path.join(ROOT, "build/empty.html")
    open(empty, "w").close()
    base, _ = run(ENGINE + ["-P", "1", empty])

    with ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        rows = list(ex.map(lambda p: page_row(p, sheets, base), pages))

    head = ["page", "bytes", "elements", "texts", "text_bytes", "attrs", "depth", "html"]
    for s in sheets:
        n = os.path.splitext(os.path.basename(s))[0].split(".")[0]
        head += ["css_" + n, "select_" + n]
    w = csv.writer(sys.stdout)
    w.writerow(head)
    w.writerows(rows)

    def col(name):
        return [r[head.index(name)] for r in rows]

    def pct(v, p):
        v = sorted(v)
        return v[min(len(v) - 1, int(len(v) * p))]

    print("start-up: %d instructions" % base, file=sys.stderr)
    for name in head[1:]:
        v = col(name)
        print("%-18s median %12d   90%% %12d" % (name, statistics.median(v), pct(v, 0.9)),
              file=sys.stderr)
    per_byte = [r[head.index("html")] / r[1] for r in rows]
    print("html: median %.0f instructions per byte" % statistics.median(per_byte), file=sys.stderr)
    per_el = {}
    for s in head:
        if s.startswith("select_"):
            per_el[s] = statistics.median([r[head.index(s)] / max(1, r[2]) for r in rows])
            print("%s: median %.0f instructions per element" % (s, per_el[s]), file=sys.stderr)


if __name__ == "__main__":
    main()
