#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make sites: every site Erez sent to be fixed (tests/sites/sites.txt;
Erez, 21 Nov 2026: "all web pages I am sending you to fix are also added to
a test scenario ... that will verify we are not screwing HTMLs"), laid out
from its recording (tests/sites/replay.py, in the private EWeb-sites
repository: SITES=, ../eweb-sites/sites by default) and held to the display list
kept there with it (NAME/list.txt), then drawn by the AddressSanitizer
build, which stops at any fault. Nothing is fetched: what EWeb asks for and
wasn't recorded is answered 404 and named.

    tests/sites/run.py EWRENDER ASAN_EWRENDER EWEB.FNT SITES [--update] [NAME...]
    tests/sites/run.py EWRENDER ASAN_EWRENDER EWEB.FNT SITES --record NAME...

--record fetches NAME's address afresh through the recorder (the network
needed; its recording replaced) and keeps its list from a replay; --update
keeps the lists as they are now, after looking at the pages (ewrender
without -d, the same proxy: run.py prints the command). A site's line in
sites.txt may end in "-- ARGS" for ewrender (-w 320, -p 16, -J)."""
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CADIR = os.path.join(ROOT, "build", "sites", "ca")


def sites():
    out = []
    for line in open(os.path.join(HERE, "sites.txt"), encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        line, _, more = line.partition(" -- ")
        name, url = line.split()[:2]
        out.append((name, url, shlex.split(more)))
    return out


class Proxy:
    def __init__(self, mode, d):
        os.makedirs(d, exist_ok=True)
        self.portfile = os.path.join(tempfile.mkdtemp(prefix="sites"), "port")
        env = dict(os.environ)
        if os.path.exists("/root/.ccr/ca-bundle.crt"):
            env.setdefault("SSL_CERT_FILE", "/root/.ccr/ca-bundle.crt")
        self.p = subprocess.Popen([sys.executable, os.path.join(HERE, "replay.py"), mode, d, CADIR, self.portfile],
                                  env=env)
        for _ in range(200):
            if os.path.exists(self.portfile):
                break
            time.sleep(0.05)
        self.port = int(open(self.portfile).read())

    def stop(self):
        self.p.terminate()
        self.p.wait()
        shutil.rmtree(os.path.dirname(self.portfile), ignore_errors=True)


def ewrender(binary, font, url, more, port, extra):
    env = dict(os.environ, EW_PROXY="127.0.0.1:%d" % port)
    cmd = [os.path.abspath(binary), "-f", os.path.abspath(font), "-c", os.path.join(CADIR, "ca.pem")] + \
        more + extra + [url]
    return subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=600, errors="replace")


def main():
    a = sys.argv[1:]
    record = "--record" in a
    update = "--update" in a
    a = [x for x in a if x not in ("--record", "--update")]
    ew, asan, font, store = a[:4]
    only = set(a[4:])
    if not os.path.isdir(store):
        print("sites: no recordings at %s (the private EWeb-sites repository; SITES=): skipped" % store)
        return 0
    bad = 0
    for name, url, more in sites():
        if only and name not in only:
            continue
        d = os.path.join(store, name)
        if record:
            shutil.rmtree(os.path.join(d, "rec"), ignore_errors=True)
            if os.path.exists(os.path.join(d, "missing.txt")):
                os.remove(os.path.join(d, "missing.txt"))
            px = Proxy("record", d)
            try:
                r = ewrender(ew, font, url, more, px.port, ["-d"])
            finally:
                px.stop()
            with open(os.path.join(d, "url"), "w") as f:
                f.write(url + "\n")
        if not os.path.isdir(os.path.join(d, "rec")):
            print("FAIL  sites: %s isn't recorded (run.py ... --record %s)" % (name, name))
            bad = 1
            continue
        if os.path.exists(os.path.join(d, "missing.txt")):
            os.remove(os.path.join(d, "missing.txt"))
        px = Proxy("replay", d)
        try:
            r = ewrender(ew, font, url, more, px.port, ["-d"])
            png = os.path.join(tempfile.gettempdir(), "site_%s.png" % name)
            z = ewrender(asan, font, url, more, px.port, ["-o", png])
        finally:
            px.stop()
        lst = os.path.join(d, "list.txt")
        miss = open(os.path.join(d, "missing.txt")).read().split("\n") if os.path.exists(
            os.path.join(d, "missing.txt")) else []
        miss = [m for m in miss if m]
        if r.returncode != 0:
            print("FAIL  sites: %s: ewrender stopped (%d)\n%s" % (name, r.returncode, r.stderr[-800:]))
            bad = 1
            continue
        if z.returncode != 0:
            print("FAIL  sites: %s: drawn under AddressSanitizer, stopped (%d)\n%s" % (name, z.returncode,
                                                                                      z.stderr[-1500:]))
            bad = 1
        if record or update or not os.path.exists(lst):
            with open(lst, "w") as f:
                f.write(r.stdout)
            print("kept  sites: %s: %s" % (name, r.stdout.split("\n", 1)[0]))
            continue
        want = open(lst).read()
        if r.stdout == want:
            print("ok    sites: %s: as kept%s" % (name, " (%d not recorded)" % len(miss) if miss else ""))
            continue
        bad = 1
        old, new = want.split("\n"), r.stdout.split("\n")
        n = next((i for i in range(min(len(old), len(new))) if old[i] != new[i]), min(len(old), len(new)))
        print("FAIL  sites: %s: its list changed at line %d of %d%s\n  was %s\n  now %s" % (
            name, n + 1, len(old), "; asked for but not recorded: %s" % ", ".join(miss[:3]) if miss else "",
            old[n][:160] if n < len(old) else "(the end)", new[n][:160] if n < len(new) else "(the end)"))
    return bad


if __name__ == "__main__":
    sys.exit(main())
