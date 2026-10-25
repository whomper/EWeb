#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make test's check of the pictures' memory (img_trim): a picture a page
shows at two sizes that fit the memory (ewrender -D) each alone but not
both is decoded once at each, not again and again in turn (the trimming
keeps a size a page shows: img_size_shown).

    tests/browse/trim.py EWRENDER EWEB.FNT [EWRENDERST]"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
fails = 0
for ewr in sys.argv[3:4] + sys.argv[1:2]:
    try:
        p = subprocess.run([os.path.abspath(ewr), "-f", os.path.abspath(sys.argv[2]), "-v", "-d", "-D", "140000",
                            os.path.join(HERE, "twosizes.html")], capture_output=True, text=True, timeout=60)
        out = p.stdout + p.stderr
        n = len(re.findall(r"^picture \S+ decoded at", out, re.M))
        ok = p.returncode == 0 and n == 2
    except subprocess.TimeoutExpired:
        out, n, ok = "timed out", -1, False
    if not ok:
        fails += 1
        print("trim: FAIL %s: %d decodes (2 expected)\n%s" % (ewr, n, "\n".join(out.splitlines()[:20])))
if fails:
    sys.exit("trim: %d failed" % fails)
print("trim: a picture at two sizes that don't fit together decoded once at each, in %d edition%s"
      % (len(sys.argv) - 2, "s" if len(sys.argv) > 3 else ""))
