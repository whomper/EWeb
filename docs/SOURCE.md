<!-- SPDX-License-Identifier: GPL-2.0-or-later
     Copyright (C) 2026 AERA | Erez Yaary -->
# EWeb's source, folder by folder

```
docs/DESIGN.md        the design
docs/CSS.md           EWeb's CSS engine
docs/GUIDE.md         the guide, made into EWEB.HYP (tools/mkhelp.py, docs/front.json)
docs/NSBENCH.TXT      NSBENCH's guide, as it goes into a release
docs/PICBENCH.TXT     PICBENCH's guide, as it goes into a release
docs/EWGET.TXT        EWGET's guide, as it goes into a release
docs/help/            the guide's window picture (WINDOW.IMG, a Hatari capture)
gateway/              AERA-Gateway's services for the Raspberry Pi, webgate.py (EWeb's) among them:
                      a read-only copy (tools/sync-gateway.sh)
ui/                   EWEB.PRG's GEM side: the window, the toolbar (its pictures in
                      tbicons.h, made by tools/mktbicons.py), the page on the screen
                      in every mode, the menus, the Settings, the About box
src/browse/           browsing: addresses, loading, Back and Forward, EWeb's own pages (the home page:
                      home.html and its pictures in home/), saving, bookmarks (bookmark.c),
                      the history (history.c) and visited links (visited.c), the disk cache (dcache.c),
                      the pages' pictures (img.c)
src/doc/              the document: EWeb's HTML parser (html.c), the UA stylesheet, HTML's hints, forms (form.c)
src/layout/           the layout: boxes, lines, floats, inline blocks, tables, flexbox and grid, a display list
src/gfx/              the fonts (EWEB.FNT) and drawing into memory
src/css/              EWeb's CSS engine
src/js/               the pages' scripts: QuickJS's runtime with the budget, and the lean DOM (js.c);
                      js_none.c for the ST edition. On the Atari they are EWEBJS.OVL, a file of their own
                      (atari/js_ovl.c loads it, atari/js_ovl_head.c and ovl.ld make it, tools/mkovl.py)
src/net/              EWeb's URL parser and HTTP client, cookies (cookie.c), and gzip
src/plat.h            what EWeb needs from the machine (atari/plat_tos.c, tests/host/plat_posix.c)
tests/bench/          nsbench: the document tree, LibCSS's selection handler,
                      EWeb's engine on the same tree, the page set, the
                      counting (measure.py, docparse.c, jsbench.c), the Hatari
                      rig, picbench.c and picbench.py (PICBENCH.TTP, PICBST.TTP),
                      mkpage.py (NEWS.HTM and NEWS.CSS) and mkphoto.py (PHOTO.JPG)
tests/css/            the CSS engine's cases (from the specifications), and
                      csscmp, which compares it with LibCSS value by value
tests/html5lib/       EWeb's parser (ewh5test.c) and Hubbub against the Web Platform Tests' tree-construction tests
tests/hebrew/         a Hebrew page in every Hebrew encoding, with no label (unlabelled/), and visual (visual/)
tests/net/            ewget, the HTTP client's tests (nettest, and itest with server.py), cookies.txt
tests/url/            the URL parser against the URL Standard's tests
tests/fuzz/           fuzz targets for the HTTP client, the URL and HTML parsers, cookies, the bookmark file and HISTORY.TXT,
                      their seeds and past finds (found/)
tests/hatari/         EWGET.TTP, EWEB.PRG and EWEBST.PRG in Hatari (through common/tools/fakesting),
                      and what they do on the disk (disk_files.py)
tests/host/ewrender.c a page or URL drawn as EWeb draws it, into a PNG or a display list
tests/layout/         the layout's test pages and their display lists
tests/browse/         the bookmarks, the history, the disk cache, the Gateway's pens, a refresh, and a page's
                      encoding (enctest.c: the sniffing, the default by address, queries)
tests/forms/          what forms send (cases.txt, from the HTML Standard), formtest.c
tests/js/             pages whose scripts check themselves, run by ewrender -J (run.py)
tests/input/          libparserutils' input stream, against the WHATWG UTF-8 decoder
icons/                the desktop icon (EWEB.RSC, EWEBNV.RSC, EWEB.ICN, EWEBMK.ICN), made by make icons from
                      the family's drawing, common/icons/EWEB.txt (common/tools/appicon.py)
atari/                the C library pieces NetSurf and QuickJS need beyond common/ (libc_extra.c;
                      include/js, js_libc.c), the ST edition's 64-bit arithmetic (ll68000.c),
                      plat_tos.c, and EWEBJS.OVL's loader (js_ovl.c)
third_party/netsurf/  NetSurf's libparserutils, libwapcaplet, libhubbub, libcss (MIT; EWEB.PRG takes only
                      libparserutils: Hubbub and LibCSS are the yardsticks of nsbench, h5test and csscmp)
third_party/quickjs/  QuickJS, the JavaScript engine (MIT)
third_party/libm/     musl's maths functions, for QuickJS on the Atari (MIT)
tools/                mkfont.py (EWEB.FNT), mkhelp.py (EWEB.HYP), mkentities.py (the HTML parser's
                      named characters), mktbicons.py and mkthrobber.py (the toolbar's pictures and
                      throbber), mkhome.py (the home page), mkovl.py (EWEBJS.OVL), bump.py (a release), sync-common.sh and
                      sync-gateway.sh, and QEMU with plugins for counting 68030 instructions
                      (build-qemu.sh, count68k.sh, prof68k.py)
common/               the AERA shared code (git subtree of AERA-Atari): don't edit here
```
