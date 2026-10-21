#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""aerapens behind its socket, as systemd runs it on the Pi
(AERA-Gateway's webgate-pens.socket, Accept=yes): one aerapens a
connection, the connection its stdin and stdout. For make itest and the
Hatari rigs, webgate --pens-socket SOCKET.

    penssock.py SOCKET AERAPENS     prints "ready" once it listens"""
import os
import socket
import subprocess
import sys
import threading


def main():
    path, tool = sys.argv[1], sys.argv[2]
    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    os.chmod(path, 0o600)
    srv.listen(8)
    print("ready", flush=True)

    def serve(c):
        with c:
            p = subprocess.Popen([tool], stdin=c.fileno(), stdout=c.fileno(), stderr=subprocess.DEVNULL)
            try:
                p.wait(timeout=5)       # RuntimeMaxSec=5
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    while True:
        c, _ = srv.accept()
        threading.Thread(target=serve, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
