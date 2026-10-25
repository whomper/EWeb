#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""docs/screenshot.png, the README's picture: EWEB.PRG on the Falcon in
256 colours (EWeb's own palette, as it starts; Erez, 25 Oct 2026) showing a
site of the web as it is, not EWeb's own start page. The site comes
through a webgate run here, as through the Pi's gateway (webgate takes
HTTPS_PROXY upstream when this machine has one; CAFILE: the roots it checks
the sites, or the proxy, against). The page is captured once it and its
pictures have come, scrolled down SCROLL device px (0: its top).

    tools/screenshot.py URL [OUT.PNG] [--scroll SCROLL] [--cafile CAFILE]

Needs make (binary/EWEB.PRG, EWEB.FNT), Hatari and the family's
machines (tests/hatari, common/tools/machines.py)."""
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def palette_png(src, out):
    """the capture as a palette PNG of its own colours, every pixel as
    it was (a 256-colour screen has at most 256)"""
    from PIL import Image
    im = Image.open(src).convert("RGB")
    px = list(im.get_flattened_data())
    cols = sorted(set(px))
    if len(cols) > 256:
        shutil.copy(src, out)
        return
    idx = {c: i for i, c in enumerate(cols)}
    p = Image.new("P", im.size)
    pal = [v for c in cols for v in c]
    p.putpalette(pal + [0] * (768 - len(pal)))
    p.putdata([idx[c] for c in px])
    p.save(out, optimize=True)


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("-"):
        sys.exit(__doc__)
    url, out, scroll = a[0], os.path.join(ROOT, "docs", "screenshot.png"), 0
    cafile = os.environ.get("SSL_CERT_FILE") or "/etc/ssl/certs/ca-certificates.crt"
    i = 1
    while i < len(a):
        if a[i] == "--scroll" and i + 1 < len(a):
            scroll = int(a[i + 1])
            i += 2
        elif a[i] == "--cafile" and i + 1 < len(a):
            cafile = a[i + 1]
            i += 2
        else:
            out = a[i]
            i += 1
    work = os.path.join(ROOT, "build", "shot")
    os.makedirs(work, exist_ok=True)
    # the rig's Hatari, its own ports and display (another may be running)
    os.environ.setdefault("GUI_SLOT", "3")
    sys.argv = [sys.argv[0], work, "none"]
    sys.path.insert(0, os.path.join(ROOT, "tests", "hatari"))
    import gui_test as g
    gate = subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "webgate.py"), "--port", str(g.GATE),
                             "--bind", "127.0.0.1", "--cafile", cafile],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    hd = g.hd_for("shot", url, "FALCON=0\r\nGATE=127.0.0.1\r\nGATEPORT=%d\r\nPENS=0\r\n" % g.GATE)
    xvfb = g.machines.xvfb(g.DISPLAY)	# (windowed: the mouse moved out of the way)
    r = g.Run("shot", "Falcon:256", hd, windowed=True, network=True)
    try:
        ready = r.wait(r"ready ", 300)
        ev = r.wait(r"shown ", 900)
        pics = r.wait(r"pictures ", 2400)
        print(ready or "no ready line")
        print(ev or "not shown")
        print(pics or "the pictures didn't finish")
        if ev is None or not ev.startswith("shown "):
            sys.exit("screenshot: the page wasn't shown")
        if scroll > 0:
            # the window scrolled down by its arrow, a line at a time
            # (DOWN: the rig's), until it is about there
            for _ in range(max(1, scroll // 16)):
                r.key(g.DOWN)
                time.sleep(0.2)
        r.mouse.move(612, 444)	# (the status bar's end: not over the page)
        time.sleep(4)
        shot = r.shot(0)
        time.sleep(2)
        palette_png(shot, out)
        print("screenshot: %s" % out)
    finally:
        r.end()
        gate.terminate()
        gate.wait(10)
        if xvfb is not None:
            xvfb.terminate()


if __name__ == "__main__":
    main()
