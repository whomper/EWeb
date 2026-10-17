# EWeb

A web browser for the Atari Falcon, written in C for GEM, made for a Falcon
with a DFB1 accelerator (68030 at 50 MHz, FPU, 128 MB of TT-RAM) and fine on
any Falcon. **There is no browser yet**: this repository holds the design,
the first milestone's measuring tools, and EWeb's first parts: its own CSS
engine, and its URL parser and HTTP client.

- [`docs/DESIGN.md`](docs/DESIGN.md): what a Falcon can do with today's web,
  the decisions made, the design, how EWeb gets past CAPTCHAs, and the order of
  work.
- [`docs/CSS.md`](docs/CSS.md): EWeb's CSS engine, written for the 68030:
  about a tenth of NetSurf's LibCSS's time on the same pages, and closer to
  today's CSS.
- `NSBENCH.TTP`: parses a saved page with NetSurf's HTML parser, and styles
  it with NetSurf's CSS engine and with EWeb's, on the Atari, and says how
  long each part took.
- `EWGET.TTP`: fetches a page from the web with EWeb's HTTP client (TLS 1.2
  on the Atari, RSA on the DSP, gzip, redirects, kept connections), and says
  how long each part took.

## Getting started

Download the latest `EWEB-x.y.zip` from **[Releases](../../releases/latest)**
and unpack it. It contains:

| File | What it is |
|---|---|
| `NSBENCH.TTP` | the measuring tool, for any Falcon (TT-RAM used when there is some) |
| `NSBENCH.TXT` | how to use it |
| `SAMPLE.HTM` | a small page to try it on |
| `NEWS.HTM`, `NEWS.CSS` | a made-up news front page and its stylesheet, built to cost what the web's median page does |
| `EWGET.TTP` | fetches a page from the web and times it, for any Falcon with STinG or MiNTnet |
| `EWGET.TXT` | how to use it |
| `CACERT.PEM`, `ROOTS.DAT` | the authorities that vouch for secure sites (Mozilla's list), for `EWGET.TTP` |

1. Copy the files to a folder on your hard disk, with any page you want to
   test saved as a file.
2. Double-click `NSBENCH.TTP` and type `-E both NEWS.HTM NEWS.CSS` (or
   `SAMPLE.HTM`, or your page's name, then its stylesheet's).
3. The results show on the screen and go into `NSBENCH.LOG`.
4. With STinG (or MiNTnet) running, double-click `EWGET.TTP` and type an
   address, such as `-n 2 https://en.wikipedia.org/wiki/Atari_Falcon`. The
   results go into `EWGET.LOG`.

**Needs:** an Atari Falcon; plain TOS, MagiC or MiNT; for `EWGET.TTP`, a
network connection through STinG or MiNTnet.

## How it reaches the servers

Straight from the Atari, through STinG or MiNTnet: EWeb resolves the name
and connects itself, and for `https://` does TLS 1.2 on the 68030
(BearSSL, ChaCha20-Poly1305 first, which is twice as fast as AES there),
with the server's RSA signature checked on the DSP. Every certificate is
checked against the authorities in `CACERT.PEM` (Mozilla's list; `ROOTS.DAT`
is the same list read in advance), with its name and dates; there is no way
to skip the check. A kept connection serves the next requests to the same
server, and a new one to it resumes the TLS session, which saves most of
the handshake. Through AERA-Gateway, for CAPTCHAs and heavy sites, comes
later ([`docs/DESIGN.md`](docs/DESIGN.md)).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 perl gperf   # Debian/Ubuntu
make            # binary/NSBENCH.TTP, for plain TOS on a Falcon (68030 with FPU)
make bench      # build/host/nsbench, for this computer
make bench68k   # build/m68k/nsbench, a 68030 Linux program for tools/count68k.sh
make test       # nsbench, the CSS engine's cases, the Hebrew pages, the input stream, the HTTP client, with ASan and UBSan
make itest      # ewget against servers on this computer, over TCP and TLS
make hatarinet  # EWGET.TTP on an emulated Falcon, through FAKESTNG, against those servers
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
docs/NSBENCH.TXT      NSBENCH's guide, as it goes into a release
src/css/              EWeb's CSS engine
src/net/              EWeb's URL parser and HTTP client, and gzip
src/plat.h            what EWeb needs from the machine (atari/plat_tos.c, tests/host/plat_posix.c)
ui/                   EWeb's GEM layer (so far, what common/ui/dsp.c takes from it)
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
tests/hatari/         EWGET.TTP in Hatari (serial_bridge.py, from EMail)
tools/fakesting/      FAKESTNG.PRG, STinG over Hatari's serial port (from EMail)
tests/input/          libparserutils' input stream, against the WHATWG UTF-8 decoder
atari/                the C library pieces NetSurf needs beyond common/, and plat_tos.c
third_party/netsurf/  NetSurf's libparserutils, libwapcaplet, libhubbub, libcss (MIT)
tools/                QEMU with plugins, and counting 68030 instructions
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
  connection, a resumed TLS session, redirects from HTTP to HTTPS, and the
  certificates that must be refused.
- `make hatarinet`: `EWGET.TTP` on an emulated Falcon (Hatari, 68030 at
  32 MHz, the DSP emulated), through `FAKESTNG.PRG`, against the same
  servers, and with the release's `CACERT.PEM` and `ROOTS.DAT`.
- `make urltest`: the URL parser against the URL Standard's tests (Web
  Platform Tests, `tests/url/fetch.sh`), for http and https.
- `make fuzz`: the HTTP client and the URL parser under libFuzzer.
- `tests/bench/measure.py [-E ewcss]`: 68030 instructions per phase for the
  whole page set; `tests/bench/hatari.py [--stock]`: `NSBENCH.TTP` timed on an
  emulated Falcon.

## Licence

EWeb is free software under the GNU General Public License, version 2 or (at
your option) any later version; see [`COPYING`](COPYING). NetSurf's libraries
in `third_party/netsurf/`, `tests/html5lib/h5test.c` and BearSSL
(`common/third_party/bearssl`) are under the MIT licence; `CACERT.PEM` is
Mozilla's list of authorities (MPL 2.0).

---

Part of the AERA Atari family: [ECal](https://github.com/whomper/ECal),
[EContact](https://github.com/whomper/EContact),
[EMail](https://github.com/whomper/EMail),
[EFTP](https://github.com/whomper/EFTP),
[EHypView](https://github.com/whomper/EHypView),
[ClaudeST](https://github.com/whomper/ClaudeST) and
[AERA-Atari](https://github.com/whomper/AERA-Atari).
