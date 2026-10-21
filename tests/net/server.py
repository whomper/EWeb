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
  /site/pics.html  pictures: a JPEG with its size given, a PNG with alpha
                 and no size (the page is laid out again when it comes), a
                 GIF, a progressive JPEG with only its width, a big PNG held
                 to the width, and one that isn't there (made with Pillow)
  /site/bg.html  pictures from CSS and srcset: a stylesheet in
                 /site/css/ whose url(tile.png) is its folder's, and a
                 <picture> whose WebP source is passed over and whose img's
                 srcset gives logo.png for its 150px
  /site/webp.html  a WebP picture (pic.webp) and a GIF: EWeb on its own
                 doesn't fetch the WebP; through webgate it comes as JPEG;
                 /site/hits/NAME: how many times a picture was asked for
  /site/fresh.html  the photo from /site/fresh/, kept an hour (max-age):
                 the Pi's pens from the disk cache, by the pens asked for
  /site/form.html  forms: a search sent with GET, a login sent with POST,
                 a multipart one with a file field, one whose POST is
                 answered with 303; /site/echo answers each with a page
                 whose title is what came: "METHOD|Content-Type|body"
                 (CR LF as ~; multipart as its parts, name=value;...)
  /site/cookie/  cookies: set (one lasting, one for the session, a Secure
                 one that plain HTTP may not set), show (the title is
                 "COOKIE|" and the Cookie header that came), form.html (a
                 login whose POST is answered with 303 and a cookie),
                 third.html, opened as localhost (a picture from 127.0.0.1
                 that sets a cookie there: another site's, which EWeb
                 refuses)
  /site/files.html  files to save: links to /chunked (2 MB), a file that
                 isn't there, /site/ and /site/small.bin (20 KB; the same
                 bytes as /site/small.zip, application/zip)
  /site/cache/   the disk cache: fresh.html (fresh for an hour; a POST to
                 it changes it), etag.html (asked again each time: a 304 to
                 its ETag), post.html (a form that POSTs to fresh.html),
                 count (how many requests each had, and the 304s; never kept)
  /site/script.html  scripts (EWeb's src/js): one from a file
                 (/site/script.js), fetch and XMLHttpRequest of
                 /site/data.json, document.cookie beside an HttpOnly cookie
                 the page set, and the cookies a script's request sends;
                 /site/tabs.html: tabs a click shows, an alert (EWEB.PRG
                 in Hatari)
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
FILES_PAGE = (b"<!DOCTYPE html><html><head><title>Files</title></head><body><p><a href=/chunked>A big "
              b"file</a> <a href=/site/none.zip>A file that isn't there</a> <a href=/site/>A page</a> "
              b"<a href=/site/small.bin>A small file</a></p></body></html>")
SMALL = bytes(range(256)) * 80
CACHE_COUNT = {"fresh": 0, "etag": 0, "304": 0}
SITE_CSS = b"body { background: #ffeedd } h1 { color: #aa0000 }\n"
SITE_TWO = (b"<!DOCTYPE html><html><head><title>Two</title></head><body><p>Back to "
            b"<a href=/site/>the first</a>.</p><div style='height: 2000px'></div><p id=end>The end</p>"
            b"</body></html>\n")
FORM_PAGE = ("<!DOCTYPE html><html><head><meta charset=utf-8><title>Forms</title></head><body>"
             "<form action=/site/echo><p><input name=q value=atari> <input type=submit name=go value=Find></p></form>"
             "<form action=/site/echo method=post><p><input name=user> <input type=password name=pw> "
             "<label><input type=checkbox name=keep> Keep me</label> <select name=lang><option value=en>English"
             "<option value=he>עברית</select> <textarea name=note>a</textarea> "
             "<input type=submit name=login value='Log in'></p></form>"
             "<form action=/site/echo method=post enctype=multipart/form-data><p><input name=title value=x> "
             "<input type=file name=f> <input type=submit name=up value=Send></p></form>"
             "<form action=/site/after method=post><p><input name=n value=1> <input type=submit name=s value=Go>"
             "</p></form></body></html>\n").encode("utf-8")


def echo_page(method, ctype, body):
    """a page whose title says what a form sent"""
    if ctype.startswith("multipart/form-data"):
        b = ctype.split("boundary=", 1)[1].encode()
        parts = []
        for part in body.split(b"--" + b)[1:]:
            if part.startswith(b"--"):
                break
            head, _, value = part[2:].partition(b"\r\n\r\n")
            disp = head.decode().split("\r\n")[0]
            name = disp.split('name="', 1)[1].split('"', 1)[0]
            file = "(file %s)" % disp.split("filename=", 1)[1] if "filename=" in disp else ""
            parts.append("%s=%s%s" % (name, value[:-2].decode(), file))
        text = ";".join(parts)
        ctype = "multipart/form-data"
    else:
        text = body.decode("latin-1").replace("\r\n", "~")
    title = "%s|%s|%s" % (method, ctype, text)
    return ("<!DOCTYPE html><html><head><title>%s</title></head><body><p>Sent.</p></body></html>\n"
            % title.replace("&", "&amp;").replace("<", "&lt;")).encode("utf-8")


BIG = bytes(rnd.randrange(256) for _ in range(2 << 20))
PICS_PAGE = (b"<!DOCTYPE html><html><head><title>Pictures</title><style>body{background:#fff;font-family:sans-serif}"
             b".wide{max-width:100%}</style></head><body><h1>Pictures</h1>"
             b"<p>A photo: <img src=photo.jpg width=240 height=160 alt='a photo'> with text after it.</p>"
             b"<p><a href=two.html><img src=logo.png alt='the logo'></a> a link round a picture with alpha.</p>"
             b"<p>A GIF: <img src=anim.gif alt=gif> and a progressive JPEG: <img src=prog.jpg width=160 alt=prog></p>"
             b"<p><img class=wide src=big.png alt='a big one'></p>"
             b"<p>Not there: <img src=gone.png width=100 height=30 alt='gone'>.</p><p id=end>The end</p>"
             b"</body></html>\n")


BG_PAGE = (b"<!DOCTYPE html><html><head><title>Backgrounds</title><link rel=stylesheet href=css/bg.css>"
           b"</head><body><div class=box>A box tiled from its sheet's folder</div>"
           b"<p><picture><source type=image/webp srcset=no.webp><img src=gone.png "
           b"srcset='logo.png 150w, big.png 800w' sizes='(max-width: 10px) 800px, 150px' alt=chosen>"
           b"</picture></p></body></html>\n")
WEBP_PAGE = (b"<!DOCTYPE html><html><head><title>WebP</title></head><body>"
             b"<p><img src=pic.webp alt=webp> and <img src=anim.gif alt=gif></p></body></html>\n")
FRESH_PAGE = (b"<!DOCTYPE html><html><head><title>Fresh</title></head><body>"
              b"<p><img src=fresh/photo.jpg width=240 height=160 alt=photo></p></body></html>\n")
BG_CSS = b".box { background: #eee url(tile.png) repeat-x 0 100%; height: 40px }\n"


def make_pics():
    """the pictures of /site/pics.html, the same bytes every time"""
    import io
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return {}

    def photo(w, h):
        im = Image.new("RGB", (w, h))
        px = im.load()
        for y in range(h):
            for x in range(w):
                px[x, y] = (40 + 180 * x // w, 60 + 150 * y // h, 200 - 120 * x // w)
        d = ImageDraw.Draw(im)
        d.ellipse((w // 4, h // 5, 3 * w // 4, 4 * h // 5), fill=(200, 30, 40))
        d.rectangle((w // 10, h // 2, w // 3, h - 10), fill=(250, 240, 200))
        return im

    def save(im, fmt, **kw):
        b = io.BytesIO()
        im.save(b, fmt, **kw)
        return b.getvalue()
    logo = Image.new("RGBA", (150, 60), (0, 0, 0, 0))
    d = ImageDraw.Draw(logo)
    d.ellipse((2, 2, 56, 56), fill=(0, 90, 200, 255))
    d.rectangle((64, 18, 146, 42), fill=(230, 120, 0, 160))
    frames = [Image.new("P", (48, 48), i * 60) for i in range(3)]
    for i, f in enumerate(frames):
        f.putpalette([v for c in range(256) for v in (c, 255 - c, (c * 3) & 255)])
        ImageDraw.Draw(f).rectangle((8 + i * 8, 8, 24 + i * 8, 40), fill=200)
    gif = io.BytesIO()
    frames[0].save(gif, "GIF", save_all=True, append_images=frames[1:], duration=200, loop=0)
    return {
        "photo.jpg": (save(photo(480, 320), "JPEG", quality=85), "image/jpeg"),
        "prog.jpg": (save(photo(320, 240), "JPEG", quality=85, progressive=True), "image/jpeg"),
        "logo.png": (save(logo, "PNG"), "image/png"),
        "anim.gif": (gif.getvalue(), "image/gif"),
        "big.png": (save(photo(800, 600), "PNG"), "image/png"),
        "css/tile.png": (save(Image.new("RGB", (8, 8), (90, 90, 200)), "PNG"), "image/png"),
        "pic.webp": (save(photo(320, 200), "WEBP"), "image/webp"),
    }


PICS = make_pics()
HITS = {}           # how many times each picture was asked for (/site/hits/NAME)


def make_certs(work):
    """A test authority and a certificate for localhost signed by it, and a
    self-signed one, RSA; an ECDSA authority and certificate. CACERT.PEM
    holds both authorities."""
    def run(*a):
        subprocess.run(["openssl"] + list(a), cwd=work, check=True, capture_output=True)
    mark = os.path.join(work, "certs.2")    # the certificates' version (2: an IP entry for 127.0.0.1)
    if os.path.exists(mark):
        return
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "CACERT.PEM",
        "-days", "3650", "-subj", "/CN=EWeb test authority",
        "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    run("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "host.key", "-out", "host.csr",
        "-subj", "/CN=localhost")
    with open(os.path.join(work, "ext.cnf"), "w") as f:
        f.write("subjectAltName=DNS:localhost,DNS:127.0.0.1,IP:127.0.0.1\nbasicConstraints=CA:FALSE\n"
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
    open(mark, "w").close()


TABS_PAGE = (b"<!DOCTYPE html><title>Tabs</title><style>.hide { display: none } .tab { background: #ddd }</style>"
             b"<p><span id=t1 class=tab>First</span> <span id=t2 class=tab>Second</span></p>"
             b"<div id=p1>The first tab's words</div><div id=p2 class=hide>The second tab's words</div>"
             b"<p><button id=hi type=button onclick=\"alert('Hello from the page')\">Say hello</button></p>"
             b"<noscript><p>Without scripts</p></noscript><script>document.title = 'Tabs ready';"
             b"var tabs = document.querySelectorAll('.tab'); for (var i = 0; i < tabs.length; i++)"
             b" tabs[i].onclick = function () { document.getElementById('p1').className = this.id == 't1' ? '' : 'hide';"
             b" document.getElementById('p2').className = this.id == 't2' ? '' : 'hide';"
             b" document.title = 'Tab ' + this.id; };</script>")
SCRIPT_PAGE = (b"<!DOCTYPE html><title>Scripts</title><p id=a>waiting</p><p id=b>waiting</p><p id=c>waiting</p>"
               b"<p id=d></p><p id=e>waiting</p><script src=/site/script.js></script><script>"
               b"fetch('/site/data.json').then(function (r) { return r.json(); }).then(function (j) {"
               b" document.getElementById('a').textContent = 'fetch ' + j.word; });"
               b"var x = new XMLHttpRequest(); x.open('GET', '/site/data.json');"
               b"x.onload = function () { document.getElementById('b').textContent = 'xhr ' +"
               b" JSON.parse(x.responseText).word; }; x.send();"
               b"document.cookie = 'js=1; Path=/'; document.cookie = 'hidden=0; Path=/';"
               b"document.getElementById('d').textContent = 'cookies ' + document.cookie;"
               b"var y = new XMLHttpRequest(); y.open('GET', '/site/cookie/show', false); y.send();"
               b"document.getElementById('e').textContent = 'sent ' + /<title>COOKIE\\|(.*)<\\/title>/.exec(y.responseText)[1];"
               b"</script>")


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
        elif p == "/site/pics.html":
            self.send(200, PICS_PAGE)
        elif p == "/site/bg.html":
            self.send(200, BG_PAGE)
        elif p == "/site/webp.html":
            self.send(200, WEBP_PAGE)
        elif p == "/site/fresh.html":
            self.send(200, FRESH_PAGE)
        elif p == "/site/css/bg.css":
            self.send(200, BG_CSS, "text/css")
        elif p == "/site/form.html":
            self.send(200, FORM_PAGE)
        elif p == "/site/files.html":
            self.send(200, FILES_PAGE)
        elif p == "/site/tabs.html":
            self.send(200, TABS_PAGE)
        elif p == "/site/script.html":
            self.send(200, SCRIPT_PAGE, extra=[("Set-Cookie", "hidden=1; HttpOnly; Path=/"),
                                               ("Set-Cookie", "seen=2; Path=/")])
        elif p == "/site/script.js":
            self.send(200, b"document.getElementById('c').textContent = 'external ' + (1 + 1);",
                      "text/javascript")
        elif p == "/site/data.json":
            self.send(200, b'{"word": "atari"}', "application/json")
        elif p == "/site/cache/fresh.html":
            CACHE_COUNT["fresh"] += 1
            self.send(200, b"<!DOCTYPE html><title>fresh %d</title><p>Kept an hour." % CACHE_COUNT["fresh"],
                      extra=[("Cache-Control", "max-age=3600")])
        elif p == "/site/cache/etag.html":
            CACHE_COUNT["etag"] += 1
            if self.headers.get("If-None-Match") == '"v1"':
                CACHE_COUNT["304"] += 1
                self.send(304, b"", extra=[("ETag", '"v1"'), ("Cache-Control", "no-cache")])
            else:
                self.send(200, b"<!DOCTYPE html><title>etag %d</title><p>Asked each time." % CACHE_COUNT["etag"],
                          extra=[("ETag", '"v1"'), ("Cache-Control", "no-cache")])
        elif p == "/site/cache/post.html":
            self.send(200, b"<!DOCTYPE html><title>Post</title><form method=post action=/site/cache/fresh.html>"
                           b"<input name=n value=1><input type=submit name=go value=Go></form>")
        elif p == "/site/cache/count":
            self.send(200, ("fresh=%d etag=%d 304=%d" % (CACHE_COUNT["fresh"], CACHE_COUNT["etag"],
                                                        CACHE_COUNT["304"])).encode(), "text/plain",
                      extra=[("Cache-Control", "no-store")])
        elif p in ("/site/small.bin", "/site/small.zip"):
            self.send(200, SMALL, "application/octet-stream" if p.endswith(".bin") else "application/zip")
        elif p.startswith("/site/cookie/") and cookie_get(self, p):
            pass
        elif p.startswith("/site/echo"):
            self.send(200, echo_page("GET", "", p.partition("?")[2].encode()))
        elif p.startswith("/site/fresh/") and p[12:] in PICS:
            HITS[p[6:]] = HITS.get(p[6:], 0) + 1
            self.send(200, PICS[p[12:]][0], PICS[p[12:]][1], extra=[("Cache-Control", "max-age=3600")])
        elif p.startswith("/site/") and p[6:] in PICS:
            HITS[p[6:]] = HITS.get(p[6:], 0) + 1
            self.send(200, PICS[p[6:]][0], PICS[p[6:]][1])
        elif p.startswith("/site/hits/"):
            self.send(200, b"%d" % HITS.get(p[11:], 0), "text/plain")
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


def cookie_get(self, p):
    if p == "/site/cookie/set":
        self.send(200, b"<!DOCTYPE html><title>Set</title><p>Cookies set.", extra=[
            ("Set-Cookie", "lasting=1; Max-Age=3600; Path=/site/"),
            ("Set-Cookie", "session=2; Path=/"),
            ("Set-Cookie", "sec=3; Secure; Path=/")])
    elif p == "/site/cookie/show":
        c = self.headers.get("Cookie", "")
        self.send(200, ("<!DOCTYPE html><title>COOKIE|%s</title><p>What came." % c).encode())
    elif p == "/site/cookie/form.html":
        self.send(200, b"<!DOCTYPE html><title>Log in</title><form method=post action=/site/cookie/login>"
                       b"<input name=user value=erez><input type=submit name=go value=Go></form>")
    elif p == "/site/cookie/third.html":
        port = self.server.server_address[1]
        self.send(200, ("<!DOCTYPE html><title>Third</title><p><img src='http://127.0.0.1:%d/site/cookie/px.gif'"
                        " alt=px>" % port).encode())
    elif p == "/site/cookie/px.gif":
        self.send(200, PICS["anim.gif"][0], "image/gif", extra=[("Set-Cookie", "tp=1; Path=/")])
    else:
        return False
    return True


def _post(self):
    n = int(self.headers.get("Content-Length", "0"))
    body = self.rfile.read(n) if n else b""
    if self.path == "/site/echo":
        self.send(200, echo_page("POST", self.headers.get("Content-Type", ""), body))
    elif self.path == "/site/cookie/login":
        self.send(303, b"", extra=[("Location", "/site/cookie/show"),
                                   ("Set-Cookie", "sid=xyz; Path=/; HttpOnly; SameSite=Lax")])
    elif self.path == "/site/cache/fresh.html":
        self.send(200, b"<!DOCTYPE html><title>posted</title><p>It changed.")
    elif self.path == "/site/after":
        # Post/Redirect/Get: the answer is somewhere else, fetched with GET
        self.send(303, b"", extra=[("Location", "/site/echo?after=1")])
    else:
        self.send(404, b"not here\n", "text/plain")


Handler.do_POST = _post


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        # the Atari closed the connection early (Esc, another page):
        # nothing to report
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


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
