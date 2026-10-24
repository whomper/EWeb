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
  /site/media/page.html  stylesheets by their media, type and rel: one
                 for print, one for a wide screen, one for a narrow one, an
                 alternate, a disabled, one of another type, an empty href
                 and one linked in the body; pics.html the wide and narrow
                 ones with a picture that comes after 0.3 s (pic.png: a
                 window sized while it comes); count (how often each was
                 asked for)
  /site/imp/page.html  @import (CSS Cascade 4, 2): a linked sheet in
                 css/ that imports parts/one.css (which imports ../../two.css
                 and has a url(tile.png) of its folder's), a sheet for print
                 (never asked for) and one for a wide screen; a <style>
                 that imports inline.css from the page's folder; count
                 (how often each was asked for)
  /site/bg.html  pictures from CSS and srcset: a stylesheet in
                 /site/css/ whose url(tile.png) is its folder's, and a
                 <picture> whose WebP source is passed over and whose img's
                 srcset gives logo.png for its 150px
  /site/webp.html  a WebP picture (pic.webp), one of more than a megapixel
                 (big.webp) and a GIF: EWEB.PRG on its own decodes the
                 first and leaves the big one out, the ST edition fetches
                 neither; through webgate they come as JPEG;
                 /site/hits/NAME: how many times a picture was asked for
  /site/wiki.html  a page as a Wikipedia article's: an infobox photo from a
                 srcset, icons, an SVG, a 1x1 counter, a floated thumbnail
  /site/wikitls.html  the same, secure, its pictures from another secure
                 server (the ECDSA one), as Wikipedia's from upload.wikimedia.org
  /site/gwpics.html  a page as superbooth.com's: big photos (bigphoto.jpg,
                 portrait.jpg, banner.jpg) held to their columns, for the Pi
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
  /site/dl/      downloads' names: report (text/plain that says
                 attachment, its filename with a space; kept an hour, which
                 the browser mustn't do), euro (filename and filename*),
                 files.tar.gz (application/x-gzip with Content-Encoding
                 x-gzip, always, as Apache sends one), backup (octet-stream,
                 Content-Encoding gzip, named backup.tgz), notes.html.gz (a
                 page, packed: shown), inline (a text file named but shown),
                 denied (a 403 that says attachment: shown), bad (a header
                 that isn't RFC 6266's: shown), apache.tar.gz (a .tar.gz as
                 Apache sends one: application/x-tar with Content-Encoding
                 x-gzip, no Content-Disposition), inline.html (a page with
                 "inline", kept an hour), manual (a PDF named by "inline",
                 kept an hour, which the browser mustn't do), held.bin (4
                 MB, sent as nginx and Apache send a file: the connection
                 ended when a piece can't go within a second, as their
                 send timeouts end it after 60 s); links.html,
                 <a download> links to the page's own site and to the other
                 name of the same server (localhost and 127.0.0.1), and
                 buttons whose scripts click two of them (a.click());
                 starts.html, a page whose script clicks its <a download> as
                 it loads ("your download starts now"); count (how often
                 each was asked for)
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
  /site/enc/     pages that name no encoding (Content-Type without a
                 charset, no <meta>): utf8.html in UTF-8, w1255.html in
                 windows-1255 with a link and a form whose queries are
                 Hebrew (its own when fetched as hebrew.il, ewrender's
                 EW_HOSTS), plain.txt a text file in UTF-8; addr.html, in
                 windows-1255, whose addresses have Hebrew in their query:
                 two pictures (one's query the bytes 0xCA and 0x81, which
                 the decoder once left out), a sheet (a/s.css, with a
                 url()), a <style>'s and a style attribute's url(), a
                 script's fetch() and XMLHttpRequest; seen: the queries
                 under /site/enc/a/ as they came (never kept)
  /site/refresh/ a refresh: meta.html (a <meta http-equiv=refresh>),
                 header.html (the Refresh header, before the page's meta),
                 loop.html (itself at once, again and again), based.html
                 and after.html (a <base> before the meta and after it),
                 noscript.html, js.html (a javascript: address, then
                 another), later.html (in a minute), selfcss.html (itself,
                 with a stylesheet kept an hour), frag.html (a place on
                 itself), jsset.html (a script changes the content),
                 foster.html (a table puts the second meta first),
                 slowpic.html (itself in 2 s, its picture 1.5 s coming),
                 slow.html (a long page sent in two parts 2.5 s apart,
                 gzip when asked: a page drawn while it loads),
                 cam.html (itself every second: cam.png no-store, another
                 each time; still.png kept an hour; valid.png no-cache, a
                 304 when unchanged), campic.png (a picture on its own,
                 itself every second by its Refresh header, another each
                 time);
                 to.html's title is "Arrived|QUERY|REFERER"; count (how
                 often each was asked for)
