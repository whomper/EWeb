#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of selecting and copying text (src/layout/find.c's
page_pos_at, page_word, page_range, page_copy_text, page_text_index;
the canvas's mark): tests/layout/find.html and select.html through
ewrender's -x and -j, in both editions.

- a drag from one place to another: the text between, as it is read,
  a line break between lines and an empty line between paragraphs; the
  same text dragged the other way;
- a place beside a line (left of it, past its end, below the page):
  the line's start, its end, the text's end;
- a double click's word, on into a run beside it (a bold "Ata" and
  "ri"), a Hebrew word;
- Hebrew in the order it is read: the end of one word to the middle of
  another, across runs drawn right to left;
- Select all: all the text seen (not text no one sees), and ^C with
  nothing selected the page's text;
- the selection the same text when the page is laid out again at
  other widths, its lines ending elsewhere;
- the selection marked where it is drawn, from a run's right in a
  right-to-left one;
- select.html: a right-to-left line with a number or Latin words in it,
  copied and dragged over as it is read; Hebrew words in a left-to-right
  line; a superscript and a subscript part of their word ("mc2", "H2O");
  a place never between a letter and its combining accent, nor a Hebrew
  letter and its point; a double click far from any text selecting
  nothing.

    tests/layout/select.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from find import png_rows  # noqa: E402

PAGE = os.path.join(HERE, "find.html")
PAGE2 = os.path.join(HERE, "select.html")
SEL = (0x99, 0xcc, 0xff)        # ui/win.c's SEL_RGB (in true colour's 5, 6 and 5 bits, near it)
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("select: FAIL %s %s" % (name, detail))


def run(ewr, fnt, args, png=None, page=PAGE):
    cmd = [ewr, "-f", fnt] + (["-o", png] if png else ["-d"]) + args + [page]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("select: ewrender failed\n" + p.stderr)
    return p.stdout


def runs(ewr, fnt, page=PAGE):
    """the page's text runs: (x, baseline, w, h, text)"""
    out = []
    for line in run(ewr, fnt, [], page=page).splitlines():
        m = re.match(r'text (-?\d+),(-?\d+) (\d+)x(\d+) #([0-9a-f]+) .*?"(.*)"$', line)
        if m and len(m.group(5)) == 6:
            out.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(6)))
    return out


def texts(out, what="selected"):
    """each "selected:" line's text, its \\n a line break"""
    got = []
    for line in out.splitlines():
        m = re.match(what + r'(?: again)?: (?:item \d+ \d+ to item \d+ \d+ )?"(.*)"$', line)
        if m:
            got.append(m.group(1).replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\'))
        elif line.startswith(what):
            got.append(None)
    return got


def at(r, frac):
    """a point inside run r, frac of the way along it from its left"""
    return "%d,%d" % (r[0] + int(r[2] * frac), r[1] - 3)


def sel_span(rows, y):
    xs = [x for x, px in enumerate(rows[y]) if all(abs(px[i] - SEL[i]) <= 8 for i in range(3))]
    return (min(xs), max(xs)) if xs else None


def edition(ewr, fnt, label):
    rs = runs(ewr, fnt)
    by = {r[4]: r for r in rs}
    first = rs[0]                                   # "The Atari Falcon came in 1992. An "
    caps = next(r for r in rs if "ATARI" in r[4])   # "end of the line ... ATARI in capitals,"
    he = [r for r in rs if r[4] in ("לכולם", "עולם,")]
    last = rs[-1]

    one = texts(run(ewr, fnt, ["-x", at(first, 0.33) + "," + at(caps, 0.83)]))
    t = one[0] if one else None
    check(label + "a drag: from a place in a run to one in another", t is not None and
          t.startswith("lcon came in 1992. An Atari in two runs.") and t.endswith("Atari on the next line, AT"), repr(t))
    check(label + "an empty line between paragraphs, a line break between lines",
          t is not None and "two runs.\n\nA line" in t and "amet, the\nend of" in t, repr(t))
    back = texts(run(ewr, fnt, ["-x", at(caps, 0.83) + "," + at(first, 0.33)]))
    check(label + "the same dragged the other way", back == one, repr(back))

    beside = texts(run(ewr, fnt, ["-x", "%d,%d,%d,%d" % (0, caps[1] - 3, 2000, caps[1] - 3)]))
    check(label + "from left of a line to past its end: the line", beside == [caps[4]], repr(beside))
    below = texts(run(ewr, fnt, ["-x", at(last, 0.1) + ",3000,3000"]))
    check(label + "below the page: to the text's end", below and below[0] is not None and
          below[0].endswith("Atari."), repr(below))

    words = texts(run(ewr, fnt, ["-x", at(by["Ata"], 0.5)])) + texts(run(ewr, fnt, ["-x", at(by["ri in two runs."], 0.05)]))
    check(label + "a double click's word, on into the run beside it", words == ["Atari", "Atari"], repr(words))
    hw = texts(run(ewr, fnt, ["-x", at(by["לכולם"], 0.5)]))
    check(label + "a Hebrew word", hw == ["לכולם"], repr(hw))

    rtl = texts(run(ewr, fnt, ["-x", at(by["עולם,"], 0.15) + "," + at(by["לכולם"], 0.2)]))
    check(label + "Hebrew as it is read, across runs drawn right to left", rtl == [", שלום לכול"], repr(rtl))

    alls = texts(run(ewr, fnt, ["-x", "all"]))
    page = texts(run(ewr, fnt, ["-x", "page"]), "copied")
    a = alls[0] if alls else None
    check(label + "Select all: the text seen", a is not None and a.startswith("The Atari Falcon") and
          a.endswith("The last Atari.") and "hidden" not in a and "שלום עולם, שלום לכולם" in a, repr(a))
    check(label + "^C with nothing selected: the page's text", page == [a + "\n"] if a else False, repr(page))

    again = texts(run(ewr, fnt, ["-x", at(first, 0.33) + "," + at(caps, 0.83), "-j", "300", "-j", "700"]))
    flat = [" ".join(x.split()) if x else x for x in again]
    check(label + "the same text when laid out again at other widths", len(again) == 3 and
          flat[0] == flat[1] == flat[2] and again[1] != again[0], repr(again))

    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "s.png")
        run(ewr, fnt, ["-x", at(he[1] if he[1][4] == "עולם," else he[0], 0.15) + "," + at(first, 0.33)], png)
        rows = png_rows(png)
        span = sel_span(rows, first[1] - 3)
        check(label + "marked from the place in the first run", span is not None and
              first[0] + first[2] * 0.25 < span[0] < first[0] + first[2] * 0.4, "%s %s" % (span, first))
        hr = by["שלום"]
        span = sel_span(rows, hr[1] - 3)
        w = by["עולם,"]
        check(label + "a right-to-left run marked from its right", span is not None and span[1] >= hr[0] + hr[2] - 2 and
              w[0] < span[0] < w[0] + w[2], "%s %s %s" % (span, hr, w))


