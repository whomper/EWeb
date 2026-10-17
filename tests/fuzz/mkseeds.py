#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes tests/fuzz/seeds/: the starting inputs for make fuzz. For
fuzz_http the first byte picks the request (see fuzz_http.c), the rest is
what the server says; for fuzz_url, an href, a newline and a base.

tests/fuzz/found/ holds the inputs that once broke a parser, as the fuzzer
saved them; make test runs them all again (common/tests/fuzz/replay.c)."""
import gzip
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
NEXT = b"\n==next==\n"


def put(target, name, data):
    d = os.path.join(HERE, "seeds", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def http(status, body, headers=(), chunked=False, enc=None, version="1.1"):
    b = body.encode() if isinstance(body, str) else body
    h = ["HTTP/%s %s" % (version, status), "Content-Type: text/html; charset=utf-8"] + list(headers)
    if enc == "gzip":
        b = gzip.compress(b, mtime=0)
        h.append("Content-Encoding: gzip")
    elif enc == "deflate":
        b = zlib.compress(b)
        h.append("Content-Encoding: deflate")
    if chunked:
        h.append("Transfer-Encoding: chunked")
        parts = [b[i:i + 40] for i in range(0, len(b), 40)]
        b = b"".join(b"%x\r\n" % len(p) + p + b"\r\n" for p in parts) + b"0\r\n\r\n"
    elif chunked is not None:
        h.append("Content-Length: %d" % len(b))
    return ("\r\n".join(h) + "\r\n\r\n").encode() + b


PAGE = "<!DOCTYPE html><html><body><p>שלום Falcon</p>" * 20 + "</body></html>"
put("http", "plain", b"\x00" + http("200 OK", PAGE))
put("http", "chunked", b"\x00" + http("200 OK", PAGE, chunked=True))
put("http", "gzip", b"\x00" + http("200 OK", PAGE, enc="gzip"))
put("http", "deflate-chunked", b"\x00" + http("200 OK", PAGE, chunked=True, enc="deflate"))
put("http", "close", b"\x00" + http("200 OK", PAGE, chunked=None, version="1.0"))
put("http", "redirects", b"\x40" + http("301 Moved", "", ["Location: ../c?q#f"]) +
    http("302 Found", "", ["Location: //x.test/d"]) + http("200 OK", "here"))
put("http", "continue", b"\x02" + b"HTTP/1.1 100 Continue\r\n\r\n" + http("303 See Other", "", ["Location: /x"]) +
    http("200 OK", "ok"))
put("http", "kept", b"\x80" + http("204 No Content", "", chunked=None) + http("200 OK", "abc"))
put("http", "reconnect", b"\x80" + http("200 OK", "one") + NEXT + http("200 OK", "two"))
put("http", "head", b"\x01" + b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n")
put("http", "sink", b"\x08" + http("200 OK", PAGE) )
put("http", "small", b"\x04" + http("200 OK", PAGE * 3, enc="gzip"))
put("http", "folded", b"\x20" + b"HTTP/1.1 200 OK\r\nX-A: a\r\n b\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"
    b"3;x=y\r\nabc\r\n0\r\nT: 1\r\n\r\n")

for name, href, base in [
    ("relative", "../g?y#s", "http://a/b/c/d;p?q"),
    ("absolute", "HTTPS://User:Pass@Example.COM:443/a/./b/../c?x y#z", ""),
    ("ipv6", "http://[0:0:0:0:0:ffff:7f00:1]:80/", ""),
    ("ipv4", "//0x7f.0.0.1./x", "https://example.org/"),
    ("idna", "https://שלום.co.il/א", ""),
    ("backslash", "\\\\cdn.example.org\\x\\..\\y.css", "http://www.example.org/a/"),
    ("opaque", "javascript:alert(1)", "http://example.org/"),
    ("query", "?a=é&b='c'", "http://example.org/p"),
]:
    put("url", name, href + ("\n" + base if base else ""))
