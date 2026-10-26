#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWeb's speed rig (the guidelines' Speed: the stock machines set it;
Erez, 26 Oct 2026), on the family's rig (common/tests/speed/speed.py):
EWEB.PRG on the stock Falcon (Falcon-4:16) and EWEBST.PRG on the 8 MHz ST
(ST:mono), both built with -DAES_PROFILE=20 (make build/prof/EWEB.PRG
build/prof/EWEBST.PRG), on Erez's TOS when it is here. For each of the
user's actions, the longest stretch between EWeb's event calls against
the guidelines' time:

- the start (AESSTART) and EWeb's own home page shown;
- a page from the disk loaded and laid out: the sample page
  (tests/bench/sample.html), the news page (tests/bench/mkpage.py) and
  the page of pictures (tests/net/server.py's); a load is a long job, so
  its time is Esc's, 100 ms;
- on the news page: a line scrolled (Down), a page scrolled (Shift-Down);
- a letter typed into the address field (^L);
- Settings (^,) opened, and closed with Esc (what it uncovers drawn).

    tests/hatari/speed.py OUT [MACHINE...] [--nvdi] [--prg DIR]

MACHINE is Falcon-4:16 or ST:mono (both when none is named); --nvdi runs
the Falcon under Erez's NVDI (machines.py, AERA_NVDI); --prg the folder
the programs are taken from (build/prof; another for a build before a
change, to time both). Each machine's
table is printed; 1 when any action is over its time.
"""
import os
import re
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "speed"))
sys.path.insert(0, os.path.join(ROOT, "tests", "net"))
import speed  # noqa: E402

LOAD = 100      # a long job answers Esc within 100 ms


def gaps_since(r, start):
    with open(r.log, errors="replace") as f:
        f.seek(start)
        return [int(g) for g in re.findall(r"AESGAP (\d+) ms", f.read())]


def load(r, what, url, most=900):
    """the address typed after ^L and Return: the stretches until EWeb says
    it showed the page and its pictures, and the screen is still"""
    r.key("ctrl+l")
    time.sleep(1)
    start = os.path.getsize(r.log)
    r.type(url)
    time.sleep(0.5)
    typed = gaps_since(r, start)
    start = os.path.getsize(r.log)
    n = r.console().count("EWEB shown ")
    r.key("Return")
    t_end = time.time() + most
    while time.time() < t_end and r.console().count("EWEB shown ") == n:
        time.sleep(2)
    if r.console().count("EWEB shown ") == n:
        print("NOTE  %s: not shown in %d s" % (what, most), flush=True)
    while time.time() < t_end and not r.still():
        pass
    g = gaps_since(r, start)
    r.n += 1
    r.shot("%02d-%s" % (r.n, re.sub(r"[^a-z0-9]+", "-", what.lower()).strip("-")))
    r.results.append((what, max(g) if g else 0, LOAD))
    return typed


def run(out, machine, nvdi, prgdir):
    st = machine.startswith("ST")
    prg = "EWEBST.PRG" if st else "EWEB.PRG"
    r = speed.Rig(os.path.join(out, re.sub(r"[^a-z0-9]+", "-", machine.lower()) + ("-nvdi" if nvdi else "")),
                  machine, nvdi=nvdi, display=":93" if st else ":92")
    try:
        r.put(os.path.join(prgdir, prg), "EWEB")
        r.put(os.path.join(ROOT, "binary", "EWEB.FNT"), "EWEB")
        if not st:
            r.put(os.path.join(ROOT, "binary", "EWEBJS.OVL"), "EWEB")
        r.put(os.path.join(ROOT, "tests", "bench", "sample.html"))
        os.rename(os.path.join(r.hd, "SAMPLE.HTML"), os.path.join(r.hd, "SAMPLE.HTM"))
        r.put(os.path.join(ROOT, "build", "news", "NEWS.HTM"))
        r.put(os.path.join(ROOT, "build", "news", "NEWS.CSS"))
        import server
        with open(os.path.join(r.hd, "PICS.HTM"), "wb") as f:
            f.write(server.PICS_PAGE)
        for k, (data, _) in server.PICS.items():
            p = os.path.join(r.hd, k.upper())
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(data)
        r.write(os.path.join("EWEB", "EWEB.INF"), "HOME=\n")
        r.start("C:\\EWEB\\" + prg, ready="EWEB shown ", boot=600 if st else 300)
        g = gaps_since(r, 0)
        r.results.append(("the home page shown", max(g) if g else 0, LOAD))
        r.shot("00-home")
        load(r, "the sample page loaded", "file:///C:/SAMPLE.HTM")
        typed = load(r, "the news page loaded", "file:///C:/NEWS.HTM")
        r.results.append(("the address typed (each letter)", max(typed) if typed else 0, speed.KEY))
        r.act("a line scrolled", speed.LINE, lambda: r.key("Down"))
        r.act("a page scrolled", speed.PAGE, lambda: r.key("shift+Down"))
        r.act("Settings opened", speed.DIALOG, lambda: r.key("ctrl+comma"))
        r.act("Settings closed (what it uncovers)", speed.WINDOW, lambda: r.key("Escape"))
        load(r, "the pictures' page loaded", "file:///C:/PICS.HTM", most=1800)
        r.key("ctrl+q")
        time.sleep(3)
        return r.report()
    finally:
        r.stop()


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    nvdi = "--nvdi" in a
    a = [x for x in a if x != "--nvdi"]
    prgdir = os.path.join(ROOT, "build", "prof")
    if "--prg" in a:
        i = a.index("--prg")
        prgdir = os.path.abspath(a[i + 1])
        del a[i:i + 2]
    out, machines = a[0], a[1:] or ["Falcon-4:16", "ST:mono"]
    bad = 0
    for m in machines:
        bad |= run(out, m, nvdi and m.startswith("Falcon"), prgdir)
    sys.exit(bad)


if __name__ == "__main__":
    main()
