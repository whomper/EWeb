#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Makes EWEB.FNT, EWeb's own bitmap fonts, from the DejaVu fonts.

EWeb draws its pages' text itself, the same on every Falcon whether or not
a GDOS is installed (Erez's decision, 18 Oct 2026). The faces are DejaVu
Sans, Serif and Sans Mono, each in regular, bold, italic and bold italic,
drawn by FreeType (through Pillow) without smoothing at a set of pixel
sizes, for the Latin-1 and Latin Extended-A letters, Hebrew, and the
punctuation and signs pages use most, and in blocks of their own more
Latin letters, Greek, Cyrillic and the common symbols (Erez's choice, 24
Oct 2026: not Armenian, Georgian or the rarer symbols).

    tools/mkfont.py OUT.FNT [FONTDIR]

FONTDIR (default /usr/share/fonts/truetype/dejavu) needs the Debian
packages fonts-dejavu-core and fonts-dejavu-extra. The same fonts and
the same Pillow (tools/requirements.txt: its wheel carries the FreeType
that draws the glyphs) give the same glyphs, and with the same zlib (which
packs them) the same file, byte for byte; tests/layout's lists were made
with them. Glyphs a face lacks (DejaVu Serif has no
Hebrew) are DejaVu Sans's in the same style.

The characters come in blocks, each unpacked the first time a page needs
one of its characters: the first (Latin-1, Latin Extended-A, Hebrew and
the signs pages use most, BLOCKS[0]) holds what nearly every page shows;
the others (BLOCKS) more Latin letters and the combining accents, Greek,
Cyrillic, and symbols, so that a page that shows none costs no more than
before.

The file (big-endian, as the 68030 reads it):
    "EWFNT3\\0\\0", u16 faces, u16 blocks, u16 codes[blocks] (how many each),
    then each block's codes (u16, sorted)
    per face: u8 family, u8 style, u8 px, u8 0, then for each block:
              u32 offset, u32 stored, u32 size
    at offset: the block, DEFLATE'd (raw, RFC 1951: stored bytes that
    unpack to size). Unpacked:
               u8 ascent, u8 descent, u16 0 (the face's; 0 in the other blocks),
               per code: u8 advance, s8 x, s8 y (top, from the baseline),
                         u8 w, u8 h, u8 0, u32 bitmap (from the block's start)
               bitmaps: h rows of (w + 7) / 8 bytes, the left pixel in bit 7
"EWFNT2" was the same with one block.
"""
import os
import struct
import sys
import zlib

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
          0x2212, 0x25A0, 0x25AA, 0x25B2, 0x25B6, 0x25B8, 0x25BC, 0x25BE, 0x25C0, 0x25C2,
          0x25CB, 0x25CF, 0x25E6, 0x2713, 0x2715, 0xFFFD}             # ▸ ▾ ◂: summaries' triangles
    c |= {0x152, 0x153, 0x160, 0x161, 0x178, 0x17D, 0x17E, 0x192, 0x2C6, 0x2DC}
    return sorted(c)


# the blocks after the first: (name, ranges); each keeps the characters
# DejaVu Sans has that the first block hasn't
SYMBOLS = (  # the common ones (B, not taken, had about 540)
    [0x2016, 0x2023, 0x2024, 0x2025, 0x2027, 0x2031, 0x2034, 0x2035, 0x203B, 0x203C, 0x203D, 0x203E,
     0x2047, 0x2048, 0x2049] +                                                                  # ‖ ‣ ‱ ‴ ※ ‼ ‽ ‾ ⁇ ⁈ ⁉
    list(range(0x2070, 0x208F)) + list(range(0x20A0, 0x20C1)) +                                 # super- and subscripts, currency
    [0x2103, 0x2109, 0x2113, 0x2117, 0x2120, 0x2126, 0x212E] + list(range(0x2150, 0x215F)) +    # ℃ ℉ ℓ ℗ ℠ Ω ℮, ⅐-⅞
    [0x2194, 0x2195, 0x2196, 0x2197, 0x2198, 0x2199, 0x21A9, 0x21AA, 0x21BA, 0x21BB, 0x21C4, 0x21C6,
     0x21D0, 0x21D1, 0x21D2, 0x21D3, 0x21D4] +                                                  # ↔ ↕ ↖ ↗ ↘ ↙ ↩ ↪ ↺ ↻ ⇄ ⇆ ⇐ ⇑ ⇒ ⇓ ⇔
    [0x2200, 0x2202, 0x2203, 0x2205, 0x2206, 0x2207, 0x2208, 0x2209, 0x220B, 0x220F, 0x2211, 0x2213,
     0x2215, 0x2217, 0x2218, 0x2219, 0x221A, 0x221D, 0x221E, 0x221F, 0x2220, 0x2221, 0x2225, 0x2227,
     0x2228, 0x2229, 0x222A, 0x222B, 0x222C, 0x222E, 0x2234, 0x2235, 0x2236, 0x223C, 0x2243, 0x2245,
     0x2248, 0x2260, 0x2261, 0x2264, 0x2265, 0x226A, 0x226B, 0x2282, 0x2283, 0x2286, 0x2287, 0x2295,
     0x2297, 0x22A5, 0x22C5, 0x22EE, 0x22EF] +                                                  # ∀ ∂ ∃ ∅ ∆ ∇ ∈ ∑ √ ∞ ∫ ≈ ≠ ≤ ≥ ⊂ ⊕ ⋅ ⋯
    [0x2302, 0x2318, 0x2325, 0x2326, 0x232B, 0x23CE, 0x23CF] + list(range(0x2460, 0x2474)) +     # ⌂ ⌘ ⌥ ⌦ ⌫ ⏎ ⏏, ①-⑳
    [0x25A1, 0x25AB, 0x25AC, 0x25B3, 0x25B4, 0x25B5, 0x25B7, 0x25B9, 0x25BA, 0x25BD, 0x25BF, 0x25C1,
     0x25C3, 0x25C4, 0x25C6, 0x25C7, 0x25C8, 0x25C9, 0x25CA, 0x25CE, 0x25D0, 0x25D1, 0x25EF] +  # □ △ ▷ ► ▽ ◁ ◄ ◆ ◇ ◊ ◎ ◐ ◯
    [0x2600, 0x2601, 0x2602, 0x2603, 0x2605, 0x2606, 0x260E, 0x2610, 0x2611, 0x2612, 0x261B, 0x261E,
     0x2620, 0x2622, 0x262E, 0x262F, 0x2630, 0x2639, 0x263A, 0x263B, 0x2640, 0x2642] + list(range(0x2660, 0x2670)) +
    [0x26A0, 0x26A1, 0x26AA, 0x26AB, 0x2708, 0x2709, 0x270C, 0x270E, 0x270F, 0x2712, 0x2714, 0x2716, 0x2717,
     0x2718, 0x271A, 0x2726, 0x2727, 0x2729, 0x272A, 0x272F, 0x2730, 0x2744, 0x2756, 0x2764, 0x2765, 0x2766,
     0x2794, 0x279C, 0x279D, 0x279E, 0x27A1, 0x27A4])                                            # ☀ ★ ☎ ☑ ♥ ⚠ ✈ ✉ ✔ ✖ ❤ ➔ ➜
BLOCKS = [
    ("latin", [(0x180, 0x250), (0x2B9, 0x2DE), (0x300, 0x334), (0x1E80, 0x1E86), (0x1E9E, 0x1E9F),
               (0x1EA0, 0x1EFA), (0xFB00, 0xFB07)]),                       # Romanian, pinyin, accents, Welsh, Vietnamese
    ("greek", [(0x370, 0x400)]),
    ("cyrillic", [(0x400, 0x460), (0x490, 0x494), (0x496, 0x49C), (0x4A2, 0x4A4), (0x4AE, 0x4B4),
                  (0x4BA, 0x4BC), (0x4D8, 0x4DA), (0x4E8, 0x4EA)]),       # Russian, Ukrainian, Belarusian, Serbian, Kazakh...
    ("symbols", [(c, c + 1) for c in SYMBOLS]),
]


def blocks(fontdir):
    """the code lists, the first block's first"""
    first = codes()
    f = ImageFont.truetype(os.path.join(fontdir, "DejaVuSans.ttf"), 16, layout_engine=ImageFont.Layout.BASIC)
    notdef = signature(f, "\ue000")
    out = [first]
    have = set(first)
    for _, ranges in BLOCKS:
        cs = []
        for lo, hi in ranges:
            for cp in range(lo, hi):
                if cp not in have and signature(f, chr(cp)) != notdef:
                    cs.append(cp)
                    have.add(cp)
        out.append(sorted(cs))
    return out


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
    lists = blocks(fontdir)
    faces = []
    for fam, stems in FAMILIES:
        for style, stem in enumerate(stems):
            path = os.path.join(fontdir, stem + ".ttf")
            if not os.path.exists(path):
                sys.exit("mkfont: %s is missing (fonts-dejavu-core and fonts-dejavu-extra)" % path)
            fallback = os.path.join(fontdir, FAMILIES[0][1][style] + ".ttf")
            for px in SIZES:
                made = []
                for k, cps in enumerate(lists):
                    asc, desc, glyphs = render(path, px, cps, fallback if fallback != path else None)
                    head = struct.pack(">BBH", asc, desc, 0) if k == 0 else struct.pack(">BBH", 0, 0, 0)
                    metrics = b""
                    bitmaps = b""
                    base = 4 + 10 * len(cps)
                    for adv, x, y, w, h, bits in glyphs:
                        metrics += struct.pack(">BbbBBBI", adv, x, y, w, h, 0, base + len(bitmaps) if bits else 0)
                        bitmaps += bits
                    made.append(head + metrics + bitmaps)
                faces.append((fam, style, px, made))
    data = b"EWFNT3\0\0" + struct.pack(">HH", len(faces), len(lists))
    data += b"".join(struct.pack(">H", len(cps)) for cps in lists)
    data += b"".join(struct.pack(">H", c) for cps in lists for c in cps)
    off = len(data) + (4 + 12 * len(lists)) * len(faces)
    dirs = b""
    packs = b""
    for fam, style, px, made in faces:
        dirs += struct.pack(">BBBB", fam, style, px, 0)
        for block in made:
            z = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
            packed = z.compress(block) + z.flush()
            dirs += struct.pack(">III", off + len(packs), len(packed), len(block))
            packs += packed
    with open(out, "wb") as f:
        f.write(data + dirs + packs)
    print("%s: %d faces, %d characters in %d blocks (%s), %d KB" % (
        out, len(faces), sum(len(c) for c in lists), len(lists), " ".join(str(len(c)) for c in lists),
        (len(data + dirs + packs) + 1023) // 1024))


if __name__ == "__main__":
    main()
