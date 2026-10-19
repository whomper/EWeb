#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEB.PRG in Hatari, on the family's machines (common/tools/machines.py):

- browse (the Falcon, 16 colours, in a window on an X display so that
  xdotool can move the mouse): pages from tests/net/server.py through
  FAKESTNG, a link clicked, Undo back, an address typed after ^L with an
  anchor on the page, a secure page (TLS on the emulated 68030 and DSP),
  a slow download stopped with Esc, a page of pictures (fetched after the
  page is shown, the window scrolled meanwhile), ^Q;
- screens: a page from the disk drawn on the Falcon in 256 colours and in
  true colour (640x200 on an RGB monitor), on the TT in its medium mode
  and on its monochrome monitor; each must say it showed the page; then
  the page of pictures from the disk, each picture in the screen's own
  colours.

EWEB.PRG says what it does as "EWEB ..." lines through NatFeats; each step
waits for the line it expects, then a screenshot goes to OUT.

    tests/hatari/gui_test.py OUT [browse] [screens]"""
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "net"))
import machines  # noqa: E402

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/gui")
WHICH = sys.argv[2:] or ["browse", "screens"]
HTTP, HTTPS = 18280, 18643
DISPLAY = ":98"
CTRL, SHIFT = 0x1D, 0x2A
L, Q, C = 0x26, 0x10, 0x2E
RETURN, UNDO, DOWN, SPACE = 0x1C, 0x61, 0x50, 0x39

# the keys for a URL's characters on the US keyboard: Hatari's keypress
# takes letters and digits as they are
KEYS = {":": (SHIFT, 0x27), "/": (None, 0x35), ".": (None, 0x34), "#": (SHIFT, 0x04), "-": (None, 0x0C)}


def hd_for(name, home):
    hd = os.path.join(OUT, name)
    shutil.rmtree(hd, ignore_errors=True)
    os.makedirs(hd)
    for f in ("binary/EWEB.PRG", "binary/EWEB.FNT"):
        shutil.copy(os.path.join(ROOT, f), hd)
    shutil.copy(os.path.join(ROOT, "tests/layout/tables.html"), os.path.join(hd, "TABLES.HTM"))
    shutil.copy(os.path.join(ROOT, "tests/layout/text.html"), os.path.join(hd, "TEXT.HTM"))
    import server
    with open(os.path.join(hd, "PICS.HTM"), "wb") as f:
        f.write(server.PICS_PAGE)
    for k, (data, _) in server.PICS.items():
        with open(os.path.join(hd, k.upper()), "wb") as f:
            f.write(data)
    with open(os.path.join(hd, "EWEB.INF"), "w") as f:
        f.write("HOME=%s\r\n" % home)
    return hd


class Mouse:
    """xdotool on the X display, in Atari screen coordinates (EHypView's)"""

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
        """glide there: the AES sees the mouse only when it moves"""
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0 = int(loc["X"]), int(loc["Y"])
        for i in range(1, 13):
            self.xdo("mousemove", str(x0 + (wx + x - x0) * i // 12), str(y0 + (wy + y - y0) * i // 12))
            time.sleep(0.03)
        time.sleep(0.4)

    def click(self, x, y, button="1"):
        self.move(x, y)
        self.xdo("click", button)
        time.sleep(0.4)


class Run:
    """Hatari with EWEB.PRG, its events and its command FIFO"""

    def __init__(self, name, spec, hd, windowed=False, network=False, truecolour=False):
        self.name = name
        self.fifo = os.path.join(OUT, name + ".fifo")
        self.log = os.path.join(OUT, name + ".log")
        for p in (self.fifo,):
            if os.path.exists(p):
                os.remove(p)
        self.m = machines.Machine(spec, hd)
        args = ["hatari"] + self.m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                             "--cmd-fifo", self.fifo, "--auto", r"C:\EWEB.PRG"]
        if truecolour:
            # the Falcon on an RGB monitor, 640x200 in 16-bit colour: the
            # NVRAM's mode (16 bits, 80 columns, PAL; EmuTOS stopped at
            # boot with 640x400 interlaced)
            nv = os.path.join(self.m.home, ".config", "hatari", "hatari.nvram")
            b = bytearray(open(nv, "rb").read())
            b[14:16] = struct.pack(">H", 0x002C)
            s = sum(b[:48]) & 0xFF
            b[48], b[49] = ~s & 0xFF, s
            open(nv, "wb").write(b)
            args[args.index("--monitor") + 1] = "rgb"
            # TOS 4's desktop takes the mode from NEWDESK.INF's #E line
            # (machines.py writes one for a real TOS): the same mode there
            inf = os.path.join(hd, "NEWDESK.INF")
            if os.path.exists(inf):
                t = open(inf, newline="").read()
                t = re.sub(r"#E (\w\w) (\w\w) (\w\w) (\w\w) \w\w \w\w", r"#E \1 \2 \3 \4 00 2C", t)
                open(inf, "w", newline="").write(t)
        if network:
            args += self.m.network()
        env = self.m.env()
        if windowed:
            env["DISPLAY"] = DISPLAY
            env.pop("SDL_VIDEODRIVER")
            args += ["-w", "--borders", "no", "--zoom", "1"]
        else:
            args += ["--fast-forward", "yes"]
        self.h = subprocess.Popen(args, env=env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.fifo):
                break
            time.sleep(0.1)
        self.ctl = open(self.fifo, "w")
        self.seen = 0
        self.mouse = Mouse() if windowed else None

    def send(self, cmd):
        self.ctl.write(cmd + "\n")
        self.ctl.flush()
        time.sleep(0.25)

    def events(self):
        with open(self.log, errors="replace") as f:
            return [l.strip()[5:] for l in f if l.startswith("EWEB ")]

    def wait(self, pattern, timeout=120):
        """the next event matching pattern (a regular expression); None"""
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

    def type(self, text):
        for c in text:
            if c in KEYS:
                mod, scan = KEYS[c]
                self.key(scan, (mod,) if mod else ())
            else:
                self.send("hatari-event keypress %s" % c)
            time.sleep(0.15)

    def shot(self, n):
        path = os.path.join(OUT, "%s-%02d.png" % (self.name, n))
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


fails = 0


def check(name, ok, detail=""):
    global fails
    print("%s  %s%s" % ("ok  " if ok else "FAIL", name, "" if ok else "\n      " + detail))
    if not ok:
        fails += 1
    return ok


def link_point(url, area, top, text):
    """where on the screen the link with this text is: ewrender's display
    list of the same page, at the window's width"""
    x0, y0, w, _ = area
    p = subprocess.run([os.path.join(ROOT, "build/host/ewrender"), "-f", os.path.join(ROOT, "binary/EWEB.FNT"),
                        "-w", str(w), "-d", url], capture_output=True, text=True, timeout=60)
    for line in p.stdout.splitlines():
        m = re.match(r'text (\d+),(\d+) (\d+)x(\d+) \S+ .* link (\d+) "(.*)"$', line)
        if m and m.group(5) != "0" and text in m.group(6):
            x, base = int(m.group(1)), int(m.group(2))
            return x0 + x + 4, y0 + base - top - 4
    return None


def browse():
    import server  # noqa: F401  (the pages)
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("browse: the test server", False)
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    site = "http://127.0.0.1:%d/site/" % HTTP
    hd = hd_for("browse", site)
    shutil.copy(os.path.join(work, "CACERT.PEM"), hd)      # the test authority
    r = Run("browse", "Falcon:16", hd, windowed=True, network=True)
    try:
        ev = r.wait(r"shown ", 300)
        check("browse: the start page over the network", ev is not None and ev.startswith("shown " + site + " "),
              str(r.events()))
        r.shot(0)
        if ev is None:
            return
        m = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev)
        area = tuple(int(m.group(i)) for i in range(1, 5))
        pt = link_point(site, area, int(m.group(5)), "the second page")
        if not check("browse: the link found on the page", pt is not None):
            return
        r.mouse.click(*pt)
        ev = r.wait(r"shown ", 120)
        check("browse: a link clicked", ev is not None and ev.startswith("shown %stwo.html " % site), str(r.events()))
        r.shot(1)
        r.key(UNDO)
        ev = r.wait(r"shown ", 120)
        check("browse: Undo goes back", ev is not None and ev.startswith("shown %s " % site), str(r.events()))
        r.shot(2)
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/two.html#end" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        top = int(re.search(r"top (\d+)", ev).group(1)) if ev else 0
        check("browse: an address typed, scrolled to its anchor",
              ev is not None and ev.startswith("shown %stwo.html#end " % site) and top > 1000, str(r.events()))
        r.shot(3)
        # a long page scrolled the way people do: Down a line at a time,
        # Shift-Down and Space a page (what stays on the screen is moved,
        # the new rows drawn), then Back from memory; EWEB.PRG still answers
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/page" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        check("browse: a long page", ev is not None and ev.startswith("shown http://127.0.0.1:%d/page " % HTTP),
              str(r.events()))
        for _ in range(25):
            r.key(DOWN)
        for _ in range(6):
            r.key(DOWN, (SHIFT,))
        for _ in range(6):
            r.key(SPACE)
        for _ in range(4):
            r.key(SPACE, (SHIFT,))
        time.sleep(3)
        r.shot(6)
        r.key(UNDO)
        ev = r.wait(r"shown ", 120)
        ms = re.search(r"fetch (\d+)", ev or "")
        check("browse: scrolled, then Back from memory",
              ev is not None and ev.startswith("shown %stwo.html#end " % site) and ms is not None and
              int(ms.group(1)) < 200, str(r.events()))
        # a secure page: the TLS handshake on the emulated 68030 and DSP,
        # the window alive meanwhile
        r.key(L, (CTRL,))
        r.type("https://127.0.0.1:%d/site/" % HTTPS)
        r.key(RETURN)
        ev = r.wait(r"shown ", 600)
        check("browse: a secure page", ev is not None and ev.startswith("shown https://127.0.0.1:%d/site/ " % HTTPS),
              str(r.events()))
        r.shot(4)
        # 2 MB that come slowly over the serial line: Esc stops them, and
        # the page before stays
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/chunked" % HTTP)
        r.key(RETURN)
        time.sleep(8)
        r.key(0x01)
        ev = r.wait(r"(stopped|shown )", 120)
        check("browse: Esc stops a load", ev == "stopped", str(r.events()))
        r.shot(5)
        # a page of pictures: shown first, its pictures after, the window
        # scrolled while they come (Down doesn't stop them)
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/pics.html" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        check("browse: a page of pictures shown", ev is not None and ev.startswith("shown %spics.html " % site),
              str(r.events()))
        r.key(DOWN)
        r.key(DOWN, (SHIFT,))
        ev = r.wait(r"pictures ", 600)
        evs = r.events()
        dec = [e for e in evs if e.startswith("picture %s" % site) and " decoded at " in e]
        check("browse: its pictures, with the window scrolled meanwhile",
              ev is not None and ev.endswith(" 6") and len(dec) == 5 and
              any(e.startswith("picture %sgone.png failed" % site) for e in evs) and
              any(e.startswith("relaid ") for e in evs), "\n".join(evs[-14:]))
        r.key(0x47)		# ClrHome: the top
        time.sleep(2)
        r.shot(7)
        r.key(Q, (CTRL,))
        check("browse: ^Q quits", r.wait(r"quit$", 60) is not None, str(r.events()))
    finally:
        r.end()
        xvfb.terminate()
        srv.stdin.close()
        srv.wait(10)


