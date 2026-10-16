# EWeb

A web browser for the Atari Falcon, written in C for GEM, made for a Falcon
with a DFB1 accelerator (68030 at 50 MHz, FPU, 128 MB of TT-RAM) and fine on
any Falcon. **There is no browser yet**: this repository holds the design and
the first milestone's measuring tools.

- [`docs/DESIGN.md`](docs/DESIGN.md): what a Falcon can do with today's web,
  the decisions made, the design, how EWeb gets past CAPTCHAs, and the order of
  work.
- `NSBENCH.TTP`: parses a saved page with NetSurf's HTML parser and CSS
  engine on the Atari and says how long each part took.

## Getting started

Download the latest `EWEB-x.y.zip` from **[Releases](../../releases/latest)**
and unpack it. It contains:

| File | What it is |
|---|---|
| `NSBENCH.TTP` | the measuring tool, for any Falcon (TT-RAM used when there is some) |
| `NSBENCH.TXT` | how to use it |
| `SAMPLE.HTM` | a small page to try it on |
| `NEWS.HTM`, `NEWS.CSS` | a made-up news front page and its stylesheet, built to cost what the web's median page does |

1. Copy the files to a folder on your hard disk, with any page you want to
   test saved as a file.
2. Double-click `NSBENCH.TTP` and type `NEWS.HTM NEWS.CSS` (or `SAMPLE.HTM`,
   or your page's name, then its stylesheet's).
3. The results show on the screen and go into `NSBENCH.LOG`.

**Needs:** an Atari Falcon; plain TOS, MagiC or MiNT.

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 perl gperf   # Debian/Ubuntu
make            # binary/NSBENCH.TTP, for plain TOS on a Falcon (68030 with FPU)
make bench      # build/host/nsbench, for this computer
make bench68k   # build/m68k/nsbench, a 68030 Linux program for tools/count68k.sh
make test       # nsbench with AddressSanitizer and UBSan
make h5test     # Hubbub against the HTML parsing tests (tests/html5lib/fetch.sh first)
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
docs/NSBENCH.TXT      NSBENCH's guide, as it goes into a release
tests/bench/          nsbench: the document tree, LibCSS's selection handler,
                      the page set, the counting, the Hatari rig and mkpage.py
                      (NEWS.HTM and NEWS.CSS)
tests/html5lib/       Hubbub against the Web Platform Tests' tree-construction tests
tests/hebrew/         a Hebrew page in every Hebrew encoding
atari/                the C library pieces NetSurf needs beyond common/
third_party/netsurf/  NetSurf's libparserutils, libwapcaplet, libhubbub, libcss (MIT)
tools/                QEMU with plugins, and counting 68030 instructions
common/               the AERA shared code (git subtree of AERA-Atari): don't edit here
```

## Tests

- `make test`: nsbench on `tests/bench/sample.html` and the Hebrew pages in
  every Hebrew encoding (`tests/hebrew`), with AddressSanitizer and UBSan.
- `make h5test`: Hubbub against the Web Platform Tests' tree-construction
  tests, here and as 68030 code under QEMU.
- `tests/bench/measure.py`: 68030 instructions per phase for the whole page
  set; `tests/bench/hatari.py`: `NSBENCH.TTP` timed on an emulated Falcon.

## Licence

EWeb is free software under the GNU General Public License, version 2 or (at
your option) any later version; see [`COPYING`](COPYING). NetSurf's libraries
in `third_party/netsurf/` and `tests/html5lib/h5test.c` are under the MIT
licence.

---

Part of the AERA Atari family: [ECal](https://github.com/whomper/ECal),
[EContact](https://github.com/whomper/EContact),
[EMail](https://github.com/whomper/EMail),
[EFTP](https://github.com/whomper/EFTP),
[EHypView](https://github.com/whomper/EHypView),
[ClaudeST](https://github.com/whomper/ClaudeST) and
[AERA-Atari](https://github.com/whomper/AERA-Atari).
