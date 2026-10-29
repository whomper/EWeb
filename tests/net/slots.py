# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The rigs' reading of a file EWeb keeps in two slots (src/net/slots.c:
the cache's index, COOKIES.TXT, HISTORY.TXT): NAME and its twin (NAME.TX2
for NAME.TXT), each "EWS1 GEN LEN\\r\\n" and its data, the newer whole one
read; a file of before slots (no head) is the oldest."""
import os


def twin(path):
    d, b = os.path.split(path)
    if "." not in b[1:]:
        return os.path.join(d, b + ".2")
    if b.endswith("."):
        return os.path.join(d, b + "2")
    return os.path.join(d, b[:-1] + ("3" if b[-1] == "2" else "2"))


def _look(path, legacy):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if data[:5] == b"EWS1 " and len(data) >= 24 and data[22:24] == b"\r\n":
        try:
            gen, n = int(data[5:13], 16), int(data[14:22], 16)
        except ValueError:
            return None
        return (gen, data[24:24 + n]) if n <= len(data) - 24 else None
    if legacy and data[:3] != b"EWS":
        return (0, data)
    return None


def read(path):
    """the newer slot's data (bytes), or None when there is none"""
    a, b = _look(path, True), _look(twin(path), False)
    if b is not None and (a is None or b[0] > a[0]):
        return b[1]
    return a[1] if a is not None else None


def exists(path):
    return read(path) is not None


def remove(path):
    for p in (path, twin(path)):
        if os.path.exists(p):
            os.remove(p)
