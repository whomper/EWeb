#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the stack's floor (ec_stack_floor, DESIGN.md, The
stack): pages nested deeper than the stack holds (blocks, tables, lists,
flex and grid boxes, floats, inline elements) are shown, cut where the
stack's floor is, under AddressSanitizer; a page as deep as real ones are
is shown whole.

    deep.py EWRENDER FONT"""
import os
import subprocess
import sys
import tempfile

ewrender, font = sys.argv[1], sys.argv[2]
# the deep pages: the stack the cascade and the layout may take, ewrender
# -S, as the Falcon's (256 KB, the floor 16 KB above its end), so they are
# cut soon and the test is quick (the HTML Standard's li steps look through
# the open elements, and nested flex, grid and floats are measured again
# at each level). The shallow ones (as deep as real pages go) on all of
# the host's stack: its frames are several times the 68030's under
# AddressSanitizer.
STACK_KB = 240
DEEP = 3000
KINDS = {
    "div": ("<div>d", "</div>"),
    "table": ("<table><tr><td>t", "</td></tr></table>"),
    "list": ("<ul><li>l", "</li></ul>"),
    "flex": ('<div style="display:flex"><div>f', "</div></div>"),
    "grid": ('<div style="display:grid"><div>g', "</div></div>"),
    "span": ("<span>s", "</span>"),
    "float": ('<div style="float:left">f', "</div>"),
}
failed = 0


def render(html, stack):
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False) as f:
        f.write("<!DOCTYPE html><title>deep</title>" + html)
    try:
        return subprocess.run([os.path.abspath(ewrender)] + (["-S", str(stack)] if stack else []) +
                              ["-f", os.path.abspath(font), "-d", f.name],
                              capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(f.name)


for kind, (open_, close) in KINDS.items():
    for depth, cut in ((DEEP, True), (60, False)):
        p = render(open_ * depth + close * depth, STACK_KB if cut else 0)
        ok = p.returncode == 0 and ("deep:" in p.stderr) == cut
        if not ok:
            failed += 1
            print("deep: %s %d: rc %d, %s" % (kind, depth, p.returncode, p.stderr[-2000:].strip() or "not cut"))
print("deep: %d pages, %d failed" % (2 * len(KINDS), failed))
sys.exit(1 if failed else 0)
