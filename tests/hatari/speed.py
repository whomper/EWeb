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

- the start: AESSTART and the stretches until EWeb's window is drawn
  (its toolbar, its bars and the page's white: "EWEB window drawn"),
  the guidelines' "its window drawn"; then EWeb's own home page, made
  and shown as any page is, a load (each stretch Esc's 100 ms);
- a page from the disk loaded and laid out: the sample page
  (tests/bench/sample.html), the news page (tests/bench/mkpage.py) and
  the page of pictures (tests/net/server.py's); a load is a long job, so
  its time is Esc's, 100 ms;
- on the news page: a line scrolled (Down), a page scrolled (Shift-Down);
- a letter typed into the address field (^L);
- Settings (^,) opened, and closed by its Cancel (what it uncovers drawn);
- the About box opened from the Desk menu (the guidelines' Speed: 200 ms
  on an 8 MHz ST in low or medium, else a dialog's 150; Erez, 2 Nov
  2026), and closed by Esc (what it uncovers drawn).

    tests/hatari/speed.py OUT [MACHINE...] [--nvdi] [--prg DIR]

MACHINE is Falcon-4:16, ST:mono or ST:med (the first two when none is
named), or Falcon-14:16 (Falcon-4 with 14 MB of ST RAM, as Erez runs his
with its DFB1X and TT-RAM off); --nvdi runs each under Erez's NVDI with its GDOS font (machines.py,
AERA_NVDI; on the ST too, Erez, 2 Nov 2026); --prg the folder
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


def scrolls_since(r, start):
    """the scrolls whose window came whole since start: (ms from their
    event, the page's top), from the speed build's AESSCROLL lines"""
    with open(r.log, errors="replace") as f:
        f.seek(start)
        return [(int(a), int(b)) for a, b in re.findall(r"AESSCROLL (\d+) ms top (-?\d+)", f.read())]


def held(r, key, what, secs=3):
    """a key held down for secs: how far the page went a second, how many
    times the window came whole, the longest wait for it, the longest
    stretch between event calls"""
    start = os.path.getsize(r.log)
    con = r.console()
    tops = re.findall(r"AESSCROLL \d+ ms top (-?\d+)", con)
    top0 = int(tops[-1]) if tops else 0
    r.xdo("keydown", key)
    time.sleep(secs)
    r.xdo("keyup", key)
    time.sleep(1)
    r.still()
    s = scrolls_since(r, start)
    gaps = gaps_since(r, start)
    r.n += 1
    r.shot("%02d-%s" % (r.n, re.sub(r"[^a-z0-9]+", "-", what.lower()).strip("-")))
    if not s:
        return (what, None, "no scroll came whole")
    rows = abs(s[-1][1] - top0)
    return (what, max(ms for ms, _ in s),
            "%d rows a second, whole %d times, longest stretch %d ms" % (rows // secs, len(s), max(gaps) if gaps else 0))


def pages_fresh(r, n=3):
    """pages the kept page hasn't made yet, as Erez's Page Downs on a
    stock Falcon (4 Nov 2026): the page's end (Shift-ClrHome), then n
    pages up a third of a second apart; each one's window whole, from its
    key"""
    out = []
    start = os.path.getsize(r.log)
    r.key("shift+Home")
    time.sleep(1)
    r.still()
    s = scrolls_since(r, start)
    out.append(("the page's end (nothing kept there)", s[-1][0] if s else None, ""))
    start = os.path.getsize(r.log)
    for _ in range(n):
        r.key("shift+Up")
        time.sleep(0.33)
    time.sleep(1)
    r.still()
    s = scrolls_since(r, start)
    gaps = gaps_since(r, start)
    r.n += 1
    r.shot("%02d-pages-up" % r.n)
    out.append(("%d pages up, one after another" % n, max(ms for ms, _ in s) if s else None,
                "whole %d times, longest stretch %d ms" % (len(s), max(gaps) if gaps else 0)))
    return out


def pages_read(r, n=4, rest=10):
    """the page's top read for rest seconds (EWeb draws on down the page
    meanwhile), then n pages down a third of a second apart: each one's
    window whole, from its key (Erez, 4 Nov 2026)"""
    r.key("Home")
    time.sleep(1)
    r.still()
    time.sleep(rest)
    start = os.path.getsize(r.log)
    for _ in range(n):
        r.key("shift+Down")
        time.sleep(0.33)
    time.sleep(1)
    r.still()
    s = scrolls_since(r, start)
    gaps = gaps_since(r, start)
    r.n += 1
    r.shot("%02d-pages-read" % r.n)
    return ("%d pages down after %d s at the top" % (n, rest), max(ms for ms, _ in s) if s else None,
            "whole %d times, longest stretch %d ms" % (len(s), max(gaps) if gaps else 0))


def run(out, machine, nvdi, prgdir):
    st = machine.startswith("ST")
    prg = "EWEBST.PRG" if st else "EWEB.PRG"
    # Falcon-14: a stock Falcon (16 MHz, no FPU, no TT-RAM) with 14 MB of
    # ST RAM, Erez's own with its DFB1X and TT-RAM off (4 Nov 2026):
    # Falcon-4's settings and Hatari's --memsize 14 after them
    base, extra = machine, ()
    if machine.startswith("Falcon-14"):
        base, extra = "Falcon-4" + machine[len("Falcon-14"):], ("--memsize", "14")
    r = speed.Rig(os.path.join(out, re.sub(r"[^a-z0-9]+", "-", machine.lower()) + ("-nvdi" if nvdi else "")),
                  base, nvdi=nvdi, display=":93" if st else ":92", extra=extra)
    if extra:
        print("NOTE  %s: %s with %s" % (machine, base, " ".join(extra)), flush=True)
    try:
        r.put(os.path.join(prgdir, prg), "EWEB")
        r.put(os.path.join(ROOT, "binary", "EWEB.FNT"), "EWEB")
        if not st:
            r.put(os.path.join(ROOT, "binary", "EWEBJS.OVL"), "EWEB")
            r.put(os.path.join(ROOT, "binary", "TLS.OVL"), "EWEB")
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
        r.write(os.path.join("EWEB", "eweb.inf"), "HOME=\n")
        r.start("C:\\EWEB\\" + prg, ready="EWEB shown ", boot=600 if st else 300)
        # (the start ends where the window is drawn; the home page after
        # it is a load)
        con = r.console()
        drawn = con.find("EWEB window drawn")
        if drawn >= 0:
            r.start_end = drawn
        else:
            print("NOTE  no 'EWEB window drawn': the start counts until the home page", flush=True)
        g = gaps_since(r, drawn if drawn >= 0 else 0)
        r.results.append(("the home page shown", max(g) if g else 0, LOAD))
        r.shot("00-home")
        load(r, "the sample page loaded", "file:///C:/SAMPLE.HTM")
        typed = load(r, "the news page loaded", "file:///C:/NEWS.HTM")
        r.results.append(("the address typed (each letter)", max(typed) if typed else 0, speed.KEY))
        seen = []
        start = os.path.getsize(r.log)
        r.act("a line scrolled", speed.LINE, lambda: r.key("Down"))
        s = scrolls_since(r, start)
        seen.append(("a line scrolled", max(ms for ms, _ in s) if s else None, ""))
        start = os.path.getsize(r.log)
        r.act("a page scrolled", speed.PAGE, lambda: r.key("shift+Down"))
        s = scrolls_since(r, start)
        seen.append(("a page scrolled", max(ms for ms, _ in s) if s else None, ""))
        seen.append(held(r, "Down", "an arrow held 3 s, down"))
        seen.append(held(r, "Up", "an arrow held 3 s, up"))
        seen.extend(pages_fresh(r))
        seen.append(pages_read(r))
        r.act("Settings opened", speed.DIALOG, lambda: r.key("ctrl+comma"))
        # (closed by its Cancel: Esc empties its text size field, the
        # guidelines' 3.5 of 27 Oct 2026; EWeb logs where the dialog is,
        # Cancel at its 30th column, 18th row, 8x16 characters)
        at = re.findall(r"EWEB dialog at (\d+) (\d+)", r.console())
        dx, dy = (int(v) for v in at[-1]) if at else (0, 0)
        r.act("Settings closed (what it uncovers)", speed.WINDOW, lambda: r.click(dx + 35 * 8, dy + 18 * 16 + 8))
        # (the Desk menu's first entry: its title at the bar's left, the
        # entry the first row under the bar, 8x16 characters)
        r.act("the About box opened", r.about, lambda: r.menu(20, 28))
        r.act("the About box closed (what it uncovers)", speed.WINDOW, lambda: r.key("Escape"))
        load(r, "the pictures' page loaded", "file:///C:/PICS.HTM", most=1800)
        r.key("ctrl+q")
        time.sleep(3)
        bad = r.report()
        # what the user sees (Erez, 4 Nov 2026: the user's experience
        # first), recorded, not held to a time: the window whole after
        # the key, from EWeb's AESSCROLL lines
        print("seen on %s (the window whole, ms from the key)" % r.describe())
        for what, ms, more in seen:
            print("  %-40s %8s  %s" % (what, "?" if ms is None else ms, more), flush=True)
        return bad
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
        bad |= run(out, m, nvdi, prgdir)
    sys.exit(bad)


if __name__ == "__main__":
    main()
