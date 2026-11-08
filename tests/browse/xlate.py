#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A page translated Direct (src/browse/xlate.c; DESIGN.md, Translation),
against a stand-in translategate (fakexl.py): the screen's text first, in
pieces of about 2 KB, every paragraph put in, links kept, <pre>, scripts,
translate="no" and text without a letter left as they are; the same
language left alone; a refusal leaves the page as it came.

xlate.py EWRENDER FNT"""
import os
import re
import socket
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ewr, fnt = sys.argv[1], sys.argv[2]
fails = 0


def check(name, ok, detail=""):
    global fails
    print("%s  %s%s" % ("ok  " if ok else "FAIL", name, "" if ok else "\n      " + detail[:1500]))
    if not ok:
        fails += 1


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


work = tempfile.mkdtemp(prefix="xlate")
log = os.path.join(work, "log")
port = free_port()
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "fakexl.py"), str(port), log],
                       stdout=subprocess.PIPE, text=True)
assert srv.stdout.readline().strip() == "ready"


def run(page, *extra):
    if os.path.exists(log):
        os.remove(log)
    out = subprocess.run([ewr, "-f", fnt, "-o", os.path.join(work, "x.png"), "-H", "300", "-d"] + list(extra) +
                         ["--translate", "127.0.0.1:%d:en" % port, page],
                         capture_output=True, text=True, timeout=120)
    reqs = open(log).read().splitlines() if os.path.exists(log) else []
    return out.stdout + out.stderr, reqs


def texts(out):
    return [m.group(1) for m in re.finditer(r'^text .*? link \d+ "(.*)"$', out, re.M)]


try:
    page = os.path.join(HERE, "xlate.html")
    out, reqs = run(page)
    t = texts(out)
    pieces = re.findall(r"^piece \d+: (-?\d)( \(screen\))?", out, re.M)
    check("the screen's piece first, the title in it, then the rest",
          len(pieces) >= 3 and pieces[0][1] and not pieces[-1][1] and len(reqs) == len(pieces) and
          reqs[0].split(" ", 2)[2] == "The test page" and reqs[1].startswith(tuple("%d " % n for n in range(1, 2100))) and
          re.search(r" Paragraph number (\d+) ", reqs[1]) and int(re.search(r" Paragraph number (\d+) ",
                                                                           reqs[1]).group(1)) > 3,
          out + "\n".join(reqs))
    check("pieces of about 2 KB", all(int(r.split()[0]) <= 2100 for r in reqs), "\n".join(reqs))
    check("every paragraph translated, the title too",
          '"<en>THE TEST PAGE</en>"' in out and sum(1 for x in t if x.startswith("<en>PARAGRAPH NUMBER")) == 57 and
          "<en>THE LAST PARAGRAPH.</en>" in t and re.search(r"^translated: (\d+) of \1 ", out, re.M), out)
    check("the link kept, its words translated", re.search(r'#0000ee .* link 1 "<en>A LINK INSIDE IT</en>"', out) and
          ", 1 links," in out, out)
    check("<pre>, translate=no and text without a letter as they are",
          "code stays as it is" in t and "EWeb stays" in t and "12345 67" in t and
          not any("SCRIPT'S" in x for x in t), out)
    check("the page laid out again at the screen and at the end",
          pieces[0][0] == "2" and pieces[-1][0] == "2", out)
    # the page in the target language already: as it was
    same = os.path.join(work, "same.html")
    open(same, "w").write("<!DOCTYPE html><title>t</title><p>SAMETEXT here</p>")
    out, reqs = run(same)
    check("the same language: the page as it was", "SAMETEXT here" in texts(out) and len(reqs) == 1, out)
    # a refusal: the page as it came, the reason said
    ref = os.path.join(work, "refuse.html")
    open(ref, "w").write("<!DOCTYPE html><title>t</title><p>REFUSE this</p>")
    out, reqs = run(ref)
    check("a refusal: the page as it came, why said",
          "REFUSE this" in texts(out) and re.search(r"^piece 1: -1 \(screen\) \S", out, re.M), out)
    # no Gateway answering: said, the page as it came
    out = subprocess.run([ewr, "-f", fnt, "-o", os.path.join(work, "x.png"), "-d", "--translate",
                          "127.0.0.1:%d:en" % free_port(), page], capture_output=True, text=True, timeout=120).stdout
    check("no Gateway: the page as it came", re.search(r"^piece 1: -1 ", out, re.M) and
          "First heading" in texts(out), out)
finally:
    srv.terminate()
    srv.wait(10)
print("xlate: %d failed" % fails)
sys.exit(1 if fails else 0)
