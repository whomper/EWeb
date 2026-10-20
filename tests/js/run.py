#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EWeb's scripts (src/js) on test pages: each tests/js/*.html runs
through ewrender -J -d, with the arguments its <!-- args: ... --> line
gives (-K ID clicks, -T MS timers). Every <!-- expect: TEXT --> must be
in the output; a FAIL line (the pages' own checks) or a "js error"
(unless <!-- errors --> says the page means one) fails it.

  tests/js/run.py EWRENDER EWEB.FNT
"""
import os
import re
import subprocess
import sys


def main():
    ewr, fnt = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    pages = sorted(f for f in os.listdir(here) if f.endswith('.html'))
    failed = checks = 0
    for name in pages:
        path = os.path.join(here, name)
        src = open(path, encoding='utf-8').read()
        args = []
        for m in re.finditer(r'<!-- args: (.*?) -->', src):
            args += m.group(1).split()
        expects = re.findall(r'<!-- expect: (.*?) -->', src)
        absents = re.findall(r'<!-- absent: (.*?) -->', src)
        errors_ok = '<!-- errors -->' in src
        r = subprocess.run([ewr, '-J', '-d', '-f', fnt] + args + [path], capture_output=True, text=True,
                           timeout=120)
        out = r.stdout + r.stderr
        bad = []
        if r.returncode != 0:
            bad.append('exit %d' % r.returncode)
        for line in out.splitlines():
            if 'FAIL' in line or (not errors_ok and line.startswith('js error')):
                bad.append(line)
            if 'ERROR: AddressSanitizer' in line or 'runtime error:' in line:
                bad.append(line)
        for e in expects:
            checks += 1
            if e not in out:
                bad.append('missing: ' + e)
        for a in absents:
            checks += 1
            if a in out:
                bad.append('there: ' + a)
        if bad:
            failed += 1
            print('FAIL  %s' % name)
            for b in bad[:12]:
                print('      %s' % b)
    print('js: %d pages, %d checks, %d failed' % (len(pages), checks, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
