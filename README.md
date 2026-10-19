# EWeb

A web browser for the Atari Falcon, written in C for GEM, made for a Falcon
with a DFB1 accelerator (68030 at 50 MHz, FPU, 128 MB of TT-RAM) and fine on
any Falcon or TT. It shows the document web: pages laid out with their
stylesheets, links, lists, tables and pictures, in EWeb's own fonts.
Forms come next.

![EWeb on a Falcon](docs/screenshot.png)

## Highlights

- **`EWEB.PRG`**: one GEM window, as a browser has it: Back, Forward,
  Reload or Stop, Home and the address; links followed with a click,
  scrolling with the slider and the keys, 50 pages of history, the page's
  text to the clipboard, larger and smaller text. Words typed in the address
  go to FrogFind, the search engine made for vintage computers.
- **EWeb's own engine**: NetSurf's HTML parser (Hubbub), EWeb's CSS engine
  (about a tenth of LibCSS's time on the 68030, [`docs/CSS.md`](docs/CSS.md))
  and fresh layout code: blocks, lines, lists, tables, colours, borders, and
  Hebrew right to left.
- **Pictures**: JPEG (libjpeg-turbo, every kind), PNG with transparency and
  GIF, fetched after the page is shown (you read and scroll meanwhile),
  decoded straight at the size shown, dithered to the screen's colours in
  16 and 256 colours and monochrome; off with one menu item on a slow line.
  The decoders are the family's (AERA-Atari's `common/src/pic.c`).
- **EWeb's own fonts** (`EWEB.FNT`): DejaVu Sans, Serif and Mono in four
  styles and twelve sizes, the same on every Atari, with or without GDOS.
- **Every screen**: monochrome, 4, 16 and 256 colours and the Falcon's true
  colour, through ST-RAM so that it runs from TT-RAM.
- **Secure sites itself**: TLS 1.2 on the Atari, the arithmetic on the DSP.
- **The family's ways**: the About box, menus, shortcuts, right-click menus,
  VA_START and drag and drop, the help file, iconify, the crash report.
- And the first milestone's tools: `NSBENCH.TTP` times the HTML parser and
  both CSS engines on the Atari, `EWGET.TTP` fetches a page and times it.
  [`docs/DESIGN.md`](docs/DESIGN.md) has what was measured, the decisions
  and the plan.

## Getting started

Download the latest `EWEB-x.y.zip` from **[Releases](../../releases/latest)**
and unpack it into a folder of its own. It contains:

