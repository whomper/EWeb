#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A stand-in for the Gateway's translategate (AERA-Atari's
standards/TRANSLATION-SERVICE.md) for tests/browse/xlate.py: POST
/translate?to=CODE answers each paragraph (blank lines between them) as
"<CODE>PARAGRAPH IN CAPITALS</CODE>" (into he: Hebrew words round it and
2026), from "de"; a body with "SAMETEXT"
comes back X-Translate-Same: 1, one with "REFUSE" 503. Each request's size
and first paragraph is written to LOG, a line each.

fakexl.py PORT LOG   (prints "ready" when it listens)
fakexl.py /PATH LOG  the same on a Unix socket, as translategate's local
                     one that webgate asks (tests/browse/xlgate.py)"""
import http.server
import sys
import urllib.parse

LOG = sys.argv[2]


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        # the installed languages (Settings > Translate into), and all of
        # them with their sizes (the Languages dialog)
        u = urllib.parse.urlsplit(self.path)
        if u.path != "/languages":
            self.send_error(404)
            return
        if "all=1" in u.query:
            out = ("cap_mb 600\nused_mb 229\nen\tEnglish\t0\tinstalled\nhe\tHebrew\t229\tinstalled\n"
                   "de\tGerman\t286\tavailable\nfr\tFrench\t125\tavailable\n")
        else:
            out = "en\tEnglish\nhe\tHebrew\n"
        with open(LOG, "a") as f:
            f.write("GET %s\n" % self.path)
        b = out.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query)
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n).decode("utf-8")
        to = (q.get("to") or ["?"])[0]
        paras = body.split("\n\n")
        with open(LOG, "a") as f:
            f.write("%d %d %s\n" % (n, len(paras), paras[0][:60]))
        if u.path != "/translate":
            self.send_error(404)
            return
        if "REFUSE" in body:
            out = b"translategate: busy\n"
            self.send_response(503)
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)
            return
        self.send_response(200)
        self.send_header("X-Translate-From", "de")
        if "SAMETEXT" in body:
            self.send_header("X-Translate-Same", "1")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if to == "he":
            # Hebrew around the words in capitals and a number: a right to
            # left paragraph with a Latin run and digits in it
            out = "\n\n".join("\u05e9\u05dc\u05d5\u05dd %s 2026 \u05e2\u05d5\u05dc\u05dd" % p.upper()
                                for p in paras).encode("utf-8")
        else:
            out = "\n\n".join("<%s>%s</%s>" % (to, p.upper(), to) for p in paras).encode("utf-8")
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)


if sys.argv[1].startswith("/"):
    import socketserver

    class U(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
        daemon_threads = True

    class HU(H):
        def address_string(self):
            return "local"

    s = U(sys.argv[1], HU)
else:
    s = http.server.HTTPServer(("127.0.0.1", int(sys.argv[1])), H)
print("ready", flush=True)
s.serve_forever()
