#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tools/safari-cookies.py held to a Cookies.binarycookies made here from
scratch (no real Safari file in the repository): two pages, a domain
cookie and a host's, a subdomain's, a parent domain's, Secure, HttpOnly, an
expired one and another site's; only the sites named come out, in
Netscape's form, with no expired cookie; no site named, or cookies.txt as
the output, is refused.

    python3 tests/cookies/safari.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "safari-cookies.py")
MAC_EPOCH = 978307200
fails = 0


def check(ok, what):
    global fails
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def cookie(domain, name, path, value, expires, flags):
    strings = b""
    offs = []
    base = 56
    for s in (domain, name, path, value):
        offs.append(base + len(strings))
        strings += s.encode() + b"\0"
    size = base + len(strings)
    head = struct.pack("<8i", size, 0, flags, 0, *offs) + b"\0" * 8 + \
        struct.pack("<dd", expires - MAC_EPOCH, time.time() - MAC_EPOCH)
    return head + strings


def page(cookies):
    n = len(cookies)
    at = 4 + 4 + 4 * n + 4
    offs, body = [], b""
    for c in cookies:
        offs.append(at + len(body))
        body += c
    return b"\0\0\x01\0" + struct.pack("<i", n) + struct.pack("<%di" % n, *offs) + b"\0\0\0\0" + body


def binarycookies(pages):
    out = b"cook" + struct.pack(">i", len(pages)) + b"".join(struct.pack(">i", len(p)) for p in pages)
    return out + b"".join(pages) + b"\0\0\0\0" + bytes.fromhex("071720050000004b")


later = int(time.time()) + 86400 * 30
data = binarycookies([
    page([cookie(".atariage.com", "session", "/", "abc123", later, 1 | 4),
          cookie("forums.atariage.com", "theme", "/forums", "dark", later, 0),
          cookie(".atariage.com", "old", "/", "gone", int(time.time()) - 60, 0)]),
    page([cookie(".example.org", "other", "/", "x", later, 0),
          cookie("atariage.com", "host", "/", "h", later, 1)]),
])
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "Cookies.binarycookies")
    with open(src, "wb") as f:
        f.write(data)
    out = os.path.join(d, "safari-cookies.txt")
    r = subprocess.run([sys.executable, TOOL, "--from", src, "-o", out, "forums.atariage.com"],
                       capture_output=True, text=True)
    check(r.returncode == 0, "the tool ran (%s)" % (r.stderr.strip() or r.stdout.strip()))
    lines = [l for l in open(out).read().splitlines() if l and not l.startswith("# ")]
    byname = {l.split("\t")[5]: l.split("\t") for l in lines}
    check(set(byname) == {"session", "theme", "host"}, "forums.atariage.com: its own, its parent domain's "
          "and the host atariage.com's, not the expired one or example.org's (%s)" % sorted(byname))
    s = byname.get("session", [""] * 7)
    check(s[0] == "#HttpOnly_.atariage.com" and s[1] == "TRUE" and s[2] == "/" and s[3] == "TRUE" and
          s[4] == str(later) and s[6] == "abc123", "a Secure, HttpOnly domain cookie as Netscape's line")
    t = byname.get("theme", [""] * 7)
    check(t[0] == "forums.atariage.com" and t[1] == "FALSE" and t[2] == "/forums" and t[3] == "FALSE",
          "a host's cookie: no subdomains, its path")
    check("forums.atariage.com: 3 cookies" in r.stdout, "the count for the site said")
    r = subprocess.run([sys.executable, TOOL, "--from", src, "-o", out], capture_output=True, text=True)
    check(r.returncode != 0 and "name the sites" in r.stderr, "no site named: refused, no 'all'")
    r = subprocess.run([sys.executable, TOOL, "--from", src, "-o", os.path.join(d, "COOKIES.TXT"), "x.org"],
                       capture_output=True, text=True)
    check(r.returncode != 0 and not os.path.exists(os.path.join(d, "COOKIES.TXT")),
          "cookies.txt, EWeb's store's name, refused as the output")
    r = subprocess.run([sys.executable, TOOL, "--from", src, "example.org"], capture_output=True, text=True,
                       cwd=d)
    check(r.returncode == 0 and os.path.exists(os.path.join(d, "safari-cookies.txt")),
          "safari-cookies.txt by default")
print("safari: %s" % ("all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
