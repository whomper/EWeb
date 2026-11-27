#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""safari-cookies.py: a Mac's Safari cookies for the sites named, as a
cookies.txt (Netscape's form) for EWeb's File > Import cookies... (decided
27 Nov 2026; docs/GUIDE.md, Cookies from a Mac's Safari). Python 3's
standard library only.

    python3 safari-cookies.py [-o FILE] [--from COOKIES.BINARYCOOKIES] SITE...

SITE is a host, e.g. atariage.com: its cookies and its subdomains' are
taken, and a parent domain's cookies that it would be sent (a cookie for
.atariage.com for forums.atariage.com). With no site it takes nothing: there
is no "all". Expired cookies are left out; Safari's session-only cookies
aren't in its file. The file written is safari-cookies.txt unless -o names
another, never cookies.txt, so that it can't land over EWeb's own store. It
holds logins in clear text: import it, then let EWeb delete it.

Safari's file is Cookies.binarycookies: "cook", the number of pages and
their sizes (big-endian), then each page (little-endian inside): 0x00000100,
its cookies' count and offsets, each cookie its size, flags (1 Secure, 4
HttpOnly), the offsets of its domain, name, path and value, its expiry and
creation as seconds from 1 January 2001, and its strings. A Terminal needs
Full Disk Access to read it (System Settings > Privacy & Security > Full
Disk Access).
"""
import argparse
import os
import struct
import sys
import time

MAC_EPOCH = 978307200		# 2001-01-01 00:00:00 UTC in Unix time
FLAG_SECURE, FLAG_HTTPONLY = 1, 4
PLACES = ("~/Library/Containers/com.apple.Safari/Data/Library/Cookies/Cookies.binarycookies",
          "~/Library/Cookies/Cookies.binarycookies")


class Cookie:
    def __init__(self, domain, name, path, value, expires, secure, httponly):
        self.domain, self.name, self.path, self.value = domain, name, path, value
        self.expires, self.secure, self.httponly = expires, secure, httponly


def _cstr(page, at):
    end = page.index(b"\0", at)
    return page[at:end].decode("utf-8", "replace")


def parse(data):
    """the cookies of a Cookies.binarycookies file's bytes"""
    if data[:4] != b"cook":
        raise ValueError("not Safari's cookie file (no 'cook' at its start)")
    n = struct.unpack(">i", data[4:8])[0]
    sizes = struct.unpack(">%di" % n, data[8:8 + 4 * n])
    at = 8 + 4 * n
    out = []
    for size in sizes:
        page = data[at:at + size]
        at += size
        if page[:4] != b"\0\0\x01\0":
            raise ValueError("a page with no page header")
        count = struct.unpack("<i", page[4:8])[0]
        offsets = struct.unpack("<%di" % count, page[8:8 + 4 * count])
        for off in offsets:
            c = page[off:]
            csize, _, flags, _, d_off, n_off, p_off, v_off = struct.unpack("<8i", c[:32])
            expiry = struct.unpack("<d", c[40:48])[0]
            body = c[:csize]
            out.append(Cookie(_cstr(body, d_off), _cstr(body, n_off), _cstr(body, p_off), _cstr(body, v_off),
                              int(expiry + MAC_EPOCH), bool(flags & FLAG_SECURE), bool(flags & FLAG_HTTPONLY)))
    return out


def applies(cookie_domain, site):
    """the cookie is one the site has or would be sent: its own host's or a
    subdomain's, or a parent domain's"""
    d = cookie_domain.lstrip(".").lower()
    s = site.lower().strip(".")
    return d == s or d.endswith("." + s) or s.endswith("." + d)


def netscape(cookies):
    lines = ["# Netscape HTTP Cookie File", "# from Safari by EWeb's tools/safari-cookies.py: logins in clear text,"
             " delete after importing", ""]
    for c in cookies:
        dom = c.domain
        sub = "TRUE" if dom.startswith(".") else "FALSE"
        if c.httponly:
            dom = "#HttpOnly_" + dom
        lines.append("\t".join((dom, sub, c.path or "/", "TRUE" if c.secure else "FALSE", str(c.expires),
                                c.name, c.value)))
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="A Mac's Safari cookies for the sites named, for EWeb")
    ap.add_argument("sites", nargs="*", metavar="SITE", help="a host, e.g. atariage.com (and its subdomains)")
    ap.add_argument("-o", "--out", default="safari-cookies.txt", help="the file to write (safari-cookies.txt)")
    ap.add_argument("--from", dest="src", help="Safari's Cookies.binarycookies (found by itself)")
    a = ap.parse_args(argv)
    if not a.sites:
        print("safari-cookies: name the sites whose cookies EWeb is to have, e.g.\n"
              "  python3 safari-cookies.py atariage.com\n"
              "(there is no 'all': a site's cookies are its logins)", file=sys.stderr)
        return 2
    if os.path.basename(a.out).lower() == "cookies.txt":
        print("safari-cookies: not cookies.txt, EWeb's own store's name: name the file otherwise",
              file=sys.stderr)
        return 2
    src = a.src or next((os.path.expanduser(p) for p in PLACES if os.path.exists(os.path.expanduser(p))),
                        os.path.expanduser(PLACES[0]))
    try:
        with open(src, "rb") as f:
            data = f.read()
    except PermissionError:
        print("safari-cookies: the Mac won't let this Terminal read Safari's cookies.\n"
              "Give it Full Disk Access: System Settings > Privacy & Security > Full Disk Access,\n"
              "switch Terminal on (or add it with +), then quit and open Terminal again and run this again.",
              file=sys.stderr)
        return 1
    except FileNotFoundError:
        print("safari-cookies: no Safari cookie file at %s" % src, file=sys.stderr)
        return 1
    try:
        cookies = parse(data)
    except (ValueError, struct.error, IndexError) as e:
        print("safari-cookies: %s can't be read: %s" % (src, e), file=sys.stderr)
        return 1
    now = time.time()
    taken = []
    for site in a.sites:
        mine = [c for c in cookies if applies(c.domain, site) and c.expires > now]
        print("%s: %d cookie%s" % (site, len(mine), "" if len(mine) == 1 else "s"))
        taken += [c for c in mine if c not in taken]
    with open(a.out, "w", newline="\n") as f:
        f.write(netscape(taken))
    print("%d cookies written to %s. Copy it to the Atari, import it with EWeb's File > Import cookies...,"
          " and let EWeb delete it." % (len(taken), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
