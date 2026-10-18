#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Makes EWEB.FNT, EWeb's own bitmap fonts, from the DejaVu fonts.

EWeb draws its pages' text itself, the same on every Falcon whether or not
a GDOS is installed (Erez's decision, 18 Oct 2026). The faces are DejaVu
Sans, Serif and Sans Mono, each in regular, bold, italic and bold italic,
drawn by FreeType (through Pillow) without smoothing at a set of pixel
sizes, for the Latin-1 and Latin Extended-A letters, Hebrew, and the
punctuation and signs pages use most.

    tools/mkfont.py OUT.FNT [FONTDIR]

FONTDIR (default /usr/share/fonts/truetype/dejavu) needs the Debian
packages fonts-dejavu-core and fonts-dejavu-extra. The same fonts and
the same Pillow (tools/requirements.txt: its wheel carries the FreeType
that draws the glyphs) give the same file, byte for byte; tests/layout's
lists were made with them. Glyphs a face lacks (DejaVu Serif has no
Hebrew) are DejaVu Sans's in the same style.

The file (big-endian, as the 68030 reads it):
    "EWFNT1\\0\\0", u16 faces, u16 codes, u16 code[codes] (sorted)
    per face: u8 family, u8 style, u8 px, u8 0, u32 offset, u32 size
    at offset: u8 ascent, u8 descent, u16 0,
               per code: u8 advance, s8 x, s8 y (top, from the baseline),
                         u8 w, u8 h, u8 0, u32 bitmap (from the face's start)
               bitmaps: h rows of (w + 7) / 8 bytes, the left pixel in bit 7
"""
import os
import struct
import sys

import PIL
from PIL import Image, ImageDraw, ImageFont

PILLOW = "12.3.0"  # tools/requirements.txt

FAMILIES = [  # family number, file stems for regular, bold, italic, bold italic
    (0, ["DejaVuSans", "DejaVuSans-Bold", "DejaVuSans-Oblique", "DejaVuSans-BoldOblique"]),
    (1, ["DejaVuSerif", "DejaVuSerif-Bold", "DejaVuSerif-Italic", "DejaVuSerif-BoldItalic"]),
    (2, ["DejaVuSansMono", "DejaVuSansMono-Bold", "DejaVuSansMono-Oblique",
         "DejaVuSansMono-BoldOblique"]),
]
SIZES = [9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32]


def codes():
    c = set(range(0x20, 0x7F)) | set(range(0xA0, 0x180))
    c |= set(range(0x5D0, 0x5EB)) | {0x5BE, 0x5F3, 0x5F4}          # Hebrew letters, maqaf, geresh
    c |= {0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2015, 0x2018, 0x2019, 0x201A, 0x201C,
          0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2032, 0x2033, 0x2039,
          0x203A, 0x2044, 0x20AA, 0x20AC, 0x2116, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193,
          0x2212, 0x25A0, 0x25AA, 0x25B2, 0x25B6, 0x25BC, 0x25C0, 0x25CB, 0x25CF, 0x25E6,
          0x2713, 0x2715, 0xFFFD}
    c |= {0x152, 0x153, 0x160, 0x161, 0x178, 0x17D, 0x17E, 0x192, 0x2C6, 0x2DC}
    return sorted(c)


def signature(f, ch):
    """what FreeType draws for ch: to tell a missing letter (the font's
    "not defined" box) from a real one"""
    box = f.getbbox(ch, anchor="ls", mode="1")
    return box, f.getlength(ch, mode="1"), bytes(f.getmask(ch, mode="1"))


def render(path, px, cps, fallback=None):
    # the basic layout, for each glyph's hinted advance: the one its
    # one-bit picture was drawn for
    f = ImageFont.truetype(path, px, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = f.getmetrics()
    # letters the face hasn't got (DejaVu Serif has no Hebrew) come from
    # the fallback (DejaVu Sans in the same style), at the same size
    fb = ImageFont.truetype(fallback, px, layout_engine=ImageFont.Layout.BASIC) if fallback else None
    notdef = signature(f, "\ue000")
    face = f
    glyphs = []
    for cp in cps:
        ch = chr(cp)
        f = face
        if fb is not None and cp > 0x7f and signature(face, ch) == notdef:
            f = fb
        adv = int(round(f.getlength(ch, mode="1")))
        l, t, r, b = f.getbbox(ch, anchor="ls", mode="1")
        w, h = r - l, b - t
        bits = b""
        if w > 0 and h > 0 and ch != " ":
            # drawn with a margin: the hinted ink can reach past FreeType's box
            m = px // 2 + 2
            l -= m
            t -= m
            img = Image.new("1", (w + 2 * m, h + 2 * m), 0)
            d = ImageDraw.Draw(img)
            d.fontmode = "1"
            d.text((-l, -t), ch, font=f, fill=1, anchor="ls")
            # trim the empty rows and columns round the ink
            box = img.getbbox()
            if box is None:
                w = h = 0
            else:
                img = img.crop(box)
                l += box[0]
                t += box[1]
                w, h = img.size
                rows = []
                stride = (w + 7) // 8
                px_ = img.load()
                for y in range(h):
                    row = bytearray(stride)
                    for x in range(w):
                        if px_[x, y]:
                            row[x >> 3] |= 0x80 >> (x & 7)
                    rows.append(bytes(row))
                bits = b"".join(rows)
        else:
            w = h = 0
        clamp = lambda v, lo, hi: max(lo, min(hi, v))
        glyphs.append((clamp(adv, 0, 255), clamp(l, -128, 127), clamp(t, -128, 127),
                       clamp(w, 0, 255), clamp(h, 0, 255), bits))
    return min(ascent, 255), min(descent, 255), glyphs


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    if PIL.__version__ != PILLOW:
        print("mkfont: Pillow %s, not %s (tools/requirements.txt): the glyphs may come out otherwise "
              "than tests/layout's lists expect" % (PIL.__version__, PILLOW), file=sys.stderr)
    fontdir = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/truetype/dejavu"
    cps = codes()
    faces = []
    for fam, stems in FAMILIES:
        for style, stem in enumerate(stems):
            path = os.path.join(fontdir, stem + ".ttf")
            if not os.path.exists(path):
                sys.exit("mkfont: %s is missing (fonts-dejavu-core and fonts-dejavu-extra)" % path)
            fallback = os.path.join(fontdir, FAMILIES[0][1][style] + ".ttf")
            for px in SIZES:
                asc, desc, glyphs = render(path, px, cps, fallback if fallback != path else None)
                head = struct.pack(">BBH", asc, desc, 0)
                metrics = b""
                bitmaps = b""
                base = 4 + 10 * len(cps)
                for adv, x, y, w, h, bits in glyphs:
                    metrics += struct.pack(">BbbBBBI", adv, x, y, w, h, 0, base + len(bitmaps) if bits else 0)
                    bitmaps += bits
                faces.append((fam, style, px, head + metrics + bitmaps))
    data = b"EWFNT1\0\0" + struct.pack(">HH", len(faces), len(cps))
    data += b"".join(struct.pack(">H", c) for c in cps)
    off = len(data) + 12 * len(faces)
    dirs = b""
    blocks = b""
    for fam, style, px, block in faces:
        dirs += struct.pack(">BBBBII", fam, style, px, 0, off + len(blocks), len(block))
        blocks += block
    with open(out, "wb") as f:
        f.write(data + dirs + blocks)
    print("%s: %d faces, %d characters, %d KB" % (out, len(faces), len(cps), (len(data + dirs + blocks) + 1023) // 1024))


if __name__ == "__main__":
    main()
