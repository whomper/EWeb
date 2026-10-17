#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The other end of FAKESTNG.PRG: Hatari's emulated serial port on one
side, real TCP connections on the other (see tools/fakesting/fakesting.c
for the frames).

    serial_bridge.py FROM_ATARI TO_ATARI [--rate BYTES_PER_SECOND]

FROM_ATARI / TO_ATARI are the files or FIFOs given to Hatari as
--rs232-out / --rs232-in. Data to the Atari is paced, as a serial line
would be, so the emulated port's buffer doesn't overflow.
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time


def log(*a):
    print("bridge:", *a, file=sys.stderr, flush=True)


class Bridge:
    def __init__(self, src, dst, rate):
        self.rate = rate
        self.lock = threading.Lock()
        self.socks = {}
        # read-write opens never block on a FIFO, whoever opens first
        self.out = os.fdopen(os.open(dst, os.O_RDWR), "wb", buffering=0)
        self.inp = os.fdopen(os.open(src, os.O_RDWR), "rb", buffering=0)

    def send(self, data):
        with self.lock:
            for i in range(0, len(data), 256):
                self.out.write(data[i:i + 256])
                time.sleep(len(data[i:i + 256]) / self.rate)

    def read(self, n):
        buf = b""
        while len(buf) < n:
            c = self.inp.read(n - len(buf))
            if not c:
                raise EOFError
            buf += c
        return buf

    def reader(self, cn, s):
        while True:
            try:
                data = s.recv(1024)
            except OSError:
                data = b""
            if not data:
                break
            self.send(b"D" + bytes([cn]) + struct.pack(">H", len(data)) + data)
        if self.socks.get(cn) is s:
            log("conn %d closed by the server" % cn)
            self.send(b"E" + bytes([cn]))

    def run(self):
        while True:
            t = self.read(1)
            cn = self.read(1)[0]
            if t == b"O":
                ip = socket.inet_ntoa(self.read(4))
                port = struct.unpack(">H", self.read(2))[0]
                log("conn %d: open %s:%d" % (cn, ip, port))
                try:
                    s = socket.create_connection((ip, port), timeout=10)
                    s.settimeout(None)
                    self.socks[cn] = s
                    self.send(b"S" + bytes([cn, 1]))
                    threading.Thread(target=self.reader, args=(cn, s), daemon=True).start()
                except OSError as e:
                    log("conn %d: %s" % (cn, e))
                    self.send(b"S" + bytes([cn, 0]))
            elif t == b"D":
                n = struct.unpack(">H", self.read(2))[0]
                data = self.read(n)
                s = self.socks.get(cn)
                if s:
                    try:
                        s.sendall(data)
                    except OSError:
                        pass
            elif t == b"C":
                s = self.socks.pop(cn, None)
                log("conn %d: closed by the Atari" % cn)
                if s:
                    s.close()
            else:
                log("unknown frame %r" % t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--rate", type=int, default=20000)
    a = ap.parse_args()
    for p in (a.src, a.dst):
        if not os.path.exists(p):
            os.mkfifo(p)
    try:
        Bridge(a.src, a.dst, a.rate).run()
    except EOFError:
        log("Hatari went away")


if __name__ == "__main__":
    main()
