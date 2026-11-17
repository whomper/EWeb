#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mintnet.py [--ewget EWGET.TTP] [--prg EWEB.PRG] [--only ewget|wait|esc]:
common's connections over MiNTnet, FreeMiNT 1.17.0's own TCP/IP
(inet4.xdd) on ARAnyM's network card (nfeth.xif, a TAP device to this
computer: 192.168.0.2 there, 192.168.0.1 here), as on a FreeMiNT
machine's network (a tester's V4SA, 17 Nov 2026: EWeb stood still on a
page's counter picture whose server never answers). Hatari has no
network card.

IFUP.PRG (tests/aranym/ifup.c, made here) brings the card up from
MINT.CNF, then:
  ewget  EWGET.TTP fetches, each from MINT.CNF's exec lines:
           ok       a page from a server here (port 8000): it comes;
           refused  a port nothing listens on (8001): it fails at once;
           dropped  a port whose packets this computer drops (8002,
                    iptables): common's MiNTnet connect (net_tos.c,
                    mint_connect) gives up in its 20 s, where a blocking
                    Fconnect waited on MiNT's TCP (minutes);
  wait   EWEB.PRG under XaAES on a page with a picture on port 8002 and
         one on 8000: the page is shown at once, the first picture is
         given up in 20 s, and the second then comes;
  esc    the same page, Esc pressed 5 s into the dropped picture's
         connection: it stops then, not 20 s on.
EWEB.LOG (LOG=1) is read from the disk afterwards; EWeb's events come
through NatFeats.

