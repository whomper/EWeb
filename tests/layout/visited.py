#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of visited links and the history (Selectors 4, 8.2;
src/browse/history.c): tests/layout/visited.html through ewrender with
-V, which keeps the history in a file as EWEB.PRG keeps HISTORY.TXT.

- with nothing in the history, no link visited but the one to the page
  itself, put in it before it is styled;
- to page B and Back (-N): its two links in their :visited colours (the
  nested one's by the more specific rule), with the background colour
  the rule gives, and nothing else of the rule taken, and :link's rule
  taken but for its colour: the page laid out as before, word for word;
  the links to a page never shown, and to a place on the page, as they
  were;
- the history file written at the end: both pages, with their titles;
  read at the next start, B's links visited at once; about:history
  lists them newest first under the day;
- a link to a place on the page followed (-K): it is visited, the page
  otherwise the same (EWEB.PRG's edition: the ST edition's -K clicks
  only summaries);
- a page whose scripts run (-J): no link of it visited, though B is in
  the history (a script could read it); its scripts not run, it is;
- the ST edition (EWRENDERST) the same, but for -K and -J.

    tests/layout/visited.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "visited.html")
B = os.path.join(HERE, "visited", "b.html")
SCRIPTED = os.path.join(HERE, "visited", "script.html")
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("visited: FAIL %s %s" % (name, detail))


def run(ewr, fnt, args, page=PAGE):
    p = subprocess.run([ewr, "-f", fnt, "-d"] + args + [page], capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("visited: ewrender failed\n" + p.stderr)
    return p.stdout.splitlines()


def texts(out):
    """each text item: (x, y, w, h, words) and its colour"""
    r = []
    for line in out:
        m = re.match(r'text (\d+),(\d+) (\d+)x(\d+) (#[0-9a-f]+) .* "(.*)"$', line)
        if m:
            r.append(((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(6)),
                      m.group(5)))
    return r


def colour(out, words):
    for where, col in texts(out):
        if where[4] == words:
            return col
    return None


def fills(out, col):
    return [line for line in out if line.startswith("fill ") and line.endswith(" " + col)]


def edition(ewr, fnt, full):
    with tempfile.TemporaryDirectory() as tmp:
        hist = os.path.join(tmp, "HISTORY.TXT")
        fresh = run(ewr, fnt, ["-V", hist])
        check("none visited at first", colour(fresh, "page B") == "#000080" and
              colour(fresh, "B again") == "#000080" and colour(fresh, "this page") == "#800000" and
              len(fills(fresh, "#ffff00")) == 1,
              str([c for _, c in texts(fresh)]))
        os.remove(hist)
        back = run(ewr, fnt, ["-V", hist, "-N", B])
        check("to B and Back: its links visited", colour(back, "page B") == "#800000" and
              colour(back, "B again") == "#008000", "%s %s" % (colour(back, "page B"), colour(back, "B again")))
        check("their background colour taken", len(fills(back, "#ffff00")) == 3, str(fills(back, "#ffff00")))
        check("the others as they were", colour(back, "a page never seen") == "#000080" and
              colour(back, "a place on this page") == "#000080")
        check("nothing else of the rule taken: laid out as before",
              [w for w, _ in texts(back)] == [w for w, _ in texts(fresh)])
        with open(hist, encoding="utf-8") as f:
            lines = [line.rstrip("\n").split("\t") for line in f if not line.startswith("#")]
        check("the history written", len(lines) == 2 and
              lines[0][1:] == ["file://" + PAGE, "Visited links"] and lines[1][1:] == ["file://" + B, "Page B"] and
              all(line[0].isdigit() for line in lines), str(lines))
        again = run(ewr, fnt, ["-V", hist])
        check("read at the next start", colour(again, "page B") == "#800000" and
              [w for w, _ in texts(again)] == [w for w, _ in texts(fresh)])
        with open(hist, encoding="utf-8") as f:
            titles = [line.rstrip("\n").split("\t")[2] for line in f if not line.startswith("#")]
        page = run(ewr, fnt, ["-V", hist], "about:history")
        words = [w[4] for w, _ in texts(page)]
        check("about:history: newest first, under the day", sorted(titles) == ["Page B", "Visited links"] and
              "Today" in words and all(t in words for t in titles) and
              words.index("Today") < words.index(titles[1]) < words.index(titles[0]), "%s %s" % (titles, words))
        check("about:history: a page number out of range", "Page B" in
              [w[4] for w, _ in texts(run(ewr, fnt, ["-V", hist], "about:history?page=-5"))])
        if not full:
            return
        part = run(ewr, fnt, ["-V", hist, "-K", "part"])
        check("a place on the page followed: visited", colour(part, "a place on this page") == "#800000" and
              any(line.startswith("followed: ") for line in part), str([l for l in part if "followed" in l]))
        check("a place on the page followed: the page the same",
              [w for w, _ in texts(part)] == [w for w, _ in texts(fresh)])
        unrun = run(ewr, fnt, ["-V", hist], SCRIPTED)
        check("its scripts not run: visited", colour(unrun, "page B") == "#800000")
        ran = run(ewr, fnt, ["-V", hist, "-J"], SCRIPTED)
        check("its scripts run: not visited", colour(ran, "page B") == "#0000ee" and
              "console: its script ran" in ran, "%s %s" % (colour(ran, "page B"), [l for l in ran if "console" in l]))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ewr, fnt = sys.argv[1], sys.argv[2]
    edition(ewr, fnt, True)
    if len(sys.argv) > 3:
        edition(sys.argv[3], fnt, False)
    if fails:
        print("visited: %d failed" % fails)
        sys.exit(1)
    print("visited: links to pages shown in their colours only, the history kept and read again, "
          "a place followed, none on a page whose scripts run%s" % (", in 2 editions" if len(sys.argv) > 3 else ""))


main()
