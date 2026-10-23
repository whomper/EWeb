#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWEB.PRG in Hatari, on the family's machines (common/tools/machines.py):

- browse (the Falcon, 16 colours, in a window on an X display so that
  xdotool can move the mouse): pages from tests/net/server.py through
  FAKESTNG, a link clicked, Undo back, an address typed after ^L with an
  anchor on the page, a secure page (TLS on the emulated 68030 and DSP),
  a slow download stopped with Esc, a page of pictures (fetched after the
  page is shown, the window scrolled meanwhile), forms (a field clicked
  and typed into, Return sends it with GET; Back shows it as it was;
  Tab from field to field, a box ticked with Space and its label clicked,
  sent with POST by its button clicked), saving (^S and the file
  selector's OK: the page as it came; Alternate-click: a link's file; a
  file EWeb doesn't show, opened: saved; one its server says to save,
  under its filename's name; a link with a download attribute, clicked:
  saved under its name), bookmarks (^D adds the page,
  BOOKMARK.HTM written; ^B shows them, the Bookmarks menu has it; Remove
  clicked), a page that names no encoding read again in Windows-1255
  (View > Encoding..., its popup by the keyboard), ^Q;
- scripts (the Falcon with its 68882, SCRIPTS= naming the test server):
  a page whose script makes tabs, one clicked (the page laid out again),
  a button whose onclick says hello in an alert, answered with Return; a
  page whose script clicks its <a download> as it loads: drawn, then its
  file saved;
- screens: a page from the disk drawn on the Falcon in 256 colours and in
  true colour (640x200 on an RGB monitor), on the TT in its medium mode
  and on its monochrome monitor; each must say it showed the page; then
  the page of pictures from the disk, each picture in the screen's own
  colours, the page of translucent colours and opacity
  (tests/layout/alpha.html), mixed through the screen's own palette, and
  the page of CSS gradients (tests/layout/gradients.html), dithered to it;
- details (the Falcon, 16 colours, in a window; then EWEBST.PRG on the
  1040 ST): tests/layout/details.html from the disk, a closed details'
  summary clicked (it opens, the page taller, the window where it was),
  clicked again (closed, the page as tall as before); then the page
  typed with a #fragment in a closed details (it opens, the window
  there), a summary further down clicked (the window still there), and a
  #fragment of the same page in another closed details typed (it opens
  and the window goes there, the page laid out again).

- refresh (the Falcon, pages from the disk): a page whose <meta
  http-equiv=refresh> goes on to another in 20 s, said on the status line,
  then shown in its place in the history (Undo goes to the page before
  it); a page with pictures says it again once they are in (and the
  ribbon goes); Esc keeps a page whose refresh waits; while the address
  field has the keyboard it waits, and goes on after Tab; a page that
  refreshes itself at once waits for its picture (read again, not decoded
  again), and stops after five, saying so; two that send each other on
  stop after five too, and the status line says so after the pictures of
  the one they stop on. The status line is read from the "status" lines
  win_status() logs at each change.

EWEB.PRG says what it does as "EWEB ..." lines through NatFeats; each step
waits for the line it expects, then a screenshot goes to OUT.

    tests/hatari/gui_test.py OUT [browse] [scripts] [screens] [st] [gateway] [falcon] [deep] [refresh] [details]"""
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
WHICH = sys.argv[2:] or ["browse", "scripts", "screens", "st", "gateway", "falcon", "falcon4", "deep", "refresh",
                         "details"]
# GUI_SLOT=N gives the rigs their own ports (the test server's untrusted
# and ECDSA ones are HTTPS + 1 and + 2) and X display, so two trees can
# run them at once
SLOT = int(os.environ.get("GUI_SLOT", "0"))
HTTP, HTTPS, GATE = (p + 1000 * SLOT for p in (18280, 18643, 18289))
DISPLAY = ":%d" % (98 - SLOT)
CTRL, SHIFT = 0x1D, 0x2A
L, Q, C, S, D, B, ALT = 0x26, 0x10, 0x2E, 0x1F, 0x20, 0x30, 0x38
COMMA, ESC = 0x33, 0x01
RETURN, UNDO, DOWN, SPACE, TAB = 0x1C, 0x61, 0x50, 0x39, 0x0F
BACKSPACE, LEFT, RIGHT = 0x0E, 0x4B, 0x4D

# the keys for a URL's characters on the US keyboard: Hatari's keypress
# takes letters and digits as they are
KEYS = {":": (SHIFT, 0x27), "/": (None, 0x35), ".": (None, 0x34), "#": (SHIFT, 0x04), "-": (None, 0x0C)}


def hd_for(name, home, inf=""):
    hd = os.path.join(OUT, name)
    shutil.rmtree(hd, ignore_errors=True)
    os.makedirs(hd)
    for f in ("binary/EWEB.PRG", "binary/EWEBJS.OVL", "binary/EWEBST.PRG", "binary/EWEB.FNT"):
        if os.path.exists(os.path.join(ROOT, f)):
            shutil.copy(os.path.join(ROOT, f), hd)
    shutil.copy(os.path.join(ROOT, "tests/layout/tables.html"), os.path.join(hd, "TABLES.HTM"))
    shutil.copy(os.path.join(ROOT, "tests/layout/text.html"), os.path.join(hd, "TEXT.HTM"))
    shutil.copy(os.path.join(ROOT, "tests/layout/alpha.html"), os.path.join(hd, "ALPHA.HTM"))
    shutil.copy(os.path.join(ROOT, "tests/layout/gradients.html"), os.path.join(hd, "GRAD.HTM"))
    os.makedirs(os.path.join(hd, "IMG"), exist_ok=True)
    shutil.copy(os.path.join(ROOT, "tests/layout/img/one.png"), os.path.join(hd, "IMG", "ONE.PNG"))
    import server
    with open(os.path.join(hd, "PICS.HTM"), "wb") as f:
        f.write(server.PICS_PAGE)
    for k, (data, _) in server.PICS.items():
        os.makedirs(os.path.dirname(os.path.join(hd, k.upper())), exist_ok=True)
        with open(os.path.join(hd, k.upper()), "wb") as f:
            f.write(data)
    with open(os.path.join(hd, "EWEB.INF"), "w") as f:
        f.write("HOME=%s\r\n%s" % (home, inf))
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

    def __init__(self, name, spec, hd, windowed=False, network=False, truecolour=False, prg="EWEB.PRG", accs=()):
        self.name = name
        self.fifo = os.path.join(OUT, name + ".fifo")
        self.log = os.path.join(OUT, name + ".log")
        for p in (self.fifo,):
            if os.path.exists(p):
                os.remove(p)
        self.m = machines.Machine(spec, hd)
        if accs:
            self.m.accs(accs)       # the family's accessories, from their releases
        args = ["hatari"] + self.m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                             "--cmd-fifo", self.fifo, "--auto", "C:\\" + prg]
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


def link_point(url, area, top, text, extra=()):
    """where on the screen the link with this text is: ewrender's display
    list of the same page, at the window's width"""
    x0, y0, w, _ = area
    p = subprocess.run([os.path.join(ROOT, "build/host/ewrender"), "-f", os.path.join(ROOT, "binary/EWEB.FNT"),
                        "-w", str(w), "-d"] + list(extra) + [url], capture_output=True, text=True, timeout=60)
    for line in p.stdout.splitlines():
        m = re.match(r'text (\d+),(\d+) (\d+)x(\d+) \S+ .* link (\d+) "(.*)"$', line)
        if m and m.group(5) != "0" and text in m.group(6):
            x, base = int(m.group(1)), int(m.group(2))
            return x0 + x + 4, y0 + base - top - 4
    return None