Needs root (the TAP device, iptables), aranym, aratapif, Xvfb, xdotool
and mtools, and what deep.py fetches. EWGET.TTP and EWEB.PRG: make.
"""
import argparse
import http.server
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deep  # noqa: E402

ROOT = deep.ROOT
OUT = os.path.join(ROOT, "build", "aranym", "mintnet")
HOST, ATARI = "192.168.0.1", "192.168.0.2"
OK, REFUSED, DROPPED = 8000, 8001, 8002
CASES = [("ok", OK), ("refused", REFUSED), ("dropped", DROPPED)]
WAIT = 20		# s: net_tos.c's CONNECT_WAIT
CC = ["m68k-linux-gnu-gcc", "-m68030", "-msoft-float", "-O2", "-std=gnu99", "-ffreestanding", "-fno-builtin",
      "-fno-pic", "-fno-pie", "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables",
      "-nostdinc", "-isystem", "/usr/lib/gcc-cross/m68k-linux-gnu/13/include", "-Iatari/include",
      "-Icommon/atari/include", "-Wall", "-Wextra", "-Werror"]
LD = ["m68k-linux-gnu-gcc", "-m68030", "-msoft-float", "-nostdlib", "-static", "-no-pie", "-Wl,--emit-relocs",
      "-Wl,-T,common/atari/link.ld", "-Wl,--build-id=none", "-Wl,-z,noexecstack", "-Wl,--no-warn-rwx-segments",
      "-Wl,--gc-sections"]
RT = ["build/tos/rt/crt0.o", "build/tos/rt/nf.o", "build/tos/rt/libc.o", "build/tos/rt/tos.o"]



def png():
    """a picture of 8 by 8 pixels, one colour"""
    import io
    from PIL import Image
    b = io.BytesIO()
    Image.new("RGB", (8, 8), (0xb8, 0x73, 0x33)).save(b, "PNG")
    return b.getvalue()


PNG = png()
PAGE = ("<!DOCTYPE html><title>mintnet</title><body><p>A page whose first picture never answers."
        "<p><img src='http://%s:%d/never.png' width=40 height=40> <img src='http://%s:%d/comes.png' width=40 "
        "height=40>\n" % (HOST, DROPPED, HOST, OK)).encode()


def ifup():
    os.makedirs(OUT, exist_ok=True)
    subprocess.run(["make", "-s"] + RT, cwd=ROOT, check=True)
    o, elf, prg = (os.path.join(OUT, n) for n in ("ifup.o", "ifup.elf", "IFUP.PRG"))
    subprocess.run(CC + ["-c", "tests/aranym/ifup.c", "-o", o], cwd=ROOT, check=True)
    subprocess.run(LD + ["-o", elf] + RT + [o, "-lgcc"], cwd=ROOT, check=True)
    subprocess.run(["python3", "common/tools/elf2tos.py", elf, prg], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return prg


def net_modules(root, z):
    sysdir = os.path.join(root, "mint", "1-17-0")
    os.makedirs(sysdir, exist_ok=True)
    for name in ("driver/xdd/inet4.xdd", "driver/xif/nfeth.xif"):
        with open(os.path.join(sysdir, os.path.basename(name)), "wb") as f:
            f.write(z.read(name))


def tree_ewget(ewget, ifup_prg, root):
    shutil.rmtree(root, ignore_errors=True)
    z = zipfile.ZipFile(deep.mintfetch.get_zip(None))
    net_modules(root, z)
    cnf = os.path.join(ROOT, "common", "tests", "xaaes", "cnf")
    shutil.copy(os.path.join(cnf, "mint.ini"), os.path.join(root, "mint", "1-17-0", "mint.ini"))
    lines = ["exec u:\\c\\ifup.prg"]
    for name, port in CASES:		# (a .CMD file: EWGET.TTP waits for no key then)
        with open(os.path.join(root, name + ".cmd"), "w", newline="") as f:
            f.write("-l u:\\c\\%s.log http://%s:%d/\r\n" % (name, HOST, port))
        lines.append("exec u:\\c\\ewget.ttp u:\\c\\%s.cmd" % name)
    with open(os.path.join(root, "mint", "1-17-0", "mint.cnf"), "w") as f:
        f.write("# mintnet.py: the card up, then EWGET.TTP's cases\n" + "\n".join(lines) + "\n")
    shutil.copy(ifup_prg, os.path.join(root, "ifup.prg"))
    shutil.copy(ewget, os.path.join(root, "ewget.ttp"))
    return z


def tree_eweb(prg, ifup_prg, root):
    """deep.py's FreeMiNT, fVDI (16 bits) and XaAES starting EWEB.PRG, with
    the network up first and EWeb's home the test page"""
    z = deep.tree(16, prg, root)
    net_modules(root, z)
    cnf = os.path.join(root, "mint", "1-17-0", "mint.cnf")
    with open(cnf) as f:
        text = f.read()
    with open(cnf, "w") as f:
        f.write("exec u:\\c\\ifup.prg\n" + text)
    shutil.copy(ifup_prg, os.path.join(root, "ifup.prg"))
    # EWeb on H:, a folder here (hostfs.xfs): EWEB.LOG is written beside
    # it as it goes, where on C: it stayed in MiNT's cache
    host = os.path.join(os.path.dirname(root), "h")
    shutil.rmtree(host, ignore_errors=True)
    os.makedirs(host)
    for n in ("eweb.prg", "eweb.fnt"):
        shutil.move(os.path.join(root, n), os.path.join(host, n))
    with open(os.path.join(host, "eweb.inf"), "w", newline="") as f:
        f.write("HOME=http://%s:%d/page.html\r\nNATFEATS=1\r\nLOG=1\r\n" % (HOST, OK))
    os.remove(os.path.join(root, "eweb.inf"))
    with open(os.path.join(root, "mint", "1-17-0", "hostfs.xfs"), "wb") as f:
        f.write(z.read("driver/xfs/hostfs.xfs"))
    xa = os.path.join(root, deep.mintfetch.XA, "xaaes.cnf")
    with open(xa) as f:
        text = f.read()
    assert "run c:\\eweb.prg" in text
    with open(xa, "w") as f:
        f.write(text.replace("run c:\\eweb.prg", "run h:\\eweb.prg"))
    return z


