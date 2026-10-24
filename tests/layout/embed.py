#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of bidirectional embeddings (UAX #9's explicit
levels; CSS Writing Modes 3, 2.4.2; the HTML Standard, 15.3.5): each
case a paragraph, its words as drawn from left to right ("~" before one
drawn right to left), worked out from UAX #9 by hand.

- <bdo>, unicode-bidi: bidi-override and RLO ... PDF in the text: the
  words in it in the direction given, each drawn so;
- dir on an inline element (an isolate), a <bdi> and dir=auto: what is
  in it ordered by its own direction, and its outside not by what is in
  it (the "User 3 : name" a plain span gives);
- LRM and RLM: a neutral beside one goes its way;
- unicode-bidi: plaintext (a <pre dir=auto>): each paragraph by its
  first strong letter, and aligned so;
- an embedding (unicode-bidi: embed) left open at a line's end goes on
  on the next, as a paragraph's levels are worked out whole;
- a right-to-left table (its collapsed borders too), flex row and grid:
  their first cell, item or column at the right, row-reverse from the
  left; and in a right-to-left grid the third item under the first.

    tests/layout/embed.py EWRENDER EWEB.FNT"""
import os
import re
import subprocess
import sys
import tempfile

ewrender, font = sys.argv[1], os.path.abspath(sys.argv[2])
A, B, C, D = "אב", "גד", "שלום", "עולם"
fails = 0

CASES = [
    ("bdo dir=rtl in English", "<p>abc <bdo dir=rtl>def ghi</bdo> jkl", ["abc ~ghi ~def jkl"]),
    ("bdo dir=ltr in Hebrew", "<p dir=rtl>%s <bdo dir=ltr>%s %s</bdo> %s" % (A, B, C, D),
     ["~%s %s %s ~%s" % (D, B, C, A)]),
    ("bidi-override in CSS", "<p>abc <span style='unicode-bidi:bidi-override;direction:rtl'>def ghi</span> jkl",
     ["abc ~ghi ~def jkl"]),
    ("RLO ... PDF in the text", "<p>abc &#x202E;def ghi&#x202C; jkl", ["abc ~ghi ~def jkl"]),
    ("LRO ... PDF in Hebrew", "<p dir=rtl>%s &#x202D;%s %s&#x202C; %s" % (A, B, C, D),
     ["~%s %s %s ~%s" % (D, B, C, A)]),
    ("dir=rtl on a span: a number and English in it", "<p>Price: <span dir=rtl>%s 5 world</span>!" % C,
     ["Price: world 5 ~%s !" % C]),
    ("dir=ltr on a span in Hebrew", "<p dir=rtl>%s <span dir=ltr>abc def</span> %s" % (C, D),
     ["~%s abc def ~%s" % (D, C)]),
    ("a plain span: its Hebrew takes the colon and the number", "<p>User <span>%s</span>: 3 posts" % C,
     ["User 3 ~: ~%s posts" % C]),
    ("bdi: isolated, the colon and the number the English's", "<p>User <bdi>%s</bdi>: 3 posts" % C,
     ["User ~%s : 3 posts" % C]),
    ("dir=auto: Hebrew first, right to left", "<p>abc <span dir=auto>%s 5</span> def" % C,
     ["abc 5 ~%s def" % C]),
    ("dir=auto: English first, left to right", "<p dir=rtl>%s <span dir=auto>abc 5</span> %s" % (C, D),
     ["~%s abc 5 ~%s" % (D, C)]),
    ("RLM: the full stop goes with the Hebrew", "<p>%s.&#x200F; abc" % C, ["~. ~%s abc" % C]),
    ("no RLM: the full stop with the English line", "<p>%s. abc" % C, ["~%s . abc" % C]),
    ("LRM: the mark stays by the English", "<p dir=rtl>%s world!&#x200E;" % C, ["world! ~%s" % C]),
    ("no LRM: the mark at the line's left", "<p dir=rtl>%s world!" % C, ["~! world ~%s" % C]),
    ("pre dir=auto: each paragraph its own way", "<pre dir=auto>abc def\n%s xyz</pre>" % C,
     ["abc def", "xyz ~%s" % C]),
    ("a right-to-left table: its first cell at the right", "<table dir=rtl><tr><td>one<td>two<td>three</table>",
     ["three two one"]),
    ("and its collapsed borders' columns too", "<table dir=rtl style='border-collapse:collapse'><tr>"
     "<td style='border:1px solid'>one<td>two</table>", ["two one"]),
    ("a right-to-left flex row: its first item at the right", "<div dir=rtl style='display:flex'><span>one</span>"
     "<span>two</span><span>three</span></div>", ["three two one"]),
    ("row-reverse right to left: from the left", "<div dir=rtl style='display:flex;flex-direction:row-reverse'>"
     "<span>one</span><span>two</span></div>", ["one two"]),
    ("a right-to-left grid: its first column at the right", "<div dir=rtl style='display:grid;"
     "grid-template-columns:60px 60px'><div>one</div><div>two</div><div>three</div></div>", ["two one", "three"]),
    ("an embedding open across lines", "<p style='width:140px'>abc <span style='unicode-bidi:embed;"
     "direction:rtl'>%s %s %s %s %s</span> def" % (A, B, C, D, A),
     None),
]


def lines(page, extra=()):
    """the page's lines: each its text items (x, word, drawn right to left)"""
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
        f.write("<!DOCTYPE html><meta charset=utf-8><body style='font-size:13px'>" + page)
        path = f.name
    try:
        out = subprocess.run([ewrender, "-f", font, "-d"] + list(extra) + [path], capture_output=True,
                             text=True, timeout=60).stdout
    finally:
        os.unlink(path)
    rows = {}
    for line in out.splitlines():
        m = re.match(r'text (-?\d+),(-?\d+) (\d+)x\d+ \S+ \S+( rtl)? .*"(.*)"$', line)
        if m:
            rows.setdefault(int(m.group(2)), []).append((int(m.group(1)), m.group(5).strip(), bool(m.group(4)),
                                                         int(m.group(3))))
    return [sorted(rows[y]) for y in sorted(rows)]