def text_point(url, area, top, text):
    """where on the screen some text of the page is"""
    x0, y0, w, _ = area
    p = subprocess.run([os.path.join(ROOT, "build/host/ewrender"), "-f", os.path.join(ROOT, "binary/EWEB.FNT"),
                        "-w", str(w), "-d", url], capture_output=True, text=True, timeout=60)
    for line in p.stdout.splitlines():
        m = re.match(r'text (\d+),(\d+) .* "(.*)"$', line)
        if m and text in m.group(3):
            return x0 + int(m.group(1)) + 4, y0 + int(m.group(2)) - top - 4
    return None


def ctl_point(url, area, top, n, right=False):
    """where on the screen the page's control n (in the order drawn) is:
    its middle, or near its right end"""
    x0, y0, w, _ = area
    p = subprocess.run([os.path.join(ROOT, "build/host/ewrender"), "-f", os.path.join(ROOT, "binary/EWEB.FNT"),
                        "-w", str(w), "-d", url], capture_output=True, text=True, timeout=60)
    ctls = [re.match(r"control (\d+),(\d+) (\d+)x(\d+) ", line) for line in p.stdout.splitlines()
            if line.startswith("control ")]
    if n >= len(ctls):
        return None
    x, y, cw, ch = (int(ctls[n].group(i)) for i in range(1, 5))
    return x0 + x + (cw - 4 if right else cw // 2), y0 + y + ch // 2 - top


def browse():
    import server  # the pages and files
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS),
                            "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("browse: the test server", False)
    xvfb = machines.xvfb(DISPLAY)    # once it answers (None: one was there)
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
        check("browse: Esc stops a load", ev is not None and ev.startswith("stopped"), str(r.events()))
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
        # the throbber went round while they came (each 110 ms)
        thr = r.wait(r"throbber ", 30)
        check("browse: the throbber moved while the pictures came", thr is not None and int(thr.split()[1]) >= 8,
              thr or "no throbber line")
        r.key(0x47)		# ClrHome: the top
        time.sleep(2)
        r.shot(7)
        # forms: the search field clicked at its end, typed into, Return
        # sends it with GET; Back shows it as it was left
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/form.html" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        check("forms: the page shown", ev is not None and ev.startswith("shown %sform.html " % site), str(r.events()))
        m = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev or "")
        if m:
            area = tuple(int(m.group(i)) for i in range(1, 5))
            pt = ctl_point(site + "form.html", area, int(m.group(5)), 0, right=True)
            r.mouse.click(*pt)
            r.type("st")
            r.shot(8)
            r.key(RETURN)
            ev = r.wait(r"shown ", 120)
            # (the log has no query: what was typed stays out of it, 7.3;
            # the echo page's title says what came)
            check("forms: typed into a field, sent with Return", ev is not None and
                  ev.startswith("shown %secho " % site) and "atarist" not in ev.split(" title ")[0] and
                  ev.endswith("title GET||q=atarist&go=Find"), str(r.events()))
            r.key(UNDO)
            ev = r.wait(r"shown ", 120)
            check("forms: Back to the form", ev is not None and ev.startswith("shown %sform.html " % site),
                  str(r.events()))
            # the login: its name typed, Tab to the password, Tab to the
            # box, Space ticks it; its button clicked sends it with POST
            r.mouse.click(*ctl_point(site + "form.html", area, int(m.group(5)), 2))
            r.type("erez")
            r.key(TAB)
            r.type("pw")
            r.key(TAB)
            r.key(SPACE)
            # its label's words clicked twice: off, then on again
            lp = text_point(site + "form.html", area, int(m.group(5)), "Keep me")
            r.mouse.click(*lp)
            r.mouse.click(*lp)
            r.shot(9)
            r.mouse.click(*ctl_point(site + "form.html", area, int(m.group(5)), 7))
            ev = r.wait(r"shown ", 120)
            check("forms: a login sent with POST", ev is not None and ev.startswith("shown %secho " % site) and
                  ev.endswith("title POST|application/x-www-form-urlencoded|user=erez&pw=pw&keep=on&lang=en"
                              "&note=a&login=Log+in"), str(r.events()))
            r.shot(10)
        # saving: the page shown (^S, the file selector's OK), a link's
        # file (Alternate-click), a file EWeb doesn't show (opened)
        def saved(name):
            path = os.path.join(hd, name)
            return open(path, "rb").read() if os.path.exists(path) else None
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/files.html" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        check("saving: the page shown", ev is not None and ev.startswith("shown %sfiles.html " % site),
              str(r.events()))
        m = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev or "")
        if m:
            area = tuple(int(m.group(i)) for i in range(1, 5))
            r.key(S, (CTRL,))
            time.sleep(3)		# the file selector
            r.shot(11)
            r.key(RETURN)
            ev = r.wait(r"saved ", 60)
            check("saving: ^S, the page as it came", ev == "saved %d" % len(server.FILES_PAGE) and
                  saved("FILES.HTM") == server.FILES_PAGE, str(r.events()))
            r.send("hatari-event keydown 0x%x" % ALT)
            r.mouse.click(*link_point(site + "files.html", area, int(m.group(5)), "A small file"))
            r.send("hatari-event keyup 0x%x" % ALT)
            time.sleep(3)
            r.key(RETURN)
            ev = r.wait(r"saved ", 120)
            check("saving: Alternate-click, a link's file", ev == "saved %d" % len(server.SMALL) and
                  saved("SMALL.BIN") == server.SMALL, str(r.events()))
            r.key(L, (CTRL,))
            r.type("127.0.0.1:%d/site/small.zip" % HTTP)
            r.key(RETURN)
            dl = r.wait(r"download ", 120)
            time.sleep(3)
            r.key(RETURN)
            ev = r.wait(r"saved ", 60)
            check("saving: a file EWeb doesn't show", dl == "download application/zip %d" % len(server.SMALL) and
                  ev == "saved %d" % len(server.SMALL) and saved("SMALL.ZIP") == server.SMALL, str(r.events()))
            # a text file its server says to save (Content-Disposition:
            # attachment): the file selector offers its filename's name
            r.key(L, (CTRL,))
            r.type("127.0.0.1:%d/site/dl/report" % HTTP)
            r.key(RETURN)
            dl = r.wait(r"download ", 120)
            time.sleep(3)
            r.shot(17)
            r.key(RETURN)
            ev = r.wait(r"saved ", 60)
            check("saving: attachment, its filename", dl == "download text/plain %d" % len(server.REPORT) and
                  ev == "saved %d" % len(server.REPORT) and saved("REPORT20.TXT") == server.REPORT,
                  str(r.events()))
            # a link with a download attribute, clicked: saved as it comes
            # under its name, not shown; Undo goes back to the files
            r.key(L, (CTRL,))
            r.type("127.0.0.1:%d/site/dl/links.html" % HTTP)
            r.key(RETURN)
            ev = r.wait(r"shown ", 120)
            m2 = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev or "")
            pt = link_point(site + "dl/links.html", tuple(int(m2.group(i)) for i in range(1, 5)),
                            int(m2.group(5)), "named") if m2 else None
            if check("saving: the download links shown", ev is not None and
                     ev.startswith("shown %sdl/links.html " % site) and pt is not None, str(r.events())):
                r.mouse.click(*pt)
                sv = r.wait(r"save link", 60)
                time.sleep(3)
                r.shot(18)
                r.key(RETURN)
                ev = r.wait(r"saved ", 120)
                check("saving: <a download>, its name", sv is not None and ev == "saved %d" % len(server.SMALL) and
                      saved("MYFILE.BIN") == server.SMALL, str(r.events()))
                r.key(UNDO)
                ev = r.wait(r"shown ", 60)
                check("saving: Back to the files", ev is not None and ev.startswith("shown %sfiles.html " % site),
                      str(r.events()))
            # bookmarks: ^D adds the page shown (on the disk at once), ^B
            # shows them; the Bookmarks menu has it; Remove takes it away
            r.key(D, (CTRL,))
            ev = r.wait(r"bookmarked ", 30)
            bm = (saved("BOOKMARK.HTM") or b"").decode("utf-8", "replace")
            check("bookmarks: ^D adds the page", ev == "bookmarked 1 1" and
                  'HREF="%sfiles.html">Files</A>' % site in bm, str(r.events()) + "\n" + bm)
            r.key(D, (CTRL,))
            check("bookmarks: once", r.wait(r"bookmarked ", 30) == "bookmarked -1 1", str(r.events()))
            r.key(B, (CTRL,))
            ev = r.wait(r"shown ", 60)
            check("bookmarks: ^B shows them", ev is not None and ev.startswith("shown about:bookmarks ") and
                  ev.endswith("title Bookmarks"), str(r.events()))
            m = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev or "")
            if m:
                area = tuple(int(m.group(i)) for i in range(1, 5))
                r.mouse.move(8 * len(" EWeb  File  Edit  View  Book"), 5)	# the Bookmarks menu, dropped
                r.shot(12)
                # a click on the page's empty part closes it (TOS 4 holds a
                # dropped menu until one)
                r.mouse.click(area[0] + area[2] // 2, area[1] + area[3] - 20)
                time.sleep(0.5)
                pt = link_point("about:bookmarks", area, int(m.group(5)), "Remove",
                                ("-m", os.path.join(hd, "BOOKMARK.HTM")))
                r.shot(13)
                if check("bookmarks: Remove found on the page", pt is not None):
                    r.mouse.click(*pt)
                    ev = r.wait(r"shown ", 60)
                    check("bookmarks: Remove takes it away", ev is not None and
                          ev.startswith("shown about:bookmarks ") and
                          "HREF" not in (saved("BOOKMARK.HTM") or b"HREF").decode("utf-8", "replace"),
                          str(r.events()))
                    r.shot(14)
        # a page in windows-1255 that names no encoding: windows-1252 here
        # (not under .il); View > Encoding... reads it again in windows-1255
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/enc/w1255.html" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        check("encoding: a page that names none, read as windows-1252", ev is not None and
              ev.startswith("shown %senc/w1255.html " % site) and ev.endswith("title \u00f9\u00ec\u00e5\u00ed "
                                                                          "\u00f2\u00e5\u00ec\u00ed"),
              str(r.events()))
        r.mouse.move(16 + 8 * len(" EWeb  File  Edit  Vi"), 5)	# the View menu, dropped
        r.mouse.click(16 + 8 * len(" EWeb  File  Edit  View"), 19 + 7 * 16 + 8)	# Encoding...
        time.sleep(1)
        for _ in range(5):		# Auto, UTF-8, Western twice, Hebrew (Windows-1255)
            r.key(DOWN)
        r.shot(15)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        evs = r.events()
        check("encoding: View > Encoding... reads it in windows-1255", ev is not None and
              "encoding windows-1255" in evs and ev.startswith("shown %senc/w1255.html " % site) and
              ev.endswith("title \u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"), str(evs[-6:]))
        r.shot(16)
        r.key(Q, (CTRL,))
        check("browse: ^Q quits", r.wait(r"quit$", 60) is not None, str(r.events()))
    finally:
        r.end()
        if xvfb is not None:
            xvfb.terminate()
        srv.stdin.close()
        srv.wait(10)