def screens():
    for name, spec, tc in (("falcon256", "Falcon:256", False), ("truecolour", "Falcon:16", True),
                           ("ttmed", "TT:ttmed", False), ("ttmono", "TT:mono", False)):
        r = Run(name, spec, hd_for(name, r"C:\TABLES.HTM"), truecolour=tc)
        try:
            ready = r.wait(r"ready ", 120)
            ev = r.wait(r"shown ", 120)
            check("screens: %s (%s)" % (name, ready.split(" ", 1)[1] if ready else "?"),
                  ev is not None and ev.startswith("shown file:///C:/TABLES.HTM "), str(r.events()))
            if tc:
                # a mode TOS didn't take must not pass as true colour
                check("screens: %s is true colour" % name, ready is not None and "true colour" in ready,
                      ready or "no ready line")
            time.sleep(2)
            r.shot(0)
            # the page of pictures, from the disk: dithered to the screen's
            # colours (true colour: as they are)
            r.key(L, (CTRL,))
            r.type("file:///C:/PICS.HTM")
            r.key(RETURN)
            ev = r.wait(r"pictures ", 300)
            dec = [e for e in r.events() if " decoded at " in e]
            check("screens: %s pictures" % name, ev is not None and len(dec) == 5, "\n".join(r.events()[-10:]))
            time.sleep(2)
            r.shot(1)
            r.key(Q, (CTRL,))
            check("screens: %s quits" % name, r.wait(r"quit$", 60) is not None, str(r.events()))
        finally:
            r.end()


os.makedirs(OUT, exist_ok=True)
if not os.path.exists(os.path.join(ROOT, "binary/EWEB.PRG")):
    sys.exit("gui_test: make binary/EWEB.PRG binary/EWEB.FNT build/host/ewrender first")
if "browse" in WHICH:
    if shutil.which("Xvfb") and shutil.which("xdotool"):
        browse()
    else:
        print("skip  browse: needs Xvfb and xdotool")
if "screens" in WHICH:
    screens()
print("gui_test: %s" % ("all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
