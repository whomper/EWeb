# EWeb

A web browser for the Atari Falcon, written in C for GEM, made for a Falcon
with a DFB1 accelerator (68030 at 50 MHz, FPU, 128 MB of TT-RAM) and fine on
any Falcon. **There is no browser yet**: this repository holds the design and
the first milestone's measuring tools.

- [`docs/DESIGN.md`](docs/DESIGN.md): what a Falcon can do with today's web,
  the decisions made, the design, how EWeb gets past CAPTCHAs, and the order of
  work.
- `NSBENCH.TTP` (`make`): parses a saved page with NetSurf's HTML parser and
  CSS engine on the Atari and says how long each part took.

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 perl gperf   # Debian/Ubuntu
make            # binary/NSBENCH.TTP, for plain TOS on a Falcon (68030 with FPU)
make bench      # build/host/nsbench, for this computer
make bench68k   # build/m68k/nsbench, a 68030 Linux program for tools/count68k.sh
make test       # nsbench with AddressSanitizer and UBSan
make h5test     # Hubbub against the HTML parsing tests (tests/html5lib/fetch.sh first)
```

`binary/` and `build/` are what make builds; they are never committed.

## Source

```
docs/DESIGN.md        the design
tests/bench/          nsbench: the document tree, LibCSS's selection handler,
                      the page set, the counting and the Hatari rig
tests/html5lib/       Hubbub against the Web Platform Tests' tree-construction tests
atari/                the C library pieces NetSurf needs beyond common/
third_party/netsurf/  NetSurf's libparserutils, libwapcaplet, libhubbub, libcss (MIT)
tools/                QEMU with plugins, and counting 68030 instructions
common/               the AERA shared code (git subtree of AERA-Atari): don't edit here
```

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
