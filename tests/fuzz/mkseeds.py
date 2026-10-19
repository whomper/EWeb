#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes tests/fuzz/seeds/: the starting inputs for make fuzz. For
fuzz_http the first byte picks the request (see fuzz_http.c), the rest is
what the server says; for fuzz_url, an href, a newline and a base; for
fuzz_cookie, Set-Cookie values a line each, the first byte picking the
address it came from (see fuzz_cookie.c); for fuzz_html, a page, the first
byte picking its charset, scripting and a fragment's context (see
fuzz_html.c); for fuzz_bookmark, a bookmark file as browsers export them.

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

for name, lines in [
    ("plain", ["aa=1", "bb=2; Max-Age=60; Path=/"]),
    ("attributes", ["xsid=abc; Domain=example.co.il; Secure; HttpOnly; SameSite=Lax; "
                    "Expires=Wed, 21 Oct 2037 07:28:00 GMT"]),
    ("prefixes", ["c__Host-x=1; Secure; Path=/", "\x03y=2; Domain=example.com; Path=/deep",
                  "b__Secure-z=3; Secure; Max-Age=99999999999"]),
    ("dates", ["ad=1; expires=Sunday, 06-Nov-94 08:49:37 GMT", "ae=2; Expires=Tue Nov 15 08:12:31 2044",
               "af=3; Expires=31-Feb-2030 00:00:00", "ag=4; Expires=Thu, 31 Dec 9999 23:59:59 GMT"]),
    ("odd", ["a=", "a", "=b", " a = 1 ; ; = ; Path ; Domain=", "bq=1; Domain=.EXAMPLE.co.il; Max-Age=-1"]),
]:
    put("cookie", name, "\n".join(lines) + "\n")

# pages: each part of the parser at least once (the first byte: charset,
# scripting, fragment context)
for name, first, page in [
    ("doc", 0, "<!DOCTYPE html><html lang=en><head><meta charset=utf-8><title>T &amp; t</title>"
     "<style>p{color:red}</style><script>if (a < b && c) x('</scr' + 'ipt>');<!-- <script>"
     "</script> --></script></head><body class='a b' id=x><p>One<p>Two &notin; &#x41;&#128;&#0;"
     "&nbsp&lt=<b><i>bold</b>italic</i><a href=\"/x?a=1&b=2\">link<div>block</a></div>"),
    ("tables", 3, "<table><caption>c<tr><td>1<td>2<tr><th>h</table>x<table><tbody><tr>"
     "<td><table><td>in</table></td>loose<form><input type=hidden></table><b>after"),
    ("forms", 4, "<form action=/f><select name=s><optgroup label=g><option selected>A"
     "<option>B</select><textarea>\nline</textarea><button><selectedcontent></button>"
     "<input name=q value='&quot;'><fieldset><legend>L</legend><label>x<input type=checkbox>"
     "</label></form><form><form>"),
    ("foreign", 0x08, "<svg viewbox='0 0 1 1'><foreignObject><p>html</p></foreignObject>"
     "<path xlink:href=#a /><![CDATA[x]]></svg><math><mi>x</mi><annotation-xml encoding="
     "text/html><div>d</div></annotation-xml></math><svg><desc><b>x</b></desc><font color=1>"),
    ("template", 0x50, "<template><tr><td>t</td></tr></template><td>cell<col><frameset>"),
    ("rawtext", 0x70, "<xmp><b></xmp><plaintext><x></plaintext>&amp;"),
    ("misc", 1, "<?xml version=1?><?pi data?><!--c--!><!DOCTYPE html PUBLIC \"-//W3C//DTD "
     "HTML 4.01 Transitional//EN\"><noscript><b>n</b></noscript><frameset><frame>"
     "<noframes>x</noframes></frameset><!-- end"),
    ("hebrew", 6, "<html dir=rtl><body>\xf9\xec\xe5\xed <b>\xf2\xe5\xec\xed</b> 123\r\n"
     "\r<pre>\r\nx</pre>"),
    ("utf16", 5, "<\0p\0>\0h\0\x01\xd8\x37\xdc"),
    ("broken", 0, "<p>\xff\xc3(\xe2\x82\xf0\x9f\x98<a <b\0c=\"\0\" d='' e=f\0g> "
     "</b x=1/> &# &#x; &#99999999999; &unknown; &amp"),
]:
    put("html", name, bytes([first]) + page.encode("latin-1"))

# bookmark files: Netscape's, as Firefox and Chrome export it, and odd ones
for name, data in [
    ("firefox", "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; "
     "charset=UTF-8\">\n<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks Menu</H1>\n<DL><p>\n    <DT><H3 ADD_DATE=\"1\">"
     "Atari</H3>\n    <DL><p>\n        <DT><A HREF=\"https://en.wikipedia.org/wiki/Atari\" ADD_DATE=\"2\" "
     "ICON=\"data:image/png;base64,iVBOR\">Atari &amp; more</A>\n    </DL><p>\n    <DT><A HREF=\"place:x\">"
     "q</A>\n</DL><p>\n"),
    ("chrome", "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL><p>\n<DT><A HREF='http://68k.news/?a=1&amp;b=2' "
     "add_date=1>68k &#x5e9;&#1500; <b>x</b>\n news</a>\n<DT><A HREF=javascript:x()>js</A>\n</DL>"),
    ("odd", "<a href=\"\">e</a><a  href = 'x' >y</a><A NAME=n>no</A><a href=\"u\"><a href=v>w</a>"
     "<a\thref=\"t\">&#0;&#xffffff;&#55296;&quot</a><a href=\"z"),
]:
    put("bookmark", name, data)
