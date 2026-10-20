# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Tests for webgate: the proxy against a local server, as EWeb uses it."""

import gzip
import http.client
import json
import os
import shutil
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(__file__))
import webgate  # noqa: E402

PAGE = b"<!DOCTYPE html><title>Page</title><p>\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"
BIG = bytes(range(256)) * 1024          # 256 KB, more than one piece


class Upstream(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def log_message(self, *a):
        pass

    def answer(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        Upstream.seen.append((self.command, self.path, dict(self.headers), body))
        if self.path == "/page":
            data = gzip.compress(PAGE) if "gzip" in self.headers.get("Accept-Encoding", "") else PAGE
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            if data is not PAGE:
                self.send_header("Content-Encoding", "gzip")
            self.send_header("Set-Cookie", "a=1; Path=/")
            self.send_header("Set-Cookie", "b=2; Path=/")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(data)
        elif self.path == "/big":
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(BIG), 10000):
                part = BIG[i:i + 10000]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/moved":
            self.send_response(302)
            self.send_header("Location", "/page")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/same":
            self.send_response(304)
            self.send_header("ETag", '"v1"')
            self.end_headers()
        elif self.path.startswith("/echo"):
            data = b"%s %s %s" % (self.command.encode(), self.path.encode(), body)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            data = b"none"
            self.send_response(404)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    do_GET = do_POST = do_HEAD = answer


class ProxyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.up = ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
        threading.Thread(target=cls.up.serve_forever, daemon=True).start()
        webgate.Config.allow_local = True
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()
        cls.base = "http://127.0.0.1:%d" % cls.up.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.up.shutdown()
        cls.gw.shutdown()
        cls.up.server_close()
        cls.gw.server_close()
        webgate.Config.allow_local = False

    def ask(self, method, path, headers=None, body=None, conn=None):
        c = conn or http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        if conn is None:
            self.addCleanup(c.close)
        c.request(method, self.base + path, body=body, headers=headers or {})
        r = c.getresponse()
        return r, r.read(), c

    def test_page_gzip_passed_through(self):
        r, data, _ = self.ask("GET", "/page", {"Accept-Encoding": "gzip, deflate, br"})
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("Content-Encoding"), "gzip")
        self.assertEqual(gzip.decompress(data), PAGE)
        # only what the Atari unpacks was asked for
        self.assertEqual(Upstream.seen[-1][2]["Accept-Encoding"], "gzip, deflate")
        self.assertIsNone(r.getheader("X-Gateway-TLS"))       # plain HTTP upstream

    def test_identity_and_cookies(self):
        r, data, _ = self.ask("GET", "/page")
        self.assertEqual(data, PAGE)
        self.assertEqual(Upstream.seen[-1][2]["Accept-Encoding"], "identity")
        self.assertEqual([v for k, v in r.getheaders() if k.lower() == "set-cookie"], ["a=1; Path=/", "b=2; Path=/"])

    def test_chunked_big(self):
        r, data, _ = self.ask("GET", "/big")
        self.assertEqual(r.getheader("Transfer-Encoding"), "chunked")
        self.assertEqual(data, BIG)

    def test_redirect_and_304_and_404_as_they_came(self):
        r, data, c = self.ask("GET", "/moved")
        self.assertEqual((r.status, r.getheader("Location")), (302, "/page"))
        r, data, c = self.ask("GET", "/same", {"If-None-Match": '"v1"'}, conn=c)
        self.assertEqual((r.status, data), (304, b""))
        self.assertEqual(Upstream.seen[-1][2]["If-None-Match"], '"v1"')
        r, data, c = self.ask("GET", "/nothing", conn=c)
        self.assertEqual((r.status, data), (404, b"none"))

    def test_post_and_head_kept_connection(self):
        r, data, c = self.ask("POST", "/echo?x=1", {"Content-Type": "application/x-www-form-urlencoded"},
                              b"q=atari")
        self.assertEqual(data, b"POST /echo?x=1 q=atari")
        r, data, c = self.ask("HEAD", "/page", conn=c)
        self.assertEqual((r.status, data), (200, b""))
        r, data, c = self.ask("GET", "/echo", conn=c)
        self.assertEqual(data, b"GET /echo ")

    def test_status_page_and_bad_requests(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "/")
        r = c.getresponse()
        self.assertEqual(r.status, 200)
        self.assertIn(b"webgate", r.read())
        c.request("GET", "ftp://example.com/x")
        r = c.getresponse()
        self.assertEqual(r.status, 400)
        self.assertTrue(r.read().startswith(b"webgate: "))

    def test_unknown_host(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=20)
        self.addCleanup(c.close)
        c.request("GET", "http://no-such-host.invalid/")
        r = c.getresponse()
        self.assertEqual(r.status, 502)
        self.assertIn(b"webgate: can't find no-such-host.invalid", r.read())

    def test_home_network_refused(self):
        webgate.Config.allow_local = False
        try:
            r, data, _ = self.ask("GET", "/page")
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)
        self.assertIn(b"webgate: won't connect to the local network", data)


