#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkhome.py - EWeb's home page (about:home, the start page) as C strings:
src/browse/home.html with its pictures put in as data: addresses and the
white space between its tags taken out, cut at the places EWeb fills in as
it makes the page (<!--NAME-->: the version, the greeting, the search box,
your pages, the tip). Written as build/gen/home.h for src/browse/browse.c.

The pictures (@NAME@ in the page):
    GLOBE   EWeb's globe, the family's drawing (common/icons/EWEB.txt,
            its 256-colour image), three times its size
    WORD    the EWeb wordmark, src/browse/home/eweb.png
    AERA    the AERA logo, src/browse/home/aera.png
    RULER   the copper tick ruler under the top, tiled
each made solid on the page's black, so that every screen draws it the
same (no edge mixed with what is under it).

    python3 tools/mkhome.py src/browse/home.html build/gen/home.h [PAGE.HTML]

PAGE.HTML: the page itself as well, its parts filled in as EWeb fills them
for a machine with five pages in its history and as many bookmarks (for
tests/layout and for measuring it).

The wordmark and the logo are made once from AERA's brand files (Oxanium
ExtraBold, OFL; the AERA logo) and kept in src/browse/home/:

    python3 tools/mkhome.py --assets OXANIUM_800.TTF AERA-LOGO-LIGHT.PNG
"""
import base64
import io
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BLACK = (0, 0, 0)
TEXT = (0xe5, 0xe5, 0xe5)               # AERA's text colour
COPPER = (0xb8, 0x73, 0x33)             # AERA's copper (structure: the ruler)
SLOTS = ["VERSION", "HELLO", "SEARCH", "YOURS", "TIP"]


def png(im):
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return b.getvalue()


def data(im):
    return "data:image/png;base64," + base64.b64encode(png(im)).decode()


def globe():
    sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
    import appicon
    rows = appicon.load("EWEB")[256]
    im = Image.new("RGB", (appicon.W, appicon.H), BLACK)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != ".":
                im.putpixel((x, y), appicon.hx(appicon.C256[c]))
    return im.resize((appicon.W * 3, appicon.H * 3), Image.NEAREST).quantize(colors=32, dither=Image.Dither.NONE)


def ruler():
    """a tick every 8 pixels over a hairline, the AERA tick ruler"""
    im = Image.new("RGB", (8, 5), BLACK)
    for y in range(5):
        im.putpixel((0, y), COPPER)
    for x in range(8):
        im.putpixel((x, 4), COPPER)
    return im.quantize(colors=2)


def kept(name):
    return Image.open(os.path.join(ROOT, "src", "browse", "home", name)).convert("RGB")


def assets(font, logo):
    out = os.path.join(ROOT, "src", "browse", "home")
    os.makedirs(out, exist_ok=True)
    # the wordmark: EWeb in Oxanium ExtraBold, 40 pixels, AERA's text colour
    f = ImageFont.truetype(font, 40)
    box = f.getbbox("EWeb")
    im = Image.new("RGB", (box[2] - box[0] + 2, box[3] - box[1] + 2), BLACK)
    ImageDraw.Draw(im).text((1 - box[0], 1 - box[1]), "EWeb", font=f, fill=TEXT)
    im.quantize(colors=8, dither=Image.Dither.NONE).save(os.path.join(out, "eweb.png"), optimize=True)
    # the logo, 84 pixels wide, on black
    lg = Image.open(logo).convert("RGBA")
    lg = lg.crop(lg.getbbox())
    w = 84
    lg = lg.resize((w, round(lg.height * w / lg.width)), Image.LANCZOS)
    bg = Image.new("RGBA", lg.size, BLACK + (255,))
    bg.alpha_composite(lg)
    bg.convert("RGB").quantize(colors=8, dither=Image.Dither.NONE).save(os.path.join(out, "aera.png"), optimize=True)


def c_string(s):
    out, line = [], ""
    for ch in s:
        e = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\t": "\\t"}.get(ch, ch)
        if len(line) + len(e) > 100:
            out.append('"%s"' % line)
            line = ""
        line += e
    out.append('"%s"' % line)
    return "\n\t".join(out)


def main(argv):
    if len(argv) > 1 and argv[1] == "--assets":
        assets(argv[2], argv[3])
        return
    page = open(argv[1], encoding="utf-8").read()
    page = re.sub(r"<!--[^A-Z][^>]*-->", "", page)          # the template's own comments
    pics = {"GLOBE": globe(), "WORD": kept("eweb.png"), "AERA": kept("aera.png"), "RULER": ruler()}
    for k, im in pics.items():
        page = page.replace("@%s@" % k, data(im))
        page = page.replace("@%s_W@" % k, str(im.width)).replace("@%s_H@" % k, str(im.height))
    assert "@" not in re.sub(r"@media", "", page), re.findall(r"@\w+@", page)
    page = re.sub(r">\s+<", "><", page.strip())
    page = re.sub(r"\s*\n\s*", " ", page)
    parts = re.split(r"<!--([A-Z]+)-->", page)
    lines = ["/* SPDX-License-Identifier: GPL-2.0-or-later",
             " * Copyright (C) 2026 AERA | Erez Yaary */",
             "/* home.h - EWeb's home page, made by tools/mkhome.py from",
             "   src/browse/home.html: its text, and between each piece the part",
             "   EWeb fills in (HOME_...). */",
             "enum { HOME_END, %s };" % ", ".join("HOME_" + s for s in SLOTS),
             "static const struct { const char *text; int then; } home_parts[] = {"]
    for i in range(0, len(parts), 2):
        then = parts[i + 1] if i + 1 < len(parts) else None
        assert then is None or then in SLOTS, then
        lines.append("\t{ %s,\n\t  %s }," % (c_string(parts[i]), "HOME_" + then if then else "HOME_END"))
    lines.append("};")
    open(argv[2], "w").write("\n".join(lines) + "\n")
    if len(argv) > 3:
        open(argv[3], "w", encoding="utf-8").write(sample(parts))


def sample(parts):
    """the page with its parts as EWeb fills them in (src/browse/browse.c)"""
    dots = '<span class=d> &middot; </span>'
    links = dots.join('<a href="https://example.com/%d">A page with a title %d</a>' % (i, i) for i in range(5))
    fill = {"VERSION": "0.32",
            "HELLO": "<p class=hello>Good morning. <span>Saturday 24 October 2026</span></p>",
            "SEARCH": '<form action="about:search"><input type=text name=q size=32>'
                      '<input type=submit value=Search></form>',
            "YOURS": '<div class=row><div class=lab>// Lately</div><div class=links>%s</div></div>'
                     '<div class=row><div class=lab>// Bookmarks</div><div class=links>%s</div></div>' % (links, links),
            "TIP": "<p class=tip>Tip: <b>^F</b> finds words on the page, and <b>^G</b> the next of them.</p>"}
    return "".join(p if i % 2 == 0 else fill[p] for i, p in enumerate(parts))


if __name__ == "__main__":
    main(sys.argv)
