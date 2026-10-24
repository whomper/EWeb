#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of Esc while a page is made (doc_poll, ec_poll:
src/browse's build, the parser, the CSS engine and the layout asking
now and then): a long page (made here) through ewrender's -I, in both
editions.

- each stage asks (the parse every 256 tags, the styling every 256
  elements, the layout every 256 boxes), and asking changes nothing: the
  page is the same as made without;
- stopped at each stage, at its first turn and halfway, the page shown
  before stays (with the sanitizers: nothing left behind).

    tests/layout/stop.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
BEFORE = os.path.join(HERE, "find.html")
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("stop: FAIL %s %s" % (name, detail))


def page(path):
    """a long page: paragraphs with words in spans and links, lists and a
    table, about 9,000 tags"""
    parts = ["<!DOCTYPE html><html><head><meta charset=utf-8><title>Long</title>",
             "<style>p { margin: 4px } .b { font-weight: bold } td { border: 1px solid }</style></head><body>"]
    for i in range(600):
        parts.append("<p>Paragraph %d with <span class=b>bold</span> words, <a href=#p%d>a link</a> "
                     "and <em>more</em> text.</p>" % (i, i))
    parts.append("<ul>" + "".join("<li>item %d <b>x</b></li>" % i for i in range(400)) + "</ul>")
    parts.append("<table>" + "".join("<tr><td>%d<td>cell<td><i>%d</i></tr>" % (i, i * i) for i in range(300)) +
                 "</table></body></html>")
    with open(path, "w") as f:
        f.write("\n".join(parts))


def run(ewr, fnt, args, path):
    p = subprocess.run([ewr, "-f", fnt, "-d"] + args + [path], capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout, p.stderr


def edition(ewr, fnt, path, label):
    rc, plain, err = run(ewr, fnt, [], path)
    rc2, counted, err2 = run(ewr, fnt, ["-I", "count:0"], path)
    m = re.search(r"polls: parse (\d+) style (\d+) layout (\d+)", counted)
    polls = tuple(int(g) for g in m.groups()) if m else (0, 0, 0)
    check(label + "each stage asks", rc == 0 and rc2 == 0 and all(n >= 2 for n in polls), "%s %s" % (polls, err2))
    lines = [l for l in counted.splitlines() if not l.startswith("polls:")]
    check(label + "asking changes nothing", plain.splitlines() == lines)
    for k, stage in enumerate(("parse", "style", "layout")):
        for at in sorted({1, max(1, polls[k] // 2)}):
            rc, out, err = run(ewr, fnt, ["-I", "%s:%d" % (stage, at), "-P", BEFORE], path)
            check(label + "stopped while %s, at its turn %d: the page before stays" % (stage, at),
                  rc == 0 and 'stopped: "Find in page"' in out and "Sanitizer" not in err and
                  re.search(r'^page .* title "Find in page"$', out, re.M) is not None, out[:300] + err[-500:])


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "long.html")
        page(path)
        edition(sys.argv[1], sys.argv[2], path, "")
        if len(sys.argv) > 3:
            edition(sys.argv[3], sys.argv[2], path, "ST: ")
    if fails:
        print("stop: %d failed" % fails)
        sys.exit(1)
    print("stop: each stage asks, asking changes nothing, stopped while parsing, styling or laying out the page "
          "before stays%s" % (", in 2 editions" if len(sys.argv) > 3 else ""))


if __name__ == "__main__":
    main()
