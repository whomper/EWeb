#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of frames and iframes (the HTML Standard, 4.8.5 and
15.6), in both editions: tests/layout/frameset.html and frames.html
through ewrender, clicked with -M X,Y as EWEB.PRG clicks (in a frame too)
and -M back.

- the frameset's frames each show their page, the menu's links reach the
  frame they name ("main"), a link with no target its own frame, one to
  _top the window; Back shows each frame as it was before, the page's
  address the same;
- a form in a frame is sent from the frame and its answer shown there;
- a details in a frame opens and closes, the page laid out again;
- Find reaches a frame's words, the frame scrolled to them;
- a frame whose file isn't there says so in its box;
- an iframe page: a link whose target is a named iframe loads it there;
- the ST edition (EWRENDERST) the same as EWEB.PRG's.

    tests/layout/frames.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
fails = 0


def check(name, ok, detail=""):
    global fails
    if not ok:
        fails += 1
        print("frames: FAIL %s %s" % (name, detail))


def run(ewr, fnt, page, clicks=(), more=()):
    args = [ewr, "-f", fnt, "-d"] + list(more)
    for c in clicks:
        args += ["-M", c]
    p = subprocess.run(args + [page], capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        sys.exit("frames: ewrender failed\n" + p.stderr)
    return p.stdout.splitlines()


def texts(out):
    """each text item: (x, its baseline, its text)"""
    r = []
    for line in out:
        m = re.match(r'text (-?\d+),(-?\d+) \d+x\d+ .* "(.*)"$', line)
        if m:
            r.append((int(m.group(1)), int(m.group(2)), m.group(3)))
    return r


def at(out, words):
    """a place to click on the text with these words: a little in from its
    start, just above its baseline (the dump gives the baseline)"""
    for x, y, t in texts(out):
        if words in t:
            return "%d,%d" % (x + 3, y - 4)
    return None


def button(out, label):
    """a place to click on a control showing label"""
    for line in out:
        m = re.match(r'control (-?\d+),(-?\d+) \d+x\d+ .* "(.*)"$', line)
        if m and m.group(3) == label:
            return "%d,%d" % (int(m.group(1)) + 3, int(m.group(2)) + 5)
    return None


def has(out, words):
    return any(words in t for _, _, t in texts(out))


def edition(ewr, fnt, name):
    fs = os.path.join(HERE, "frameset.html")
    out = run(ewr, fnt, fs)
    check("%s: the frameset's frames" % name, out[0].startswith("page 608 x 400") and has(out, "Menu") and
          has(out, "Page one, in green.") and has(out, "Page two starts here") and has(out, "Nested, with"),
          "\n".join(out[:12]))
    one, form, top, own = at(out, "One in main"), at(out, "A form in main"), at(out, "The iframes page"), \
        at(out, "Two in the menu")
    # a link into main, then Back
    o = run(ewr, fnt, fs, [one])
    main = [t for _, y, t in texts(o) if 70 < y < 282 and t.startswith("Page")]
    check("%s: a link reaches the frame it names" % name, any(l.startswith("followed: 0 \"Frames\"") for l in o) and
          main[:1] == ["Page one, in green."], "\n".join(o[:30]))
    o = run(ewr, fnt, fs, [one, "back"])
    main = [t for _, y, t in texts(o) if 70 < y < 282 and t.startswith("Page")]
    check("%s: Back shows the frame as it was" % name, any(l.startswith("back: 0 \"Frames\"") for l in o) and
          main[:1] == ["Page two starts here, with "], "\n".join(o[:30]))
    # a link with no target: its own frame (the menu)
    o = run(ewr, fnt, fs, [own])
    check("%s: a link with no target stays in its frame" % name,
          not has(o, "One in main") and has(o, "Page two starts here") and has(o, "Menu") is False,
          "\n".join(o[:20]))
    # _top: the window
    o = run(ewr, fnt, fs, [top])
    check("%s: _top goes to the window" % name, any(l.startswith("followed: 0 \"Iframes\"") for l in o), "\n".join(o[:6]))
    # a form in a frame, sent from it
    o = run(ewr, fnt, fs, [form])
    send = button(o, "Send")
    o = run(ewr, fnt, fs, [form] + ([send] if send else []))
    check("%s: a form in a frame is answered in it" % name, send is not None and
          any(l.startswith("sent: 0 \"Frames\"") for l in o) and has(o, "Page one, in green."), "\n".join(o[:30]))
    # a details in a frame
    det = at(out, "More in a details")
    o = run(ewr, fnt, fs, [det])
    o2 = run(ewr, fnt, fs, [det, det])
    check("%s: a details in a frame opens and closes" % name,
          any("details -: opened" in l for l in o) and has(o, "Folded words, opened.") and
          any("details -: closed" in l for l in o2) and not has(o2, "Folded words"), "\n".join(o[:12]))
    # Find in a frame (scrolled to the word)
    o = run(ewr, fnt, fs, more=["-G", "quokka"])
    f = [l for l in o if l.startswith("found:")]
    q = [y for _, y, t in texts(o) if "quokka" in t]
    check("%s: Find reaches a frame's words" % name, f and "1 of 1" in f[0] and q and 72 < q[0] < 280,
          "\n".join(f) + " %s" % q)
    # an iframe page: a link into a named iframe
    ip = os.path.join(HERE, "frames.html")
    o = run(ewr, fnt, ip)
    o2 = run(ewr, fnt, ip, [at(o, "One into the box")])
    check("%s: a link into a named iframe" % name, has(o, "Page two starts here") and has(o2, "Page one, in green.") and
          any(l.startswith("followed: 0 \"Iframes\"") for l in o2) and has(o2, "Nested, with"), "\n".join(o2[:16]))
    # a frame whose file isn't there
    tmp = tempfile.mkdtemp()
    try:
        page = os.path.join(tmp, "gone.html")
        with open(page, "w") as fh:
            fh.write("<!DOCTYPE html><title>Gone</title><iframe src=nothere.html width=300 height=120></iframe>")
        o = run(ewr, fnt, page)
        check("%s: a frame whose file isn't there says so" % name, has(o, "EWeb can't open"), "\n".join(o[:12]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


ewr, fnt = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
edition(ewr, fnt, "EWEB.PRG")
if len(sys.argv) > 3:
    edition(os.path.abspath(sys.argv[3]), fnt, "the ST edition")
if fails:
    sys.exit("frames: %d failed" % fails)
print("frames: a frameset's frames, links into frames and to the window, Back, a form, details and Find in a frame, "
      "iframes, in %d edition%s" % (2 if len(sys.argv) > 3 else 1, "s" if len(sys.argv) > 3 else ""))
