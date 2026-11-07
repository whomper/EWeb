#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A forum topic's page of avatars, scrolled while its pictures come.

After Erez's crash in 0.73 scrolling an atariage.com topic (7 Nov 2026: a
bus error in TOS's AES, low memory overwritten by supervisor code; the
DSP writes a JPEG's pixels and a dither's pens in supervisor mode).
The page is made here, the same every run, of no real data: 60 posts in
flex rows, each with a JPEG avatar at 60 to 90 px (a third of them
progressive) and a 24 px copy of another's. EWEB.PRG opens it from the
disk on a stock Falcon with its DSP (Falcon-4:16; under NVDI with
AERA_WITH_NVDI=1, machines.py), and while the avatars arrive it is
paged down and scrolled up with a held arrow, three times over. It
fails on:
- a crash (CRASH.LOG next to EWEB.PRG, common's crash.c);
- the DSP refusing a picture's destination ("dsp: bad destination" in
  EWEB.LOG, common's dspjpeg.c);
- a hang (^Q at the end not answered with "quit" within a minute);
- the page or its pictures not shown.

    tests/hatari/avatars.py [OUT] [MACHINE]

MACHINE is Falcon-4:16 by default (machines.py's names). A step that
can't run here says SKIPPED and the summary counts it (the
guidelines' 1.2).
"""
import io
import os
import random
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "avatars"))
MACHINE = sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:16"
sys.argv = [sys.argv[0], OUT, "none"]	# (gui_test's groups: none of its own)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gui_test as g  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

PAGE_DOWN, UP, HOME = 0x51, 0x48, 0x47


def make_page(hd):
    """the topic: 60 posts, their avatars made with a fixed seed"""
    rnd = random.Random(7)
    os.makedirs(os.path.join(hd, "AV"), exist_ok=True)
    posts = []
    for i in range(60):
        w = rnd.choice([90, 100, 120, 150])
        im = Image.new("RGB", (w, w), (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)))
        d = ImageDraw.Draw(im)
        for _ in range(30):
            x, y = rnd.randrange(w), rnd.randrange(w)
            d.ellipse([x, y, x + 4 + rnd.randrange(20), y + 4 + rnd.randrange(20)],
                      fill=(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)))
        b = io.BytesIO()
        im.save(b, "JPEG", quality=85, progressive=(i % 3 == 0))
        with open(os.path.join(hd, "AV", "A%02d.JPG" % i), "wb") as f:
            f.write(b.getvalue())
        side = 60 + i % 4 * 10
        posts.append("<div class=post><div class=side><img src='AV/A%02d.JPG' width=%d height=%d><br>user%d</div>"
                     "<div class=body><img src='AV/A%02d.JPG' width=24 height=24 style='float:right'>"
                     "<p>%s</p><p>%s</p></div></div>" %
                     (i, side, side, i, (i + 7) % 60, "Building and testing the board, page by page. " * 10,
                      "A reply that quotes the post before it. " * 6))
    with open(os.path.join(hd, "TOPIC.HTM"), "w") as f:
        f.write("<!DOCTYPE html><title>Avatars</title><style>.post{display:flex;border:1px solid #ccc;"
                "margin:8px}.side{width:110px;padding:6px;background:#eef}.body{flex:1;padding:6px}</style>" +
                "".join(posts))


def main():
    nvdi = bool(os.environ.get("AERA_WITH_NVDI"))
    name = "avatars" + ("-nvdi" if nvdi else "")
    hd = g.hd_for(name, r"C:\TOPIC.HTM", "LOG=1\r\n")
    make_page(hd)
    xvfb = g.machines.xvfb(g.DISPLAY)
    r = g.Run(name, MACHINE, hd, windowed=True)
    try:
        ev = r.wait(r"shown ", 600)
        g.check("avatars: the topic shown on %s%s" % (MACHINE, " under NVDI" if nvdi else ""),
                ev is not None and ev.startswith("shown file:///C:/TOPIC.HTM "), str(r.events()[-6:]))
        if ev is None:
            return
        # paged down and scrolled back while the avatars arrive
        for _ in range(3):
            for _ in range(12):
                r.key(PAGE_DOWN)
                time.sleep(0.3)
            for _ in range(40):
                r.key(UP)
                time.sleep(0.05)
            r.key(HOME)
            time.sleep(1)
        pics = r.wait(r"pictures file:///C:/TOPIC.HTM", 600)
        r.shot(0)
        g.check("avatars: their pictures all came (%s)" % (pics or "none"),
                pics is not None and pics.endswith(" 60"), str(r.events()[-6:]))
        r.key(g.Q, (g.CTRL,))
        g.check("avatars: ^Q answered (no hang)", r.wait(r"quit$", 60) is not None, str(r.events()[-6:]))
    finally:
        r.end()
        if xvfb is not None:
            xvfb.terminate()
    crash = [f for f in os.listdir(hd) if f.lower() == "crash.log"]
    g.check("avatars: no crash (CRASH.LOG)", not crash,
            open(os.path.join(hd, crash[0]), errors="replace").read()[:1500] if crash else "")
    logs = [f for f in os.listdir(hd) if f.lower() == "eweb.log"]
    text = open(os.path.join(hd, logs[0]), errors="replace").read() if logs else ""
    if not logs:
        g.skip("avatars: no EWEB.LOG to look in for the DSP's refusals")
    else:
        bad = [l for l in text.splitlines() if "dsp: bad destination" in l]
        g.check("avatars: the DSP refused no destination", not bad, "\n".join(bad[:5]))


if __name__ == "__main__":
    main()
    print("avatars: %s" % ("%d failed" % g.fails if g.fails else
                           "passed, %d skipped" % g.skipped if g.skipped else "all passed"))
    sys.exit(1 if g.fails else 0)
