#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Web servers for EWeb's HTTP tests (itest.py, and the Hatari test): the
same pages over plain HTTP and HTTPS, the HTTPS one with a certificate for
"localhost" and "127.0.0.1" (the emulated Atari can't look names up) from a
test authority made here (WORK/CACERT.PEM), the same server on 127.0.0.2
(not a name in its certificate), and a second HTTPS server whose certificate
nobody signed; and one with only an ECDSA certificate (a P-256 key signed
by a P-384 test authority, as Wikipedia's are), for the elliptic curves.

    server.py WORK [--http PORT] [--https PORT] [--untrusted PORT] [--ecdsa PORT]

Pages:
  /page          a page of text (gzip when it is asked for), keep-alive
  /close         the same, then the connection closes (for TLS resumption)
  /chunked       2 MB in chunks of 1 to 64 KB
  /hebrew        a page in windows-1255, said in Content-Type
  /redirect      302 to /page; /secure 301 to the HTTPS server's /page (on
                 the host the request named)
  /deflate       the page, deflated
  /site/         a page for the browser (make itest's ewrender checks): a
                 stylesheet linked (/site/style.css), links to /site/two.html
                 and an anchor on it
It prints "ready" when the servers listen."""
import argparse
import gzip
import http.server
import os
import random
import socketserver
import ssl
import subprocess
import sys
import threading
import zlib

rnd = random.Random(7)
WORDS = ["Falcon", "Atari", "EWeb", "page", "TT-RAM", "68030", "DSP", "שלום"]
PAGE = ("<!DOCTYPE html><html><head><meta charset=utf-8><title>EWeb test</title></head><body>"
        + " ".join(rnd.choice(WORDS) for _ in range(6000)) + "</body></html>\n").encode("utf-8")
HEBREW = "<html><body>שלום עולם</body></html>\n".encode("cp1255")
SITE = (b"<!DOCTYPE html><html><head><title>EWeb site</title><link rel=stylesheet href=style.css>"
        b"</head><body><h1>Welcome</h1><p>See <a href=two.html>the second page</a> or "
        b"<a href=two.html#end>its end</a>.</p></body></html>\n")
SITE_CSS = b"body { background: #ffeedd } h1 { color: #aa0000 }\n"
SITE_TWO = (b"<!DOCTYPE html><html><head><title>Two</title></head><body><p>Back to "
            b"<a href=/site/>the first</a>.</p><div style='height: 2000px'></div><p id=end>The end</p>"
            b"</body></html>\n")
BIG = bytes(rnd.randrange(256) for _ in range(2 << 20))


def make_certs(work):
    """A test authority and a certificate for localhost signed by it, and a
    self-signed one, RSA; an ECDSA authority and certificate. CACERT.PEM
    holds both authorities."""
    def run(*a):
        subprocess.run(["openssl"] + list(a), cwd=work, check=True, capture_output=True)
    if os.path.exists(os.path.join(work, "ec.pem")):
        return
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "CACERT.PEM",
        "-days", "3650", "-subj", "/CN=EWeb test authority",
        "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "host.key", "-out", "host.csr",
        "-subj", "/CN=localhost")
    with open(os.path.join(work, "ext.cnf"), "w") as f:
        f.write("subjectAltName=DNS:localhost,DNS:127.0.0.1\nbasicConstraints=CA:FALSE\n"
                "keyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n")
    run("x509", "-req", "-in", "host.csr", "-CA", "CACERT.PEM", "-CAkey", "ca.key", "-CAcreateserial",
        "-out", "host.pem", "-days", "825", "-extfile", "ext.cnf")
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "self.key", "-out", "self.pem",
        "-days", "825", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost")
    run("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-384", "-nodes", "-keyout", "ecca.key",
        "-out", "ecca.pem", "-days", "3650", "-sha384", "-subj", "/CN=EWeb test ECDSA authority",
        "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    run("req", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", "ec.key",
        "-out", "ec.csr", "-subj", "/CN=localhost")
    run("x509", "-req", "-in", "ec.csr", "-CA", "ecca.pem", "-CAkey", "ecca.key", "-CAcreateserial",
        "-out", "ec.pem", "-days", "825", "-sha384", "-extfile", "ext.cnf")
    with open(os.path.join(work, "CACERT.PEM"), "a") as f:
        f.write(open(os.path.join(work, "ecca.pem")).read())


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    secure_port = 0

    def log_message(self, fmt, *args):
        pass

    def send(self, status, body, ctype="text/html; charset=utf-8", extra=(), close=False):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        packed = None
        if body and "gzip" in self.headers.get("Accept-Encoding", "") and status == 200 and ctype.startswith("text/"):
            packed = gzip.compress(body, mtime=0)
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(packed if packed is not None else body)))
        for k, v in extra:
            self.send_header(k, v)
        if close:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(packed if packed is not None else body)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        p = self.path
        if p == "/page":
            self.send(200, PAGE)
        elif p == "/close":
            self.send(200, PAGE, close=True)
        elif p == "/hebrew":
            self.send(200, HEBREW, "text/html; charset=windows-1255")
        elif p == "/site/":
            self.send(200, SITE)
        elif p == "/site/style.css":
            self.send(200, SITE_CSS, "text/css")
        elif p == "/site/two.html":
            self.send(200, SITE_TWO)
        elif p == "/redirect":
            self.send(302, b"", extra=[("Location", "/page")])
        elif p == "/secure":
            host = self.headers.get("Host", "localhost").rsplit(":", 1)[0]
            self.send(301, b"", extra=[("Location", "https://%s:%d/page" % (host, self.secure_port))])
        elif p == "/deflate":
            body = zlib.compress(PAGE)
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Encoding", "deflate")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif p == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            at = 0
            while at < len(BIG):
                n = min(rnd.randrange(1, 65536), len(BIG) - at)
                self.wfile.write(b"%x\r\n" % n + BIG[at:at + n] + b"\r\n")
                at += n
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send(404, b"not here\n", "text/plain")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(port, ctx=None, addr="127.0.0.1"):
    s = Server((addr, port), Handler)
    if ctx:
        s.socket = ctx.wrap_socket(s.socket, server_side=True)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s


def tls_context(work, cert, key):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(work, cert), os.path.join(work, key))
    return ctx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--http", type=int, default=18080)
    ap.add_argument("--https", type=int, default=18443)
    ap.add_argument("--untrusted", type=int, default=18444)
    ap.add_argument("--ecdsa", type=int, default=18445)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    make_certs(a.work)
    Handler.secure_port = a.https
    serve(a.http)
    serve(a.https, tls_context(a.work, "host.pem", "host.key"))
    serve(a.https, tls_context(a.work, "host.pem", "host.key"), "127.0.0.2")
    serve(a.untrusted, tls_context(a.work, "self.pem", "self.key"))
    serve(a.ecdsa, tls_context(a.work, "ec.pem", "ec.key"))
    print("ready", flush=True)
    try:
        sys.stdin.read()		# until whoever started us closes our stdin
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
