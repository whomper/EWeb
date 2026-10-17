#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes a table of LibCSS's computed-value constants and their CSS names,
for csscmp to compare LibCSS's styles with EWeb's engine's by name.

    tests/css/lcnames.py third_party/netsurf/libcss/include/libcss/properties.h OUT

Each `enum css_<property>_e` gives entries { "<property>", CSS_..., "<name>" },
the constant left for the compiler to resolve (some are defined as others)."""
import re
import sys

src, out = sys.argv[1], sys.argv[2]
text = open(src).read()
rows = []
for m in re.finditer(r"enum css_(\w+)_e\s*\{(.*?)\};", text, re.S):
    prop = m.group(1)
    prefix = "CSS_" + prop.upper() + "_"
    for c in re.finditer(r"\b(" + prefix + r"\w+)\s*=", m.group(2)):
        const = c.group(1)
        name = const[len(prefix):].lower().replace("_", "-")
        rows.append('\t{ "%s", %s, "%s" },' % (prop.replace("_", "-"), const, name))
with open(out, "w") as f:
    f.write("/* made by tests/css/lcnames.py from LibCSS's properties.h */\n")
    f.write("\n".join(rows) + "\n")
