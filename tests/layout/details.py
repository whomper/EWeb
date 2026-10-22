#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of details and summary clicked (the HTML Standard,
4.11), in both editions: tests/layout/details.html through ewrender
with -K, which clicks a summary (or a details, through its summary) as
EWEB.PRG and EWEBST.PRG do (page_summary_at, br_details).

- a closed details opened and closed again: the page as tall as before,
  its display list the same;
- one without a summary opened by its "Details";
- one of a group (name="g") opened: the group's open one closed; one in
  a template is no rival;
- an open one's summary drawn first, whatever comes before it;
- a summary laid out inline (inline-block, inline) clicked by its line,
  and one with display: contents (no box) by its text's;
- a details in a summary opened by its own summary, not the outer one's;
- an address with a #fragment in a closed details: the details opened,
  the window at the place ("top:"); a link to it followed (EWEB.PRG's
  edition: the ST edition's -K clicks only summaries), after the details
  was opened and closed, the same;
- the ST edition (EWRENDERST, ewrender built -DEW_ST) the same as
  EWEB.PRG's, click for click.

    tests/layout/details.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "details.html")
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("details: FAIL %s %s" % (name, detail))


def run(ewr, fnt, clicks, fragment="", full=False):
    args = [ewr, "-f", fnt, "-d"]
    for c in clicks:
        args += ["-K", c]
    p = subprocess.run(args + [PAGE + fragment], capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("details: ewrender failed\n" + p.stderr)
    out = p.stdout.splitlines()
    did = [line for line in out if line.startswith(("details ", "followed: ", "top: "))] if full else \
        [line for line in out if line.startswith("details ")]
    page = [line for line in out if line.split(" ", 1)[0] in ("page", "fill", "text", "summary")]
    return did, page


def text_y(page, words):
    for line in page:
        m = re.match(r'text \d+,(\d+) .* "(.*)"$', line)
        if m and words in m.group(2):
            return int(m.group(1))
    return None


def state(page, det):
    for line in page:
        m = re.match(r"summary \S+ \S+ (open|closed) (\S+)$", line)
        if m and m.group(2) == det:
            return m.group(1)
    return None


def edition(ewr, fnt, links):
    _, before = run(ewr, fnt, [])
    did, after = run(ewr, fnt, ["s1", "s1"])
    check("a summary clicked opens its details, again closes it",
          len(did) == 2 and did[0].startswith("details c1: opened") and did[1].startswith("details c1: closed"),
          str(did))
    check("closed again, the page as it was", after == before)
    did, page = run(ewr, fnt, ["s1"])
    check("opened, the details' text shown", any('"Text straight in the details, hidden."' in l for l in page)
          and state(page, "c1") == "open")
    did, page = run(ewr, fnt, ["nosum"])
    check("no summary: its own Details opens it", did[:1] and did[0].startswith("details nosum: opened")
          and any("No summary: a" in l for l in page), str(did))
    did, page = run(ewr, fnt, ["g2"])
    check("a group's other one closed", state(page, "gd1") == "closed" and state(page, "gd2") == "open",
          str([l for l in page if l.startswith("summary")]))
    did, page = run(ewr, fnt, ["s3"])
    check("an inner details opens in its outer one", state(page, "inner") == "open" and
          any('"Inner text, hidden."' in l for l in page))
    check("a template's details is no rival", state(page, "h1") == "open")
    a, b = text_y(page, "Open: its summary drawn first"), text_y(page, "Before the summary, drawn after it.")
    check("an open one's summary first", a is not None and b is not None and a < b, "%s %s" % (a, b))
    for sid, det, under in (("sib", "ib", "Under the button"), ("sil", "il", "Under the inline one"),
                            ("sdc", "dc", "Under the contents one")):
        did, page = run(ewr, fnt, [sid])
        check("a summary laid out inline or with no box (%s) opens its details" % det,
              did[:1] and did[0].startswith("details %s: opened" % det) and text_y(page, under) is not None,
              str(did))
    did, page = run(ewr, fnt, ["sin"])
    check("a details in a summary opens, not the outer one", did[:1] and did[0].startswith("details din: opened")
          and state(page, "dout") == "closed", str(did))
    did, page = run(ewr, fnt, [], "#c1p", True)
    y = text_y(page, "A paragraph, hidden.")
    m = re.match(r"top: (\d+)$", did[0] if did else "")
    check("a #fragment in a closed details: opened, and scrolled there", m is not None and
          state(page, "c1") == "open" and y is not None and int(m.group(1)) < y <= int(m.group(1)) + 24,
          str(did) + " " + str(y))
    if links:
        did, page = run(ewr, fnt, ["s1", "s1", "toc1p"], full=True)
        m = re.match(r"followed: \d+ .*#c1p top (\d+) opened$", did[2] if len(did) > 2 else "")
        check("a link to a place in a details opened and closed again: opened, and scrolled there",
              m is not None and state(page, "c1") == "open" and int(m.group(1)) < y <= int(m.group(1)) + 24,
              str(did))
    return run(ewr, fnt, ["s1", "nosum", "g2", "s3", "s2", "sib", "sil", "sdc", "sin"])


def main():
    ewr, fnt = sys.argv[1], sys.argv[2]
    falcon = edition(ewr, fnt, True)
    if len(sys.argv) > 3:
        st = edition(sys.argv[3], fnt, False)
        check("the ST edition's the same", st == falcon)
    print("details: %s" % ("clicked and laid out again as kept, in %d edition%s" %
                           (2 if len(sys.argv) > 3 else 1, "s" if len(sys.argv) > 3 else "")
                           if not fails else "%d failed" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
