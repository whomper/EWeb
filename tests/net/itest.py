#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make itest: ewget (EWeb's HTTP client, with AddressSanitizer and UBSan)
against the local servers of server.py, over TCP and TLS: pages plain and
packed, kept connections, a resumed TLS session, redirects from HTTP to
HTTPS, chunks, a charset in Content-Type, and the certificates that must be
refused (another host's, one nobody signed).

    tests/net/itest.py EWGET [WORK]"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import server  # noqa: E402

ewget = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "build/itest")
os.makedirs(work, exist_ok=True)
HTTP, HTTPS, BAD = 18080, 18443, 18444
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "server.py"), work, "--http", str(HTTP),
                        "--https", str(HTTPS), "--untrusted", str(BAD)],
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
if srv.stdout.readline().strip() != "ready":
    sys.exit("itest: the servers didn't start")
fails = 0
checks = 0


def run(*args):
    p = subprocess.run([ewget] + list(args), cwd=work, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def check(name, ok, out):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("itest: %s failed\n%s" % (name, out))


def saved(name):
    with open(os.path.join(work, name), "rb") as f:
        return f.read()


try:
    rc, out = run("-n", "2", "-o", "plain.htm", "http://localhost:%d/page" % HTTP)
    check("http", rc == 0 and "200 OK, text/html, charset utf-8" in out and "unpack" in out
          and "the connection was kept open" in out and saved("plain.htm") == server.PAGE, out)

    rc, out = run("-n", "2", "-o", "tls.htm", "https://localhost:%d/page" % HTTPS)
    check("https", rc == 0 and re.search(r"TLS +\d+ ms +TLS 1\.2", out) is not None
          and "the connection was kept open" in out and saved("tls.htm") == server.PAGE, out)

    rc, out = run("-n", "2", "https://localhost:%d/close" % HTTPS)
    second = out.split("fetch 2:")[-1]
    check("TLS resumed", rc == 0 and "resumed" in second and "kept open" not in second, out)

    rc, out = run("-o", "moved.htm", "http://localhost:%d/secure" % HTTP)
    check("http to https", rc == 0 and "after 1 redirect: https://localhost:%d/page" % HTTPS in out
          and saved("moved.htm") == server.PAGE, out)

    rc, out = run("-o", "rel.htm", "http://localhost:%d/redirect" % HTTP)
    check("redirect", rc == 0 and "after 1 redirect" in out and saved("rel.htm") == server.PAGE, out)

    rc, out = run("-o", "big.bin", "https://localhost:%d/chunked" % HTTPS)
    check("chunked", rc == 0 and saved("big.bin") == server.BIG, out)

    rc, out = run("-o", "deflate.htm", "http://localhost:%d/deflate" % HTTP)
    check("deflate", rc == 0 and "deflate" in out and saved("deflate.htm") == server.PAGE, out)

    rc, out = run("-o", "he.htm", "http://localhost:%d/hebrew" % HTTP)
    check("charset", rc == 0 and "charset windows-1255" in out and saved("he.htm") == server.HEBREW, out)

    rc, out = run("http://localhost:%d/nothing" % HTTP)
    check("404", rc == 0 and "404 Not Found" in out, out)

    rc, out = run("https://127.0.0.2:%d/page" % HTTPS)
    check("another host's certificate", rc != 0 and "failed" in out, out)

    rc, out = run("https://localhost:%d/page" % BAD)
    check("a certificate nobody signed", rc != 0 and "failed" in out, out)

    rc, out = run("http://localhost:1/page")
    check("nobody listening", rc != 0 and "localhost" in out, out)
finally:
    srv.stdin.close()
    srv.wait(timeout=10)

print("itest: %s" % ("all %d passed" % checks if not fails else "%d of %d failed" % (fails, checks)))
sys.exit(1 if fails else 0)