def scripts():
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS),
                            "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("scripts: the test server", False)
    xvfb = machines.xvfb(DISPLAY)
    site = "http://127.0.0.1:%d/site/" % HTTP
    try:
        scripts_run(site)
        scripts_without(site)
    finally:
        if xvfb is not None:
            xvfb.terminate()
        srv.stdin.close()
        srv.wait(10)


def scripts_run(site):
    """a page's scripts on the Falcon, from EWEBJS.OVL"""
    hd = hd_for("scripts", site + "tabs.html", "SCRIPTS=127.0.0.1\r\n")
    r = Run("scripts", "Falcon:16", hd, windowed=True, network=True)
    try:
        ran = r.wait(r"scripts ran ", 300)
        ev = r.wait(r"shown ", 120)
        check("scripts: the page's ran (%s)" % (ran or "")[12:], ran is not None and ev is not None and
              ev.endswith("title Tabs ready"), str(r.events()))
        check("scripts: from EWEBJS.OVL, loaded for them",
              any(e.startswith("js: EWEBJS.OVL loaded at ") for e in r.events()),
              str(r.events()[:8]))
        r.shot(0)
        if ev is None:
            return
        m = re.search(r"area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev)
        area = tuple(int(m.group(i)) for i in range(1, 5))
        r.mouse.click(*text_point(site + "tabs.html", area, int(m.group(5)), "Second"))
        ev = r.wait(r"scripted ", 120)
        check("scripts: a tab clicked shows its words", ev is not None and ev.endswith("title Tab t2"),
              str(r.events()))
        r.shot(1)
        r.mouse.click(*ctl_point(site + "tabs.html", area, int(m.group(5)), 0))
        ev = r.wait(r"script alert", 120)
        time.sleep(2)
        r.shot(2)
        r.key(RETURN)
        check("scripts: onclick's alert, answered", ev is not None, str(r.events()))
        # a page whose script clicks its <a download> as it loads ("your
        # download starts now"): the window draws the page first, then the
        # file selector comes; saved under the link's name
        import server
        r.key(L, (CTRL,))
        r.type("127.0.0.1:%d/site/dl/starts.html" % HTTP)
        r.key(RETURN)
        ev = r.wait(r"shown ", 120)
        sv = r.wait(r"save link", 60)
        time.sleep(3)
        r.shot(3)
        r.key(RETURN)
        got = r.wait(r"saved ", 120)
        path = os.path.join(hd, "GOT.BIN")
        check("scripts: a download clicked as the page loads: the page drawn, then saved", ev is not None and
              ev.startswith("shown %sdl/starts.html " % site) and ev.endswith("title Your download") and
              sv is not None and got == "saved %d" % len(server.SMALL) and os.path.exists(path) and
              open(path, "rb").read() == server.SMALL, str(r.events()))
        r.shot(4)
        r.key(Q, (CTRL,))
        check("scripts: ^Q quits", r.wait(r"quit$", 60) is not None, str(r.events()))
    finally:
        r.end()


def scripts_without(site):
    """without EWEBJS.OVL: the page as with its scripts off, and the status
    line says why"""
    hd = hd_for("scriptsnoovl", site + "tabs.html", "SCRIPTS=127.0.0.1\r\n")
    os.remove(os.path.join(hd, "EWEBJS.OVL"))
    r = Run("scriptsnoovl", "Falcon:16", hd, windowed=True, network=True)
    try:
        ev = r.wait(r"shown ", 300)
        said = r.wait(r"note ", 60)
        check("scripts: without EWEBJS.OVL the page shows without them (%s)" % (said or "")[5:],
              ev is not None and ev.startswith("shown %stabs.html " % site) and
              not any(e.startswith("scripts ran") for e in r.events()) and
              said is not None and "EWEBJS.OVL is missing" in said and "status " + said[5:] in r.events(),
              str(r.events()[-8:]))
        r.shot(0)
        r.key(Q, (CTRL,))
        check("scripts: without EWEBJS.OVL, ^Q quits", r.wait(r"quit$", 60) is not None, str(r.events()[-5:]))
    finally:
        r.end()


def big_page():
    """a news page as big as most of the page set's (about 130 KB, 2,300
    elements, 300 links), the same bytes every time"""
    words = ("falcon atari window picture river garden network bread family museum engine desk city "
             "solar train joystick reader build tracker energy paper science photo flower holiday").split()
    out = ["<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Big</title><style>"
           ".story{margin:0 0 1em;border-bottom:1px solid #ccc} .story h2{font-size:1.2em} "
           "td{padding:2px 6px} .kicker{color:#a00;text-transform:uppercase}</style></head><body>"]
    k = 0
    for i in range(150):
        text = []
        for j in range(65):
            text.append(words[(i * 7 + j * 3 + k) % len(words)])
            k += 1
        out.append("<div class=\"story\"><p class=\"kicker\">Section %d</p><h2><a href=\"s%d.html\">Story %d: %s</a>"
                   "</h2><p>%s.</p><ul><li><a href=\"r%d.html\">Related %d</a></li><li>%s</li></ul>"
                   "<table><tr><td>%d</td><td>%s</td><td><em>%s</em></td></tr></table></div>" %
                   (i % 12, i, i, " ".join(text[:5]), " ".join(text), i, i, " ".join(text[5:15]), i,
                    " ".join(text[15:25]), " ".join(text[25:30])))
    out.append("</body></html>")
    return "\n".join(out).encode()


def falcon4():
    """EWEB.PRG on the stock Falcon (16 MHz, no FPU, 4 MB of ST RAM, no TT
    RAM, TOS 4.04; Erez, 23 Oct 2026: one EWEB.PRG for every Falcon): the
    window drawn in bands, a page as big as most of the page set's from the
    disk and loaded again, scripts off (no FPU: EWEBJS.OVL never loaded),
    the pictures' page, then ^Q"""
    name = "falcon4"
    hd = hd_for(name, r"C:\BIG.HTM", "SCRIPTS=file\r\n")
    with open(os.path.join(hd, "BIG.HTM"), "wb") as f:
        f.write(big_page())
    r = Run(name, "Falcon-4:16", hd)
    try:
        ready = r.wait(r"ready ", 300)
        ev = r.wait(r"shown ", 600)
        m = re.search(r"height (\d+) links (\d+) ", ev or "")
        check("falcon4: a big page shows (%s)" % (ready or "?")[6:],
              ready is not None and " buf 32 " in ready and ev is not None and
              ev.startswith("shown file:///C:/BIG.HTM ") and m is not None and int(m.group(2)) == 300 and
              int(m.group(1)) > 20000, str(r.events()[-6:]))
        time.sleep(2)
        r.shot(0)
        n = len([e for e in r.events() if e.startswith("shown ")])
        r.key(0x13, (CTRL,))                   # ^R
        t0 = time.time()
        while time.time() - t0 < 600 and len([e for e in r.events() if e.startswith("shown ")]) == n:
            time.sleep(2)
        last = [e for e in r.events() if e.startswith("shown ")][-1]
        check("falcon4: the big page loaded again", last.startswith("shown file:///C:/BIG.HTM ") and
              " links 300 " in last, last)
        check("falcon4: no FPU, so no scripts and no EWEBJS.OVL",
              not any(e.startswith("js: EWEBJS.OVL") for e in r.events()) and
              not any(e.startswith("scripts ran") for e in r.events()), str(r.events()[-6:]))
        r.key(L, (CTRL,))
        r.type("file:///C:/PICS.HTM")
        r.key(RETURN)
        ev = r.wait(r"pictures ", 1200)
        shown = [e for e in r.events() if e.startswith("shown file:///C:/PICS.HTM ")]
        check("falcon4: the pictures' page", ev is not None and len(shown) == 1, "\n".join(r.events()[-8:]))
        time.sleep(2)
        r.shot(1)
        r.key(Q, (CTRL,))
        check("falcon4: ^Q quits", r.wait(r"quit$", 120) is not None and
              not os.path.exists(os.path.join(hd, "CRASH.LOG")), str(r.events()[-5:]))
    finally:
        r.end()


def screens():
    for name, spec, tc in (("falcon256", "Falcon:256", False), ("truecolour", "Falcon:16", True),
                           ("ttmed", "TT:ttmed", False), ("ttmono", "TT:mono", False)):
        r = Run(name, spec, hd_for(name, r"C:\TABLES.HTM"), truecolour=tc)
        try:
            ready = r.wait(r"ready ", 120)
            ev = r.wait(r"shown ", 120)
            check("screens: %s (%s)" % (name, ready.split(" ", 1)[1] if ready else "?"),
                  ev is not None and ev.startswith("shown file:///C:/TABLES.HTM "), str(r.events()))
            # drawn whole off screen: buffers for the screen's height
            m = re.search(r"(\d+)x(\d+) buf (\d+)", ready or "")
            check("screens: %s draws the window in one go" % name, m is not None and m.group(2) == m.group(3),
                  ready or "no ready line")
            if tc:
                # a mode TOS didn't take must not pass as true colour
                check("screens: %s is true colour" % name, ready is not None and "true colour" in ready,
                      ready or "no ready line")
            if name == "falcon256":
                # EWeb's own 256 colours (PALETTE=1, as it starts)
                check("screens: falcon256 in EWeb's own colours", ready is not None and
                      "256 colours (EWeb's)" in ready, ready or "no ready line")
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
            # translucent colours and opacity, mixed with what is under them
            # through the screen's palette (looked at in its screenshot)
            r.key(L, (CTRL,))
            r.type("file:///C:/ALPHA.HTM")
            r.key(RETURN)
            ev = r.wait(r"shown ", 120)
            check("screens: %s translucent colours" % name,
                  ev is not None and ev.startswith("shown file:///C:/ALPHA.HTM "), str(r.events()[-5:]))
            r.wait(r"pictures ", 120)
            time.sleep(2)
            r.shot(3)
            # CSS gradients, in the screen's colours (looked at in its
            # screenshot)
            r.key(L, (CTRL,))
            r.type("file:///C:/GRAD.HTM")
            r.key(RETURN)
            ev = r.wait(r"shown ", 120)
            check("screens: %s gradients" % name,
                  ev is not None and ev.startswith("shown file:///C:/GRAD.HTM "), str(r.events()[-5:]))
            time.sleep(2)
            r.shot(5)
            if name == "falcon256":
                # ^, the Settings dialog (looked at in its screenshot); Esc
                # leaves it as it was
                r.key(COMMA, (CTRL,))
                ev = r.wait(r"settings shown", 60)
                time.sleep(2)
                r.shot(2)
                r.key(ESC)
                check("screens: ^, shows the Settings, Esc keeps them", ev is not None and
                      r.wait(r"settings ", 60) == "settings kept", str(r.events()[-5:]))
                # the search address holds more than its field shows (40;
                # the guidelines' 5.4): typed in whole and saved whole
                r.key(COMMA, (CTRL,))
                ev = r.wait(r"settings shown", 60)
                time.sleep(2)  # (drawn and waiting for keys)
                r.key(TAB)
                for _ in range(30):
                    r.key(BACKSPACE)
                long_search = "http://search.example.com/the/words/typed/in/the/address/go/here/s"
                r.type(long_search)
                time.sleep(1)
                r.shot(6)
                r.key(RETURN)
                saved = r.wait(r"settings ", 60) == "settings changed"
                time.sleep(2)
                inf = open(os.path.join(OUT, name, "EWEB.INF"), "rb").read().decode("latin-1")
                check("screens: the search address holds more than its field shows",
                      ev is not None and saved and "\r\nSEARCH=%s\r\n\r\n" % long_search in inf,
                      repr(inf[:400]))
            r.key(Q, (CTRL,))
            check("screens: %s quits" % name, r.wait(r"quit$", 60) is not None, str(r.events()))
            if name == "falcon256":
                check("screens: falcon256 gives TOS's colours back", r.wait(r"colours given back", 30) is not None,
                      str(r.events()[-4:]))
        finally:
            r.end()
        if name == "falcon256":
            cached_pictures(name, spec, dec)
    system_colours()


def system_colours():
    """PALETTE=0: in 256 colours, the system's own (TOS's) as they are, the
    page of translucent colours and the pictures in them (looked at beside
    falcon256's screenshots)"""
    name = "falcon256tos"
    r = Run(name, "Falcon:256", hd_for(name, r"C:\ALPHA.HTM", "PALETTE=0\r\n"))
    try:
        ready = r.wait(r"ready ", 120)
        ev = r.wait(r"shown ", 120)
        check("screens: falcon256 with PALETTE=0 in the system's colours", ready is not None and
              "256 colours" in ready and "(EWeb's)" not in ready and ev is not None and
              ev.startswith("shown file:///C:/ALPHA.HTM "), str(r.events()[-5:]))
        r.wait(r"pictures ", 120)
        time.sleep(2)
        r.shot(0)
        r.key(Q, (CTRL,))
        check("screens: falcon256 with PALETTE=0 quits, with no colours to give back",
              r.wait(r"quit$", 60) is not None and not r.wait(r"colours given back", 5), str(r.events()[-4:]))
    finally:
        r.end()


def cached_pictures(name, spec, dec):
    """the next run on the same disk: the page of pictures again, each read
    as the screen showed it from the cache (but the small one, decoded each
    time); the times of both, for DESIGN.md"""
    hd = os.path.join(OUT, name)
    r = Run(name + "-again", spec, hd)
    try:
        r.wait(r"shown ", 120)
        r.key(L, (CTRL,))
        r.type("file:///C:/PICS.HTM")
        r.key(RETURN)
        ev = r.wait(r"pictures ", 300)
        again = [e for e in r.events() if e.startswith("picture ") and " at " in e]
        ms = lambda evs: sum(int(e.rsplit(" ", 1)[1]) for e in evs if " decode " in e)
        print("  pictures in %s: decoded %d ms, from the cache %d ms" % (name, ms(dec), ms(again)))
        check("screens: %s pictures read back from the cache" % name, ev is not None and
              sum(" from the cache at " in e for e in again) == 4 and sum(" decoded at " in e for e in again) == 1,
              "\n".join(again or r.events()[-10:]))
        r.key(Q, (CTRL,))
        r.wait(r"quit$", 60)
    finally:
        r.end()


def st():
    """EWEBST.PRG, the ST edition, on Erez's 1040 ST (68000 at 8 MHz, 4 MB,
    TOS 1.04): a page and its pictures from the disk in monochrome and in
    medium resolution, and the pages of
    translucent colours and opacity (tests/layout/alpha.html) and of CSS
    gradients (tests/layout/gradients.html); then, over
    the network, a secure page through AERA-Gateway's webgate (the ST has
    no TLS of its own)"""
    if not os.path.exists(os.path.join(ROOT, "binary/EWEBST.PRG")):
        return check("st: EWEBST.PRG made", False, "make binary/EWEBST.PRG first")
    for name, spec in (("stmono", "ST:mono"), ("stmed", "ST:med")):
        r = Run(name, spec, hd_for(name, r"C:\TABLES.HTM"), prg="EWEBST.PRG")
        try:
            ready = r.wait(r"ready ", 300)
            ev = r.wait(r"shown ", 300)
            check("st: %s (%s)" % (name, ready.split(" ", 1)[1] if ready else "?"),
                  ev is not None and ev.startswith("shown file:///C:/TABLES.HTM "), str(r.events()))
            time.sleep(2)
            r.shot(0)
            r.key(L, (CTRL,))
            r.type("file:///C:/PICS.HTM")
            r.key(RETURN)
            ev = r.wait(r"pictures ", 1200)
            dec = [e for e in r.events() if " decoded at " in e]
            # the 800x600 PNG takes 1.9 MB decoded: more than a 4 MB ST
            # gives pictures (a sixteenth of its RAM), so it says so
            big = [e for e in r.events() if "big.png not enough memory" in e]
            check("st: %s pictures (the big one too big for the memory)" % name,
                  ev is not None and len(dec) == 4 and len(big) == 1, "\n".join(r.events()[-10:]))
            time.sleep(2)
            r.shot(1)
            # translucent colours and opacity as the ST edition's 68000
            # code mixes them: monochrome's patterns, medium's 4 colours
            # (looked at in its screenshot)
            r.key(L, (CTRL,))
            r.type("file:///C:/ALPHA.HTM")
            r.key(RETURN)
            ev = r.wait(r"shown ", 300)
            check("st: %s translucent colours" % name,
                  ev is not None and ev.startswith("shown file:///C:/ALPHA.HTM "), str(r.events()[-5:]))
            r.wait(r"pictures ", 300)
            time.sleep(2)
            r.shot(4)
            # CSS gradients as the ST edition draws them: those to a
            # side or at right angles a row or a column at a time, any
            # other its colours' average (DESIGN.md, CSS gradients;
            # make test checks the pixels, tests/layout/gradients.py);
            # here that the page shows (looked at in its screenshot)
            r.key(L, (CTRL,))
            r.type("file:///C:/GRAD.HTM")
            r.key(RETURN)
            ev = r.wait(r"shown ", 300)
            check("st: %s gradients" % name,
                  ev is not None and ev.startswith("shown file:///C:/GRAD.HTM "), str(r.events()[-5:]))
            time.sleep(3)
            r.shot(5)
            if name == "stmono":
                # a secure page with no gateway set: said plainly, the page
                # stays (the hub, 20 Oct 2026)
                r.key(L, (CTRL,))
                r.type("https://example.com/")
                r.key(RETURN)
                ev = r.wait(r"secure page", 120)
                time.sleep(2)
                r.shot(2)
                r.key(RETURN)
                check("st: a secure page without the gateway says it needs it",
                      ev == "secure page: needs the gateway", str(r.events()[-5:]))
                r.key(COMMA, (CTRL,))
                ev = r.wait(r"settings shown", 120)
                time.sleep(3)
                r.shot(3)
                r.key(ESC)
                check("st: ^, shows the Settings, Esc keeps them", ev is not None and
                      r.wait(r"settings ", 120) == "settings kept", str(r.events()[-5:]))
                # the search address longer than its field, its first and
                # last characters put in with Shift+left and Shift+right
                # (TOS 1.04's AES gives them no Shift: field.c reads it from
                # the key's character)
                r.key(COMMA, (CTRL,))
                ev = r.wait(r"settings shown", 120)
                r.key(TAB)
                for _ in range(30):
                    r.key(BACKSPACE)
                long_search = "http://search.example.com/the/words/typed/in/the/address/go/here/s"
                r.type(long_search[1:-1])
                r.key(LEFT, (SHIFT,))
                r.type(long_search[0])
                r.key(RIGHT, (SHIFT,))
                r.type(long_search[-1])
                time.sleep(2)
                r.shot(4)
                r.key(RETURN)
                saved = r.wait(r"settings ", 120) == "settings changed"
                time.sleep(3)
                inf = open(os.path.join(OUT, name, "EWEB.INF"), "rb").read().decode("latin-1")
                check("st: the search address holds more than its field shows, Shift+arrows at its ends",
                      ev is not None and saved and "\r\nSEARCH=%s\r\n\r\n" % long_search in inf,
                      repr(inf[:400]))
            r.key(Q, (CTRL,))
            check("st: %s quits" % name, r.wait(r"quit$", 120) is not None, str(r.events()))
        finally:
            r.end()
    webgate = os.environ.get("WEBGATE") or os.path.join(ROOT, "gateway", "webgate.py")
    if not os.path.exists(webgate):
        print("skip  st: through the gateway (no webgate at %s)" % webgate)
        return
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS),
                            "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("st: the test server", False)
    env = {k: v for k, v in os.environ.items() if k.lower() not in ("https_proxy", "http_proxy")}
    # the Pi's pens (aerapens behind its socket, tests/net/penssock.py), when this webgate makes them
    tool = os.path.join(ROOT, "common", "tools", "pens", "build", "aerapens")
    pens = "--pens-socket" in open(webgate).read() and os.access(tool, os.X_OK)
    sock = os.path.join(work, "pens.sock")
    penssock = None
    if pens:
        penssock = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/penssock.py"), sock, tool],
                                    stdout=subprocess.PIPE, text=True)
        pens = penssock.stdout.readline().strip() == "ready"
    gate = subprocess.Popen([sys.executable, webgate, "--port", str(GATE), "--bind", "127.0.0.1", "--allow-local",
                             "--cafile", os.path.join(work, "CACERT.PEM")] +
                            (["--pens-socket", sock] if pens else []),
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    site = "https://127.0.0.1:%d/site/" % HTTPS
    r = Run("stnet", "ST:mono", hd_for("stnet", site, "GATE=127.0.0.1\r\nGATEPORT=%d\r\n" % GATE), network=True,
            prg="EWEBST.PRG")
    try:
        ev = r.wait(r"shown ", 600)
        check("st: a secure page through the gateway", ev is not None and ev.startswith("shown " + site + " ")
              and ev.endswith("title EWeb site"), str(r.events()))
        time.sleep(2)
        r.shot(0)
        if pens:
            # its pictures as the screen's pens, made on the Pi: all five,
            # the big one too (its pens fit where its decoded pixels didn't)
            r.key(L, (CTRL,))
            r.type(site + "pics.html")
            r.key(RETURN)
            ev = r.wait(r"pictures ", 1200)
            got = [e for e in r.events() if " pens from the Pi at " in e]
            check("st: pictures through the gateway as the Pi's pens", ev is not None and len(got) == 5,
                  "\n".join(r.events()[-12:]))
            time.sleep(2)
            r.shot(1)
        else:
            print("skip  st: the Pi's pens (no aerapens, or a webgate without them)")
        r.key(Q, (CTRL,))
        check("st: quits after the network", r.wait(r"quit$", 120) is not None, str(r.events()))
    finally:
        r.end()
        gate.terminate()
        gate.wait(10)
        if penssock is not None:
            penssock.terminate()
            penssock.wait(10)
        srv.stdin.close()
        srv.wait(10)


def gateway():
    """EWEB.PRG on Erez's Falcon in 256 colours, through AERA-Gateway's
    webgate (Falcon mode off) on a page as superbooth.com's, its big
    pictures made smaller by the Pi (PENS=0: JPEG and PNG, decoded on the
    DSP) and as the screen's pens (PENS=1, aerapens behind its socket):
    every picture shown, and EWeb quits, CACHE written"""
    webgate = os.environ.get("WEBGATE") or os.path.join(ROOT, "gateway", "webgate.py")
    if not os.path.exists(webgate):
        print("skip  gateway: no webgate at %s" % webgate)
        return
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS),
                            "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("gateway: the test server", False)
    env = {k: v for k, v in os.environ.items() if k.lower() not in ("https_proxy", "http_proxy")}
    tool = os.path.join(ROOT, "common", "tools", "pens", "build", "aerapens")
    sock = os.path.join(work, "gwpens.sock")
    penssock = None
    if "--pens-socket" in open(webgate).read() and os.access(tool, os.X_OK):
        penssock = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/penssock.py"), sock, tool],
                                    stdout=subprocess.PIPE, text=True)
        penssock.stdout.readline()
    gate = subprocess.Popen([sys.executable, webgate, "--port", str(GATE), "--bind", "127.0.0.1", "--allow-local",
                             "--cafile", os.path.join(work, "CACERT.PEM")] +
                            (["--pens-socket", sock] if penssock else []),
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    page = "http://127.0.0.1:%d/site/gwpics.html" % HTTP
    wiki = "http://127.0.0.1:%d/site/wiki.html" % HTTP
    try:
        for pens in (0, 1) if penssock else (0,):
            name = "gwpens%d" % pens
            inf = "FALCON=0\r\nGATE=127.0.0.1\r\nGATEPORT=%d\r\nPENS=%d\r\n" % (GATE, pens)
            hd = hd_for(name, page, inf)
            # with the family's accessories loaded, as on Erez's Falcon
            r = Run(name, "Falcon:256", hd, network=True, accs=("ECal", "EContact", "EHypView"))
            try:
                ev = r.wait(r"pictures ", 1800)
                evs = r.events()
                got = [e for e in evs if re.search(r"^picture \S+ (decoded|pens from the Pi) at ", e)]
                time.sleep(2)
                r.shot(0)
                # then a page as a Wikipedia article's, and back
                r.key(L, (CTRL,))
                r.type(wiki)
                r.key(RETURN)
                wev = r.wait(r"pictures ", 1800)
                time.sleep(2)
                r.shot(1)
                check("gateway: %s, a page as Wikipedia's after it" % name, wev is not None and
                      not os.path.exists(os.path.join(hd, "CRASH.LOG")), "\n".join(r.events()[-25:]))
                r.key(UNDO)
                r.wait(r"shown ", 600)
                r.key(Q, (CTRL,))
                quit = r.wait(r"quit$", 300)
                if quit is None:
                    r.shot(2)
                # (the test server's pictures can't be kept by HTTP's rules:
                # only pictures decoded here are kept, as EWeb's own)
                check("gateway: %s, every picture %s, quits" % (
                      name, "as the Pi's pens" if pens else "the Pi's JPEG and PNG, decoded"),
                      ev is not None and len(got) == 5 and quit is not None and
                      all(("pens from the Pi" in e) == bool(pens) for e in got) and
                      not os.path.exists(os.path.join(hd, "CRASH.LOG")) and
                      (pens or os.path.isdir(os.path.join(hd, "CACHE"))), "\n".join(r.events()[-20:]))
            finally:
                r.end()
    finally:
        gate.terminate()
        gate.wait(10)
        if penssock is not None:
            penssock.terminate()
            penssock.wait(10)
        srv.stdin.close()
        srv.wait(10)


def falcon():
    """EWEB.PRG on Erez's Falcon in 256 colours in Falcon mode (TLS on the
    68030 and the DSP, no gateway), a secure page as a Wikipedia
    article's with its pictures from another secure host: the DSP turns
    from the handshakes' arithmetic to the JPEGs and the dither and back;
    scrolled while the pictures come, then the page loaded again"""
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS),
                            "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        return check("falcon: the test server", False)
    page = "https://127.0.0.1:%d/site/wikitls.html" % HTTPS
    hd = hd_for("falconwiki", page)
    shutil.copy(os.path.join(work, "CACERT.PEM"), os.path.join(hd, "CACERT.PEM"))
    r = Run("falconwiki", "Falcon:256", hd, network=True)
    try:
        ev = r.wait(r"shown ", 1200)
        for _ in range(12):             # scrolled while the pictures come
            r.key(DOWN)
            time.sleep(1)
        pv = r.wait(r"pictures ", 2400)
        r.shot(0)
        evs = r.events()
        dec = [e for e in evs if re.search(r"^picture \S+ decoded at ", e)]
        r.key(L, (CTRL,))
        r.type(page)
        r.key(RETURN)
        again = r.wait(r"shown ", 1200)
        r.wait(r"pictures ", 120)       # none when they are all still in memory
        r.shot(1)
        r.key(Q, (CTRL,))
        quit = r.wait(r"quit$", 300)
        check("falcon: a secure page's pictures from another secure host on the DSP, scrolled, again",
              ev is not None and pv is not None and len(dec) >= 7 and again is not None and quit is not None and
              not os.path.exists(os.path.join(hd, "CRASH.LOG")), "\n".join(r.events()[-30:]))
    finally:
        r.end()
        srv.stdin.close()
        srv.wait(10)


