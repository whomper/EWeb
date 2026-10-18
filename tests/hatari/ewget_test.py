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

HTTP, HTTPS, EC = 18180, 18543, 18545
RUNS = [
    ("https", "-n 2 -o PAGE.HTM https://127.0.0.1:%d/page" % HTTPS),
    ("http to https", "-o MOVED.HTM http://127.0.0.1:%d/secure" % HTTP),
    # an ECDSA certificate (P-256, signed with P-384): the elliptic curves
    # on the DSP, ECDSA offered at once
    ("ECDSA", "-l EC.LOG -o EC.HTM https://127.0.0.1:%d/page" % EC),
    # again: the certificate's signature remembered in SIGS.DAT, so the
    # DSP does only the key exchange and its signature
    ("ECDSA again", "-l EC2.LOG https://127.0.0.1:%d/page" % EC),
    # the release's certificates (Mozilla's list, and ROOTS.DAT made from
    # it on this computer): read from the cache, and the test authority
    # isn't among them
    ("Mozilla's list", "-l NET.LOG https://127.0.0.1:%d/page" % HTTPS),
    # again: ROOTS.DAT now has CACERT.PEM's date, so the 186 KB file
    # isn't read for its checksum
    ("Mozilla's list again", "-l NET2.LOG https://127.0.0.1:%d/page" % HTTPS),
]


def run_atari(tos, hd, cmdline, log, dropped=None):
    """dropped: the line goes in that .CMD file, and EWGET.CMD names it, as
    the desktop does when the file is dropped on EWGET.TTP"""
    if dropped:
        with open(os.path.join(hd, dropped), "w") as f:
            f.write(cmdline + "\r\n")
        cmdline = "C:\\" + dropped
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
                            "--http", str(HTTP), "--https", str(HTTPS), "--untrusted", str(HTTPS + 1),
                            "--ecdsa", str(EC)],
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
            text = run_atari(tos, hd, cmdline, os.path.join(work, "hatari-%s.log" % name.replace(" ", "-")),
                             "MOVED.CMD" if name == "http to https" else None)
            print("---- %s: EWGET.TTP %s" % (name, cmdline))
            print("\n".join(l for l in text.splitlines() if l.strip()))
            ok = "done (0)" in text and "200 OK" in text
            # files to and from TT-RAM through EWGET's own buffer in ST-RAM (DMA reaches ST-RAM only)
            files_ok = "files: to and from TT-RAM through EWGET's 8 KB in ST-RAM" in text
            if name.startswith("Mozilla's list"):
                net = open(os.path.join(hd, "NET.LOG" if name == "Mozilla's list" else "NET2.LOG"),
                           errors="replace").read()
                m = re.search(r"(\d+) root certificates read in (\d+) ms", net)
                roots = open(os.path.join(hd, "ROOTS.DAT"), "rb").read(22)
                ok = "done (1)" in text and "isn't signed by any authority" in text and m is not None \
                     and int(m.group(1)) > 100 and roots[:10] == b"MAILROOTS2" and roots[18:22] != bytes(4)
                if m:
                    print("---- %s root certificates read from ROOTS.DAT in %s ms" % m.groups())
            elif name == "ECDSA":
                net = open(os.path.join(hd, "EC.LOG"), errors="replace").read()
                ok = ok and "ECDHE-ECDSA" in text and "elliptic curves on the DSP" in text \
                     and "(3 tries)" not in text and "passed its elliptic-curve self-test" in net \
                     and "ECDSA P-384 on the DSP" in net and "ECDSA P-256 on the DSP" in net \
                     and "the server's first answer" in net and "the handshake: " in net \
                     and "0 of 1 ECDSA signatures checked before" in net \
                     and "the answer's first bytes came" in net and "bytes decrypted" in net \
                     and open(os.path.join(hd, "EC.HTM"), "rb").read() == server.PAGE
                for l in net.splitlines():
                    if "TLS --" in l or "TLS .." in l:
                        print("---- " + l.strip())
            elif name == "ECDSA again":
                net = open(os.path.join(hd, "EC2.LOG"), errors="replace").read()
                ok = ok and "1 of 1 ECDSA signatures checked before" in net and "ECDSA P-384" not in net \
                     and "ECDSA P-256 on the DSP" in net
                for l in net.splitlines():
                    if "TLS --" in l:
                        print("---- " + l.strip())
            elif name == "https":
                ok = ok and "TLS 1.2" in text and "the connection was kept open" in text \
                     and "RSA on the DSP" in text and open(os.path.join(hd, "PAGE.HTM"), "rb").read() == server.PAGE
            else:
                ok = ok and "after 1 redirect: https://127.0.0.1:%d/page" % HTTPS in text \
                     and "command line from C:\\MOVED.CMD" in text \
                     and open(os.path.join(hd, "MOVED.HTM"), "rb").read() == server.PAGE
            ok = ok and files_ok
            print("---- %s: %s" % (name, "ok" if ok else "FAILED"))
            fails += not ok
    finally:
        srv.stdin.close()
        srv.wait(timeout=10)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
