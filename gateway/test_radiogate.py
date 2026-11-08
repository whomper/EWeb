#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""test_radiogate.py - radiogate against stations of its own on 127.0.0.1:
MP3 passed on with its titles, a redirect to a .pls, AAC with titles
converted by ffmpeg, HLS pieces converted, and what it refuses (a station on
the home network without --allow-local, a URL it doesn't take, /version).
A test that needs ffmpeg says SKIPPED where there is none.

    python3 radio/test_radiogate.py
"""
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import radiogate  # noqa: E402

HAVE_FFMPEG = radiogate.ffmpeg_version() is not None
TMP = tempfile.mkdtemp(prefix="radiogate-")


def make(name, *args):
    path = os.path.join(TMP, name)
    if not os.path.exists(path):
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "sine=frequency=1000:duration=6", "-ac", "2", *args, path], check=True)
    return path


class Station(threading.Thread):
    """answers each path as routes says: (head lines, body bytes, metaint,
    title) or a callable of the request path"""

    def __init__(self, routes):
        super().__init__(daemon=True)
        self.routes = routes
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.start()

    def run(self):
        while True:
            try:
                c, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(c,), daemon=True).start()

    def serve(self, c):
        try:
            req = b""
            while b"\r\n\r\n" not in req:
                d = c.recv(1024)
                if not d:
                    return
                req += d
            path = req.split()[1].decode()
            head, body, metaint, title = self.routes[path]
            c.sendall(head.encode())
            if not metaint:
                c.sendall(body)
                return
            # (ICY: a title block every metaint bytes, the body over again)
            pos, n = 0, 0
            while n < 400000:
                k = min(metaint, len(body) - pos)
                c.sendall(body[pos:pos + k])
                pos = (pos + k) % len(body)
                n += k
                if k == metaint or pos == 0:
                    t = ("StreamTitle='%s';" % title).encode()
                    t += b"\0" * (-len(t) % 16)
                    c.sendall(bytes([len(t) // 16]) + t)
                time.sleep(0.005)
        except (OSError, KeyError):
            pass
        finally:
            c.close()


class Gate:
    def __init__(self, allow_local=True):
        radiogate.Config.allow_local = allow_local
        radiogate.Config.allow = []
        radiogate.Config.timeout = 5
        srv = radiogate.ThreadingHTTPServer(("127.0.0.1", 0), radiogate.Handler)
        srv.daemon_threads = True
        self.srv, self.port = srv, srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()

    def ask(self, url, meta=True, want=120000, path=None):
        """(status line, heads, the bytes after them, up to want)"""
        s = socket.create_connection(("127.0.0.1", self.port), timeout=20)
        p = path or "/stream?url=" + urllib.parse.quote(url, safe="")
        s.sendall(("GET %s HTTP/1.0\r\n%s\r\n" % (p, "Icy-MetaData: 1\r\n" if meta else "")).encode())
        got = b""
        while len(got) < want:
            d = s.recv(65536)
            if not d:
                break
            got += d
        s.close()
        head, _, body = got.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        heads = dict((k.strip().lower(), v.strip()) for k, _, v in (l.partition(":") for l in lines[1:]))
        return lines[0], heads, body

    def stop(self):
        self.srv.shutdown()


def strip_icy(body, metaint):
    """(audio, titles) out of an ICY stream"""
    audio, titles, i = bytearray(), [], 0
    while i < len(body):
        audio += body[i:i + metaint]
        i += metaint
        if i >= len(body):
            break
        n = body[i] * 16
        m = re.search(rb"StreamTitle='(.*?)';", body[i + 1:i + 1 + n])
        if m:
            titles.append(m.group(1).decode())
        i += 1 + n
    return bytes(audio), titles


def mp3_frames(audio):
    """how many MP3 frame headers in a row from the first (MPEG-1 layer III)"""
    rates = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    i = audio.find(b"\xff\xfb")
    n = 0
    while 0 <= i and i + 4 <= len(audio) and audio[i] == 0xff and (audio[i + 1] & 0xfe) == 0xfa:
        br = rates[audio[i + 2] >> 4] if (audio[i + 2] >> 4) < 15 else 0
        sr = (44100, 48000, 32000, 0)[(audio[i + 2] >> 2) & 3]
        if not br or not sr:
            break
        i += 144000 * br // sr + ((audio[i + 2] >> 1) & 1)
        n += 1
    return n


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mp3 = open(make("tone.mp3", "-c:a", "libmp3lame", "-b:a", "128k"), "rb").read() \
            if HAVE_FFMPEG else b""

    def test_mp3_titles(self):
        if not HAVE_FFMPEG:
            print("SKIPPED: mp3_titles (no ffmpeg to make the tone)")
            return
        st = Station({"/live": ("ICY 200 OK\r\nicy-name: Test FM\r\nicy-br: 128\r\nicy-metaint: 4000\r\n"
                                "Content-Type: audio/mpeg\r\n\r\n", self.mp3, 4000, "Artist - Song")})
        g = Gate()
        status, heads, body = g.ask("http://127.0.0.1:%d/live" % st.port)
        g.stop()
        self.assertIn("200", status)
        self.assertEqual(heads.get("icy-name"), "Test FM")
        self.assertEqual(heads.get("icy-metaint"), str(radiogate.METAINT))
        audio, titles = strip_icy(body, radiogate.METAINT)
        self.assertIn("Artist - Song", titles)
        self.assertGreater(mp3_frames(audio), 20)

    def test_redirect_pls(self):
        if not HAVE_FFMPEG:
            print("SKIPPED: redirect_pls (no ffmpeg to make the tone)")
            return
        st = Station({})
        st.routes.update({
            "/go": ("HTTP/1.1 302 Found\r\nLocation: /list.pls\r\nContent-Length: 0\r\n\r\n", b"", 0, ""),
            "/list.pls": ("HTTP/1.0 200 OK\r\nContent-Type: audio/x-scpls\r\n\r\n",
                          ("[playlist]\r\nFile1=http://127.0.0.1:%d/live\r\n" % st.port).encode(), 0, ""),
            "/live": ("ICY 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n", self.mp3 * 3, 0, "")})
        g = Gate()
        status, heads, body = g.ask("http://127.0.0.1:%d/go" % st.port, meta=False, want=60000)
        g.stop()
        self.assertIn("200", status)
        self.assertNotIn("icy-metaint", heads)
        self.assertGreater(mp3_frames(body), 20)

    def test_aac_converted(self):
        if not HAVE_FFMPEG:
            print("SKIPPED: aac_converted (no ffmpeg)")
            return
        aac = open(make("tone.aac", "-c:a", "aac", "-b:a", "64k", "-f", "adts"), "rb").read()
        st = Station({"/aac": ("ICY 200 OK\r\nicy-name: AAC FM\r\nicy-metaint: 2000\r\n"
                               "Content-Type: audio/aacp\r\n\r\n", aac, 2000, "AAC - Title")})
        g = Gate()
        status, heads, body = g.ask("http://127.0.0.1:%d/aac" % st.port, want=60000)
        g.stop()
        self.assertIn("200", status)
        self.assertEqual(heads.get("content-type"), "audio/mpeg")
        audio, titles = strip_icy(body, radiogate.METAINT)
        self.assertIn("AAC - Title", titles)
        self.assertGreater(mp3_frames(audio), 10)

    def test_hls(self):
        if not HAVE_FFMPEG:
            print("SKIPPED: hls (no ffmpeg)")
            return
        d = os.path.join(TMP, "hls")
        os.makedirs(d, exist_ok=True)
        if not os.path.exists(os.path.join(d, "live.m3u8")):
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "sine=frequency=1000:duration=12", "-ac", "2", "-c:a", "aac", "-b:a", "64k",
                            "-f", "hls", "-hls_time", "2", "-hls_list_size", "0",
                            "-hls_segment_filename", os.path.join(d, "p%02d.ts"),
                            os.path.join(d, "live.m3u8")], check=True)
        routes = {"/live.m3u8": ("HTTP/1.0 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n\r\n",
                                 open(os.path.join(d, "live.m3u8"), "rb").read(), 0, "")}
        for f in os.listdir(d):
            if f.endswith(".ts"):
                routes["/" + f] = ("HTTP/1.0 200 OK\r\nContent-Type: video/mp2t\r\n\r\n",
                                   open(os.path.join(d, f), "rb").read(), 0, "")
        st = Station(routes)
        g = Gate()
        status, heads, body = g.ask("http://127.0.0.1:%d/live.m3u8" % st.port, meta=False, want=40000)
        g.stop()
        self.assertIn("200", status)
        self.assertGreater(mp3_frames(body), 10)

    def test_home_network_refused(self):
        st = Station({"/live": ("ICY 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n", b"x" * 100, 0, "")})
        g = Gate(allow_local=False)
        status, _, body = g.ask("http://127.0.0.1:%d/live" % st.port)
        g.stop()
        self.assertIn("403", status)
        self.assertIn(b"home network", body)

    def test_bad_urls(self):
        g = Gate()
        for u in ("ftp://x/y", "http://user:pw@x/", "http://x/a b", "javascript:1", ""):
            status, _, _ = g.ask(u)
            self.assertIn("400", status, u)
        status, _, _ = g.ask("", path="/nothing")
        self.assertIn("404", status)
        status, _, body = g.ask("", path="/version")
        self.assertIn("200", status)
        self.assertTrue(body.startswith(b"radiogate " + radiogate.VERSION.encode()))
        g.stop()

    def test_not_answering(self):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()                       # (nothing listens there now)
        g = Gate()
        status, _, body = g.ask("http://127.0.0.1:%d/live" % port)
        g.stop()
        self.assertIn("502", status)
        self.assertIn(b"doesn't answer", body)

    def test_icy_in_out(self):
        """the metadata taken out at every cut, and put back"""
        audio = bytes(range(256)) * 40
        out = radiogate.IcyOut()
        wrapped = out.wrap(audio, "A - B")
        for cut in (1, 7, 100, radiogate.METAINT, len(wrapped)):
            icy = radiogate.IcyIn(radiogate.METAINT)
            got = b"".join(icy.feed(wrapped[i:i + cut]) for i in range(0, len(wrapped), cut))
            self.assertEqual(got, audio)
            self.assertEqual(icy.title, "A - B")


if __name__ == "__main__":
    unittest.main(verbosity=2)
