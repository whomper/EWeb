#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tools/safari-cookies.html in headless Chromium (Playwright), held to the
same made-up Cookies.binarycookies as tests/cookies/safari.py: the sites
listed (subdomains folded, the expired cookie left out), Select all and a
search's "Select these N", and the saved safari-cookies.txt's cookie lines
the very ones tools/safari-cookies.py writes for the same site.

    python3 tests/cookies/page.py
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "..", "..", "tools", "safari-cookies.html")
TOOL = os.path.join(HERE, "..", "..", "tools", "safari-cookies.py")
MAC_EPOCH = 978307200
fails = 0


def check(ok, what):
    global fails
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def cookie(domain, name, path, value, expires, flags):
    strings, offs, base = b"", [], 56
    for s in (domain, name, path, value):
        offs.append(base + len(strings))
        strings += s.encode() + b"\0"
    head = struct.pack("<8i", base + len(strings), 0, flags, 0, *offs) + b"\0" * 8 + \
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


def lines(text):
    return sorted(l for l in text.splitlines() if l and not l.startswith("# "))


later = int(time.time()) + 86400 * 30
data = binarycookies([
    page([cookie(".atariage.com", "session", "/", "abc123", later, 1 | 4),
          cookie("forums.atariage.com", "theme", "/forums", "dark", later, 0),
          cookie(".atariage.com", "old", "/", "gone", int(time.time()) - 60, 0)]),
    page([cookie(".example.org", "other", "/", "x", later, 0),
          cookie("atariage.com", "host", "/", "h", later, 1),
          cookie(".shop.co.uk", "cart", "/", "3", later, 0)]),
])
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "Cookies.binarycookies")
    with open(src, "wb") as f:
        f.write(data)
    want = os.path.join(d, "want.txt")
    r = subprocess.run([sys.executable, TOOL, "--from", src, "-o", want, "atariage.com"], capture_output=True, text=True)
    check(r.returncode == 0, "safari-cookies.py for atariage.com (%s)" % (r.stderr.strip() or "ran"))
    got = os.path.join(d, "got.txt")
    r = subprocess.run(["node", os.path.join(HERE, "page.js"), os.path.abspath(PAGE), src, got],
                       capture_output=True, text=True)
    if r.returncode != 0 and "Cannot find module 'playwright'" in r.stderr:
        print("page: no Playwright for Node here: skipped")
        sys.exit(0)
    check(r.returncode == 0, "the page driven in Chromium (%s)" % r.stderr.strip()[-300:])
    s = json.loads(r.stdout or "{}")
    check(sorted(s.get("names", [])) == ["atariage.com", "example.org", "shop.co.uk"],
          "the sites, subdomains folded, co.uk kept (%s)" % s.get("names"))
    check(s.get("fname") == "5 cookies for 3 sites", "the expired cookie left out (%s)" % s.get("fname"))
    check(s.get("selall0") == "Select all" and s.get("unsel0"), "Select all, nothing to unselect yet")
    check(s.get("sumAll") == "Ticked: 3 sites, 5 cookies" and s.get("selallOff"),
          "Select all ticks every site (%s)" % s.get("sumAll"))
    check(s.get("sumNone") == "Ticked: none", "Unselect all clears")
    check(s.get("selallQ") == "Select these 1", "a search: Select these N (%s)" % s.get("selallQ"))
    check(s.get("sumQ") == "Ticked: 1 site, 3 cookies", "the site shown ticked (%s)" % s.get("sumQ"))
    check(s.get("file") == "safari-cookies.txt", "saved as safari-cookies.txt")
    check(os.path.exists(got) and lines(open(got).read()) == lines(open(want).read()),
          "the page's lines are safari-cookies.py's for the same site")
    check(s.get("valueShown") is False, "no cookie's value on the page")
    check(not s.get("errors"), "no script errors (%s)" % s.get("errors"))
print("page: %s" % ("all passed" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
