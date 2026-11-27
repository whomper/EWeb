#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEB.ACC, the accessory (DESIGN.md, The accessory: EWEB.ACC; the
guidelines' section 2): EWEB.PRG under the name EWEB.ACC in the boot
drive's root, EWEB.INF there with FOLDER=, nothing else of the family
loaded (the guidelines' 1.2). The Desk menu's entry is clicked with
xdotool on Hatari's window (Xvfb), EWeb's events read through NatFeats.

  ttram   a Falcon with TT RAM (machines.py's Falcon, TOS 4.04): the quiet
          start (its heap from TT RAM, a little ST RAM of its own, no
          window), the Desk entry opening the home page, ^U closing it and
          leaving it resident, the Desk entry again, a page shown after it,
          the toolbar's menu (its first title's list), and the file opened
          again from the toolbar menu's File > Close window
  tt      the same on the TT (TOS 3.06, 16 MB of TT RAM), ttmed
  vastart a program (VASEND.PRG, the rig's: build/hatari) hands it an
          address with VA_START: the window opens on it; the program
          ends, so the AES closes the window (AC_CLOSE): what is due
          written with nothing shown, and the accessory opens again
          from the Desk menu, its memory its own
  apterm  the same with AP_TERM sent while the page is made (the news
          page): passed over, the page shown, the window kept (2.2)
  nottram the stock Falcon (Falcon-4, no TT RAM): nothing taken at boot,
          the Desk entry's alert says it needs TT RAM

    make && python3 tests/hatari/acc.py WORK [ttram] [nottram]
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

DISPLAY = ":97"
CTRL, U, RETURN = 0x1D, 0x16, 0x1C
work = os.path.abspath(sys.argv[1])
groups = sys.argv[2:] or ["ttram", "tt", "vastart", "apterm", "nottram"]
failed = 0


def check(ok, what, detail=""):
    global failed
    print("%s %s%s" % ("ok  " if ok else "FAIL", what, (": " + detail) if detail and not ok else ""), flush=True)
    failed += not ok


class Mouse:
    """xdotool on the X display, in Atari screen coordinates"""

    def __init__(self):
        self.env = dict(os.environ, DISPLAY=DISPLAY)
        self.origin = None

    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def window(self):
        for _ in range(100):
            if self.origin:
                return self.origin
            found = (self.xdo("search", "--name", "Hatari") or self.xdo("search", "--class", "hatari")).split()
            if found:
                geo = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", found[0]).split())
                self.origin = (int(geo["X"]), int(geo["Y"]))
            else:
                time.sleep(0.2)
        raise RuntimeError("no Hatari window")

    def move(self, x, y):
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0 = int(loc["X"]), int(loc["Y"])
        for i in range(1, 13):
            self.xdo("mousemove", str(x0 + (wx + x - x0) * i // 12), str(y0 + (wy + y - y0) * i // 12))
            time.sleep(0.03)
        time.sleep(0.4)

    def click(self, x, y):
        self.move(x, y)
        self.xdo("click", "1")
        time.sleep(0.1)
        self.xdo("mousemove_relative", "1", "0")
        self.xdo("mousemove_relative", "--", "-1", "0")
        time.sleep(0.4)


class Run:
    def __init__(self, name, spec, hd, auto=None):
        self.name = name
        self.fifo = os.path.join(work, name + ".fifo")
        self.log = os.path.join(work, name + ".log")
        if os.path.exists(self.fifo):
            os.remove(self.fifo)
        self.m = machines.Machine(spec, hd)
        args = ["hatari"] + self.m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                             "--cmd-fifo", self.fifo, "-w", "--borders", "no", "--zoom", "1"] + \
            (["--auto", auto] if auto else [])
        env = self.m.env()
        env["DISPLAY"] = DISPLAY
        env.pop("SDL_VIDEODRIVER", None)
        self.h = subprocess.Popen(args, env=env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.fifo):
                break
            time.sleep(0.1)
        self.ctl = open(self.fifo, "w")
        self.seen = 0
        self.mouse = Mouse()

    def send(self, cmd):
        self.ctl.write(cmd + "\n")
        self.ctl.flush()
        time.sleep(0.25)

    def events(self):
        with open(self.log, errors="replace") as f:
            return [l.strip()[5:] for l in f if l.startswith("EWEB ")]

    def wait(self, pattern, timeout=120):
        deadline = time.time() + timeout
        while time.time() < deadline:
            evs = self.events()
            for i in range(self.seen, len(evs)):
                if re.match(pattern, evs[i]):
                    self.seen = i + 1
                    return evs[i]
            if self.h.poll() is not None:
                break
            time.sleep(0.3)
        return None

    def key(self, scan, mods=()):
        for m in mods:
            self.send("hatari-event keydown 0x%x" % m)
        self.send("hatari-event keypress 0x%x" % scan)
        for m in reversed(mods):
            self.send("hatari-event keyup 0x%x" % m)

    def shot(self, n):
        path = os.path.join(work, "%s-%02d.png" % (self.name, n))
        self.send("hatari-debug screenshot %s" % path)
        time.sleep(1)
        return path

    def end(self):
        try:
            self.send("hatari-debug quit")
        except BrokenPipeError:
            pass
        try:
            self.h.wait(15)
        except subprocess.TimeoutExpired:
            self.h.kill()
        self.m.stop()


def hd_for(name):
    hd = os.path.join(work, name, "hd")
    shutil.rmtree(os.path.dirname(hd), ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", "EWEB.PRG"), os.path.join(hd, "EWEB.ACC"))
    for f in ("EWEB.FNT", "EWEBJS.OVL", "EWEBTW.OVL", "TLS.OVL"):
        shutil.copy(os.path.join(ROOT, "binary", f), os.path.join(hd, f.lower()))
    shutil.copy(os.path.join(ROOT, "tests", "bench", "sample.html"), os.path.join(hd, "sample.htm"))
    shutil.copy(os.path.join(ROOT, "tests", "layout", "text.html"), os.path.join(hd, "text.htm"))
    with open(os.path.join(hd, "eweb.inf"), "w", newline="") as f:
        f.write("HOME=file:///C:/sample.htm\r\nNATFEATS=1\r\nFOLDER=C:\\\r\n")
    return hd


# the Desk menu: its title, then the accessory's entry (the only one, under
# the desktop's own item and a line), in a 640-wide mode's system font
DESK = (20, 8)
ENTRY = (60, 19 + 16 * 2 + 8)


def desk_entry(r):
    r.mouse.click(*DESK)
    time.sleep(0.8)
    r.mouse.click(*ENTRY)


def ttram(spec="Falcon:256", name="ttram"):
    hd = hd_for(name)
    r = Run(name, spec, hd)
    desc = r.m.describe()
    try:
        boot = r.wait(r"accessory: heap", 240)
        check(boot is not None and " in TT RAM" in boot, "%s: the accessory's heap from TT RAM at boot" % desc, str(boot))
        time.sleep(20)		# the desktop up
        r.shot(1)
        check(r.wait(r"(shown|ready)", 1) is None, "%s: no window at boot (a quiet start)" % desc)
        desk_entry(r)
        shown = r.wait(r"shown file:///C:/sample\.htm", 120)
        r.shot(2)
        check(shown is not None, "%s: the Desk entry opens EWeb's window, the home page shown" % desc)
        r.key(U, (CTRL,))
        closed = r.wait(r"accessory closed", 60)
        r.shot(3)
        check(closed is not None, "%s: ^U closes the window, the accessory stays" % desc)
        desk_entry(r)
        again = r.wait(r"shown file:///C:/sample\.htm", 120)
        check(again is not None, "%s: the Desk entry opens it again" % desc)
        if again:
            time.sleep(2)
            tb = [e for e in r.events() if e.startswith("toolbar ")]
            if tb:
                x, y, w, h = map(int, tb[-1].split()[1:5])
                # (a pop-up's rows 18 pixels apart in the 16-row font)
                # the menu button is before Back: a button's step to the left
                r.mouse.click(x - w // 2 - 2, y + h // 2)
                time.sleep(1.5)
                r.shot(4)
                # its first title, File (below About EWeb...), then its
                # Close window (the ninth row: two lines above it)
                r.mouse.click(x - w, y + h + 8 + 18 * 1)
                time.sleep(1.5)
                r.shot(5)
                r.mouse.click(x - w, y + h + 8 + 18 * 8)
                check(r.wait(r"accessory closed", 60) is not None,
                      "%s: the toolbar's menu: File > Close window closes it" % desc)
            else:
                check(False, "%s: the toolbar's place in the events" % desc)
    finally:
        r.end()


def vastart(apterm=False):
    name = "apterm" if apterm else "vastart"
    hd = hd_for(name)
    shutil.copy(os.path.join(ROOT, "build", "hatari", "VASEND.PRG"), os.path.join(hd, "VASEND.PRG"))
    page = "NEWS.HTM" if apterm else "text.htm"
    if apterm:
        # a page that takes a while to make (tests/bench/mkpage.py's news
        # page): AP_TERM comes in the middle
        subprocess.run([sys.executable, os.path.join(ROOT, "tests", "bench", "mkpage.py"), hd], check=True)
        with open(os.path.join(hd, "VATERM.TXT"), "w") as f:
            f.write("300")
    with open(os.path.join(hd, "VAURL.TXT"), "w", newline="") as f:
        f.write("file:///C:/%s\r\n" % page)
    r = Run(name, "Falcon:256", hd, auto="C:\\VASEND.PRG")
    desc = r.m.describe()

    def told(pattern, timeout):
        """VASEND.PRG's own lines (not EWeb's events)"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if any(re.match(pattern, l) for l in open(r.log, errors="replace")):
                return True
            time.sleep(0.3)
        return False
    try:
        check(told(r"VASEND sent", 240), "%s: the program found EWeb and sent VA_START" % desc)
        shown = r.wait(r"shown file:///C:/%s" % re.escape(page), 120)
        r.shot(1)
        check(shown is not None, "%s: VA_START opens the window on its address" % desc)
        if apterm:
            check(told(r"VASEND sent AP_TERM", 10) and r.wait(r"accessory closed", 5) is None and
                  not any(e.startswith("quit step") for e in r.events()),
                  "%s: AP_TERM while the page was made passed over: the page shown, the window kept" % desc)
        open(os.path.join(hd, "VAEND.TXT"), "w").close()
        check(told(r"VASEND end", 60), "%s: the program ends" % desc)
        closed = r.wait(r"accessory closed", 60)
        evs = r.events()
        check(closed is not None and any(e.startswith("quit step: settings written") for e in evs) and
              not any(e.startswith("quit box") for e in evs),
              "%s: AC_CLOSE: the window gone, what is due written, nothing shown" % desc)
        time.sleep(10)		# the desktop back
        desk_entry(r)
        again = r.wait(r"shown file:///C:/sample\.htm", 120)
        r.shot(2)
        check(again is not None, "%s: the Desk entry opens it after the program ended (its memory its own)" % desc)
    finally:
        r.end()


def nottram():
    hd = hd_for("nottram")
    r = Run("nottram", "Falcon-4:256", hd)
    desc = r.m.describe()
    try:
        boot = r.wait(r"accessory: ", 240)
        check(boot is not None and "no TT RAM, nothing taken" in boot, "%s: no TT RAM: nothing taken" % desc, str(boot))
        time.sleep(20)
        desk_entry(r)
        time.sleep(3)
        r.shot(1)
        r.key(RETURN)
        time.sleep(2)
        r.shot(2)
        check(r.wait(r"(shown|ready)", 2) is None and r.h.poll() is None,
              "%s: the Desk entry shows its alert, nothing opened (screens nottram-01, -02)" % desc)
    finally:
        r.end()


os.makedirs(work, exist_ok=True)
xv = machines.xvfb(DISPLAY)
try:
    for g in groups:
        {"ttram": ttram, "tt": lambda: ttram("TT:ttmed", "tt"), "vastart": vastart, "apterm": lambda: vastart(True),
         "nottram": nottram}[g]()
finally:
    machines.xvfb_stop(xv)
print("acc: all passed" if not failed else "acc: %d failed" % failed)
sys.exit(1 if failed else 0)