def bidi(ewr, fnt, label):
    rs = runs(ewr, fnt, PAGE2)
    by = {r[4]: r for r in rs}
    page = texts(run(ewr, fnt, ["-x", "page"], page=PAGE2), "copied")
    want = ("\u05d4\u05e4\u05dc\u05e7\u05d5\u05df \u05d9\u05e6\u05d0 \u05d1\u05e9\u05e0\u05ea 1992 "
            "\u05d1\u05d2\u05e8\u05de\u05e0\u05d9\u05d4")
    p = page[0] if page else ""
    check(label + "a right-to-left line with a number: as it is read", p.startswith(want + "\n"), repr(p[:80]))
    check(label + "Latin words in a right-to-left line, Hebrew in a left-to-right one",
          "\u05e9\u05dc\u05d5\u05dd Atari ST \u05e2\u05d5\u05dc\u05dd\n" in p and
          "Hello \u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd world" in p, repr(p))
    check(label + "a superscript and a subscript part of their word", "E = mc2 is famous, H2O too." in p, repr(p))
    first, last = by["\u05d4\u05e4\u05dc\u05e7\u05d5\u05df"], by["\u05d1\u05d2\u05e8\u05de\u05e0\u05d9\u05d4"]
    line = texts(run(ewr, fnt, ["-x", "%d,%d,%d,%d" % (first[0] + first[2] - 1, first[1] - 3, last[0] + 1,
                                                       last[1] - 3)], page=PAGE2))
    check(label + "a right-to-left line dragged over from its right to its left: all of it", line == [want],
          repr(line))
    lat = by["Atari ST"]
    shalom = next(r for r in rs if r[4] == "\u05e9\u05dc\u05d5\u05dd" and r[1] == lat[1])
    half = texts(run(ewr, fnt, ["-x", "%d,%d,%d,%d" % (shalom[0] + shalom[2] - 1, lat[1] - 3,
                                                       lat[0] + 20, lat[1] - 3)], page=PAGE2))
    check(label + "into the Latin words of a right-to-left line", half == ["\u05e9\u05dc\u05d5\u05dd Ata"],
          repr(half))
    comb = by["e\u0301tude, and "]
    acc = texts(run(ewr, fnt, ["-x", "%d,%d,%d,%d" % (comb[0] + 5, comb[1] - 3, comb[0] + 30, comb[1] - 3)],
                    page=PAGE2))
    check(label + "not between a letter and its accent", acc and acc[0] is not None and acc[0].startswith("tu"),
          repr(acc))
    pt = by["\u05e9\u05b8\u05dc\u05d5\u05b9\u05dd"]
    pts = texts(run(ewr, fnt, ["-x", "%d,%d,%d,%d" % (pt[0] + pt[2] - 1, pt[1] - 3, pt[0] + pt[2] - 8, pt[1] - 3)],
                    page=PAGE2))
    check(label + "a Hebrew letter with its point", pts and pts[0] is not None and pts[0].startswith("\u05e9\u05b8"),
          repr(pts))
    far = texts(run(ewr, fnt, ["-x", "300,260"], page=PAGE2))
    check(label + "a double click far from any text: nothing", far == [None], repr(far))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    edition(sys.argv[1], sys.argv[2], "")
    bidi(sys.argv[1], sys.argv[2], "")
    if len(sys.argv) > 3:
        edition(sys.argv[3], sys.argv[2], "ST: ")
        bidi(sys.argv[3], sys.argv[2], "ST: ")
    if fails:
        print("select: %d failed" % fails)
        sys.exit(1)
    print("select: a drag's text as it is read either way, beside a line and below the page, a double click's "
          "word, Hebrew as it is read, Select all and the page's text, the same text laid out again, marked where "
          "it is drawn, right-to-left lines with numbers and Latin, super- and subscripts, accents and points kept with "
          "their letters, no word far from text%s" % (", in 2 editions" if len(sys.argv) > 3 else ""))


if __name__ == "__main__":
    main()
