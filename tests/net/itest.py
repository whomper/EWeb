#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make itest: ewget (EWeb's HTTP client, with AddressSanitizer and UBSan)
against the local servers of server.py, over TCP and TLS: pages plain and
packed, kept connections, a resumed TLS session, redirects from HTTP to
HTTPS, chunks, a charset in Content-Type, and the certificates that must be
refused (another host's, one nobody signed). With EWRENDER, the browser
too (src/browse, through tests/host/ewrender.c): a page and its
stylesheet over HTTP and HTTPS, its links made absolute, only the
stylesheets whose media holds fetched (and those that come to hold as the
window is sized, then), EWeb's own page when a server isn't
there and when what came isn't a page, a page's refresh (<meta
http-equiv=refresh>, the Refresh header), and pages that name no encoding
(UTF-8 found; Windows-1255 under .il, through EW_HOSTS, or chosen by hand;
their links' and forms' queries in it; a text file), and the queries of a
page's other addresses (pictures, sheets, CSS's url()s, scripts').

    tests/net/itest.py EWGET [WORK [EWRENDER EWEB.FNT]]"""
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import server  # noqa: E402

ewget = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "build/itest")
ewrender = os.path.abspath(sys.argv[3]) if len(sys.argv) > 4 else None
font = os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 else None
os.makedirs(work, exist_ok=True)
# ITEST_PORTS=N moves every port up by N, so two trees can run at once
OFF = int(os.environ.get("ITEST_PORTS", "0"))
HTTP, HTTPS, BAD, EC, GATE = (p + OFF for p in (18080, 18443, 18444, 18445, 18288))
GATE2, EXT = 18298 + OFF, 18299 + OFF	# webgate with --browser, and its extension's port
# AERA-Gateway's webgate, for the gateway's cases: EWeb's copy of the
# gateway (gateway/, from AERA-Gateway), or WEBGATE=path/webgate.py
WEBGATE = os.environ.get("WEBGATE") or os.path.join(HERE, "..", "..", "gateway", "webgate.py")
# aerapens for it (make -C common/tools/pens), behind a socket as on the Pi
# (tests/net/penssock.py)
PENS = os.path.join(HERE, "..", "..", "common", "tools", "pens", "build", "aerapens")
srv = subprocess.Popen([sys.executable, os.path.join(HERE, "server.py"), work, "--http", str(HTTP),
                        "--https", str(HTTPS), "--untrusted", str(BAD), "--ecdsa", str(EC)],
                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
if srv.stdout.readline().strip() != "ready":
    sys.exit("itest: the servers didn't start")
fails = 0
checks = 0


def run(*args, cwd=None):
    p = subprocess.run([ewget] + list(args), cwd=cwd or work, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def snapshots():
    """the Pi's browser, through webgate --browser (View > Through the Pi's
    browser, View > Live view): a stand-in for webgate-ext answers as it
    does, a page "made by script" for an address, a JPEG for the screen"""
    import http.client
    import json
    import threading
    import urllib.request
    env = {k: v for k, v in os.environ.items() if k.lower() not in ("https_proxy", "http_proxy")}
    gate = subprocess.Popen([sys.executable, WEBGATE, "--port", str(GATE2), "--bind", "127.0.0.1", "--allow-local",
                             "--cafile", os.path.join(work, "CACERT.PEM"), "--browser", "--browser-port", str(EXT)],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    stop = []
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    jpeg = opener.open("http://localhost:%d/site/photo.jpg" % HTTP).read()

    def extension():
        while not stop:
            try:
                c = http.client.HTTPConnection("127.0.0.1", EXT, timeout=30)
                c.request("GET", "/next")
                r = c.getresponse()
                body = r.read()
                if r.status != 200:
                    c.close()
                    continue
                w = json.loads(body)
                if w.get("shot"):
                    c.request("POST", "/shot?id=%s&w=1024" % w["id"], body=jpeg)
                else:
                    url = w["url"] or "https://localhost:%d/site/now.html" % HTTPS
                    page = ("<!DOCTYPE html><html><head><base href='%s'><title>Made by script</title></head>"
                            "<body><p>The page as the browser shows it: %s</p><a href='next.html'>next</a>"
                            % (url, url))
                    c.request("POST", "/snapshot?id=" + w["id"], body=json.dumps(
                        {"url": url + "#shown", "title": "Made by script", "html": page}).encode())
                c.getresponse().read()
                c.close()
            except OSError:
                import time
                time.sleep(0.2)

    t = threading.Thread(target=extension, daemon=True)
    g = "127.0.0.1:%d" % GATE2
    try:
        import time
        for _ in range(50):
            try:
                if b"webgate" in opener.open("http://%s/" % g).read():
                    break
            except OSError:
                time.sleep(0.1)
        t.start()
        page = "https://localhost:%d/site/page.html" % HTTPS
        p = subprocess.run([ewrender, "-f", font, "-W", g, "-R", "-v", "-d", page], cwd=work,
                           capture_output=True, text=True, timeout=120)
        out = p.stdout + p.stderr
        check("the Pi's browser: a page as it shows it", p.returncode == 0 and 'title "Made by script"' in out
              and "a snapshot from the Pi's browser" in out and page + "#shown" in out, out)
        live = "http://webgate/live?url=" + urllib.request.quote(page, safe="")
        p = subprocess.run([ewrender, "-f", font, "-W", g, "-v", "-d", live], cwd=work,
                           capture_output=True, text=True, timeout=120)
        out = p.stdout + p.stderr
        check("the Pi's browser: live view, its screen a picture button", p.returncode == 0
              and 'title "Live: Made by script"' in out
              and re.search(r"^picture http://webgate/live/shot\.jpg\S* decoded", out, re.M) is not None, out)
    finally:
        stop.append(1)
        gate.terminate()
        gate.wait(timeout=10)


def check(name, ok, out):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("itest: %s failed\n%s" % (name, out))


def saved(name):
    with open(os.path.join(work, name), "rb") as f:
        return f.read()


try:
    rc, out = run("-n", "2", "-o", "plain.htm", "http://localhost:%d/page" % HTTP)
    check("http", rc == 0 and "200 OK, text/html, charset utf-8" in out and "unpack" in out
          and "the connection was kept open" in out and saved("plain.htm") == server.PAGE, out)

    rc, out = run("-n", "2", "-o", "tls.htm", "https://localhost:%d/page" % HTTPS)
    check("https", rc == 0 and re.search(r"TLS +\d+ ms +TLS 1\.2", out) is not None
          and "the connection was kept open" in out and saved("tls.htm") == server.PAGE, out)

    rc, out = run("-n", "2", "https://localhost:%d/close" % HTTPS)
    second = out.split("fetch 2:")[-1]
    check("TLS resumed", rc == 0 and "resumed" in second and "kept open" not in second, out)

    rc, out = run("-o", "moved.htm", "http://localhost:%d/secure" % HTTP)
    check("http to https", rc == 0 and "after 1 redirect: https://localhost:%d/page" % HTTPS in out
          and saved("moved.htm") == server.PAGE, out)

    rc, out = run("-o", "rel.htm", "http://localhost:%d/redirect" % HTTP)
    check("redirect", rc == 0 and "after 1 redirect" in out and saved("rel.htm") == server.PAGE, out)

    rc, out = run("-o", "big.bin", "https://localhost:%d/chunked" % HTTPS)
    check("chunked", rc == 0 and saved("big.bin") == server.BIG, out)

    rc, out = run("-o", "deflate.htm", "http://localhost:%d/deflate" % HTTP)
    check("deflate", rc == 0 and "deflate" in out and saved("deflate.htm") == server.PAGE, out)

    rc, out = run("-o", "he.htm", "http://localhost:%d/hebrew" % HTTP)
    check("charset", rc == 0 and "charset windows-1255" in out and saved("he.htm") == server.HEBREW, out)

    rc, out = run("http://localhost:%d/nothing" % HTTP)
    check("404", rc == 0 and "404 Not Found" in out, out)

    rc, out = run("https://127.0.0.2:%d/page" % HTTPS)
    check("another host's certificate", rc != 0 and "failed" in out, out)

    rc, out = run("https://localhost:%d/page" % BAD)
    check("a certificate nobody signed", rc != 0 and "failed" in out, out)

    # ECDSA only: RSA asked for first (no DSP here), so the third try takes
    # it, and the report counts the tries the server turned down as TLS
    for f in ("SIGS.DAT", "ec.log", "ec2.log"):
        try:
            os.remove(os.path.join(work, f))
        except OSError:
            pass
    seed = saved("SEED.DAT")
    rc, out = run("-l", "ec.log", "-o", "ec.htm", "https://localhost:%d/page" % EC)
    check("ECDSA", rc == 0 and "ECDHE-ECDSA" in out and "(3 tries)" in out and saved("ec.htm") == server.PAGE,
          out)
    # the seed written over in place, a new one each session; the
    # certificate's signature remembered (SIGS.DAT), and taken from there
    # the next time
    check("SEED.DAT", len(saved("SEED.DAT")) == 32 and saved("SEED.DAT") != seed, "")
    log = saved("ec.log").decode("latin-1")
    rc, out = run("-l", "ec2.log", "https://localhost:%d/page" % EC)
    log2 = saved("ec2.log").decode("latin-1") if rc == 0 else ""
    check("signatures checked before", "0 of 1 ECDSA signatures checked before" in log
          and "1 of 1 ECDSA signatures checked before" in log2 and len(saved("SIGS.DAT")) == 8 + 64 * 32,
          out + log + log2)

    # the handshake's steps in the log, in order, written out once it ended
    try:
        os.remove(os.path.join(work, "hs.log"))
    except OSError:
        pass
    rc, out = run("-l", "hs.log", "https://localhost:%d/page" % HTTPS)
    log = saved("hs.log").decode("latin-1") if rc == 0 else ""
    steps = ["root certificates read", "random seed ready in", "the server's first answer, ",
             "decoding and checking them", "the handshake: ", "secure: TLS 1.2"]
    at = [log.find(w) for w in steps]
    check("the handshake's log lines", rc == 0 and -1 not in at and at == sorted(at), out + log)

    # the test authorities after Mozilla's 121: their names are found
    # among the others (tls.c remembers the roots' name hashes), from
    # CACERT.PEM and then from ROOTS.DAT
    mz = os.path.join(work, "mz")
    shutil.rmtree(mz, ignore_errors=True)
    os.makedirs(mz)
    with open(os.path.join(mz, "CACERT.PEM"), "wb") as f:
        f.write(open(os.path.join(HERE, "..", "..", "common", "certs", "CACERT.PEM"), "rb").read())
        f.write(saved("CACERT.PEM"))
    for k in range(2):
        rc1, out1 = run("https://localhost:%d/page" % HTTPS, cwd=mz)
        rc2, out2 = run("https://localhost:%d/page" % EC, cwd=mz)
        check("among Mozilla's roots (%s)" % ("CACERT.PEM", "ROOTS.DAT")[k],
              rc1 == 0 and rc2 == 0 and "ECDHE-ECDSA" in out2, out1 + out2)

    # ROOTS.DAT as a release has it (made on this computer, CACERT.PEM's
    # date unknown): the first run checks the checksum and puts the date
    # in it; the next is told by the date; another CACERT.PEM, whatever
    # its date, is read again
    import struct
    mz3 = os.path.join(work, "mz3")
    shutil.rmtree(mz3, ignore_errors=True)
    os.makedirs(mz3)
    shutil.copy(os.path.join(mz, "CACERT.PEM"), mz3)
    subprocess.run([os.path.join(os.path.dirname(ewget), "..", "host", "mkroots"),
                    os.path.join(mz3, "CACERT.PEM"), os.path.join(mz3, "ROOTS.DAT")], check=True)
    def stamp_of():
        b = open(os.path.join(mz3, "ROOTS.DAT"), "rb").read(22)
        return b[:10], struct.unpack(">I", b[18:22])[0]
    before = stamp_of()
    rc1, out1 = run("https://localhost:%d/page" % HTTPS, cwd=mz3)
    after = stamp_of()
    rc2, out2 = run("https://localhost:%d/page" % HTTPS, cwd=mz3)
    pem = os.path.join(mz3, "CACERT.PEM")
    data = bytearray(open(pem, "rb").read())
    data[data.index(b"-----BEGIN CERTIFICATE-----") + 40] ^= 1    # one root spoiled, same size
    open(pem, "wb").write(bytes(data))
    rc3, out3 = run("https://localhost:%d/page" % HTTPS, cwd=mz3)
    check("ROOTS.DAT told by CACERT.PEM's date", before == (b"MAILROOTS2", 0) and after[1] != 0
          and rc1 == 0 and rc2 == 0 and rc3 == 0 and stamp_of() != after, out1 + out2 + out3)

    # the request's answer: when its first bytes came, when they were read
    rc, out = run("-l", "ans.log", "https://localhost:%d/page" % HTTPS)
    log = saved("ans.log").decode("latin-1") if rc == 0 else ""
    at = [log.find(w) for w in (">> GET /page", "the answer's first", "bytes decrypted", "<< HTTP/1.1 200")]
    check("the answer's times in the log", -1 not in at and at == sorted(at), out + log)

    # and without them, a remembered signature changes nothing
    mz2 = os.path.join(work, "mz2")
    shutil.rmtree(mz2, ignore_errors=True)
    os.makedirs(mz2)
    shutil.copy(os.path.join(HERE, "..", "..", "common", "certs", "CACERT.PEM"), mz2)
    shutil.copy(os.path.join(mz, "SIGS.DAT"), mz2)
    rc, out = run("https://localhost:%d/page" % EC, cwd=mz2)
    check("a known signature from an authority not trusted", rc != 0 and "isn't signed by any authority" in out,
          out)

    rc, out = run("http://localhost:1/page")
    check("nobody listening", rc != 0 and "localhost" in out, out)

    if ewrender:
        def browse(url):
            p = subprocess.run([ewrender, "-f", font, "-c", os.path.join(work, "CACERT.PEM"), "-d", url],
                               cwd=work, capture_output=True, text=True, timeout=120)
            return p.returncode, p.stdout + p.stderr

        for scheme, port in (("http", HTTP), ("https", HTTPS)):
            rc, out = browse("%s://localhost:%d/site/" % (scheme, port))
            check("the browser: a page and its stylesheet (%s)" % scheme, rc == 0
                  and "background ffeedd" in out and re.search(r'#aa0000 \S+ bold link 0 "Welcome"', out)
                  and "link 1 %s://localhost:%d/site/two.html\n" % (scheme, port) in out
                  and "link 2 %s://localhost:%d/site/two.html#end\n" % (scheme, port) in out, out)
        # Back and Forward: the pages kept as laid out, shown again with no
        # fetch or parse; laid out again at another width
        p = subprocess.run([ewrender, "-f", font, "-c", os.path.join(work, "CACERT.PEM"), "-d", "-N",
                            "http://localhost:%d/site/two.html" % HTTP, "http://localhost:%d/site/" % HTTP],
                           cwd=work, capture_output=True, text=True, timeout=120)
        steps = re.findall(r'^(to|back|forward|narrower): 0 "([^"]*)" \S+ width (\d+) fetch (\d+) parse (\d+)',
                           p.stdout, re.M)
        want = [("to", "Two"), ("back", "EWeb site"), ("forward", "Two"), ("back", "EWeb site"),
                ("narrower", "EWeb site"), ("forward", "Two"), ("back", "EWeb site")]
        check("the browser: Back and Forward from pages kept", p.returncode == 0 and
              [(s[0], s[1]) for s in steps] == want and
              all(s[3] == "0" and s[4] == "0" for s in steps if s[0] in ("back", "forward")) and
              steps[5][2] == "508", p.stdout + p.stderr)
        # a stylesheet's media (the HTML Standard's 4.2.4), at two widths
        # (760 and 500 CSS px): only the sheets that hold for the screen
        # asked for and applied, none alternate, disabled, of another type
        # or with an empty href (two sheets each time, said on the status line)
        murl = "http://localhost:%d/site/media/page.html" % HTTP
        mouts = []
        for w in ("608", "400"):
            p = subprocess.run([ewrender, "-f", font, "-v", "-d", "-w", w, murl], cwd=work,
                               capture_output=True, text=True, timeout=120)
            mouts.append((p.returncode, p.stdout + p.stderr, urllib.request.build_opener(
                urllib.request.ProxyHandler({})).open("http://localhost:%d/site/media/count" % HTTP,
                                                     timeout=10).read().decode()))
        check("the browser: stylesheets by their media, type and rel",
              [o[0] for o in mouts] == [0, 0] and
              mouts[0][2] == "body.css=1 page.html=1 wide.css=1" and
              mouts[1][2] == "body.css=2 narrow.css=1 page.html=2 wide.css=1" and
              re.search(r'#0000aa \S+ link 0 "Sheets by', mouts[0][1]) and
              re.search(r'#00aa00 \S+ link 0 "Sheets by', mouts[1][1]) and
              all("fill" in o[1] and "#eeeeee" in o[1] and "stylesheet 2..." in o[1] and
                  "stylesheet 3" not in o[1] for o in mouts),
              "\n".join(o[1] + "count: " + o[2] for o in mouts))
        # the window sized (Media Queries 4, 2): at 400 px the sheet for
        # a narrow screen is fetched then, quietly (no status line), and
        # back at 608 the wide one comes from the cache, as body.css
        p = subprocess.run([ewrender, "-f", font, "-v", "-d", "-w", "608", "-L", "400", "-L", "608", murl],
                           cwd=work, capture_output=True, text=True, timeout=120)
        rcount = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(
            "http://localhost:%d/site/media/count" % HTTP, timeout=10).read().decode()
        check("the browser: a stylesheet's media again as the window is sized",
              p.returncode == 0 and rcount == "body.css=3 narrow.css=2 page.html=3 wide.css=2" and
              re.search(r'resized: 1 "Media" width 400\nresized: 1 "Media" width 608\n', p.stdout) and
              re.search(r'#0000aa \S+ link 0 "Sheets by', p.stdout) and "stylesheet 3" not in p.stdout,
              p.stdout + p.stderr + "count: " + rcount)
        # sized while a picture comes (GEM's messages come as a request
        # waits): no request inside it, the narrow sheet after it
        p = subprocess.run([ewrender, "-f", font, "-d", "-w", "608", "-Y", "400",
                            "http://localhost:%d/site/media/pics.html" % HTTP],
                           cwd=work, capture_output=True, text=True, timeout=120)
        rcount = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(
            "http://localhost:%d/site/media/count" % HTTP, timeout=10).read().decode()
        check("the browser: the window sized while a picture comes",
              p.returncode == 0 and "sized in a request: 1" in p.stdout and
              rcount == "body.css=3 narrow.css=3 page.html=3 pic.png=1 pics.html=1 wide.css=3" and
              re.search(r'page 400 x', p.stdout) and re.search(r'#00aa00 \S+ link 0 "Sheets by', p.stdout) and
              "image " in p.stdout,
              p.stdout + p.stderr + "count: " + rcount)
        rc, out = browse("http://localhost:%d/site/two.html#end" % HTTP)
        check("the browser: a page's anchor", rc == 0 and '"The end"' in out
              and "link 1 http://localhost:%d/site/\n" % HTTP in out, out)
        rc, out = browse("http://localhost:%d/redirect" % HTTP)
        check("the browser: a redirect", rc == 0 and 'title "EWeb test"' in out, out)
        rc, out = browse("http://localhost:%d/hebrew" % HTTP)
        check("the browser: a page in windows-1255", rc == 0 and re.search(r'rtl link 0 "\u05e9\u05dc\u05d5\u05dd"', out),
              out)
        # pages that name no encoding: UTF-8 found by its bytes; windows-1255
        # read as windows-1252 but under Israel's .il, or as chosen by hand
        # (View > Encoding), its link's and form's queries in it
        enc = "http://localhost:%d/site/enc/" % HTTP
        rc, out = browse(enc + "utf8.html")
        check("encodings: UTF-8 that names none", rc == 0 and 'title "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"' in out
              and "link 1 %secho?q=%%D7%%A9%%D7%%9C%%D7%%95%%D7%%9D\n" % enc[:-4] in out, out)
        rc, out = browse(enc + "w1255.html")
        check("encodings: windows-1255 that names none is windows-1252 here", rc == 0
              and 'title "\u00f9\u00ec\u00e5\u00ed \u00f2\u00e5\u00ec\u00ed"' in out
              and "link 1 %secho?q=%%F9%%EC%%E5%%ED\n" % enc[:-4] in out, out)
        il = "http://hebrew.il:%d/site/enc/" % HTTP

        def encoded(*args, url=il + "w1255.html"):
            p = subprocess.run([ewrender, "-f", font, "-d"] + list(args) + [url], cwd=work, capture_output=True,
                               text=True, timeout=120, env=dict(os.environ, EW_HOSTS="hebrew.il=127.0.0.1"))
            return p.returncode, p.stdout + p.stderr

        rc, out = encoded()
        check("encodings: windows-1255 that names none, under .il", rc == 0
              and 'title "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"' in out
              and "link 1 %secho?q=%%F9%%EC%%E5%%ED\n" % il[:-4] in out, out)
        rc, out = encoded("-F", "q=\u05d0\u05d1", "-S", "go")
        check("encodings: its form sends windows-1255", rc == 0 and re.search(
            r'^sent: 0 "GET\|\|q=%E0%E1&go=%E7%F4%F9" ', out, re.M), out)
        rc, out = encoded("-e", "windows-1255", "-N", "http://localhost:%d/site/two.html" % HTTP,
                          url=enc + "w1255.html")
        check("encodings: chosen by hand, kept with the page for Back", rc == 0
              and 'encoding: 0 windows-1255 "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd"' in out
              and re.search(r'^to: 0 "Two" ', out, re.M)
              and re.search(r'^back: 0 "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd" ', out, re.M), out)
        rc, out = encoded("-e", "windows-1255", "-F", "q=\u05d0", "-S", "go", url=enc + "w1255.html")
        check("encodings: chosen by hand, its form sends in it", rc == 0
              and re.search(r'^sent: 0 "GET\|\|q=%E0&go=%E7%F4%F9" ', out, re.M), out)
        rc, out = encoded("-e", "auto", url=il + "utf8.html")
        check("encodings: Auto again", rc == 0 and 'encoding: 0 UTF-8 "\u05e9' in out, out)
        rc, out = browse(enc + "plain.txt")
        check("encodings: a text file that names none, in UTF-8", rc == 0
              and 'rtl link 0 "\u05e9\u05dc\u05d5\u05dd"' in out and '"<b>not bold</b> & caf\u00e9"' in out, out)
        # a page's addresses: their query in its encoding for the HTML's, in
        # UTF-8 for CSS's url()s (a <style>'s, a style attribute's, a sheet's)
        # and fetch()
        p = subprocess.run([ewrender, "-f", font, "-J", "-d", enc + "addr.html"], cwd=work, capture_output=True,
                           text=True, timeout=120)
        seen = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(enc + "seen", timeout=10).read().decode()
        check("encodings: a page's addresses, their queries in its encoding or in UTF-8", p.returncode == 0 and
              seen == "ext.png?q=%D7%90 f?q=%D7%90 img.png?q=%E0 inl.png?q=%D7%90 pts.png?q=%CA%81 s.css?q=%E0 "
                      "st.png?q=%D7%90 x?q=%E0", p.stdout + p.stderr + "seen: " + seen)
        rc, out = browse("http://localhost:1/")
        check("the browser: nobody listening", rc == 0 and "EWeb can't open this page" in out, out)
        # saving: a file EWeb doesn't show (kept in memory, saved in one
        # go), a link's file (streamed to the disk), the page shown
        def save(*args):
            path = os.path.join(work, "saved.bin")
            if os.path.exists(path):
                os.remove(path)
            p = subprocess.run([ewrender, "-f", font, "-c", os.path.join(work, "CACERT.PEM"), "-d", "-s", path] +
                               list(args), cwd=work,
                               capture_output=True, text=True, timeout=120)
            data = open(path, "rb").read() if os.path.exists(path) else None
            return p.returncode, data, p.stdout + p.stderr
        rc, data, out = save("http://localhost:%d/chunked" % HTTP)
        check("the browser: not a page, saved", rc == 0 and data == server.BIG and
              "download: application/octet-stream %d CHUNKED\n" % len(server.BIG) in out, out)
        files = "http://localhost:%d/site/files.html" % HTTP
        rc, data, out = save("-g", "0", files)
        check("the browser: a link's file saved", rc == 0 and data == server.BIG and
              "saved link: 0 %d CHUNKED \n" % len(server.BIG) in out, out)
        rc, data, out = save("-g", "1", files)
        check("the browser: a link's file that isn't there", rc == 0 and data is None and
              "saved link: 1 0 NONE.ZIP The server answered 404" in out, out)
        rc, data, out = save("-g", "2", files)
        check("the browser: a link's page saved", rc == 0 and data == server.SITE and
              "saved link: 0 %d INDEX.HTM \n" % len(server.SITE) in out, out)
        rc, data, out = save("https://localhost:%d/site/" % HTTPS)
        check("the browser: the page shown saved", rc == 0 and data == server.SITE and
              "saved page: 1 %d INDEX.HTM\n" % len(server.SITE) in out, out)
        # the disk cache: a page fresh for an hour shown again with no
        # request (the next run reads the pack and the index), one asked
        # again each time answered with a 304, Reload asking again
        cache = os.path.join(work, "cache")
        shutil.rmtree(cache, ignore_errors=True)

        def cached(*args):
            p = subprocess.run([ewrender, "-f", font, "-C", cache, "-d"] + list(args), cwd=work,
                               capture_output=True, text=True, timeout=120)
            m = re.search(r'title "([^"]*)"', p.stdout)
            return p.returncode, m.group(1) if m else None, p.stdout + p.stderr

        def counts():
            import urllib.request
            with urllib.request.urlopen("http://localhost:%d/site/cache/count" % HTTP, timeout=10) as r:
                return r.read().decode()

        site = "http://localhost:%d/site/cache/" % HTTP
        r1 = cached(site + "fresh.html")
        r2 = cached(site + "fresh.html")
        check("the cache: a fresh page from the disk, no request", r1[1] == "fresh 1" and r2[1] == "fresh 1" and
              counts().startswith("fresh=1 "), r1[2] + r2[2] + counts())
        r1 = cached(site + "etag.html")
        r2 = cached(site + "etag.html")
        check("the cache: asked again, a 304", r1[1] == "etag 1" and r2[1] == "etag 1" and
              counts().endswith(" etag=2 304=1"), r1[2] + r2[2] + counts())
        check("the cache: its files", os.path.exists(os.path.join(cache, "EWEB.PAK")) and
              os.path.exists(os.path.join(cache, "EWEB.IDX")) and
              "/site/cache/fresh.html" in open(os.path.join(cache, "EWEB.IDX")).read(), " ".join(os.listdir(cache)))
        # a form sent there with POST: what was kept for it is forgotten
        # (RFC 9111, 4.4), and the next look asks the server
        r1 = cached("-S", "go", site + "post.html")
        r2 = cached(site + "fresh.html")
        check("the cache: a POST forgets the page it went to", "sent: 0 \"posted\"" in r1[2] and r2[1] == "fresh 2",
              r1[2] + r2[2] + counts())
        # cut off: a page kept is shown from the cache, marked out of date,
        # unless its server said it must always be asked (must-revalidate)
        import http.server
        import threading

        class Away(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                must = self.path == "/must.html"
                body = b"<!DOCTYPE html><title>%s</title><p>Kept." % (b"must" if must else b"kept")
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "max-age=0, must-revalidate" if must else "max-age=0")
                self.send_header("ETag", '"a"')
                self.end_headers()
                self.wfile.write(body)

        away = http.server.ThreadingHTTPServer(("127.0.0.1", 18197), Away)
        threading.Thread(target=away.serve_forever, daemon=True).start()
        r1 = cached("http://127.0.0.1:18197/kept.html")
        r2 = cached("http://127.0.0.1:18197/must.html")
        away.shutdown()
        away.server_close()
        r3 = cached("http://127.0.0.1:18197/kept.html")
        r4 = cached("http://127.0.0.1:18197/must.html")
        check("the cache: cut off, a page kept shown, said to be out of date", r1[1] == "kept" and r3[1] == "kept"
              and "note: From the cache: the server couldn't be reached" in r3[2], r1[2] + r3[2])
        check("the cache: cut off, a must-revalidate page not shown", r2[1] == "must" and r4[1] != "must",
              r2[2] + r4[2])
        # pictures: fetched after the page, decoded at the size shown (a
        # CSS pixel is 0.8 of the Falcon's), the page laid out again for
        # the one whose size it didn't know; the missing one failed
        p = subprocess.run([ewrender, "-f", font, "-v", "-d", "http://localhost:%d/site/pics.html" % HTTP],
                           cwd=work, capture_output=True, text=True, timeout=120)
        out = p.stdout + p.stderr
        site = "http://localhost:%d/site/" % HTTP
        want = {"photo.jpg": "192x128", "logo.png": "120x48", "anim.gif": "38x38", "prog.jpg": "128x96",
                "big.png": "596x447"}
        got = {m.group(2): m.group(1) for m in re.finditer(r"^image \S+ (\d+x\d+) link \d+ \"[^\"]*\" %s(\S+)$"
                                                         % re.escape(site), out, re.M)}
        decoded = re.findall(r"^picture %s(\S+) decoded at (\d+x\d+)" % re.escape(site), out, re.M)
        check("the browser: pictures", p.returncode == 0 and all(got.get(k) == v for k, v in want.items())
              and sorted(decoded) == sorted(want.items())
              and re.search(r"^picture %sgone\.png failed" % re.escape(site), out, re.M) is not None, out)
        # decoded pictures kept in the disk cache as the screen draws them:
        # the next run reads them, but for the small one (decoded each time)
        pix = os.path.join(work, "PIXCACHE")
        shutil.rmtree(pix, ignore_errors=True)
        outs = []
        for _ in range(2):
            p = subprocess.run([ewrender, "-f", font, "-v", "-d", "-C", pix, "http://localhost:%d/site/pics.html" % HTTP],
                               cwd=work, capture_output=True, text=True, timeout=120)
            outs.append(p.stdout + p.stderr)
        again = re.findall(r"^picture %s(\S+) (decoded|from the cache) at (\d+x\d+)" % re.escape(site), outs[1], re.M)
        check("the cache: decoded pictures read back", sorted(again) == sorted(
              [(k, "decoded" if k == "anim.gif" else "from the cache", v) for k, v in want.items()]) and
              outs[1].split("\nlink ")[0].count("\nimage ") == outs[0].split("\nlink ")[0].count("\nimage "),
              outs[1])
        # pictures from a sheet's url() (its folder's) and from srcset
        p = subprocess.run([ewrender, "-f", font, "-v", "-d", "http://localhost:%d/site/bg.html" % HTTP],
                           cwd=work, capture_output=True, text=True, timeout=120)
        out = p.stdout + p.stderr
        check("the browser: a background from the sheet's folder, a srcset's choice", p.returncode == 0
              and re.search(r"^background \S+ \S+ tile \S+ 6x6 across %scss/tile\.png$" % re.escape(site), out, re.M)
              and re.search(r"^image \S+ 120x48 link \d+ \"chosen\" %slogo\.png$" % re.escape(site), out, re.M)
              and "no.webp" not in out and "big.png" not in out and "gone.png" not in out, out)
        # a picture EWeb can't read, by its name: not fetched at all
        p = subprocess.run([ewrender, "-f", font, "-v", "-d", "http://localhost:%d/site/webp.html" % HTTP],
                           cwd=work, capture_output=True, text=True, timeout=120)
        out = p.stdout + p.stderr
        hits = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(
            "http://localhost:%d/site/hits/pic.webp" % HTTP, timeout=10).read()
        check("the browser: a WebP not fetched, the GIF beside it shown", p.returncode == 0
              and "pic.webp decoded" not in out and re.search(r"^picture \S+anim\.gif decoded", out, re.M)
              and hits == b"0", out + "\nhits: %r" % hits)
        rc, out = browse("http://localhost:%d/site/photo.jpg" % HTTP)
        check("the browser: a picture on its own", rc == 0 and 'title "photo.jpg (JPEG, 480 x 320)"' in out
              and re.search(r"^image \S+ 384x256 ", out, re.M) is not None, out)
        # forms: filled and sent as the HTML Standard says, the server's
        # echo page's title what came
        form = "http://localhost:%d/site/form.html" % HTTP

        def send(*args):
            p = subprocess.run([ewrender, "-f", font, "-d"] + list(args) + [form], cwd=work,
                               capture_output=True, text=True, timeout=120)
            out = p.stdout + p.stderr
            m = re.search(r'^sent: (\d+) "(.*)" (\S+)$', out, re.M)
            return p.returncode, m.group(2) if m else None, m.group(3) if m else None, out

        rc, title, url, out = send("-F", "q=hello world", "-S", "go")
        check("forms: a search sent with GET", rc == 0 and title == "GET||q=hello+world&go=Find"
              and url == "http://localhost:%d/site/echo?q=hello+world&go=Find" % HTTP, out)
        rc, title, url, out = send("-F", "q=x", "-S", "-")
        check("forms: Return in the field", rc == 0 and title == "GET||q=x&go=Find", out)
        rc, title, url, out = send("-F", "user=erez", "-F", "pw=p&ss", "-F", "keep", "-F", "lang=he",
                                   "-F", "note=line 1\nline 2", "-S", "login")
        check("forms: a login sent with POST", rc == 0 and title ==
              "POST|application/x-www-form-urlencoded|user=erez&pw=p%26ss&keep=on&lang=he"
              "&note=line+1%0D%0Aline+2&login=Log+in"
              and url == "http://localhost:%d/site/echo" % HTTP, out)
        rc, title, url, out = send("-F", "title=hi", "-S", "up")
        check("forms: multipart", rc == 0 and title == 'POST|multipart/form-data|title=hi;f=(file "");up=Send', out)
        rc, title, url, out = send("-S", "s")
        check("forms: a POST answered with 303 is fetched with GET", rc == 0 and title == "GET||after=1"
              and url == "http://localhost:%d/site/echo?after=1" % HTTP, out)
        rc, title, url, out = send("-F", "user=erez", "-S", "login", "-b")
        check("forms: Back shows the form as it was filled", rc == 0 and re.search(r'^back: 0 "Forms"', out, re.M)
              and re.search(r'^control \S+ \S+ #000000 text "erez"$', out, re.M), out)
        # what is typed and kept never reaches the log (7.3): a login sent
        # with GET and with POST, its cookie, all with the log on
        log = os.path.join(work, "FORM.LOG")
        if os.path.exists(log):
            os.remove(log)
        for how in ("get", "post"):
            p = subprocess.run([ewrender, "-f", font, "-d", "-l", log, "-F", "user=erez", "-F", "pw=s3cr3tpw",
                                "-S", "login" if how == "post" else "go"] +
                               (["-F", "q=s3cr3tq"] if how == "get" else []) + [form],
                               cwd=work, capture_output=True, text=True, timeout=120)
        p2 = subprocess.run([ewrender, "-f", font, "-d", "-l", log, "-S", "go",
                             "http://localhost:%d/site/cookie/form.html" % HTTP],
                            cwd=work, capture_output=True, text=True, timeout=120)
        text = open(log, errors="replace").read() if os.path.exists(log) else ""
        check("the log keeps no field, query, body or cookie", text != "" and "/site/echo" in text and
              not any(w in text for w in ("s3cr3t", "user=erez", "sid=xyz", "Cookie", "erez")),
              text[-2000:])
        # cookies: set and sent back, a 303's cookie on the next request,
        # the lasting one kept in a file for the next run, another site's
        # picture's refused
        jar = os.path.join(work, "COOKIES.TXT")
        for f in (jar, os.path.join(work, "EWEB.KEY")):
            if os.path.exists(f):
                os.remove(f)
        site = "http://localhost:%d/site/cookie/" % HTTP

        def titles(*args):
            p = subprocess.run([ewrender, "-f", font, "-d"] + list(args), cwd=work, capture_output=True,
                               text=True, timeout=120)
            out = p.stdout + p.stderr
            return p.returncode, re.findall(r'^\w+: \d+ "(.*)" ', out, re.M), out

        rc, t, out = titles("-k", jar, "-N", site + "show", site + "set")
        check("cookies: set, and sent back", rc == 0 and t[:1] == ["COOKIE|lasting=1; session=2"], out)
        rc2, t2, out2 = titles("-k", jar, "-N", site + "show", "http://localhost:%d/site/" % HTTP)
        kept = open(jar).read()
        check("cookies: the lasting one kept for the next run, its value encrypted", rc2 == 0
              and t2[:1] == ["COOKIE|lasting=1"] and "\tlasting\t{E}" in kept and "session" not in kept
              and os.path.getsize(os.path.join(work, "EWEB.KEY")) == 16, out2)
        os.remove(jar)
        p = subprocess.run([ewrender, "-f", font, "-d", "-S", "go", site + "form.html"], cwd=work,
                           capture_output=True, text=True, timeout=120)
        m = re.search(r'^sent: \d+ "(.*)" (\S+)$', p.stdout, re.M)
        check("cookies: a login's 303 sets one, sent with the GET after it",
              p.returncode == 0 and m is not None and m.group(1) == "COOKIE|sid=xyz", p.stdout + p.stderr)
        rc, t, out = titles("-v", "-N", "http://127.0.0.1:%d/site/cookie/show" % HTTP, site + "third.html")
        check("cookies: another site's picture sets none", rc == 0 and t[:1] == ["COOKIE|"]
              and re.search(r"^picture http://127\.0\.0\.1:\d+/site/cookie/px\.gif decoded", out, re.M) is not None,
              out)
        # scripts (src/js): a script from a file, fetch and XMLHttpRequest,
        # document.cookie without the HttpOnly cookie (which it can neither
        # see nor replace), and the cookie a script set sent with its request
        p = subprocess.run([ewrender, "-f", font, "-J", "-T", "500", "-d",
                            "http://localhost:%d/site/script.html" % HTTP], cwd=work, capture_output=True, text=True,
                           timeout=120)
        out = p.stdout + p.stderr
        check("scripts: a file's, fetch and XMLHttpRequest", p.returncode == 0 and '"external 2"' in out
              and '"fetch atari"' in out and '"xhr atari"' in out and "js error" not in out, out)
        check("scripts: document.cookie, HttpOnly kept from them", p.returncode == 0
              and '"cookies seen=2; js=1"' in out and '"sent hidden=1; seen=2; js=1"' in out, out)
        # a refresh (<meta http-equiv=refresh>, the Refresh header): followed
        # when due, in place of the page in the history, the page it came
        # from as the Referer
        rsite = "http://localhost:%d/site/refresh/" % HTTP

        def refresh(*args):
            p = subprocess.run([ewrender, "-f", font, "-d", "-r", "8"] + list(args), cwd=work, capture_output=True,
                               text=True, timeout=120)
            out = p.stdout + p.stderr
            return p.returncode, re.findall(r'^(refresh|refreshed|back|note): (.*)$', out, re.M), out

        rc, steps, out = refresh("-P", "http://localhost:%d/site/" % HTTP, "-b", rsite + "meta.html")
        check("refresh: a meta's, at once, in the page's place (Back goes before it)", rc == 0 and steps == [
            ("refresh", "0 s %sto.html?from=meta" % rsite),
            ("refreshed", '0 "Arrived|from=meta|%smeta.html" %sto.html?from=meta' % (rsite, rsite)),
            ("back", '0 "EWeb site" http://localhost:%d/site/' % HTTP)], out)
        cache = os.path.join(work, "rfcache")
        shutil.rmtree(cache, ignore_errors=True)
        for k in range(2):
            rc, steps, out = refresh("-C", cache, rsite + "header.html")
            check("refresh: the Refresh header, before the page's meta (%s)" % ("again: not from the disk cache"
                  if k else "kept an hour"), rc == 0 and steps == [
                      ("refresh", "1 s %sto.html?from=header" % rsite),
                      ("refreshed", '0 "Arrived|from=header|%sheader.html" %sto.html?from=header' % (rsite, rsite))],
                  out)
        rc, steps, out = refresh(rsite + "loop.html")
        with urllib.request.urlopen(rsite + "count", timeout=10) as r:
            count = r.read().decode()
        check("refresh: a page that refreshes itself at once stops after %d" % 5, rc == 0 and
              [s[0] for s in steps] == ["refresh", "refreshed"] * 5 + ["note"] and
              steps[1] == ("refreshed", '0 "Loop" %sloop.html' % rsite) and "again and again" in steps[-1][1]
              and "loop.html=6" in count.split(), out + count)
        rc, steps, out = refresh(rsite + "based.html")
        rc2, steps2, out2 = refresh(rsite + "after.html")
        check("refresh: resolved against the <base> before the meta, not one after", rc == 0 and rc2 == 0 and
              steps[:1] == [("refresh", "0 s http://localhost:%d/site/refresh/sub/to.html?b=before" % HTTP)] and
              steps2[:1] == [("refresh", "0 s %sto.html?b=after" % rsite)], out + out2)
        rc, steps, out = refresh(rsite + "noscript.html")
        rc2, steps2, out2 = refresh("-J", rsite + "noscript.html")
        check("refresh: a <noscript>'s only without scripts", rc == 0 and rc2 == 0 and
              steps[:1] == [("refresh", "0 s %sto.html?from=noscript" % rsite)] and steps2 == [], out + out2)
        rc, steps, out = refresh(rsite + "js.html")
        check("refresh: a javascript: address passed over for the next", rc == 0 and
              steps[:1] == [("refresh", "0 s %sto.html?from=second" % rsite)], out)
        rc, steps, out = refresh("-r", "0", rsite + "later.html")
        check("refresh: one in a minute, said on the status line", rc == 0 and
              ("note", "Going on to localhost:%d in 60 s: Esc stays here." % HTTP) in steps, out)
        rc, steps, out = refresh("-P", rsite + "later.html", "-b", "http://localhost:%d/site/two.html" % HTTP)
        check("refresh: none for a page Back goes to", rc == 0 and
              steps == [("back", '0 "Later" %slater.html' % rsite)], out)
        rc, steps, out = refresh("-r", "3", rsite + "selfcss.html")
        with urllib.request.urlopen(rsite + "count", timeout=10) as r:
            count = r.read().decode().split()
        check("refresh: itself asked for anew, its stylesheet (kept an hour) once", rc == 0 and
              [s[0] for s in steps] == ["refresh", "refreshed"] * 3 and
              "selfcss.html=4" in count and "s.css=1" in count, out + " ".join(count))
        rc, steps, out = refresh(rsite + "frag.html")
        with urllib.request.urlopen(rsite + "count", timeout=10) as r:
            count = r.read().decode().split()
        check("refresh: a place on the page itself, scrolled to, not loaded again", rc == 0 and steps == [
            ("refresh", "0 s %sfrag.html#x" % rsite), ("refreshed", '2 "Place" %sfrag.html#x' % rsite)] and
              "frag.html=1" in count, out + " ".join(count))
        rc, steps, out = refresh("-J", rsite + "jsset.html")
        rc2, steps2, out2 = refresh(rsite + "foster.html")
        check("refresh: each meta as the parser put it in (a script's change after, a table's order)",
              rc == 0 and rc2 == 0 and steps[:1] == [("refresh", "0 s %sto.html?from=orig" % rsite)] and
              steps2[:1] == [("refresh", "0 s %sto.html?from=foster" % rsite)], out + out2)
        t0 = time.time()
        rc, steps, out = refresh("-r", "1", rsite + "slowpic.html")
        took = time.time() - t0
        check("refresh: itself in 2 s from when its picture (1.5 s) is in (%.1f s)" % took, rc == 0 and steps == [
            ("refresh", "2 s %sslowpic.html" % rsite), ("refreshed", '0 "Slow" %sslowpic.html' % rsite),
            ("note", "This page loads again in 2 s: Esc stops it.")] and took >= 3.4, out)
        # a page that refreshes itself (a camera's): its pictures asked for
        # again as their headers say, through the disk cache, and decoded
        # again only when they changed
        cache = os.path.join(work, "camcache")
        shutil.rmtree(cache, ignore_errors=True)
        rc, steps, out = refresh("-v", "-C", cache, "-r", "2", rsite + "cam.html")
        with urllib.request.urlopen(rsite + "count", timeout=10) as r:
            count = r.read().decode().split()
        pics = re.findall(r"^picture %s(\w+)\.png (decoded|unchanged) " % re.escape(rsite), out, re.M)
        check("refresh: itself, its pictures asked for again as they say (no-store each time and decoded, "
              "kept an hour from the cache, no-cache a 304)", rc == 0 and
              [s[0] for s in steps[:4]] == ["refresh", "refreshed"] * 2 and
              "cam.html=3" in count and "cam.png=3" in count and "still.png=1" in count and "valid.png=3" in count and
              pics.count(("cam", "decoded")) == 3 and pics.count(("still", "unchanged")) == 2 and
              pics.count(("valid", "unchanged")) == 2 and pics.count(("still", "decoded")) == 1 and
              pics.count(("valid", "decoded")) == 1, out + " ".join(count))
        # a picture on its own that refreshes itself (its Refresh header):
        # the one that came each time is shown
        rc, steps, out = refresh("-v", "-r", "2", rsite + "campic.png")
        sizes = re.findall(r"^picture %scampic\.png decoded at \S+ (\S+) " % re.escape(rsite), out, re.M)
        check("refresh: a picture on its own, itself again: the one that came each time",
              rc == 0 and [s[0] for s in steps[:4]] == ["refresh", "refreshed"] * 2 and
              sizes == ["150x60", "1x1", "150x60"], out)
    # through AERA-Gateway's webgate (EWEB.INF's GATEWAY=, EWGET -g): the
    # Pi makes the secure connection (TLS 1.3 here), passes the server's
    # gzip, redirects, cookies and big answers through, and refuses a
    # certificate it can't check
    if not os.path.exists(WEBGATE):
        print("itest: no webgate (%s): the gateway's cases left out" % WEBGATE)
    else:
        env = {k: v for k, v in os.environ.items() if k.lower() not in ("https_proxy", "http_proxy")}
        # the Pi's pens (aerapens behind its socket), when this webgate makes them
        pens = "--pens-socket" in open(WEBGATE).read() and os.access(PENS, os.X_OK)
        sock = os.path.join(work, "pens.sock")
        penssock = None
        if pens:
            penssock = subprocess.Popen([sys.executable, os.path.join(HERE, "penssock.py"), sock, PENS],
                                        stdout=subprocess.PIPE, text=True)
            pens = penssock.stdout.readline().strip() == "ready"
        gate = subprocess.Popen([sys.executable, WEBGATE, "--port", str(GATE), "--bind", "127.0.0.1", "--allow-local",
                                 "--cafile", os.path.join(work, "CACERT.PEM")] + (["--pens-socket", sock] if pens else []),
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        g = "127.0.0.1:%d" % GATE
        try:
            import time
            for _ in range(50):
                rc, out = run("-g", g, "http://localhost:%d/page" % HTTP)
                if "200 OK" in out:
                    break
                time.sleep(0.1)
            rc, out = run("-g", g, "-n", "2", "-o", "gw.htm", "https://localhost:%d/page" % HTTPS)
            check("the gateway: https", rc == 0 and "TLS 1.3" in out and "through the gateway" in out
                  and "the connection was kept open" in out and "unpack" in out and saved("gw.htm") == server.PAGE,
                  out)
            rc, out = run("-g", g, "-o", "gwmoved.htm", "http://localhost:%d/secure" % HTTP)
            check("the gateway: http to https", rc == 0 and "after 1 redirect" in out
                  and saved("gwmoved.htm") == server.PAGE, out)
            rc, out = run("-g", g, "-o", "gwbig.bin", "https://localhost:%d/chunked" % HTTPS)
            check("the gateway: a big answer, chunked", rc == 0 and saved("gwbig.bin") == server.BIG, out)
            rc, out = run("-g", g, "-o", "gwbad.txt", "https://localhost:%d/page" % BAD)
            check("the gateway: a certificate nobody signed", "502 Bad Gateway" in out
                  and saved("gwbad.txt").startswith(b"webgate: localhost has an untrusted certificate"), out)
            rc, out = run("-g", "127.0.0.1:1", "https://localhost:%d/page" % HTTPS)
            check("the gateway: not there", rc != 0 and "the gateway (127.0.0.1)" in out, out)
            if ewrender:
                p = subprocess.run([ewrender, "-f", font, "-W", g, "-v", "-d", "https://localhost:%d/site/pics.html"
                                    % HTTPS], cwd=work, capture_output=True, text=True, timeout=120)
                out = p.stdout + p.stderr
                check("the gateway: a page and its pictures", p.returncode == 0 and 'title "Pictures"' in out
                      and len(re.findall(r"^picture \S+ decoded", out, re.M)) == 5, out)
                if "X-Gateway-Picture" in open(WEBGATE).read():
                    # the Pi makes pictures as the page shows them: big.png
                    # (800 x 600) comes 608 wide, laid out at its own size
                    # as without the gateway; the WebP comes as JPEG
                    big = re.search(r"^image \S+ (\d+x\d+) link \d+ \"a big one\" \S+big\.png$", out, re.M)
                    check("the gateway: a big picture made smaller, at its own size on the page",
                          big is not None and big.group(1) == "596x447", out)
                    p = subprocess.run([ewrender, "-f", font, "-W", g, "-v", "-d",
                                        "https://localhost:%d/site/webp.html" % HTTPS], cwd=work,
                                       capture_output=True, text=True, timeout=120)
                    out = p.stdout + p.stderr
                    check("the gateway: a WebP made JPEG and shown", p.returncode == 0 and
                          re.search(r"^image \S+ 256x160 link \d+ \"webp\" \S+pic\.webp$", out, re.M), out)
                else:
                    print("itest: webgate makes no pictures (%s): those cases left out" % WEBGATE)
                def drawn(name, *more, page="pics.html"):
                    p = subprocess.run([ewrender, "-f", font, "-E", "-v"] + list(more) +
                                       ["http://localhost:%d/site/%s" % (HTTP, page)], cwd=work,
                                       capture_output=True, text=True, timeout=120)
                    return p.returncode, p.stdout + p.stderr, sorted(re.findall(r"^pens .*$", p.stdout, re.M))
                if not pens:
                    # a webgate without pens: asked for them, it sends what it did
                    rc0, out0, png0 = drawn("")
                    rc1, out1, png1 = drawn("", "-W", g, "-Q", "1")
                    check("the gateway: pens asked of a webgate without them, the pictures as before", rc0 == 0
                          and rc1 == 0 and len(png0) == 5 and len(png1) == 5 and "pens from the Pi" not in out1, out1)
                if pens:
                    # the Pi's pens: the page drawn in 16 colours through the
                    # gateway is the page drawn without it, to the pixel (the
                    # colours sent once on its 409; big.png, whose size the
                    # layout learns from the first answer, asked for again at
                    # the size shown); then from the disk cache
                    shown = {"photo.jpg": "192x128", "logo.png": "120x48", "anim.gif": "38x38",
                             "prog.jpg": "128x96", "big.png": "596x447"}
                    rc0, out0, png0 = drawn("pens0.png")
                    rc1, out1, png1 = drawn("pens1.png", "-W", g, "-Q", "1")
                    from_pi = re.findall(r"^picture \S+/(\S+) pens from the Pi at (\d+x\d+)", out1, re.M)
                    check("the gateway: pictures as the Pi's pens, the same to the pixel", rc0 == 0 and rc1 == 0
                          and len(png0) == len(shown) and png0 == png1 and sorted(from_pi) == sorted(shown.items()),
                          out0 + out1)
                    # kept: read back for the same pens; JPEG asked for, it is fetched
                    cache = os.path.join(work, "PENSCACHE")
                    shutil.rmtree(cache, ignore_errors=True)
                    outs = [drawn("", "-W", g, "-Q", q, "-C", cache, page="fresh.html") for q in ("1", "1", "0")]

                    def hits():
                        return urllib.request.build_opener(urllib.request.ProxyHandler({})).open(
                            "http://localhost:%d/site/hits/fresh/photo.jpg" % HTTP, timeout=10).read()
                    check("the gateway: the Pi's pens from the disk cache, not for the JPEG", all(o[0] == 0 for o in outs)
                          and outs[0][2] == outs[1][2] and len(outs[0][2]) == 1 and hits() == b"2"
                          and "pens from the Pi at 192x128" in outs[1][1]
                          and re.search(r"^picture \S+ decoded at 192x128", outs[2][1], re.M) is not None,
                          "\n".join(o[1] for o in outs) + "\nhits: %r" % hits())
                else:
                    print("itest: no pens on this webgate or no aerapens (make -C common/tools/pens): left out")
                p = subprocess.run([ewrender, "-f", font, "-W", g, "-d", "-S", "go",
                                    "https://localhost:%d/site/cookie/form.html" % HTTPS], cwd=work,
                                   capture_output=True, text=True, timeout=120)
                m = re.search(r'^sent: \d+ "(.*)" (\S+)$', p.stdout, re.M)
                check("the gateway: a login's POST, its 303 and cookie", p.returncode == 0 and m is not None
                      and m.group(1) == "COOKIE|sid=xyz", p.stdout + p.stderr)
                # the Refresh header comes through webgate as the server sent it
                rs = "https://localhost:%d/site/refresh/" % HTTPS
                p = subprocess.run([ewrender, "-f", font, "-W", g, "-d", "-r", "2", rs + "header.html"], cwd=work,
                                   capture_output=True, text=True, timeout=120)
                check("the gateway: a page's Refresh header passed on", p.returncode == 0 and
                      "refresh: 1 s %sto.html?from=header\n" % rs in p.stdout and
                      'refreshed: 0 "Arrived|from=header|%sheader.html" ' % rs in p.stdout, p.stdout + p.stderr)
        finally:
            gate.terminate()
            gate.wait(timeout=10)
            if penssock is not None:
                penssock.terminate()
                penssock.wait(timeout=10)
        if ewrender:
            snapshots()
finally:
    srv.stdin.close()
    srv.wait(timeout=10)

print("itest: %s" % ("all %d passed" % checks if not fails else "%d of %d failed" % (fails, checks)))
sys.exit(1 if fails else 0)
