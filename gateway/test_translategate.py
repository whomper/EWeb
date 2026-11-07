#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translategate.py on this computer, with the stand-in engine: the
protocol (AERA-Atari standards/TRANSLATION-SERVICE.md), the text's shape,
the limits and refusals, a dropped job, a short request beside a long one,
and that no word of the text is ever logged or sent back in a refusal.

    python3 -m unittest discover -s gateway -p 'test_*.py'
"""
import contextlib
import io
import os
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translategate as tg  # noqa: E402

SECRET = "Pomegranate"      # a word that must never be logged


class Log(io.StringIO):
    """stderr while a test runs: everything aeragate.say() and Python wrote"""


class Gate:
    def __init__(self, engine=None, seconds=60, allow=None):
        tg.Config.engine = engine or tg.StandIn()
        tg.Config.seconds = seconds
        tg.Config.allow = allow or []
        tg.LANES = tg.Lanes(2)
        self.srv = tg.Server(("127.0.0.1", 0), tg.Handler)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()

    def raw(self, data, wait=True):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        s.sendall(data)
        if not wait:
            return s
        out = b""
        while True:
            b = s.recv(65536)
            if not b:
                break
            out += b
        s.close()
        return out

    def ask(self, path, body=None, headers=None):
        """(status, headers dict, body bytes)"""
        if body is None:
            req = "GET %s HTTP/1.0\r\nHost: pi\r\n\r\n" % path
            data = req.encode()
        else:
            b = body.encode() if isinstance(body, str) else body
            hs = headers if headers is not None else {"Content-Length": str(len(b))}
            req = "POST %s HTTP/1.0\r\nHost: pi\r\nContent-Type: text/plain; charset=utf-8\r\n" % path
            req += "".join("%s: %s\r\n" % kv for kv in hs.items()) + "\r\n"
            data = req.encode() + b
        out = self.raw(data)
        head, _, rest = out.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        status = int(lines[0].split()[1])
        hd = {}
        for l in lines[1:]:
            k, _, v = l.partition(":")
            hd[k.strip().lower()] = v.strip()
        return status, hd, rest


class Base(unittest.TestCase):
    engine = None

    def setUp(self):
        self.log = Log()
        self.redirect = contextlib.redirect_stderr(self.log)
        self.redirect.__enter__()
        self.gate = Gate(self.engine() if self.engine else None)

    def tearDown(self):
        self.gate.close()
        self.redirect.__exit__(None, None, None)
        # the rule of rules: nothing of the text, in anything logged
        self.assertNotIn(SECRET.lower(), self.log.getvalue().lower())

    def tr(self, text, to="en", src=None):
        q = "/translate?to=%s" % to + ("&from=%s" % src if src else "")
        st, hd, body = self.gate.ask(q, text)
        return st, hd, body.decode("utf-8")


class ProtocolTest(Base):
    def test_status(self):
        st, _, body = self.gate.ask("/status")
        self.assertEqual(st, 200)
        self.assertEqual(body, b"translategate 1\nengine stand-in 1\nready\n")

    def test_languages(self):
        st, _, body = self.gate.ask("/languages")
        self.assertEqual(st, 200)
        self.assertEqual(body.decode().splitlines(), ["de\tGerman", "en\tEnglish", "he\tHebrew"])

    def test_translate(self):
        st, hd, body = self.tr("Hello %s." % SECRET, src="de")
        self.assertEqual(st, 200)
        self.assertEqual(hd["x-translate-from"], "de")
        self.assertEqual(hd["content-type"], "text/plain; charset=utf-8")
        self.assertEqual(int(hd["content-length"]), len(body.encode()))
        self.assertEqual(body, "en: HELLO %s." % SECRET.upper())
        self.assertIn("de>en 200,", self.log.getvalue())

    def test_detect(self):
        st, hd, body = self.tr("שלום, מה שלומך היום? %s" % SECRET)
        self.assertEqual((st, hd["x-translate-from"]), (200, "he"))
        self.assertTrue(body.startswith("en: שלום, מה"))
        st, _, body = self.tr("(see https://example.com/a(b)) and [www.example.com].", src="de")
        self.assertEqual(body, "en: SEE https://example.com/a(b) AND www.example.com.")

    def test_same(self):
        st, hd, body = self.tr("Good morning, %s." % SECRET)
        self.assertEqual(st, 200)
        self.assertEqual((hd["x-translate-from"], hd["x-translate-same"]), ("en", "1"))
        self.assertEqual((body, hd["content-length"]), ("", "0"))
        st, hd, body = self.tr("Good morning.", src="he", to="he")
        self.assertEqual((st, hd.get("x-translate-same"), body), (200, "1", ""))

    def test_refusals(self):
        def one_line(st, body, code):
            self.assertEqual(st, code)
            self.assertTrue(body.startswith(b"translategate: ") and body.count(b"\n") == 1, body)
            self.assertNotIn(SECRET.encode(), body)
        st, _, b = self.gate.ask("/translate?to=EN", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en&x=1", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?from=de", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", SECRET, headers={})
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", b"\xff\xfe" + SECRET.encode())
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", SECRET * 1000)
        one_line(st, b, 413)
        st, _, b = self.gate.ask("/translate?to=fr", "Hallo %s, das ist nicht gut." % SECRET)
        one_line(st, b, 422)
        st, _, b = self.gate.ask("/nothing")
        one_line(st, b, 404)

    def test_limit_exactly(self):
        text = ("word " * 2000)[:8192]
        st, _, _ = self.tr(text, src="de")
        self.assertEqual(st, 200)

    def test_not_from_home(self):
        tg.Config.allow = ["192.168.1.99"]
        st, _, b = self.gate.ask("/status")
        self.assertEqual(st, 403)


class ShapeTest(Base):
    def test_paragraphs_kept(self):
        text = "One %s.\n\nTwo.\r\n\r\n\n  \nThree.\n" % SECRET
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: ONE %s.\n\nen: TWO.\r\n\r\n\n  \nen: THREE.\n" % SECRET.upper())

    def test_lines_joined(self):
        text = ("This is a letter that was wrapped at about seventy-two columns by\n"
                "the mailer, so its lines must be joined before they are translated.\n\nBye.")
        st, _, body = self.tr(text, src="de")
        first, rest = body.split("\n\n")
        self.assertEqual(first, "en: THIS IS A LETTER THAT WAS WRAPPED AT ABOUT SEVENTY-TWO COLUMNS BY "
                                "THE MAILER, SO ITS LINES MUST BE JOINED BEFORE THEY ARE TRANSLATED.")
        self.assertEqual(rest, "en: BYE.")

    def test_short_lines_kept(self):
        text = "Erez Yaary\n12 Long Street\nSmalltown\n\nRoses are red,\nviolets are blue"
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: EREZ YAARY\nen: 12 LONG STREET\nen: SMALLTOWN\n\n"
                               "en: ROSES ARE RED,\nen: VIOLETS ARE BLUE")

    def test_quotes(self):
        text = ("> I wrote this a long time ago and it was wrapped by the mailer at\n"
                "> seventy-two columns, as letters are.\n\n>> short\n>> lines\n")
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "> en: I WROTE THIS A LONG TIME AGO AND IT WAS WRAPPED BY THE MAILER AT "
                               "SEVENTY-TWO COLUMNS, AS LETTERS ARE.\n\n>> en: SHORT\n>> en: LINES\n")

    def test_kept_pieces(self):
        text = ("See https://example.com/a(b)?x=[1], write to some.one@example.org, "
                "it costs $12.50 or 45 € and weighs 3.5 kg at 20%.")
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: SEE https://example.com/a(b)?x=[1], WRITE TO some.one@example.org, "
                               "IT COSTS $12.50 OR 45 € AND WEIGHS 3.5 kg AT 20%.")

    def test_only_kept(self):
        st, _, body = self.tr("https://example.com/\n", src="de")
        self.assertEqual(body, "https://example.com/\n")

    def test_restore_lost_mark(self):
        text, kept = tg.protect("mail a@b.org now")
        self.assertEqual(text, "mail ZQX0QZ now")
        self.assertEqual(tg.restore("MAIL zqx 0 qz NOW", kept), "MAIL a@b.org NOW")
        self.assertEqual(tg.restore("MAIL NOW", kept), "MAIL NOW a@b.org")
        self.assertEqual(tg.restore("ZQX0QZ ZQX0QZ ZQX7QZ", kept), "a@b.org  ")

    def test_mark_survives_engine(self):
        # the stand-in drops brackets and changes case, as real engines may
        out = tg.StandIn().translate([tg.MARK % 3], "de", "en")[0]
        self.assertEqual(tg.MARK_BACK.search(out).group(1), "3")


class FakeModel:
    """CTranslate2 and SentencePiece as far as Argos.translate uses them:
    the "translation" is the source in capitals, a word alone said twice
    (as the small models do)"""
    def __init__(self):
        self.fed = []

    def encode(self, s, out_type=str):
        return s.split()

    def decode(self, toks):
        return " ".join(toks)

    def translate_batch(self, toks, **kw):
        class Hyp:
            def __init__(self, h):
                self.hypotheses = [h]
        self.fed += [" ".join(t) for t in toks]
        return [Hyp([w.upper() for w in t] * (2 if len(t) == 1 else 1)) for t in toks]


class ArgosShapeTest(unittest.TestCase):
    def setUp(self):
        self.eng = tg.Argos.__new__(tg.Argos)
        self.model = FakeModel()
        self.eng.pair = {("he", "en"): (self.model, self.model), ("en", "he"): (self.model, self.model)}

    def test_closings(self):
        self.assertEqual(tg.closing("he", "en", "בברכה,"), "Best regards,")
        self.assertEqual(tg.closing("en", "he", "Kind Regards"), "בברכה")
        self.assertIsNone(tg.closing("en", "he", "Kind regards to your mother"))
        self.assertEqual(self.eng.translate(["בברכה,", "Thanks!"], "he", "en"), ["Best regards,", "THANKS!"])
        self.assertEqual(self.model.fed, ["Thanks!"])

    def test_short_gets_a_full_stop(self):
        self.assertEqual(self.eng.translate(["Dana", "new order", "One. Two words here."], "en", "he"),
                         ["DANA", "NEW ORDER", "ONE. TWO WORDS HERE."])
        self.assertEqual(self.model.fed, ["Dana.", "new order.", "One.", "Two words here."])
        self.assertTrue(tg.short("Re: the old Falcon"))
        self.assertFalse(tg.short("Thanks,"))
        self.assertFalse(tg.short("one two three four five"))


class Slow(tg.StandIn):
    def __init__(self):
        super().__init__(delay=0.6)


class JobTest(Base):
    engine = Slow

    def test_dropped(self):
        body = ("Long text %s. " % SECRET * 5 + "\n\n") * 4
        b = body.encode()
        s = self.gate.raw(b"POST /translate?to=en&from=de HTTP/1.0\r\nContent-Length: %d\r\n\r\n" % len(b) + b,
                          wait=False)
        time.sleep(0.3)
        s.close()
        for _ in range(50):
            if "dropped" in self.log.getvalue():
                break
            time.sleep(0.1)
        line = [l for l in self.log.getvalue().splitlines() if "dropped" in l]
        self.assertEqual(line, ["translategate: 127.0.0.1 de>en dropped, %d bytes" % len(b)])

    def test_time_limit(self):
        tg.Config.seconds = 1
        st, _, body = self.tr("One.\n\nTwo.\n\nThree.", src="de")
        self.assertEqual((st, body), (504, "translategate: it took over 1 s\n"))

    def test_short_beside_long(self):
        long_text = ("Paragraph %s.\n\n" % SECRET) * 6       # 6 x 0.6 s
        got = {}

        def long_one():
            got["long"] = self.tr(long_text, src="de")
        t = threading.Thread(target=long_one)
        t.start()
        time.sleep(0.2)
        t0 = time.monotonic()
        st, _, body = self.tr("Subject line", src="de")
        took = time.monotonic() - t0
        self.assertEqual((st, body), (200, "en: SUBJECT LINE"))
        self.assertLess(took, 1.5)
        t.join()
        self.assertEqual(got["long"][0], 200)


class LoadingTest(Base):
    engine = staticmethod(lambda: tg.StandIn(load=30))

    def test_loading(self):
        st, _, body = self.gate.ask("/status")
        self.assertEqual(body, b"translategate 1\nengine stand-in 1\nloading\n")
        st, _, body = self.tr("Hello %s" % SECRET, src="de")
        self.assertEqual((st, body), (503, "translategate: the engine is loading\n"))


class Broken(tg.StandIn):
    def translate(self, texts, src, dst, check=lambda: None):
        raise ValueError("cannot translate %r" % texts)


class CrashTest(Base):
    engine = Broken

    def test_crash_says_nothing(self):
        st, _, body = self.tr("Hello %s" % SECRET, src="de")
        self.assertEqual((st, body), (500, "translategate: the engine failed\n"))
        self.assertIn("(ValueError)", self.log.getvalue())


if __name__ == "__main__":
    unittest.main()
