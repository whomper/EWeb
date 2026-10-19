#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Makes the HTML parser's table of named character references
(src/doc/html.c) from src/doc/entities.txt:

    tools/mkentities.py src/doc/entities.txt build/gen/entities.h

The names are sorted byte by byte, as html.c's search wants them, and kept
in one string; each entry has the offset of its name, the name's length and
its code points (the second 0 when there is one)."""
import sys

src, out = sys.argv[1], sys.argv[2]
ents = []
for line in open(src, encoding='ascii'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    name, *cps = line.split()
    cps = [int(c, 16) for c in cps]
    assert len(cps) in (1, 2) and (len(cps) == 1 or cps[1] < 0x10000), line
    ents.append((name.encode('ascii'), cps + [0] * (2 - len(cps))))
ents.sort(key=lambda e: e[0])
names = b''.join(e[0] for e in ents)
with open(out, 'w', encoding='ascii') as f:
    f.write('/* SPDX-License-Identifier: GPL-2.0-or-later\n'
            ' * Copyright (C) 2026 AERA | Erez Yaary */\n'
            '/* made by tools/mkentities.py from src/doc/entities.txt */\n')
    f.write('#define ENT_N %d\n#define ENT_LONGEST %d\n' %
            (len(ents), max(len(e[0]) for e in ents)))
    f.write('static const char ent_names[] =\n')
    for i in range(0, len(names), 64):
        f.write('\t"%s"\n' % names[i:i + 64].decode('ascii'))
    f.write(';\n')
    f.write('static const struct { uint16_t off; uint8_t len; uint32_t cp; uint16_t cp2; } ent[ENT_N] = {\n')
    off = 0
    for name, cps in ents:
        f.write('\t{ %d, %d, 0x%X, 0x%X },\n' % (off, len(name), cps[0], cps[1]))
        off += len(name)
    f.write('};\n')
