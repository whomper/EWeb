#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWGET.TTP on an emulated Falcon: Hatari, EmuTOS, a 68030 at 32 MHz with
TT-RAM and the DSP emulated, against tests/net/server.py on this computer.
Hatari has no network card, so FAKESTNG.PRG in C:\\AUTO carries the
Atari's TCP connections over the emulated serial port to serial_bridge.py.
Each run fetches over TLS and plain HTTP: the page must arrive whole, the
handshake must be TLS 1.2 with the server's certificate checked against the
test authority, and the second fetch must use the kept connection.

    tests/hatari/ewget_test.py ETOS1024K.IMG [WORK]"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "net"))
import server  # noqa: E402

HTTP, HTTPS = 18180, 18543
RUNS = [
    ("https", "-n 2 -o PAGE.HTM https://127.0.0.1:%d/page" % HTTPS),
    ("http to https", "-o MOVED.HTM http://127.0.0.1:%d/secure" % HTTP),
    # the release's certificates (Mozilla's list, and ROOTS.DAT made from
    # it on this computer): read from the cache, and the test authority
    # isn't among them
    ("Mozilla's list", "-l NET.LOG https://127.0.0.1:%d/page" % HTTPS),
]


def run_atari(tos, hd, cmdline, log):
    with open(os.path.join(hd, "EWGET.CMD"), "w") as f:
        f.write(cmdline)
    fifo_out, fifo_in = os.path.join(hd, "..", "rs_out"), os.path.join(hd, "..", "rs_in")
    for p in (fifo_out, fifo_in):
        if os.path.exists(p):
            os.remove(p)
        os.mkfifo(p)
    bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "serial_bridge.py"), fifo_out, fifo_in],
                              stderr=open(log + ".bridge", "w"))
    cmd = ["hatari", "--machine", "falcon", "--tos", tos, "--harddrive", hd,
           "--auto", "C:\\EWGET.TTP", "--fast-boot", "yes", "--natfeats", "yes",
           "--cpuclock", "32", "--fpu", "68882", "--dsp", "emu", "--addr24", "off",
           "--memsize", "14", "--ttram", "64", "--cpu-exact", "yes",
           "--rs232-out", fifo_out, "--rs232-in", fifo_in,
           "--fast-forward", "yes", "--sound", "off", "--confirm-quit", "no",
           "--statusbar", "no", "--monitor", "vga", "--log-level", "fatal"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(cmd, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    end = time.time() + 900
    try:
        while time.time() < end and h.poll() is None:
            time.sleep(1)
            if "done (" in open(log, errors="replace").read():
                break
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
        bridge.terminate()
    return open(log, errors="replace").read()


def main():
    tos = os.path.abspath(sys.argv[1])
    work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "hatari-net"))
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(os.path.join(hd, "AUTO"))
    srv_dir = os.path.join(work, "srv")
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "net", "server.py"), srv_dir,
                            "--http", str(HTTP), "--https", str(HTTPS), "--untrusted", str(HTTPS + 1)],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "ready":
        sys.exit("ewget_test: the servers didn't start")
    shutil.copy(os.path.join(ROOT, "binary", "EWGET.TTP"), hd)
    shutil.copy(os.path.join(srv_dir, "CACERT.PEM"), hd)	# the test authority
    shutil.copy(os.path.join(ROOT, "build", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
    fails = 0
    try:
        for name, cmdline in RUNS:
            if name == "Mozilla's list":
                for f in ("CACERT.PEM", "ROOTS.DAT"):
                    shutil.copy(os.path.join(ROOT, "binary", f), hd)
            text = run_atari(tos, hd, cmdline, os.path.join(work, "hatari-%s.log" % name.replace(" ", "-")))
            print("---- %s: EWGET.TTP %s" % (name, cmdline))
            print("\n".join(l for l in text.splitlines() if l.strip()))
            ok = "done (0)" in text and "200 OK" in text
            if name == "Mozilla's list":
                net = open(os.path.join(hd, "NET.LOG"), errors="replace").read()
                m = re.search(r"(\d+) root certificates read in (\d+) ms", net)
                ok = "done (1)" in text and "isn't signed by any authority" in text and m is not None \
                     and int(m.group(1)) > 100
                if m:
                    print("---- %s root certificates read from ROOTS.DAT in %s ms" % m.groups())
            elif name == "https":
                ok = ok and "TLS 1.2" in text and "the connection was kept open" in text \
                     and "RSA on the DSP" in text and open(os.path.join(hd, "PAGE.HTM"), "rb").read() == server.PAGE
            else:
                ok = ok and "after 1 redirect: https://127.0.0.1:%d/page" % HTTPS in text \
                     and open(os.path.join(hd, "MOVED.HTM"), "rb").read() == server.PAGE
            print("---- %s: %s" % (name, "ok" if ok else "FAILED"))
            fails += not ok
    finally:
        srv.stdin.close()
        srv.wait(timeout=10)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
