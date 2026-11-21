#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The sites' recorder and replayer: an HTTP proxy (CONNECT, as ewrender's
EW_PROXY asks for every connection) that, recording, takes each answer from
the site itself and keeps it in DIR, and, replaying, gives the kept answer
and nothing else, so a site Erez sent is laid out the same at every run,
whatever the site does since (tests/sites/run.py; the copies live in the
private EWeb-sites repository, as they are their authors' pages).

    tests/sites/replay.py record|replay DIR CA_DIR PORT_FILE

A secure connection is answered with a certificate for its host signed by a
CA made for the purpose in CA_DIR (build/sites/ca, never committed; ewrender
-c CA_DIR/ca.pem), the site's own TLS being the recorder's. Each answer is
DIR/rec/KEY (KEY a SHA-1 of the method, the address and a POST's body): a
line of JSON (status, headers) and the body as it came (gzip kept gzip). A
host that couldn't be reached is kept as such and refused again. Replaying,
an address never recorded gets 404 and is named in DIR/missing.txt, a sign
that EWeb now asks for something it didn't. The port the proxy listens on
is written to PORT_FILE once it listens."""
import datetime
import hashlib
import json
import os
import socket
import socketserver
import ssl
import sys
import threading
import urllib.error
import urllib.request

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

MODE, DIR, CADIR, PORTFILE = sys.argv[1:5]
REC = os.path.join(DIR, "rec")
HOP = {"connection", "keep-alive", "transfer-encoding", "proxy-connection", "content-length", "te", "trailer",
       "upgrade"}
lock = threading.Lock()
missing = set()


def ca():
    """the CA, made once in CADIR"""
    os.makedirs(CADIR, exist_ok=True)
    kp, cp = os.path.join(CADIR, "ca.key"), os.path.join(CADIR, "ca.pem")
    if not os.path.exists(cp):
        key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "EWeb sites test CA")])
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
                .not_valid_after(now + datetime.timedelta(days=3650))
                .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
                .sign(key, hashes.SHA256()))
        with open(kp, "wb") as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                      serialization.NoEncryption()))
        with open(cp, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))
    key = serialization.load_pem_private_key(open(kp, "rb").read(), None)
    return key, x509.load_pem_x509_certificate(open(cp, "rb").read())


CA_KEY, CA_CERT = ca()
contexts = {}


def context(host):
    """a server context with a certificate for host, made once a run"""
    with lock:
        if host in contexts:
            return contexts[host]
        key = ec.generate_private_key(ec.SECP256R1())
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)]))
                .issuer_name(CA_CERT.subject).public_key(key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now - datetime.timedelta(days=1)).not_valid_after(now + datetime.timedelta(days=365))
                .add_extension(x509.SubjectAlternativeName([x509.DNSName(host)]), critical=False)
                .sign(CA_KEY, hashes.SHA256()))
        d = os.path.join(CADIR, "hosts")
        os.makedirs(d, exist_ok=True)
        cf, kf = os.path.join(d, host + ".pem"), os.path.join(d, host + ".key")
        with open(cf, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM) + CA_CERT.public_bytes(serialization.Encoding.PEM))
        with open(kf, "wb") as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                      serialization.NoEncryption()))
        c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        c.load_cert_chain(cf, kf)
        contexts[host] = c
        return c


def keyof(method, url, body):
    return hashlib.sha1(b"%s %s\n" % (method.encode(), url.encode()) + (body or b"")).hexdigest()


def kept(key):
    p = os.path.join(REC, key)
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        meta = json.loads(f.readline())
        return meta, f.read()


def keep(key, meta, body):
    os.makedirs(REC, exist_ok=True)
    with open(os.path.join(REC, key) + ".tmp", "wb") as f:
        f.write(json.dumps(meta, sort_keys=True).encode() + b"\n" + body)
    os.replace(os.path.join(REC, key) + ".tmp", os.path.join(REC, key))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


OPENER = urllib.request.build_opener(NoRedirect, urllib.request.HTTPSHandler(
    context=ssl.create_default_context(cafile=os.environ.get("SSL_CERT_FILE") or None)))


def fetch(method, url, headers, body):
    """the site's own answer: (meta, body), meta's status 0 when it couldn't be reached"""
    h = {k: v for k, v in headers if k.lower() not in HOP and k.lower() != "host"}
    rq = urllib.request.Request(url, data=body if method == "POST" else None, headers=h, method=method)
    try:
        r = OPENER.open(rq, timeout=30)
    except urllib.error.HTTPError as e:
        r = e
    except Exception as e:
        return {"status": 0, "url": url, "error": str(e)[:200]}, b""
    data = r.read()
    return {"status": r.status if hasattr(r, "status") else r.code, "url": url,
            "headers": [[k, v] for k, v in r.headers.items() if k.lower() not in HOP]}, data


def read_request(f):
    line = f.readline(65536)
    if not line:
        return None
    method, target, _ = line.decode("latin-1").split(" ", 2)
    headers = []
    while True:
        l = f.readline(65536).decode("latin-1")
        if l in ("\r\n", "\n", ""):
            break
        k, _, v = l.partition(":")
        headers.append((k.strip(), v.strip()))
    n = int(next((v for k, v in headers if k.lower() == "content-length"), "0") or 0)
    body = f.read(n) if n else b""
    return method, target, headers, body


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        first = self.rfile.readline(65536).decode("latin-1")
        while self.rfile.readline(65536) not in (b"\r\n", b"\n", b""):
            pass
        if not first.startswith("CONNECT "):
            return
        host, _, port = first.split()[1].rpartition(":")
        self.wfile.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
        self.wfile.flush()
        conn = self.connection
        peek = conn.recv(1, socket.MSG_PEEK)
        secure = peek == b"\x16"
        if secure:
            try:
                conn = context(host).wrap_socket(conn, server_side=True)
            except (ssl.SSLError, OSError):
                return
        scheme = "https" if secure else "http"
        dflt = "443" if secure else "80"
        origin = "%s://%s%s" % (scheme, host, "" if port == dflt else ":" + port)
        rf, wf = conn.makefile("rb"), conn.makefile("wb")
        while True:
            try:
                rq = read_request(rf)
            except (OSError, ValueError, ssl.SSLError):
                return
            if rq is None:
                return
            method, target, headers, body = rq
            url = target if target.startswith("http") else origin + target
            key = keyof(method, url, body if method == "POST" else None)
            got = kept(key)
            if got is None and MODE == "record":
                got = fetch(method, url, headers, body)
                keep(key, *got)
            if got is None:
                with lock:
                    if url not in missing:
                        missing.add(url)
                        with open(os.path.join(DIR, "missing.txt"), "a") as f:
                            f.write("%s %s\n" % (method, url))
                got = ({"status": 404, "headers": [["Content-Type", "text/plain"]]}, b"not recorded\n")
            meta, data = got
            if meta["status"] == 0:
                return          # no answer then: none now
            out = "HTTP/1.1 %d X\r\n" % meta["status"]
            for k, v in meta.get("headers", []):
                out += "%s: %s\r\n" % (k, v)
            out += "Content-Length: %d\r\n\r\n" % len(data)
            try:
                wf.write(out.encode("latin-1", "replace") + (data if method != "HEAD" else b""))
                wf.flush()
            except OSError:
                return


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    s = Server(("127.0.0.1", 0), Handler)
    with open(PORTFILE + ".tmp", "w") as f:
        f.write(str(s.server_address[1]))
    os.replace(PORTFILE + ".tmp", PORTFILE)
    s.serve_forever()
