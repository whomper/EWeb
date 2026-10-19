#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""What EWEB.PRG and EWGET.TTP do on the disk (the guidelines' 6.5), from
Hatari's GEMDOS trace (--trace gemdos), on the Falcon in 16 colours
through FAKESTNG, against tests/net/server.py and a site served here:

- EWEB.PRG, twice in the same folder: start; a page with 10 pictures;
  5 pages; back 4 and forward 4; the page size changed and back; a secure
  page (the first: certificates, seed) and another; idle; quit. The
  second time, start, a secure page and quit.
- EWGET.TTP: 1 MB over http, written in one go.

Each case's Fopen, Fcreate and Fsfirst are counted between the events
EWEB.PRG gives through NatFeats, those the program makes itself (at a PC
in its TPA, not the desktop's), and must stay within the case's budget:
about one open per file the job touches, plus a safe save's. A quit
writes only COOKIES.TXT, once, as the last of the five pages set a
cookie that lasts; pages, history and idle touch nothing.

    tests/hatari/disk_files.py OUT      (IDLE=seconds, 300 by default)"""
import collections
import http.server
import os
import re
import shutil
import socketserver
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import machines  # noqa: E402

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/files")
IDLE = int(os.environ.get("IDLE", "300"))
HTTP, HTTPS, SITE = 18480, 18843, 18481
CTRL, SHIFT = 0x1D, 0x2A
L, Q, RETURN, UNDO, ZERO, PLUS = 0x26, 0x10, 0x1C, 0x61, 0x0B, 0x0D
KEYS = {":": (SHIFT, 0x27), "/": (None, 0x35), ".": (None, 0x34), "-": (None, 0x0C)}
CALLS = ("Fopen", "Fcreate", "Fsfirst")

# the most each case may do: Fopen, Fcreate, Fsfirst
BUDGET = {
    "start": (2, 0, 0),             # EWEB.INF, EWEB.FNT
    # the first page over the network: COOKIES.TXT read (not there: its
    # safe save's new file looked for too)
    "pictures": (1, 0, 1),
    "five pages": (0, 0, 0),
    "back and forward": (0, 0, 0),  # from memory
    "page size": (0, 2, 0),         # ^+ and ^0: EWEB.INF saved each time
    # the first ever: CACERT.PEM (read twice: no ROOTS.DAT yet), ROOTS.DAT
    # (tried first) and SEED.DAT made (the release brings a ROOTS.DAT)
    "first secure page": (4, 2, 3),
    "secure page": (0, 0, 0),
    "idle": (0, 0, 0),
    # COOKIES.TXT, once (p5.html's cookie), its value encrypted: EWEB.KEY
    # looked for, then made; EWEB.INF unchanged
    "quit": (1, 2, 0),
    "start again": (2, 0, 0),
    # COOKIES.TXT and EWEB.KEY; CACERT.PEM's date; ROOTS.DAT; SEED.DAT read,
    # then written over
    "secure page again": (5, 0, 1),
    "quit again": (0, 0, 0),        # the cookies as they were
    "EWGET 1 MB": (3, 3, 1),        # EWGET.CMD, EWGET.LOG, NET.LOG (-l: once), the file; CRASH.LOG looked for
}

fails = 0


def check(name, ok, detail=""):
    global fails
    print("%s  %s%s" % ("ok  " if ok else "FAIL", name, "" if ok else "\n      " + detail))
    fails += not ok
    return ok


def site(d):
    """a page with 10 pictures, 5 pages linked in a row, 1 MB"""
    os.makedirs(d, exist_ok=True)
    pics = "".join('<p><img src="i%d.png" alt="picture %d" width=64 height=48> text %d</p>' % (i, i, i)
                   for i in range(10))
    with open(os.path.join(d, "pictures.html"), "w") as f:
        f.write("<!DOCTYPE html><title>Ten pictures</title><link rel=stylesheet href=s.css><h1>Ten</h1>" + pics)
    with open(os.path.join(d, "s.css"), "w") as f:
        f.write("h1 { color: navy }\n")
    for i in range(10):
        with open(os.path.join(d, "i%d.png" % i), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + bytes(2000))
    for i in range(1, 6):
        with open(os.path.join(d, "p%d.html" % i), "w") as f:
            f.write("<!DOCTYPE html><title>Page %d</title><link rel=stylesheet href=s.css><h1>Page %d</h1>"
                    "<p>%s</p>%s" % (i, i, "words " * 400, '<a href="p%d.html">next</a>' % (i + 1) if i < 5 else ""))
    with open(os.path.join(d, "big.bin"), "wb") as f:
        f.write(bytes((i * 7 + (i >> 9)) & 0xFF for i in range(1 << 20)))


def serve(d):
    class Handler(http.server.SimpleHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def __init__(self, *a, **k):
            super().__init__(*a, directory=d, **k)

        def log_message(self, *a):
            pass

        def end_headers(self):
            # the last of the five pages sets a cookie that lasts: EWeb
            # writes COOKIES.TXT once, at quit
            if self.path.endswith("/p5.html"):
                self.send_header("Set-Cookie", "kept=1; Max-Age=86400; Path=/")
            super().end_headers()

    class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
        daemon_threads = True
        allow_reuse_address = True

        def handle_error(self, request, client_address):
            # the Atari closed the connection early (Esc, another page):
            # nothing to report
            if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
                return
            super().handle_error(request, client_address)
    s = Server(("127.0.0.1", SITE), Handler)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s


def tpa(lines, prog):
    """where prog is in memory: its basepage to the end of what its start
    code keeps (its first Mshrink), from the trace"""
    at = next(i for i, l in enumerate(lines) if "now loading 'C:\\%s'" % prog in l)
    for l in lines[at:]:
        m = re.search(r"Mshrink\(0x([0-9A-F]+), 0x([0-9A-F]+)\)", l)
        if m:
            return int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)
    raise RuntimeError("no Mshrink for " + prog)


def counts(lines, where):
    """GEMDOS calls by name and by file, those the program itself made (at a
    PC in its TPA): not the desktop's, before and after it"""
    c, files = collections.Counter(), collections.Counter()
    for l in lines:
        m = re.match(r'GEMDOS 0x\w+ (\w+)\("([^"]*)".* at PC=? ?0x([0-9A-F]+)', l)
        if m and m.group(1) in CALLS and where[0] <= int(m.group(3), 16) < where[1]:
            c[m.group(1)] += 1
            files["%s %s" % (m.group(1), m.group(2))] += 1
    return c, files


def judge(case, lines, where):
    c, files = counts(lines, where)
    got = tuple(c[k] for k in CALLS)
    over = [k for k, g, b in zip(CALLS, got, BUDGET[case]) if g > b]
    check("%-18s Fopen %d, Fcreate %d, Fsfirst %d (at most %d, %d, %d)" % ((case,) + got + BUDGET[case]),
          not over, ", ".join("%s %d" % (f, n) for f, n in sorted(files.items())))


class Run:
    """Hatari with a program, its GEMDOS trace and its command FIFO"""

    def __init__(self, name, hd, prog):
        self.fifo = os.path.join(OUT, name + ".fifo")
        self.log = os.path.join(OUT, name + ".log")
        self.trace = os.path.join(OUT, name + ".trace")
        for p in (self.fifo, self.trace):
            if os.path.exists(p):
                os.remove(p)
        self.m = machines.Machine("Falcon:16", hd)
        args = ["hatari"] + self.m.args() + ["--natfeats", "yes", "--statusbar", "no", "--log-level", "fatal",
                                             "--cmd-fifo", self.fifo, "--auto", "C:\\" + prog,
                                             "--trace", "gemdos", "--trace-file", self.trace,
                                             "--fast-forward", "yes"] + self.m.network()
        self.h = subprocess.Popen(args, env=self.m.env(), stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.fifo):
                break
            time.sleep(0.1)
        self.ctl = open(self.fifo, "w")
        self.seen = 0
        self.at = None

    def lines(self):
        with open(self.trace, errors="replace") as f:
            return f.read().splitlines()

    def case(self, name):
        """the trace from the last case's end to now is case name's"""
        time.sleep(2)
        lines = self.lines()
        if self.at is None:
            self.at = next((i for i, l in enumerate(lines) if "Pexec(0, \"C:\\EWEB.PRG\"" in l), 0)
            self.where = tpa(lines, "EWEB.PRG")
        judge(name, lines[self.at:], self.where)
        self.at = len(lines)

    def send(self, cmd):
        self.ctl.write(cmd + "\n")
        self.ctl.flush()
        time.sleep(0.25)

    def wait(self, pattern, timeout=300):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with open(self.log, errors="replace") as f:
                evs = [l.strip()[5:] for l in f if l.startswith("EWEB ")]
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

    def go(self, url):
        self.key(L, (CTRL,))
        for ch in url:
            if ch in KEYS:
                mod, scan = KEYS[ch]
                self.key(scan, (mod,) if mod else ())
            else:
                self.send("hatari-event keypress %s" % ch)
            time.sleep(0.15)
        self.key(RETURN)
        ev = self.wait(r"shown ", 600)
        check("  shown " + url, ev is not None and ev.startswith("shown "), str(ev))

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


def eweb(hd):
    s = "127.0.0.1:%d/" % SITE
    secure = "https://127.0.0.1:%d/site/" % HTTPS
    r = Run("eweb", hd, "EWEB.PRG")
    try:
        if not check("  EWEB.PRG ready", r.wait(r"shown ", 300) is not None):
            return
        r.case("start")
        r.go(s + "pictures.html")
        r.case("pictures")
        for i in range(1, 6):
            r.go(s + "p%d.html" % i)
        r.case("five pages")
        for mods in [()] * 4 + [(SHIFT,)] * 4:
            r.key(UNDO, mods)
            check("  back or forward", r.wait(r"shown ", 300) is not None)
        r.case("back and forward")
        r.key(PLUS, (CTRL,))
        r.key(ZERO, (CTRL,))
        r.case("page size")
        r.go(secure)
        r.case("first secure page")
        r.go(secure + "two.html")
        r.case("secure page")
        time.sleep(IDLE)
        r.case("idle")
        r.key(Q, (CTRL,))
        check("  quit", r.wait(r"quit$", 60) is not None)
        r.case("quit")
    finally:
        r.end()
    r = Run("eweb2", hd, "EWEB.PRG")
    try:
        if not check("  EWEB.PRG ready again", r.wait(r"shown ", 300) is not None):
            return
        r.case("start again")
        r.go(secure)
        r.case("secure page again")
        r.key(Q, (CTRL,))
        check("  quit again", r.wait(r"quit$", 60) is not None)
        r.case("quit again")
    finally:
        r.end()


def ewget(hd):
    with open(os.path.join(hd, "EWGET.CMD"), "w") as f:
        f.write("-l NET.LOG -o BIG.BIN http://127.0.0.1:%d/big.bin" % SITE)
    r = Run("ewget", hd, "EWGET.TTP")
    try:
        deadline = time.time() + 1200
        while time.time() < deadline and r.h.poll() is None:
            if "done (" in open(r.log, errors="replace").read():
                break
            time.sleep(2)
        time.sleep(3)
        lines = r.lines()
        at = next((i for i, l in enumerate(lines) if "Pexec(0, \"C:\\EWGET.TTP\"" in l), 0)
        judge("EWGET 1 MB", lines[at:], tpa(lines, "EWGET.TTP"))
        big = os.path.join(hd, "BIG.BIN")
        same = os.path.exists(big) and open(big, "rb").read() == open(os.path.join(OUT, "site", "big.bin"),
                                                                        "rb").read()
        check("  EWGET's 1 MB came whole", same, open(r.log, errors="replace").read()[-400:])
        # gathered in memory: the file is made once, after the body came
        made = [i for i, l in enumerate(lines) if 'Fcreate("BIG.BIN"' in l]
        check("  BIG.BIN made once", len(made) == 1)
        # in 32 KB pieces (as they are under EmuTOS, which sets _FRB; through
        # 32 KB of ST-RAM under a TOS without it): its writes up to its Fclose
        h = re.search(r"-> FD (\d+)", "\n".join(lines[made[0]:made[0] + 4])) if made else None
        pieces = []
        for l in lines[made[0]:] if h else []:
            if l.startswith("GEMDOS 0x3E Fclose(%s)" % h.group(1)):
                break
            if l.startswith("GEMDOS 0x40 Fwrite(%s, " % h.group(1)):
                pieces.append(l)
        check("  BIG.BIN in 32 KB pieces", len(pieces) == 32 and all(", 32768, " in l for l in pieces),
              "%d writes" % len(pieces))
    finally:
        r.end()


def main():
    if not shutil.which("hatari"):
        sys.exit("disk_files: needs Hatari")
    os.makedirs(OUT, exist_ok=True)
    site(os.path.join(OUT, "site"))
    st = serve(os.path.join(OUT, "site"))
    work = os.path.join(OUT, "server")
    os.makedirs(work, exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests/net/server.py"), work, "--http", str(HTTP),
                            "--https", str(HTTPS), "--untrusted", str(HTTPS + 1), "--ecdsa", str(HTTPS + 2)],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        if srv.stdout.readline().strip() != "ready":
            sys.exit("disk_files: the test server didn't start")
        hd = os.path.join(OUT, "hd")
        shutil.rmtree(hd, ignore_errors=True)
        os.makedirs(hd)
        for f in ("EWEB.PRG", "EWEB.FNT", "EWGET.TTP"):
            shutil.copy(os.path.join(ROOT, "binary", f), hd)
        shutil.copy(os.path.join(work, "CACERT.PEM"), hd)      # the test authority
        with open(os.path.join(hd, "EWEB.INF"), "w") as f:
            f.write("HOME=\r\n")
        eweb(hd)
        ewget(hd)
    finally:
        srv.stdin.close()
        srv.wait(10)
        st.shutdown()
    print("disk_files: %s" % ("all within budget" if not fails else "%d failed" % fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
