#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The URL Standard's tests (urltestdata.json, tests/url/fetch.sh) against
src/net/url.c, for the schemes EWeb parses: every test whose answer is an
http or https URL, and every failure where the input is one or is relative
to one (those with a NUL in them aside). Prints how many pass; -v lists the
others.
    tests/url/run.py URLTEST [build/wpt/url/resources/urltestdata.json] [-v]"""
import json
import re
import subprocess
import sys

args = [a for a in sys.argv[1:] if a != "-v"]
verbose = "-v" in sys.argv
data = json.load(open(args[1] if len(args) > 1 else "build/wpt/url/resources/urltestdata.json",
                      encoding="utf-8"))
SCHEME = re.compile(r"^([a-zA-Z][a-zA-Z0-9+.-]*):")


def scheme(s):
    m = SCHEME.match(s.strip("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                             "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f ")
                     .replace("\t", "").replace("\n", "").replace("\r", ""))
    return m.group(1).lower() if m else None


cases = []
nul = 0
for t in data:
    if isinstance(t, str):
        continue
    base = t.get("base")
    if t.get("failure"):
        s = scheme(t["input"])
        if s is not None and s not in ("http", "https"):
            continue
        if s is None and base is not None and scheme(base) not in ("http", "https"):
            continue
        want = "failure"
    else:
        if not t["href"].startswith(("http:", "https:")):
            continue
        want = t["href"]
    if "\x00" in t["input"] or (base and "\x00" in base):
        nul += 1
        continue
    cases.append((t["input"], base, want))


def enc(s):
    return s.encode("utf-8", "surrogatepass").hex() or "00"[:0]


lines = "".join("%s %s\n" % (enc(i) or "", enc(b) if b is not None else "-") for i, b, _ in cases)
out = subprocess.run([args[0]], input=lines, capture_output=True, text=True,
                     encoding="utf-8", errors="replace").stdout.splitlines()
bad = [(i, b, w, g) for (i, b, w), g in zip(cases, out) if g != w]
for i, b, w, g in bad if verbose else []:
    print("input %r base %r\n  want %s\n  got  %s" % (i, b, w, g))
print("url: %d of %d passed (%d failed; %d left out: a NUL in the input, where a C string ends, "
      "and which HTML turns into U+FFFD before it gets here)" % (len(cases) - len(bad), len(cases), len(bad), nul))
sys.exit(1 if len(out) != len(cases) else 0)