class Server(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.endswith(".png"):
            body, kind = PNG, "image/png"
        elif self.path == "/page.html":
            body, kind = PAGE, "text/html"
        else:
            body, kind = b"<!DOCTYPE html><title>mintnet</title><p>MiNTnet answers.\n", "text/html"
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def firewall(add):
    rule = ["INPUT", "-p", "tcp", "--dport", str(DROPPED), "-j", "DROP"]
    subprocess.run(["iptables", "-I" if add else "-D"] + rule, check=add)


def boot(name, root, z, display, until, wait, during=None):
    """ARAnyM on root's files until until(log text) or wait s; returns the
    log and the disk image"""
    work = os.path.join(OUT, name)
    kernel = os.path.join(work, "mintara.prg")
    os.makedirs(work, exist_ok=True)
    with open(kernel, "wb") as f:
        f.write(z.read("auto/mintara.prg"))
    img = os.path.join(work, "disk.img")
    deep.disk(root, img)
    cfg = os.path.join(work, "config")
    deep.config(cfg, kernel, img)
    with open(cfg, "a") as f:
        host = os.path.join(work, "h")
        if os.path.isdir(host):
            f.write("[HOSTFS]\nH = %s\n" % host)
        f.write("[ETH0]\nType = ptp\nTunnel = tap0\nHostIP = %s\nAtariIP = %s\nNetmask = 255.255.255.0\n"
                "MAC = 00:41:45:54:48:30\n" % (HOST, ATARI))
    log = os.path.join(work, "aranym.log")
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy")
    with open(log, "w") as lf:
        p = subprocess.Popen(["aranym", "-c", cfg, "-N"], env=env, stdout=lf, stderr=subprocess.STDOUT)
    t0 = time.time()
    try:
        while time.time() - t0 < wait and p.poll() is None:
            time.sleep(1)
            text = open(log, errors="replace").read()
            if during:
                during(text, env)
            if until(text):
                break
        time.sleep(4)		# (EWEB.LOG's last lines to the disk)
    finally:
        p.kill()
        p.wait()
    return open(log, errors="replace").read(), img, time.time() - t0


def stamps(log):
    """EWEB.LOG's lines as (seconds, text)"""
    out = []
    for ln in (log or "").splitlines():
        m = re.match(r"(\d\d):(\d\d):(\d\d)\.(\d\d) (.*)", ln)
        if m:
            h, mi, s, c = (int(x) for x in m.groups()[:4])
            out.append((h * 3600 + mi * 60 + s + c / 100, m.group(5)))
    return out


def run_ewget(a, ifup_prg, display, fails):
    root = os.path.join(OUT, "ewget", "c")
    z = tree_ewget(a.ewget, ifup_prg, root)
    text, img, took = boot("ewget", root, z, display, lambda t: t.count("done (") >= len(CASES), a.wait)
    print("\n".join(ln for ln in text.splitlines() if ln.startswith(("IFUP", "fetch", "  failed", "  total",
                                                                       "done"))))
    blocks = text.split("command line from ")[1:]
    got = {}
    for b in blocks:
        name = b.split(".cmd")[0].rsplit("\\", 1)[-1]
        got[name] = b
    ok = got.get("ok", "")
    if "200 OK" not in ok or "done (0)" not in ok:
        fails.append("ewget ok: the page didn't come")
    ref = got.get("refused", "")
    m = re.search(r"failed: .*\(after (\d+) ms\)", ref)
    if not m or int(m.group(1)) > 2000:
        fails.append("ewget refused: not failed at once")
    dr = got.get("dropped", "")
    m = re.search(r"failed: (.*) \(after (\d+) ms\)", dr)
    if not m:
        fails.append("ewget dropped: still waiting after %d s (a blocking connect)" % took)
    elif not (WAIT * 1000 - 500 <= int(m.group(2)) <= WAIT * 1000 + 3000) or "no answer" not in m.group(1):
        fails.append("ewget dropped: %s after %s ms, not given up in %d s" % (m.group(1), m.group(2), WAIT))


def run_eweb(a, ifup_prg, display, fails, esc):
    name = "esc" if esc else "wait"
    root = os.path.join(OUT, name, "c")
    z = tree_eweb(a.prg, ifup_prg, root)
    pressed = []

    def during(text, env):
        # Esc 5 s after the page is shown (by then the first picture's
        # connection is being waited for)
        if esc and not pressed and "EWEB shown" in text:
            pressed.append(time.time())
        if esc and len(pressed) == 1 and time.time() - pressed[0] >= 5:
            win = subprocess.run(["xdotool", "search", "--name", "ARAnyM"], env=env, capture_output=True,
                                 text=True).stdout.split()
            if win:
                subprocess.run(["xdotool", "windowfocus", win[0]], env=env, check=False)
                subprocess.run(["xdotool", "key", "Escape"], env=env, check=False)
            pressed.append(time.time())

    path = os.path.join(OUT, name, "h", "eweb.log")

    def until(text):
        # EWEB.LOG as it is written (ARAnyM's clock runs slower than
        # this computer's: its seconds are the ones that count)
        log = open(path, encoding="latin-1").read() if os.path.exists(path) else ""
        return "comes.png" in log if not esc else "!! stopped" in log or "no answer" in log

    text, img, took = boot(name, root, z, display, until, a.wait, during)
    log = open(path, encoding="latin-1").read() if os.path.exists(path) else None
    lines = stamps(log)
    shown = [ln for ln in text.splitlines() if ln.startswith("EWEB shown")]
    print("%s: %s" % (name, shown[0][:80] if shown else "the page not shown"))
    for t, ln in lines:
        if "%d" % DROPPED in ln or "comes.png" in ln or "stopped" in ln or "no answer" in ln:
            print("%s:   %.2f %s" % (name, t, ln))
    if not shown:
        fails.append("%s: the page not shown (see %s)" % (name, os.path.join(OUT, name, "aranym.log")))
        return
    if log is None:
        fails.append("%s: no EWEB.LOG on the disk" % name)
        return
    start = next((t for t, ln in lines if "connecting to %s:%d" % (HOST, DROPPED) in ln), None)
    if start is None:
        fails.append("%s: the dropped picture's connection not started" % name)
        return
    if esc:
        end = next((t for t, ln in lines if t >= start and "!!" in ln and "stopped" in ln), None)
        if end is None:
            fails.append("esc: Esc didn't stop the dropped picture's connection")
        elif end - start >= WAIT - 2:
            fails.append("esc: stopped %.1f s after connecting, not at Esc" % (end - start))
    else:
        end = next((t for t, ln in lines if t >= start and "!!" in ln and "no answer" in ln), None)
        if end is None:
            fails.append("wait: the dropped picture not given up")
        elif not (WAIT - 1 <= end - start <= WAIT + 3):
            fails.append("wait: given up %.1f s after connecting, not in %d s" % (end - start, WAIT))
        if not any("comes.png" in ln and t >= start for t, ln in lines):
            fails.append("wait: the second picture not fetched after the first was given up")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ewget", default=os.path.join(ROOT, "binary", "EWGET.TTP"))
    ap.add_argument("--prg", default=os.path.join(ROOT, "binary", "EWEB.PRG"))
    ap.add_argument("--only", choices=("ewget", "wait", "esc"), action="append")
    ap.add_argument("--wait", type=int, default=180, help="seconds a boot may take at most")
    ap.add_argument("--display", default=":94")
    a = ap.parse_args()
    for tool in ("aranym", "aratapif", "iptables", "mformat", "mcopy", "xdotool", "Xvfb"):
        if shutil.which(tool) is None:
            sys.exit("mintnet.py: %s is needed" % tool)
    ifup_prg = ifup()
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", OK), Server)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    firewall(True)
    xvfb = deep.machines.xvfb(a.display, "1024x768x24")
    fails = []
    try:
        for part in a.only or ("ewget", "wait", "esc"):
            if part == "ewget":
                run_ewget(a, ifup_prg, a.display, fails)
            else:
                run_eweb(a, ifup_prg, a.display, fails, part == "esc")
    finally:
        deep.machines.xvfb_stop(xvfb)
        firewall(False)
        srv.shutdown()
    for f in fails:
        print("FAIL " + f)
    print("mintnet.py: %s" % ("%d failed" % len(fails) if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