def deep():
    """a page nested 1000 deep, from the disk, on the Falcon and the 1040 ST
    (256 KB of stack each): shown cut at the stack's floor (the
    log's "deep"), not crashed, and the next page shown after it (DESIGN.md,
    The stack: such a page ran off the stack's end and bombed)"""
    page = "<!DOCTYPE html><title>Deep</title>" + "<div>level" * 1000 + "</div>" * 1000
    for name, spec, prg in (("deepfalcon", "Falcon:256", "EWEB.PRG"), ("deepst", "ST:mono", "EWEBST.PRG")):
        hd = hd_for(name, r"C:\DEEP.HTM")
        with open(os.path.join(hd, "DEEP.HTM"), "w") as f:
            f.write(page)
        r = Run(name, spec, hd, prg=prg)
        try:
            ev = r.wait(r"(shown|stopped) ", 600)
            cut = r.wait(r"deep ", 30) if ev and ev.startswith("shown ") else None
            r.shot(0)
            r.key(L, (CTRL,))
            r.type("file:///C:/TEXT.HTM")
            r.key(RETURN)
            after = r.wait(r"shown file:///C:/TEXT.HTM ", 600)
            r.key(Q, (CTRL,))
            quit = r.wait(r"quit$", 300)
            ok = after is not None and quit is not None and not os.path.exists(os.path.join(hd, "CRASH.LOG"))
            check("deep: %s, a page 1000 deep cut, then the next page" % name, ok and cut is not None,
                  "\n".join(r.events()[-10:]))
        finally:
            r.end()


