#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""make itest: ewget (EWeb's HTTP client, with AddressSanitizer and UBSan)
against the local servers of server.py, over TCP and TLS: pages plain and
packed, kept connections, a resumed TLS session, redirects from HTTP to
HTTPS, chunks, a charset in Content-Type, and the certificates that must be
refused (another host's, one nobody signed). With EWRENDER, the browser
too (src/browse, through tests/host/ewrender.c): a page and its
stylesheet over HTTP and HTTPS, its links made absolute, EWeb's own page
when a server isn't there and when what came isn't a page.

    tests/net/itest.py EWGET [WORK [EWRENDER EWEB.FNT]]"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import server  # noqa: E402

ewget = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "build/itest")
ewrender = os.path.abspath(sys.argv[3]) if len(sys.argv) > 4 else None
font = os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 else None
os.makedirs(work, exist_ok=True)
HTTP, HTTPS, BAD, EC = 18080, 18443, 18444, 18445
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
        rc, out = browse("http://localhost:%d/site/two.html#end" % HTTP)
        check("the browser: a page's anchor", rc == 0 and '"The end"' in out
              and "link 1 http://localhost:%d/site/\n" % HTTP in out, out)
        rc, out = browse("http://localhost:%d/redirect" % HTTP)
        check("the browser: a redirect", rc == 0 and 'title "EWeb test"' in out, out)
        rc, out = browse("http://localhost:%d/hebrew" % HTTP)
        check("the browser: a page in windows-1255", rc == 0 and re.search(r'rtl link 0 "\u05e9\u05dc\u05d5\u05dd"', out),
              out)
        rc, out = browse("http://localhost:1/")
        check("the browser: nobody listening", rc == 0 and "EWeb can't open this page" in out, out)
        rc, out = browse("http://localhost:%d/chunked" % HTTP)
        check("the browser: not a page", rc == 0 and "EWeb can't show this" in out
              and "application/octet-stream" in out, out)
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
        # cookies: set and sent back, a 303's cookie on the next request,
        # the lasting one kept in a file for the next run, another site's
        # picture's refused
        jar = os.path.join(work, "COOKIES.TXT")
        if os.path.exists(jar):
            os.remove(jar)
        site = "http://localhost:%d/site/cookie/" % HTTP

        def titles(*args):
            p = subprocess.run([ewrender, "-f", font, "-d"] + list(args), cwd=work, capture_output=True,
                               text=True, timeout=120)
            out = p.stdout + p.stderr
            return p.returncode, re.findall(r'^\w+: \d+ "(.*)" ', out, re.M), out

        rc, t, out = titles("-k", jar, "-N", site + "show", site + "set")
        check("cookies: set, and sent back", rc == 0 and t[:1] == ["COOKIE|lasting=1; session=2"], out)
        rc2, t2, out2 = titles("-k", jar, "-N", site + "show", "http://localhost:%d/site/" % HTTP)
        check("cookies: the lasting one kept for the next run", rc2 == 0 and t2[:1] == ["COOKIE|lasting=1"]
              and "lasting\t1" in open(jar).read() and "session" not in open(jar).read(), out2)
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
finally:
    srv.stdin.close()
    srv.wait(timeout=10)

print("itest: %s" % ("all %d passed" % checks if not fails else "%d of %d failed" % (fails, checks)))
sys.exit(1 if fails else 0)
