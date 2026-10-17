#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes the packed bodies nettest.c's answers carry (gzip and deflate of a
known text, a gzip bomb, a damaged one) as C arrays: the same bytes every
time.
    tests/net/mkvec.py OUT.h"""
import gzip
import random
import sys
import zlib

rnd = random.Random(2026)
words = ["Falcon", "Atari", "EWeb", "page", "שלום", "TT-RAM", "68030", "DSP"]
text = " ".join(rnd.choice(words) for _ in range(3000)).encode("utf-8")
vec = {
    "text": text,
    "gz": gzip.compress(text, mtime=0),
    "zl": zlib.compress(text, 9),
    "bomb": gzip.compress(bytes(4 << 20), compresslevel=9, mtime=0),
}
bad = bytearray(vec["gz"])
bad[len(bad) // 2] ^= 0x55
vec["bad"] = bytes(bad)
with open(sys.argv[1], "w") as f:
    f.write("/* written by tests/net/mkvec.py: do not edit */\n")
    for name, data in vec.items():
        f.write("static const unsigned char v_%s[%d] = {" % (name, len(data)))
        for i, b in enumerate(data):
            f.write(("\n\t" if i % 16 == 0 else "") + "%d," % b)
        f.write("\n};\n")