It prints "ready" when the servers listen."""
import argparse
import gzip
import http.server
import os
import random
import socket
import socketserver
import ssl
import subprocess
import sys
import threading
import time
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
# downloads (/site/dl/): what each sends, and the page of links to them
REPORT = b"Takings, 2026\r\n" * 40
TARGZ = gzip.compress(SMALL, mtime=0)
NOTES = b"<!DOCTYPE html><title>Notes, packed</title><p>A page sent packed, its name ending in .gz."
DL_PAGE = ("<!DOCTYPE html><title>Downloads</title><p><a id=named href=/site/small.bin download='My File.bin'>"
           "named</a> <a id=plain href=/site/two.html download>plain</a> <a id=cd href=/site/dl/report "
           "download=ignored.txt>cd</a> <a id=other href=http://OTHER/site/small.zip download=x.zip>other</a> "
           "<a id=othercd href=http://OTHER/site/dl/report download=y.txt>othercd</a> "
           "<a id=tgz href=/site/dl/files.tar.gz>tgz</a> <a id=held href=/site/dl/held.bin>held</a> "
           "<button id=jsnamed onclick=\"document.getElementById('named').click()\">jsnamed</button> "
           "<button id=jsother onclick=\"document.getElementById('other').click()\">jsother</button>")
DL_STARTS = (b"<!DOCTYPE html><title>Your download</title><p>Your download starts now. If it doesn't, "
             b"<a id=again href=/site/small.bin download=got.bin>save it</a>."
             b"<script>document.getElementById('again').click()</script>")
DL = {
    "report": (200, REPORT, "text/plain", [("Content-Disposition", 'attachment; filename="Report 2026.txt"'),
                                           ("Cache-Control", "max-age=3600")]),
    "euro": (200, SMALL, "application/octet-stream",
             [("Content-Disposition", "attachment; filename=\"EURO rates.pdf\"; "
                                      "filename*=UTF-8''%E2%82%AC%20rates.pdf")]),
    "files.tar.gz": (200, TARGZ, "application/x-gzip", [("Content-Encoding", "x-gzip")]),
    "backup": (200, TARGZ, "application/octet-stream", [("Content-Encoding", "gzip"),
                                                        ("Content-Disposition", "attachment; filename=backup.tgz")]),
    "notes.html.gz": (200, gzip.compress(NOTES, mtime=0), "text/html; charset=utf-8", [("Content-Encoding", "gzip")]),
    "inline": (200, REPORT, "text/plain", [("Content-Disposition", 'inline; filename="notes.txt"')]),
    "denied": (403, b"<!DOCTYPE html><title>Denied</title><p>Not for you.", "text/html",
               [("Content-Disposition", "attachment; filename=secret.txt")]),
    "bad": (200, REPORT, "text/plain", [("Content-Disposition", "attachment; filename=foo bar.txt")]),
    "apache.tar.gz": (200, TARGZ, "application/x-tar", [("Content-Encoding", "x-gzip")]),
    "inline.html": (200, b"<!DOCTYPE html><title>Inline</title><p>Shown, and kept.", "text/html",
                    [("Content-Disposition", "inline"), ("Cache-Control", "max-age=3600")]),
    "manual": (200, SMALL, "application/pdf", [("Content-Disposition", 'inline; filename="Manual.pdf"'),
                                               ("Cache-Control", "max-age=3600")]),
}
DL_COUNT = {}
HELD = bytes(range(256)) * 16384
CACHE_COUNT = {"fresh": 0, "etag": 0, "304": 0}
# a refresh (<meta http-equiv=refresh>, the Refresh header): each page and
# the headers it comes with; /site/refresh/to.html's title says what came
# ("Arrived|QUERY|REFERER"), /site/refresh/count how often each was asked for
REFRESH = {
    "meta.html": (b"<!DOCTYPE html><title>Going on</title>"
                  b"<meta http-equiv=\"Refresh\" content=\"0; URL='to.html?from=meta'\"><p>On to the next page.", ()),
    # the header comes before any meta (it is kept an hour: not from the cache)
    "header.html": (b"<!DOCTYPE html><title>Header</title><meta http-equiv=refresh content='0; url=wrong.html'>"
                    b"<p>The header goes first.", (("Refresh", "1; url=to.html?from=header"),
                                                   ("Cache-Control", "max-age=3600"))),
    "loop.html": (b"<!DOCTYPE html><title>Loop</title><meta http-equiv=refresh content=0><p>Again.", ()),
    # resolved against the base the page had when the meta was put in
    "based.html": (b"<!DOCTYPE html><title>Based</title><base href=/site/refresh/sub/>"
                   b"<meta http-equiv=refresh content='0;url=to.html?b=before'>", ()),
    "after.html": (b"<!DOCTYPE html><title>After</title><meta http-equiv=refresh content='0;url=to.html?b=after'>"
                   b"<base href=/site/refresh/sub/>", ()),
    # a <noscript>'s, only when scripts don't run
    "noscript.html": (b"<!DOCTYPE html><title>Noscript</title><noscript>"
                      b"<meta http-equiv=refresh content='0; url=to.html?from=noscript'></noscript>", ()),
    # javascript: isn't followed, so the next one is
    "js.html": (b"<!DOCTYPE html><title>Script address</title>"
                b"<meta http-equiv=refresh content='0; url=javascript:alert(1)'>"
                b"<meta http-equiv=refresh content='0; url=to.html?from=second'>", ()),
    "later.html": (b"<!DOCTYPE html><title>Later</title><meta http-equiv=refresh content='60; url=to.html'>", ()),
    # itself again: the page asked for anew, its stylesheet as any page's
    # (kept an hour, so asked for once)
    "selfcss.html": (b"<!DOCTYPE html><title>Itself</title><link rel=stylesheet href=s.css>"
                     b"<meta http-equiv=refresh content=0><p>Again.", ()),
    "s.css": (b"p { color: #800 }\n", (("Cache-Control", "max-age=3600"), ("ETag", '"s1"'))),
    # a place on the page itself: scrolled to, not loaded again
    "frag.html": (b"<!DOCTYPE html><title>Place</title><meta http-equiv=refresh content='0; url=#x'>"
                  b"<div style='height: 2000px'></div><p id=x>Here.", ()),
    # a script changes the content after the parser put the meta in: as it was
    "jsset.html": (b"<!DOCTYPE html><title>Script set</title>"
                   b"<meta id=m http-equiv=refresh content='0; url=to.html?from=orig'><script>"
                   b"document.getElementById('m').setAttribute('content', '0; url=javascript:void 0')</script>", ()),
    # the first put in is first, though a table puts the second before itself
    "foster.html": (b"<!DOCTYPE html><title>Foster</title><table><tr><td>"
                    b"<meta http-equiv=refresh content='0; url=javascript:x'></td></tr>"
                    b"<meta http-equiv=refresh content='0; url=to.html?from=foster'></table>", ()),
    # itself in 2 s, counted from when its picture is in (slow.png: 1.5 s)
    # itself every second, its pictures asked for again as their headers
    # say: a camera's (no-store, another each time), one kept an hour, one
    # asked about each time (a 304)
    "cam.html": (b"<!DOCTYPE html><title>Camera</title><meta http-equiv=refresh content=1><p>"
                 b"<img src=cam.png width=8 height=8 alt=cam> <img src=still.png width=8 height=8 alt=still> "
                 b"<img src=valid.png width=8 height=8 alt=valid>", ()),
    "slowpic.html": (b"<!DOCTYPE html><title>Slow</title><meta http-equiv=refresh content=2>"
                     b"<p><img src=slow.png width=16 height=16 alt=slow>", ()),
}
REFRESH_COUNT = {}
SITE_CSS = b"body { background: #ffeedd } h1 { color: #aa0000 }\n"
# a long page, sent in two parts 2.5 s apart: EWeb shows its start before
# it has all come (its preview, with the site's sheet it already has)
SLOW_PAGE = (b"<!DOCTYPE html><html><head><title>Slow</title><link rel=stylesheet href=style.css></head>"
             b"<body><h1>A slow page</h1>" +
             b"".join(b"<p>Paragraph %d of a page that comes slowly: the first part, then the rest "
                      b"two and a half seconds later.</p>" % i for i in range(600)) +
             b"</body></html>")
# a stylesheet's media (the HTML Standard's 4.2.4): only the sheets that
# hold for the screen are asked for, and none that isn't to be applied
MEDIA_PAGE = (b"<!DOCTYPE html><title>Media</title><link rel=stylesheet media=print href=print.css>"
              b"<link rel=stylesheet media='screen and (min-width: 600px)' href=wide.css>"
              b"<link rel=stylesheet media='only screen and (max-width: 600px)' href=narrow.css>"
              b"<link rel='alternate stylesheet' title=Alt href=alt.css><link rel=stylesheet disabled href=off.css>"
              b"<link rel=stylesheet type=text/plain href=plain.css><link rel=stylesheet href=''>"
              b"<p>Sheets by their media.<link rel=stylesheet href=body.css>")
MEDIA_CSS = {"print.css": b"p { color: #ff0000 }", "wide.css": b"p { color: #0000aa }",
             "narrow.css": b"p { color: #00aa00 }", "alt.css": b"p { color: #ff0000 }",
             "off.css": b"p { color: #ff0000 }", "plain.css": b"p { color: #ff0000 }",
             "body.css": b"p { background: #eeeeee }"}
MEDIA_PICS = (b"<!DOCTYPE html><title>Media pics</title>"
              b"<link rel=stylesheet media='screen and (min-width: 600px)' href=wide.css>"
              b"<link rel=stylesheet media='only screen and (max-width: 600px)' href=narrow.css>"
              b"<p>Sheets by their media, a picture.<p><img src=pic.png alt=pic>")
MEDIA_COUNT = {}
# @import (CSS Cascade 4, 2): each sheet's address against the one that
# imports it, print's never asked for, the wide screen's at 608 px only
IMP_PAGE = (b"<!DOCTYPE html><title>Imports</title><link rel=stylesheet href=css/main.css>"
            b"<style>@import 'inline.css';</style>"
            b"<p id=a>Main</p><p id=b>One</p><p id=c>Two</p><p id=d>Inline</p><p id=f>Wide</p>"
            b"<div id=e style='height: 20px'></div>")
IMP_CSS = {"css/main.css": b"@charset \"utf-8\";\n@import \"parts/one.css\";\n"
                           b"@import url(parts/print.css) print;\n"
                           b"@import url('parts/wide.css') screen and (min-width: 600px);\n#a { color: #0000aa }\n"
                           b"@import 'late.css';\n",
           "css/parts/one.css": b"@import '../../two.css';\n#b { color: #00aa00 }\n"
                                b"#e { background: url(tile.png) }\n",
           "css/parts/print.css": b"p { color: #ff0000 }", "css/parts/wide.css": b"#f { color: #00aaaa }",
           "css/late.css": b"p { color: #ff0000 }",
           "two.css": b"#c { color: #aa0000 }", "inline.css": b"#d { color: #aa00aa }"}
IMP_COUNT = {}
# Google's search as a browser without its check meets it (2025): a /search
# whose <noscript> goes on to /httpservice/retry/enablejs, that page, and
# /sorry/, its CAPTCHA; with pass=1 the results. make itest asks for them
# as www.google.com (EW_HOSTS), and EWeb shows its own page in their place
GOOGLE_WALL = (b'<!DOCTYPE html><html lang="en"><head><title>Google Search</title>'
               b'<script nonce="n">window.google={kEI:"k"};</script></head><body><noscript>'
               b'<style>table,div,span,p{display:none}</style>'
               b'<meta content="0;url=/httpservice/retry/enablejs?sei=k" http-equiv="refresh">'
               b'<div style="display:block">Please click <a href="/httpservice/retry/enablejs?sei=k">here</a>'
               b' if you are not redirected within a few seconds.</div></noscript>'
               b'<script nonce="n">(function(){var c=0;for(var i=0;i<10;i++)c+=i;})();</script></body></html>')
GOOGLE_RESULTS = b"<!DOCTYPE html><title>Results</title><p>Atari Falcon030, 1992</p>"
GOOGLE_ENABLEJS = b"<!DOCTYPE html><title>Google Search</title><p>Turn on JavaScript to keep searching</p>"
GOOGLE_SORRY = b"<!DOCTYPE html><title>Sorry</title><p>Our systems have detected unusual traffic</p>"
# Cloudflare's check as a browser without its JavaScript meets it (a
# managed challenge, 2025): a 403 with cf-mitigated: challenge, a <noscript>
# that asks for JavaScript and cookies and a <meta> refresh that brings it
# back; /site/cf/plain the same without the header (through a proxy that
# drops it), /site/cf/about a page that only mentions the check's options.
# Through the Pi (itest's stand-in for its browser), /site/cf/ and
# /site/cf/next are passed in the browser, /site/cf/click wants a click
CF_CHECK = (b'<!DOCTYPE html><html lang="en-US"><head><title>Just a moment...</title>'
            b'<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">'
            b'<meta name="robots" content="noindex,nofollow"><meta http-equiv="refresh" content="360"></head>'
            b'<body><div class="main-wrapper" role="main"><div class="main-content"><noscript><div class="h2">'
            b'<span id="challenge-error-text">Enable JavaScript and cookies to continue</span></div></noscript>'
            b'</div></div><script>(function(){window._cf_chl_opt={cvId: \'3\',cZone: "localhost",'
            b'cType: \'managed\'};var cpo = document.createElement(\'script\');'
            b'cpo.src = \'/cdn-cgi/challenge-platform/h/b/orchestrate/chl_page/v1?ray=1\';'
            b'location.reload();}());</script></body></html>')
CF_SEEN = []        # the check's pages asked for, in order (/site/cf/seen)
CF_ABOUT = (b"<!DOCTYPE html><title>About the check</title><p>Cloudflare's page sets "
            b"<code>window._cf_chl_opt</code>.</p>")
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


# pages that name no encoding (/site/enc/): the same Hebrew in UTF-8 and
# in windows-1255; the second's link and form send Hebrew in its query
ENC_PAGE = ("<!DOCTYPE html><html dir=rtl><head><title>שלום עולם</title></head><body>"
            "<p>אטארי פלקון <a href='/site/echo?q=שלום'>חיפוש</a></p>"
            "<form action=/site/echo><p><input name=q value=אבג> <input type=submit name=go value=חפש></p></form>"
            "</body></html>\n")
ENC_PLAIN = "שלום עולם\n<b>not bold</b> & café\n".encode("utf-8")
# a page in windows-1255 whose addresses have Hebrew in their query: in its
# encoding for the HTML's (a picture, a sheet, XMLHttpRequest), in UTF-8 for
# CSS's url()s and fetch() (the URL, CSS and Fetch standards)
ENC_ADDR = ("<!DOCTYPE html><html><head><title>addr</title><link rel=stylesheet href='a/s.css?q=א'>"
            "<style>.a{background:url(a/st.png?q=א)}</style></head><body>"
            "<img src='a/img.png?q=א' width=8 height=8 alt=''>"
            "<div class=a>a</div><div class=b>b</div><div style='background:url(a/inl.png?q=א)'>c</div>"
            "<script>fetch('a/f?q=א');var x=new XMLHttpRequest();x.open('GET','a/x?q=א');x.send()</script>"
            "</body></html>\n").encode("cp1255").replace(
                # and one with bytes its decoder once left out (U+05BA, a C1
                # control), sent as they are
                b"<div class=a>", b"<img src='a/pts.png?q=\xca\x81' width=8 height=8 alt=''><div class=a>")
ENC_CSS = ".b{background:url(ext.png?q=א)}\n".encode("utf-8")
ENC_SEEN = []


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


# a page as superbooth.com's: a column of text beside big photos held to
# their columns, a banner, a WebP; every picture wider than the page, so
# that the Pi's gateway makes it smaller (or the screen's pens)
GW_PAGE = (b"<!DOCTYPE html><html><head><title>Gateway pictures</title><style>"
           b"body{font-family:sans-serif;margin:0}.side{float:left;width:30%}.main{margin-left:32%}"
           b"img{max-width:100%;height:auto}</style></head><body>"
           b"<div class=side><h2>NEWS</h2><p>Instrument of the year. The synthesizer has been chosen.</p>"
           b"<img src=portrait.jpg width=1200 height=1600 alt=portrait></div>"
           b"<div class=main><h3>ROUNDUP</h3><img src=bigphoto.jpg width=2000 height=1500 alt=big>"
           b"<p><img src=banner.jpg alt=banner></p><p><img src=big.png alt=png> <img src=pic.webp alt=webp></p>"
           b"</div></body></html>\n")

# a page as a Wikipedia article's: an infobox photo from a srcset, small
# icons, an SVG (the Pi can't make it: refused), a 1x1 counter, a
# thumbnail floated beside the text, a PNG with alpha, an animated GIF
WIKI_PAGE = (b"<!DOCTYPE html><html><head><title>Atari Falcon - Wiki</title><style>"
             b".infobox{float:right;width:22em;border:1px solid #aaa;margin:0 0 1em 1em}"
             b".thumb{float:left;margin:0 1em 1em 0}img{vertical-align:middle}</style></head><body>"
             b"<h1>Atari Falcon</h1><table class=infobox><tr><th>Atari Falcon030</th></tr><tr><td>"
             b"<img src=portrait.jpg srcset='portrait.jpg 1.5x, bigphoto.jpg 2x' width=250 height=333 "
             b"alt=falcon></td></tr><tr><td><img src=logo.png width=20 height=8 alt=icon> Manufacturer: "
             b"Atari</td></tr></table><p><img src=anim.gif width=16 height=16 alt=gif> The <b>Atari Falcon030"
             b"</b> is a personal computer released by Atari in 1992.</p>"
             b"<div class=thumb><img src=banner.jpg width=220 height=69 alt=thumb><br>A thumbnail</div>"
             b"<p>Text beside the thumbnail. <img src=wiki.svg width=12 height=12 alt=svg> More text with an "
             b"SVG icon in it, and a <img src=photo.jpg width=60 height=40 alt=small> small photo.</p>"
             b"<p><img src=big.png alt=png style='max-width:100%;height:auto'></p>"
             b"<img src=counter.png width=1 height=1 alt=''></body></html>\n")

# the same as a secure page whose pictures come from another secure host
# (Wikipedia's from upload.wikimedia.org): https://127.0.0.1/site/wikitls.html,
# its pictures from the ECDSA server (an ECDSA certificate, as Wikipedia's:
# the DSP's elliptic curves for each of their connections)
WIKI_TLS_PAGE = (b"<!DOCTYPE html><html><head><title>Atari Falcon - Wiki TLS</title><style>"
                 b".infobox{float:right;width:22em}.thumb{float:left;margin:0 1em 1em 0}</style></head><body>"
                 b"<h1>Atari Falcon</h1><table class=infobox><tr><td><img src=https://localhost:PORT/site/portrait.jpg "
                 b"srcset='https://localhost:PORT/site/portrait.jpg 1.5x, https://localhost:PORT/site/bigphoto.jpg 2x' "
                 b"width=250 height=333 alt=falcon></td></tr></table>"
                 b"<p><img src=https://localhost:PORT/site/anim.gif width=16 height=16 alt=gif> The Falcon030.</p>"
                 b"<div class=thumb><img src=https://localhost:PORT/site/banner.jpg width=220 height=69 alt=t></div>"
                 b"<p>Text. <img src=https://localhost:PORT/site/wiki.svg width=12 height=12 alt=svg> "
                 b"<img src=https://localhost:PORT/site/photo.jpg width=120 height=80 alt=small> "
                 b"<img src=https://localhost:PORT/site/prog.jpg width=160 alt=prog></p>"
                 b"<p><img src=https://localhost:PORT/site/big.png style='max-width:100%;height:auto' alt=png> "
                 b"<img src=https://localhost:PORT/site/logo.png alt=logo></p>" + b"<p>More text.</p>" * 40 +
                 b"<img src=https://localhost:PORT/site/bigphoto.jpg width=400 height=300 alt=end></body></html>\n")

BG_PAGE = (b"<!DOCTYPE html><html><head><title>Backgrounds</title><link rel=stylesheet href=css/bg.css>"
           b"</head><body><div class=box>A box tiled from its sheet's folder</div>"
           b"<p><picture><source type=image/webp srcset=no.webp><img src=gone.png "
           b"srcset='logo.png 150w, big.png 800w' sizes='(max-width: 10px) 800px, 150px' alt=chosen>"
           b"</picture></p></body></html>\n")
WEBP_PAGE = (b"<!DOCTYPE html><html><head><title>WebP</title></head><body>"
             b"<p><img src=pic.webp alt=webp> and <img src=anim.gif alt=gif></p>"
             b"<p><img src=big.webp alt=bigwebp></p></body></html>\n")
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
    def camera(w, h, seed):
        """a photo as a camera makes one: shapes and fine noise, big"""
        im = photo(w // 8, h // 8).resize((w, h), Image.BICUBIC)
        noise = Image.effect_noise((w, h), 40 + seed).convert("RGB")
        return Image.blend(im, noise, 0.25)
    return {
        "bigphoto.jpg": (save(camera(2000, 1500, 0), "JPEG", quality=88), "image/jpeg"),
        "portrait.jpg": (save(camera(1200, 1600, 7), "JPEG", quality=88, subsampling=0), "image/jpeg"),
        "banner.jpg": (save(camera(1600, 500, 3), "JPEG", quality=85, progressive=True), "image/jpeg"),
        "photo.jpg": (save(photo(480, 320), "JPEG", quality=85), "image/jpeg"),
        "prog.jpg": (save(photo(320, 240), "JPEG", quality=85, progressive=True), "image/jpeg"),
        "logo.png": (save(logo, "PNG"), "image/png"),
        "anim.gif": (gif.getvalue(), "image/gif"),
        "big.png": (save(photo(800, 600), "PNG"), "image/png"),
        "css/tile.png": (save(Image.new("RGB", (8, 8), (90, 90, 200)), "PNG"), "image/png"),
        "pic.webp": (save(photo(320, 200), "WEBP"), "image/webp"),
        "big.webp": (save(photo(1400, 800), "WEBP"), "image/webp"),
        "wiki.svg": (b'<svg xmlns="http://www.w3.org/2000/svg" width="12" height="12">'
                     b'<circle cx="6" cy="6" r="5" fill="#36c"/></svg>', "image/svg+xml"),
        "counter.png": (save(Image.new("RGBA", (1, 1), (0, 0, 0, 0)), "PNG"), "image/png"),
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
    ecdsa_port = 0

    def log_message(self, fmt, *args):
        pass

    def send(self, status, body, ctype="text/html; charset=utf-8", extra=(), close=False, pause=0):
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
            data = packed if packed is not None else body
            if pause:
                # (a slow page: a part, then the rest a while later)
                self.wfile.write(data[:len(data) * 2 // 5])
                self.wfile.flush()
                time.sleep(pause)
                data = data[len(data) * 2 // 5:]
            self.wfile.write(data)

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
        elif p == "/site/slow.html":
            self.send(200, SLOW_PAGE, pause=2.5)
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
        elif p == "/site/gwpics.html":
            self.send(200, GW_PAGE)
        elif p == "/site/wiki.html":
            self.send(200, WIKI_PAGE)
        elif p == "/site/wikitls.html":
            self.send(200, WIKI_TLS_PAGE.replace(b"https://localhost:PORT", b"https://127.0.0.1:%d" % self.ecdsa_port))
        elif p.startswith("/site/media/"):
            name = p[12:]
            MEDIA_COUNT[name] = MEDIA_COUNT.get(name, 0) + 1
            if name == "page.html":
                self.send(200, MEDIA_PAGE)
            elif name == "pics.html":
                self.send(200, MEDIA_PICS)
            elif name == "pic.png":
                time.sleep(0.3)
                self.send(200, PICS["logo.png"][0], "image/png")
            elif name in MEDIA_CSS:
                self.send(200, MEDIA_CSS[name], "text/css")
            elif name == "count":
                self.send(200, " ".join("%s=%d" % kv for kv in sorted(MEDIA_COUNT.items())
                                        if kv[0] != "count").encode(), "text/plain",
                          extra=[("Cache-Control", "no-store")])
            else:
                self.send(404, b"not here\n", "text/plain")
        elif p.startswith("/site/imp/"):
            name = p[10:]
            IMP_COUNT[name] = IMP_COUNT.get(name, 0) + 1
            if name == "page.html":
                self.send(200, IMP_PAGE)
            elif name in IMP_CSS:
                self.send(200, IMP_CSS[name], "text/css")
            elif name == "css/parts/tile.png":
                self.send(200, PICS["logo.png"][0], "image/png")
            elif name == "count":
                self.send(200, " ".join("%s=%d" % kv for kv in sorted(IMP_COUNT.items())
                                        if kv[0] != "count").encode(), "text/plain",
                          extra=[("Cache-Control", "no-store")])
            else:
                self.send(404, b"not here\n", "text/plain")
        elif p == "/site/css/bg.css":
            self.send(200, BG_CSS, "text/css")
        elif p in ("/site/enc/utf8.html", "/site/enc/w1255.html"):
            self.send(200, ENC_PAGE.encode("utf-8" if "utf8" in p else "cp1255"), "text/html")
        elif p == "/site/enc/plain.txt":
            self.send(200, ENC_PLAIN, "text/plain")
        elif p == "/site/enc/addr.html":
            self.send(200, ENC_ADDR, "text/html; charset=windows-1255", extra=[("Cache-Control", "no-store")])
        elif p.startswith("/site/enc/a/"):
            ENC_SEEN.append(p[12:])
            name = p[12:].partition("?")[0]
            body, ctype = ((ENC_CSS, "text/css; charset=utf-8") if name == "s.css" else
                           (PICS["counter.png"][0], "image/png") if name.endswith(".png") else (b"ok", "text/plain"))
            self.send(200, body, ctype, extra=[("Cache-Control", "no-store")])
        elif p == "/site/enc/seen":
            self.send(200, " ".join(sorted(ENC_SEEN)).encode(), "text/plain", extra=[("Cache-Control", "no-store")])
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
        elif p.startswith("/site/refresh/"):
            name, _, query = p[14:].partition("?")
            REFRESH_COUNT[name] = REFRESH_COUNT.get(name, 0) + 1
            if name == "s.css" and self.headers.get("If-None-Match") == '"s1"':
                self.send(304, b"", "text/css", extra=REFRESH[name][1])
            elif name in REFRESH:
                self.send(200, REFRESH[name][0], "text/css" if name.endswith(".css") else "text/html; charset=utf-8",
                          extra=REFRESH[name][1])
            elif name == "cam.png":
                self.send(200, PICS["logo.png" if REFRESH_COUNT[name] % 2 else "counter.png"][0], "image/png",
                          extra=[("Cache-Control", "no-store")])
            elif name == "campic.png":
                self.send(200, PICS["logo.png" if REFRESH_COUNT[name] % 2 else "counter.png"][0], "image/png",
                          extra=[("Cache-Control", "no-store"), ("Refresh", "1")])
            elif name == "still.png":
                self.send(200, PICS["logo.png"][0], "image/png", extra=[("Cache-Control", "max-age=3600")])
            elif name == "valid.png":
                v = [("Cache-Control", "no-cache"), ("ETag", '"v1"')]
                if self.headers.get("If-None-Match") == '"v1"':
                    self.send(304, b"", "image/png", extra=v)
                else:
                    self.send(200, PICS["counter.png"][0], "image/png", extra=v)
            elif name == "slow.png":
                time.sleep(1.5)
                self.send(200, PICS["logo.png"][0], "image/png", extra=[("Cache-Control", "max-age=3600")])
            elif name in ("to.html", "sub/to.html"):
                self.send(200, ("<!DOCTYPE html><title>%s|%s|%s</title><p>Here." % (
                    "Arrived" if name == "to.html" else "Sub", query, self.headers.get("Referer", ""))).encode())
            elif name == "count":
                self.send(200, " ".join("%s=%d" % kv for kv in sorted(REFRESH_COUNT.items())).encode(),
                          "text/plain", extra=[("Cache-Control", "no-store")])
            else:
                self.send(404, b"not here\n", "text/plain")
        elif p == "/site/dl/links.html":
            host, _, port = self.headers.get("Host", "localhost").rpartition(":")
            other = ("127.0.0.1" if host == "localhost" else "localhost") + ":" + port
            self.send(200, DL_PAGE.replace("OTHER", other).encode())
        elif p == "/site/dl/starts.html":
            self.send(200, DL_STARTS)
        elif p == "/site/dl/held.bin":
            # a piece that can't go within a second (the client reads
            # nothing) ends the connection, as a server's send timeout does
            DL_COUNT["held.bin"] = DL_COUNT.get("held.bin", 0) + 1
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(HELD)))
            self.end_headers()
            self.close_connection = True
            if self.command != "HEAD":
                self.connection.settimeout(1)
                try:
                    for k in range(0, len(HELD), 65536):
                        self.connection.sendall(HELD[k:k + 65536])
                except OSError:
                    pass
        elif p == "/site/dl/count":
            self.send(200, " ".join("%s=%d" % kv for kv in sorted(DL_COUNT.items())).encode(), "text/plain",
                      extra=[("Cache-Control", "no-store")])
        elif p.startswith("/site/dl/") and p[9:] in DL:
            # as they are, whatever was asked for (send() packs only text/)
            DL_COUNT[p[9:]] = DL_COUNT.get(p[9:], 0) + 1
            status, body, ctype, extra = DL[p[9:]]
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            for k, v in extra:
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
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
        elif p in ("/site/cf/", "/site/cf/next", "/site/cf/click"):
            CF_SEEN.append(p)
            self.send(403, CF_CHECK, extra=[("cf-mitigated", "challenge"), ("Cache-Control", "no-store")])
        elif p == "/site/cf/seen":
            self.send(200, " ".join(CF_SEEN).encode(), "text/plain")
        elif p == "/site/cf/plain":
            self.send(503, CF_CHECK)
        elif p == "/site/cf/about":
            self.send(200, CF_ABOUT)
        elif p.startswith("/search?"):
            self.send(200, GOOGLE_RESULTS if "pass=1" in p else GOOGLE_WALL)
        elif p.startswith("/httpservice/retry/enablejs"):
            self.send(200, GOOGLE_ENABLEJS)
        elif p.startswith("/sorry/"):
            self.send(429, GOOGLE_SORRY)
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
    Handler.ecdsa_port = a.ecdsa
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