def bar(r):
    """what the status bar says now: win_status() logs each change ("status ...")"""
    said = [e for e in r.events() if e == "status" or e.startswith("status ")]
    return said[-1][7:] if said else None


LOOP_NOTE = "This page sends EWeb on at once again and again: it stays here."


def refresh():
    """a refresh (<meta http-equiv=refresh>) on the Falcon, from the disk:
    the idle loop's timer, the status line, Esc, the loop stopped"""
    hd = hd_for("refresh", r"C:\TABLES.HTM")
    pages = {"REFRESH.HTM": "<meta http-equiv=refresh content='20; url=TEXT.HTM'><p>On to the text in 20 s.",
             "STAY.HTM": "<meta http-equiv=refresh content='600; url=TABLES.HTM'><p>Esc keeps this page."
                         "<p><img src=PHOTO.JPG width=120 height=80 alt=photo> <img src=LOGO.PNG alt=logo>",
             # (a picture this group shows nowhere else: not yet decoded)
             "LOOP.HTM": "<meta http-equiv=refresh content=0><p>Again and again. <img src=PROG.JPG width=80 alt=p>",
             # two that send each other on at once; the one it stops on has
             # pictures shown nowhere else in the group, which come after
             # the message
             "ONE.HTM": "<meta http-equiv=refresh content='0; url=TWO.HTM'><p>One. <img src=BIG.PNG width=200 alt=big>",
             "TWO.HTM": "<meta http-equiv=refresh content='0; url=ONE.HTM'><p>Two. <img src=ANIM.GIF alt=gif>"
                        " <img src=COUNTER.PNG width=40 height=40 alt=counter>"}
    for name, body in pages.items():
        with open(os.path.join(hd, name), "w") as f:
            f.write("<!DOCTYPE html><title>%s</title>%s" % (name[:-4].title(), body))
    r = Run("refresh", "Falcon:16", hd)
    try:
        ev = r.wait(r"shown ", 600)
        if not check("refresh: the start page", ev is not None and ev.startswith("shown file:///C:/TABLES.HTM "),
                     str(r.events())):
            return
        r.key(L, (CTRL,))
        r.type("file:///C:/REFRESH.HTM")
        mark = len(r.events())
        r.key(RETURN)
        ev = r.wait(r"shown ", 300)
        t0 = time.time()
        time.sleep(0.5)
        r.shot(0)
        said = [e for e in r.events()[mark:] if e.startswith("status ")]
        check("refresh: a page that goes on, said on the status line", ev is not None and
              ev.startswith("shown file:///C:/REFRESH.HTM ") and
              "status Going on to TEXT.HTM in 20 s: Esc stays here." in said and
              re.match(r"Going on to TEXT\.HTM in (20|1?\d) s: Esc stays here\.$", bar(r) or "") is not None,
              "\n".join(r.events()[mark:]))
        went = r.wait(r"refresh ", 120)
        span = time.time() - t0     # the emulated 20 s, in real seconds (fast-forward)
        ev = r.wait(r"shown ", 300)
        r.shot(1)
        check("refresh: on to the page it names when its time is up", went == "refresh 20 file:///C:/TEXT.HTM" and
              ev is not None and ev.startswith("shown file:///C:/TEXT.HTM "), str(r.events()))
        r.key(UNDO)
        ev = r.wait(r"shown ", 300)
        check("refresh: in the page's place (Undo goes to the one before)", ev is not None and
              ev.startswith("shown file:///C:/TABLES.HTM "), str(r.events()))
        r.key(L, (CTRL,))
        r.type("file:///C:/STAY.HTM")
        r.key(RETURN)
        ev = r.wait(r"shown ", 300)
        mark = r.seen
        # its pictures' lines on the status bar, then the refresh again
        pics = r.wait(r"pictures file:///C:/STAY\.HTM ", 300)
        time.sleep(1)
        r.shot(5)
        check("refresh: said again once the pictures are in", pics is not None and
              any(e.startswith("status Pictures: ") for e in r.events()[mark:]) and
              bar(r) == "Going on to TABLES.HTM in 10 min: Esc stays here.", "\n".join(r.events()[mark:]))
        r.key(ESC)
        stopped = r.wait(r"refresh stopped$", 30)
        time.sleep(4)
        r.shot(2)
        check("refresh: Esc keeps the page", ev is not None and ev.startswith("shown file:///C:/STAY.HTM ") and
              stopped is not None and not any(e.startswith("refresh ") for e in r.events()[r.seen:]) and
              bar(r) == "Staying on this page.", str(r.events()[-10:]))
        # while the address field has the keyboard it waits (what is typed
        # there would go); Tab gives the keyboard back, and it goes on
        r.key(L, (CTRL,))
        r.type("file:///C:/REFRESH.HTM")
        r.key(RETURN)
        ev = r.wait(r"shown ", 300)
        r.key(L, (CTRL,))
        time.sleep(max(2 * span, 4))
        held = not any(e.startswith("refresh ") for e in r.events()[r.seen:])
        r.shot(4)
        r.key(TAB)
        went = r.wait(r"refresh ", 120)
        after = r.wait(r"shown ", 300)
        check("refresh: it waits while the address field has the keyboard (%.1f s)" % span,
              ev is not None and ev.startswith("shown file:///C:/REFRESH.HTM ") and held and
              went == "refresh 20 file:///C:/TEXT.HTM" and after is not None and
              after.startswith("shown file:///C:/TEXT.HTM "), str(r.events()))
        # itself at once: each time once its picture is in (read again,
        # the same: not decoded again), stopped after five, and said so
        # on the status line once its picture is in the last time
        r.key(L, (CTRL,))
        r.type("file:///C:/LOOP.HTM")
        r.key(RETURN)
        note = r.wait(r"note ", 600)
        pics = r.wait(r"pictures file:///C:/LOOP\.HTM ", 300)
        time.sleep(1)
        r.shot(3)
        evs = r.events()
        start = max(i for i, e in enumerate(evs) if e.startswith("shown file:///C:/TEXT.HTM "))
        shown = [e for e in evs[start:] if e.startswith("shown file:///C:/LOOP.HTM ")]
        went = [e for e in evs[start:] if e.startswith("refresh 0 file:///C:/LOOP.HTM")]
        first = [e.split()[0] for e in evs[start:] if e.startswith(("pictures file:///C:/LOOP.HTM ", "refresh 0 "))]
        prog = [e.split()[2] for e in evs[start:] if e.startswith("picture file:///C:/PROG.JPG ")]
        check("refresh: a page that refreshes itself at once stops after 5", len(shown) == 6 and len(went) == 5 and
              note == "note " + LOOP_NOTE and pics is not None and bar(r) == LOOP_NOTE, "\n".join(evs[start:]))
        check("refresh: itself again once its picture is in, read again (not decoded again)",
              first[:2] == ["pictures", "refresh"] and prog.count("unchanged") == 5 and
              prog.count("decoded") + prog.count("from") == 1, "\n".join(evs[start:]))
        # two that send each other on at once: stopped after five, and the
        # status line says so after the pictures of the one it stops on
        r.key(L, (CTRL,))
        r.type("file:///C:/ONE.HTM")
        r.key(RETURN)
        note = r.wait(r"note ", 600)
        mark = r.seen
        pics = r.wait(r"pictures file:///C:/TWO\.HTM ", 300)
        time.sleep(2)
        r.shot(6)
        evs = r.events()
        start = max(i for i, e in enumerate(evs) if e.startswith("shown file:///C:/LOOP.HTM "))
        went = [e for e in evs[start:] if e.startswith("refresh 0 ")]
        check("refresh: two that send each other on stop after 5, said once the pictures are in",
              len(went) == 5 and note == "note " + LOOP_NOTE and pics is not None and
              any(e.startswith("status Pictures: ") for e in evs[mark:]) and bar(r) == LOOP_NOTE,
              "\n".join(evs[start:]))
        r.key(Q, (CTRL,))
        check("refresh: ^Q quits", r.wait(r"quit$", 60) is not None, str(r.events()))
    finally:
        r.end()