class LiveTest(unittest.TestCase):
    """live view: http://webgate/live, its picture from the extension, its
    clicks and keys as xdotool commands (a stand-in that writes them down)"""

    @classmethod
    def setUpClass(cls):
        webgate.Config.browser = True
        webgate.Config.allow_local = True       # example.com without a name server here
        webgate.Config.snapshot_wait = 5
        webgate.Config.live_settle = 0
        cls.tmp = tempfile.mkdtemp()
        cls.log = os.path.join(cls.tmp, "xdo.log")
        xdo = os.path.join(cls.tmp, "xdotool")
        with open(xdo, "w") as f:
            f.write("#!/bin/sh\necho \"$*\" >> %s\n" % cls.log)
        os.chmod(xdo, 0o755)
        cls.path = os.environ.get("PATH", "")
        os.environ["PATH"] = cls.tmp + os.pathsep + cls.path
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.ext.daemon_threads = True
        threading.Thread(target=cls.ext.serve_forever, daemon=True).start()
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        for s in (cls.ext, cls.gw):
            s.shutdown()
            s.server_close()
        os.environ["PATH"] = cls.path
        shutil.rmtree(cls.tmp)
        webgate.Config.browser = False
        webgate.Config.allow_local = False
        webgate.Config.snapshot_wait = 60
        webgate.Config.live_settle = 1.0

    def setUp(self):
        if os.path.exists(self.log):
            os.remove(self.log)

    def extension(self):
        """one turn of webgate-ext, as background.js answers each kind"""
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=30)
        c.request("GET", "/next")
        w = json.loads(c.getresponse().read())
        if w.get("shot"):
            c.request("POST", "/shot?id=%s&w=1216" % w["id"], body=JPEG)
        else:
            body = json.dumps({"url": w["url"] or "https://example.com/now", "title": "Now",
                               "html": "<!DOCTYPE html><title>Now</title><p>shown"}).encode()
            c.request("POST", "/snapshot?id=" + w["id"], body=body)
        c.getresponse().read()
        c.close()
        return w

    def ask(self, url, turn=True):
        seen = []
        t = threading.Thread(target=lambda: seen.append(self.extension()))
        if turn:
            t.start()
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=30)
        self.addCleanup(c.close)
        c.request("GET", url)
        r = c.getresponse()
        data = r.read()
        if turn:
            t.join()
        return r, data, seen[0] if seen else None

    def typed(self):
        with open(self.log) as f:
            return f.read().splitlines()

    def test_page(self):
        r, data, w = self.ask("http://webgate/live?url=https%3A%2F%2Fexample.com%2Flogin")
        self.assertEqual(w["url"], "https://example.com/login")
        self.assertEqual(r.status, 200)
        page = data.decode("utf-8")
        self.assertIn("<title>Live: Now</title>", page)
        self.assertIn('<input type=image name=at', page)
        self.assertIn('action="http://webgate/live/click"', page)

    def test_live_refuses_files_and_home(self):
        r, data, w = self.ask("http://webgate/live?url=file%3A%2F%2F%2Fetc%2F", turn=False)
        self.assertEqual(r.status, 400)
        webgate.Config.allow_local = False
        try:
            r, data, w = self.ask("http://webgate/live?url=http%3A%2F%2F10.0.0.1%2F", turn=False)
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)

    def test_config_for_the_extension(self):
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "/config")
        self.assertEqual(json.loads(c.getresponse().read()), {"allow_local": True})

    def test_shot(self):
        r, data, w = self.ask("http://webgate/live/shot.jpg?n=1")
        self.assertEqual(w["shot"], webgate.Config.live_width)
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        self.assertEqual(data, JPEG)

    def test_click(self):
        self.ask("http://webgate/live/shot.jpg?n=2")          # the screen is 1216 wide: twice the picture
        r, data, w = self.ask("http://webgate/live/click?at.x=100&at.y=40", turn=False)
        self.assertEqual(r.status, 303)
        self.assertTrue(r.getheader("Location").startswith("http://webgate/live?n="))
        self.assertEqual(self.typed(), ["mousemove 200 80 click 1"])

    def test_type_and_keys(self):
        self.ask("http://webgate/live/type?t=hello+%D7%A9&enter=Type+and+Enter", turn=False)
        self.ask("http://webgate/live/key?k=back", turn=False)
        self.ask("http://webgate/live/key?k=rm+-rf", turn=False)       # not a key: nothing
        self.assertEqual(self.typed(), ["type --delay 40 -- hello \u05e9", "key Return", "key alt+Left"])

    def test_as_a_page(self):
        r, data, w = self.ask("http://webgate/live/page")
        self.assertEqual(w.get("snap"), 1)
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("X-Gateway-Snapshot-URL"), "https://example.com/now")
        self.assertIn("<p>shown", data.decode("utf-8"))


