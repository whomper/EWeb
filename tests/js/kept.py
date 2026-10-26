#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Scripts kept compiled (src/js: run_kept, run_code; docs/DESIGN.md,
Scripts kept compiled), with ewrender -J and a disk cache (-C):

- a script over 4 KB (tests/js/kept/lib.js) compiled and kept on the
  first visit, read from the cache on the next, the same either way;
- Esc while it is compiled (-I script:N) stops the page, keeps nothing;
- a short script isn't kept;
- compiled code (aerajsc's, "EWQJS TAG" and the bytecode) from anywhere
  but the Pi (a file here; a server in Direct mode) isn't run as code:
  only webgate's answer is (make itest, through webgate).

    tests/js/kept.py EWRENDER EWEB.FNT AERAJSC"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
failed = 0


def check(what, ok, out=""):
    global failed
    if not ok:
        failed += 1
        print("kept: FAIL %s\n%s" % (what, out[-2000:]))


def main():
    ewr, fnt, jsc = sys.argv[1], sys.argv[2], sys.argv[3]
    work = tempfile.mkdtemp(prefix="kept")
    try:
        def run(page, *more):
            r = subprocess.run([ewr, "-J", "-d", "-f", fnt] + list(more) + [page], capture_output=True, text=True,
                               timeout=120)
            return r.stdout + r.stderr

        page = os.path.join(HERE, "kept.html")
        cache = os.path.join(work, "cache")
        first = run(page, "-C", cache)
        said = re.findall(r"^console: kept .*$", first, re.M)
        check("the first visit compiles and keeps it", "js code: compiled and kept" in first and len(said) == 1 and
              "re1=quick,brown,jumps" in said[0] and "js error" not in first, first)
        again = run(page, "-C", cache)
        check("the next reads it, and it does the same", "js code: kept from before" in again and
              "compiled and kept" not in again and re.findall(r"^console: kept .*$", again, re.M) == said, again)
        bare = run(page)
        check("without a cache, the same", re.findall(r"^console: kept .*$", bare, re.M) == said and
              "kept from before" not in bare, bare)

        cache2 = os.path.join(work, "cache2")
        stopped = run(page, "-C", cache2, "-I", "script:2")
        check("Esc while it is compiled stops it", "js stopped: by Esc" in stopped and
              "console: kept" not in stopped and "compiled and kept" not in stopped, stopped)
        after = run(page, "-C", cache2)
        check("and nothing of it was kept", "js code: compiled and kept" in after, after)

        small = run(os.path.join(HERE, "basics.html"), "-C", os.path.join(work, "cache3"))
        check("a short script isn't kept", "js code" not in small, small)

        # compiled code, but not from the Pi: read as a script's source
        # (which it isn't), never as code
        src = open(os.path.join(HERE, "kept", "lib.js"), "rb").read()
        r = subprocess.run([jsc], input=b"EWJQ\1" + len(src).to_bytes(4, "big") + src, capture_output=True)
        line, _, code = r.stdout.partition(b"\n")
        check("aerajsc compiles it", line.startswith(b"aerajsc ") and code.startswith(b"EWQJS "), repr(r.stdout[:200]))
        pi = os.path.join(work, "pi")
        os.mkdir(pi)
        with open(os.path.join(pi, "lib.js"), "wb") as f:
            f.write(code)
        with open(os.path.join(pi, "kept.html"), "w") as f:
            f.write(open(page).read().replace("kept/lib.js", "lib.js"))
        notpi = run(os.path.join(pi, "kept.html"), "-C", os.path.join(work, "cache4"))
        check("compiled code from elsewhere isn't run as code", "js code: compiled by the Pi" not in notpi and
              "console: kept" not in notpi and "js error: SyntaxError" in notpi, notpi)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("kept: %d failed" % failed)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