| File | What it is |
|---|---|
| `EWEB.PRG` | the browser |
| `EWEB.FNT` | its fonts (keep it next to `EWEB.PRG`) |
| `EWEB.HYP` | the guide, for EHypView or ST-Guide (the Help key opens it) |
| `CACERT.PEM`, `ROOTS.DAT` | the authorities that vouch for secure sites (Mozilla's list) |
| `icons/` | EWeb's desktop icon, for DESKICON.RSC |
| `NSBENCH.TTP`, `NSBENCH.TXT`, `SAMPLE.HTM`, `NEWS.HTM`, `NEWS.CSS` | the measuring tool, its guide and two pages to try it on |
| `EWGET.TTP`, `EWGET.TXT` | fetches a page and times it, and its guide |

1. With STinG running, double-click `EWEB.PRG`. Its start page has a few
   sites that read well on an Atari.
2. Click in the address (or press ^L), type one such as `68k.news` and press
   Return. Click a link to follow it; Undo goes back.
3. Pages on your disks open too: ^O, or drop one on `EWEB.PRG`.

**Needs:** a Falcon or TT, plain TOS, EmuTOS or MagiC, a screen 640 pixels
wide, about 2 MB free; STinG for the network.

**Under TOS, put Atari's `FOLDR100.PRG` first in `AUTO`**, renamed for more room (`FOLDR400.PRG`).
TOS keeps an entry in a small internal pool for every folder a program or accessory has looked
into, and halts with "OUT OF INTERNAL MEMORY" when it runs out; FOLDRxxx makes the pool bigger.

## How it reaches the servers

Straight from the Atari, through STinG or MiNTnet: EWeb resolves the name
and connects itself, and for `https://` does TLS 1.2 on the 68030
(BearSSL, ChaCha20-Poly1305 first, which is twice as fast as AES there),
with the server's RSA or ECDSA signatures and the key exchange worked out
on the DSP (about 2 s for a handshake with ECDSA certificates, which took
86 s on the 68030). Every certificate is
checked against the authorities in `CACERT.PEM` (Mozilla's list; `ROOTS.DAT`
is the same list read in advance), with its name and dates; there is no way
to skip the check. A kept connection serves the next requests to the same
server, and a new one to it resumes the TLS session, which saves most of
the handshake. Through AERA-Gateway, for CAPTCHAs and heavy sites, comes
later ([`docs/DESIGN.md`](docs/DESIGN.md)).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 perl gperf fonts-dejavu-core fonts-dejavu-extra   # Debian/Ubuntu
pip install -r tools/requirements.txt   # Pillow, which draws EWEB.FNT
make            # binary/EWEB.PRG and EWEB.FNT, NSBENCH.TTP, EWGET.TTP, for plain TOS on a Falcon
make help HCP=/path/to/hcp   # binary/EWEB.HYP (common/tools/build-hcp.sh builds hcp)
make bench      # build/host/nsbench, for this computer
make bench68k   # build/m68k/nsbench, a 68030 Linux program for tools/count68k.sh
make test       # nsbench, the CSS engine's cases, the Hebrew pages, the input stream, the HTTP client, the layout, with ASan and UBSan
make itest      # ewget and the browser against servers on this computer, over TCP and TLS
make hatarinet  # EWGET.TTP on an emulated Falcon, through FAKESTNG, against those servers
make hatarigui  # EWEB.PRG on emulated Falcons and TTs: browsing over the network, every screen mode
make hatarifiles  # what EWEB.PRG and EWGET.TTP do on the disk, against each case's budget
make urltest    # the URL parser against the URL Standard's tests (tests/url/fetch.sh first)
make fuzz       # the HTTP client and the URL parser under libFuzzer (clang)
make h5test     # Hubbub against the HTML parsing tests (tests/html5lib/fetch.sh first)
make csscmp     # EWeb's CSS engine against LibCSS on the page set (tests/bench/pages.sh first)
make dist       # dist/EWEB-x.y.zip, as on the Releases page
```

`binary/`, `build/` and `dist/` are what make builds; they are never
committed. A release: `tools/bump.py`, commit and push, then **Actions >
Release > Run workflow** on `main` with what is new, or push a tag made with
`git tag -a vX.Y`. The workflow tests, builds, tags the commit and publishes
the files.

## Source

```
docs/DESIGN.md        the design
docs/CSS.md           EWeb's CSS engine
docs/GUIDE.md         the guide, made into EWEB.HYP (tools/mkhelp.py, docs/front.json)
docs/NSBENCH.TXT      NSBENCH's guide, as it goes into a release
ui/                   EWEB.PRG's GEM side: the window, the toolbar, the page on the
                      screen in every mode, the menus, the About box
src/browse/           browsing: addresses, loading, history, EWeb's own pages, the pages' pictures (img.c)
src/doc/              the document: Hubbub's tree, the UA stylesheet, HTML's hints
src/layout/           the layout: boxes, lines, tables, a display list
src/gfx/              the fonts (EWEB.FNT) and drawing into memory
src/css/              EWeb's CSS engine
src/net/              EWeb's URL parser and HTTP client, and gzip
src/plat.h            what EWeb needs from the machine (atari/plat_tos.c, tests/host/plat_posix.c)
tests/bench/          nsbench: the document tree, LibCSS's selection handler,
                      EWeb's engine on the same tree, the page set, the
                      counting, the Hatari rig and mkpage.py (NEWS.HTM and
                      NEWS.CSS)
tests/css/            the CSS engine's cases (from the specifications), and
                      csscmp, which compares it with LibCSS value by value
tests/html5lib/       Hubbub against the Web Platform Tests' tree-construction tests
tests/hebrew/         a Hebrew page in every Hebrew encoding
tests/net/            ewget, the HTTP client's tests (nettest, and itest with server.py)
tests/url/            the URL parser against the URL Standard's tests
tests/fuzz/           fuzz targets for the HTTP client and the URL parser, their seeds
tests/hatari/         EWGET.TTP and EWEB.PRG in Hatari (through common/tools/fakesting)
tests/host/ewrender.c a page or URL drawn as EWeb draws it, into a PNG or a display list
tests/layout/         the layout's test pages and their display lists
tests/input/          libparserutils' input stream, against the WHATWG UTF-8 decoder
atari/                the C library pieces NetSurf needs beyond common/, and plat_tos.c
third_party/netsurf/  NetSurf's libparserutils, libwapcaplet, libhubbub, libcss (MIT)
tools/                mkfont.py (EWEB.FNT), mkhelp.py, icon/ (the icon), QEMU with
                      plugins and counting 68030 instructions
common/               the AERA shared code (git subtree of AERA-Atari): don't edit here
```

## Tests

- `make test`: nsbench with both CSS engines on `tests/bench/sample.html` and
  `NEWS.HTM`, the Hebrew pages in every Hebrew encoding (`tests/hebrew`), the
  CSS engine's cases (`tests/css/cases.txt`), csscmp on the test pages, and
  the input stream that feeds Hubbub (`tests/input`: documents in random
  chunks, valid UTF-8 and not, against the WHATWG UTF-8 decoder), with
  AddressSanitizer and UBSan. `make hebtest68k` runs the Hebrew pages and the
  input stream as 68030 code.
- `make csscmp`: EWeb's CSS engine against LibCSS on the whole page set: every
  difference must be one of LibCSS's known gaps.
- `make h5test`: Hubbub against the Web Platform Tests' tree-construction
  tests, here and as 68030 code under QEMU.
- `make test` also runs the HTTP client against servers played by
  `common/tests/fuzz/fuzz_net.c` (`tests/net/nettest.c`: lengths, chunks,
  gzip and deflate, redirects, kept connections, and what a hostile server
  might send), and the fuzz targets' seeds and past finds (`tests/fuzz`).
- `make itest`: ewget against `tests/net/server.py` on this computer, over
  TCP and TLS with a test authority: pages plain and packed, a kept
  connection, a resumed TLS session, redirects from HTTP to HTTPS, a server
  with only an ECDSA certificate, the handshake's steps in the log, the seed
  and the remembered signatures (`SEED.DAT`, `SIGS.DAT`), the test
  authorities among Mozilla's roots, `ROOTS.DAT` told by `CACERT.PEM`'s
  date, the answer's times, and the certificates that must be refused (one
  a remembered signature can't rescue).
- `make hatarinet`: `EWGET.TTP` on an emulated Falcon (Hatari, 68030 at
  32 MHz, the DSP emulated), through `FAKESTNG.PRG`, against the same
  servers (an ECDSA one too, its curves on the DSP, twice), with a `.CMD`
  file dropped on it, and with the release's `CACERT.PEM` and `ROOTS.DAT`
  (twice: the second time `ROOTS.DAT` knows `CACERT.PEM`'s date).
- `common/tests/ec`: the DSP's elliptic curves against BearSSL (AERA-Atari's
  test, run there).
- `make test` also lays out three pages (`tests/layout`) and compares their
  display lists with the ones kept, and the Hebrew test page in each of its
  encodings, with AddressSanitizer and UBSan; `make itest` loads a site from
  `tests/net/server.py` in the browser over HTTP and HTTPS (its stylesheet,
  its links, a redirect, an anchor, EWeb's own pages for a server that isn't
  there and a file that isn't a page).
- `make hatarigui`: `EWEB.PRG` on an emulated Falcon through FAKESTNG, the
  mouse moved by xdotool: a page and its stylesheet, a link clicked, Undo,
  an address typed with an anchor, a secure page, Esc stopping a download,
  a page of pictures (scrolled while they come); and a page and a page of
  pictures drawn on the Falcon in 256 colours and true colour and on the
  TT in 16 colours and monochrome.
- `make itest` also loads a page of pictures (`tests/net/server.py`'s
  `/site/pics.html`): each decoded at the size shown, the page laid out
  again for the one whose size it didn't give, the missing one failed; and
  a picture on its own. The decoders' own tests (PngSuite, libjpeg-turbo's
  and libnsgif's pictures against Pillow, the 68030 build under QEMU) are
  AERA-Atari's `common/tests/pic`.
- `make hatarifiles`: what `EWEB.PRG` and `EWGET.TTP` do on the disk, from
  Hatari's GEMDOS trace (the guidelines' 6.5): the opens and lookups of
  each case (start, a page with pictures, five pages, back and forward, the
  page size changed, secure pages, five minutes idle, quit, a second start,
  `EWGET.TTP` fetching 1 MB) against its budget. Pages, history and idle
  touch nothing, and a quit with nothing changed writes nothing.
- `make urltest`: the URL parser against the URL Standard's tests (Web
  Platform Tests, `tests/url/fetch.sh`), for http and https.
- `make fuzz`: the HTTP client and the URL parser under libFuzzer.
- `tests/bench/measure.py [-E ewcss]`: 68030 instructions per phase for the
  whole page set; `tests/bench/hatari.py [--stock]`: `NSBENCH.TTP` timed on an
  emulated Falcon.

## Licence

EWeb is free software under the GNU General Public License, version 2 or (at
your option) any later version; see [`COPYING`](COPYING). `EWEB.FNT` is drawn
from the DejaVu fonts (Bitstream Vera's licence, free to use and to change,
not to sell alone). NetSurf's libraries
in `third_party/netsurf/` and `common/third_party/nsgif`, `tests/html5lib/h5test.c` and BearSSL
(`common/third_party/bearssl`) are under the MIT licence; libjpeg-turbo
(`common/third_party/libjpeg-turbo`) under the IJG's and the modified BSD
licences; `CACERT.PEM` is Mozilla's list of authorities (MPL 2.0).

---

Part of the AERA Atari family: [ECal](https://github.com/whomper/ECal),
[EContact](https://github.com/whomper/EContact),
[EMail](https://github.com/whomper/EMail),
[EFTP](https://github.com/whomper/EFTP),
[EHypView](https://github.com/whomper/EHypView),
[ClaudeST](https://github.com/whomper/ClaudeST) and
[AERA-Atari](https://github.com/whomper/AERA-Atari).