def words(row):
    return " ".join(("~" if r else "") + w for _, w, r, _ in row if w)


for name, page, want in CASES:
    got = lines(page)
    if want is None:
        # an embedding left open at a line's end: its Hebrew words, on
        # whichever line, all at a right-to-left level, each line's
        # Hebrew run in the right-to-left order (the later word left)
        flat = [w for row in got for w in row]
        heb = [(x, w, r) for x, w, r, _ in flat if w and w[0] >= "֐"]
        ok = len(got) >= 2 and len(heb) == 5 and all(r for _, _, r in heb)
        rows = [[w for _, w, r, _ in row if w and w[0] >= "֐"] for row in got]
        seq = [w for row in rows for w in reversed(row)]
        ok = ok and seq == [A, B, C, D, A]
        if not ok:
            fails += 1
            print("embed: %s: %s" % (name, [words(r) for r in got]))
        continue
    have = [words(r) for r in got]
    if have != want:
        fails += 1
        print("embed: %s: %s, not %s" % (name, have, want))

# plaintext: the right-to-left paragraph aligned to the right (300 CSS
# pixels are 240 of the screen's, from 6)
got = lines("<pre dir=auto style='width:300px'>abc\n%s</pre>" % C)
if len(got) != 2 or not got[1] or not got[0] or got[1][-1][0] + got[1][-1][3] < 240 or got[0][0][0] > 20:
    fails += 1
    print("embed: pre dir=auto: its Hebrew paragraph not at the right, its English one not at the left: %s" % got)

print("embed: %s" % ("all %d passed" % (len(CASES) + 1) if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