JPEG = b"\xff\xd8\xff\xe0" + b"stand-in"


class SnapshotTest(unittest.TestCase):
    """--browser: the address to the extension (here a stand-in that does
    what webgate-ext does over the same port), its snapshot to EWeb"""

    @classmethod
    def setUpClass(cls):
        webgate.Config.browser = True
        webgate.Config.allow_local = True       # example.com without a name server here
        webgate.Config.snapshot_wait = 5
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.ext.daemon_threads = True
        threading.Thread(target=cls.ext.serve_forever, daemon=True).start()
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        for s in (cls.ext, cls.gw):
            s.shutdown()
            s.server_close()
        webgate.Config.browser = False
        webgate.Config.allow_local = False
        webgate.Config.snapshot_wait = 60

    def extension(self, answer=True):
        """one turn of webgate-ext: the next address, then its page"""
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=30)
        c.request("GET", "/next")
        r = c.getresponse()
        w = json.loads(r.read())
        if answer:
            body = json.dumps({"url": w["url"] + "#after", "title": "T",
                               "html": "<!DOCTYPE html><title>T</title><p>made by script \u05e9"}).encode()
            c.request("POST", "/snapshot?id=" + w["id"], body=body, headers={"Content-Type": "application/json"})
            c.getresponse().read()
        c.close()
        return w

    def ask(self, url, headers):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=30)
        self.addCleanup(c.close)
        c.request("GET", url, headers=headers)
        r = c.getresponse()
        return r, r.read()

    def test_snapshot(self):
        seen = []
        t = threading.Thread(target=lambda: seen.append(self.extension()))
        t.start()
        r, data = self.ask("https://example.com/app", {"X-Gateway-Snapshot": "1", "Accept-Encoding": "gzip"})
        t.join()
        self.assertEqual(seen[0]["url"], "https://example.com/app")
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("Content-Encoding"), "gzip")
        self.assertEqual(r.getheader("X-Gateway-Snapshot-URL"), "https://example.com/app#after")
        self.assertIn("made by script \u05e9", gzip.decompress(data).decode("utf-8"))

    def test_browser_too_slow(self):
        t = threading.Thread(target=lambda: self.extension(answer=False))
        t.start()
        r, data = self.ask("https://example.com/slow", {"X-Gateway-Snapshot": "1"})
        t.join()
        self.assertEqual(r.status, 504)
        self.assertTrue(data.startswith(b"webgate: the browser didn't finish example.com"))

    def test_home_network_refused(self):
        """7.3 holds for the browser: an address in the home network is
        refused before the browser sees it (no extension turn here)"""
        webgate.Config.allow_local = False
        try:
            r, data = self.ask("http://192.168.1.1/", {"X-Gateway-Snapshot": "1"})
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)
        self.assertTrue(data.startswith(b"webgate: won't connect to the local network (192.168.1.1)"))

    def test_no_browser(self):
        webgate.Config.browser = False
        try:
            r, data = self.ask("https://example.com/", {"X-Gateway-Snapshot": "1"})
        finally:
            webgate.Config.browser = True
        self.assertEqual(r.status, 501)
        self.assertTrue(data.startswith(b"webgate: snapshots need the Pi's browser"))


class TlsTest(unittest.TestCase):
    def test_tls_of(self):
        self.assertEqual(webgate.tls_of(http.client.HTTPConnection("x")), "")
        fake = type("C", (), {})()
        fake.sock = None
        self.assertEqual(webgate.tls_of(fake), "")


if __name__ == "__main__":
    unittest.main()