def details():
    """a details' summary clicked: it opens where it is (the page laid out
    again, taller, the window's place kept), and closes again"""
    xvfb = machines.xvfb(DISPLAY)
    page = os.path.join(ROOT, "tests/layout/details.html")
    try:
        for name, spec, prg in (("details", "Falcon:16", "EWEB.PRG"), ("detailsst", "ST:mono", "EWEBST.PRG")):
            hd = hd_for(name, r"C:\DETAILS.HTM")
            shutil.copy(page, os.path.join(hd, "DETAILS.HTM"))
            r = Run(name, spec, hd, windowed=True, prg=prg)
            try:
                ev = r.wait(r"shown ", 300)
                check("details: %s shows the page" % name,
                      ev is not None and ev.startswith("shown file:///C:/DETAILS.HTM "), str(r.events()))
                if ev is None:
                    continue
                time.sleep(2)
                r.shot(0)
                m = re.search(r"height (\d+) .*area (\d+) (\d+) (\d+) (\d+) top (\d+)", ev)
                h0, area, top = int(m.group(1)), tuple(int(m.group(i)) for i in range(2, 6)), int(m.group(6))
                pt = text_point(page, area, top, "Closed: only")
                if not check("details: %s, the summary found" % name, pt is not None):
                    continue
                r.mouse.click(*pt)
                ev = r.wait(r"details ", 120)
                m = re.match(r"details (\w+) height (\d+) top (\d+) ms (\d+)", ev or "")
                check("details: %s, a summary clicked opens it (%s ms)" % (name, m.group(4) if m else "?"),
                      m is not None and m.group(1) == "opened" and int(m.group(2)) > h0 and int(m.group(3)) == top,
                      str(r.events()[-5:]))
                time.sleep(2)
                r.shot(1)
                r.mouse.click(*pt)
                ev = r.wait(r"details ", 120)
                m = re.match(r"details (\w+) height (\d+) top (\d+) ms (\d+)", ev or "")
                check("details: %s, clicked again it closes (%s ms)" % (name, m.group(4) if m else "?"),
                      m is not None and m.group(1) == "closed" and int(m.group(2)) == h0, str(r.events()[-5:]))
                time.sleep(2)
                r.shot(2)
                # an address whose #fragment is in a closed details: it
                # opens, and the window is there
                r.key(L, (CTRL,))
                r.type("c:/details.htm#nosump")
                r.key(RETURN)
                ev = r.wait(r"shown ", 120)
                m = re.search(r"height (\d+) .*top (\d+)", ev or "")
                check("details: %s, a #fragment in a closed details: opened, the window there" % name,
                      ev is not None and re.match(r"shown file:///c:/details\.htm#nosump ", ev, re.I) and
                      m is not None and int(m.group(1)) > h0 and int(m.group(2)) > 0, str(r.events()[-5:]))
                if ev is None or m is None:
                    continue
                time.sleep(2)
                r.shot(3)
                # a summary further down clicked: the window stays where
                # it was (not at the page's top)
                h1, top = int(m.group(1)), int(m.group(2))
                pt = text_point(page + "#nosump", area, top, "A summary that isn")
                if not check("details: %s, a summary further down found in the window" % name,
                             pt is not None and area[1] < pt[1] < area[1] + area[3], str(pt)):
                    continue
                r.mouse.click(*pt)
                ev = r.wait(r"details ", 120)
                m = re.match(r"details (\w+) height (\d+) top (\d+) ms (\d+)", ev or "")
                check("details: %s, scrolled down, a summary clicked opens it where the window was" % name,
                      m is not None and m.group(1) == "opened" and int(m.group(2)) > h1 and int(m.group(3)) == top,
                      str(r.events()[-5:]))
                time.sleep(2)
                r.shot(4)
                # a place on the same page in another closed details: it
                # opens, the page laid out again, the window there
                r.key(L, (CTRL,))
                r.type("c:/details.htm#c1p")
                r.key(RETURN)
                ev = r.wait(r"same ", 120)
                m = re.search(r"top (\d+) opened height (\d+)", ev or "")
                check("details: %s, a #fragment on the page in a closed details: opened, the window there" % name,
                      m is not None and 0 < int(m.group(1)) < top, str(r.events()[-5:]))
                time.sleep(2)
                r.shot(5)
                r.key(Q, (CTRL,))
                check("details: %s quits" % name, r.wait(r"quit$", 120) is not None, str(r.events()[-5:]))
            finally:
                r.end()
    finally:
        if xvfb is not None:
            xvfb.terminate()


os.makedirs(OUT, exist_ok=True)
if not os.path.exists(os.path.join(ROOT, "binary/EWEB.PRG")):
    sys.exit("gui_test: make binary/EWEB.PRG binary/EWEB.FNT build/host/ewrender first")
if "browse" in WHICH:
    if shutil.which("Xvfb") and shutil.which("xdotool"):
        browse()
    else:
        print("skip  browse: needs Xvfb and xdotool")
if "scripts" in WHICH:
    if shutil.which("Xvfb") and shutil.which("xdotool"):
        scripts()
    else:
        print("skip  scripts: needs Xvfb and xdotool")
if "screens" in WHICH:
    screens()
if "st" in WHICH:
    st()
if "gateway" in WHICH:
    gateway()
if "falcon" in WHICH:
    falcon()
if "falcon4" in WHICH:
    falcon4()
if "deep" in WHICH:
    deep()
if "refresh" in WHICH:
    refresh()
if "details" in WHICH:
    if shutil.which("Xvfb") and shutil.which("xdotool"):
        details()
    else:
        print("skip  details: needs Xvfb and xdotool")
print("gui_test: %s" % ("all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
