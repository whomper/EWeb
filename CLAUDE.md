# Notes for working on EWeb

EWeb is the AERA family's web browser for the Atari Falcon, tuned for a DFB1 (68030 at 50 MHz, FPU,
128 MB of TT-RAM): `EWEB.PRG`, a text browser so far (pictures and forms come next), with the design
(`docs/DESIGN.md`) and the first milestone's measuring tools. The other AERA apps (ECal, EContact,
EMail, EFTP, EHypView, ClaudeST) are meant to look and behave as one family, and EWeb is one of them.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a GitHub Release.
  A build must leave every tracked file unchanged and shows the version in `VERSION` as it is (there is
  no `-dev` label). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from
  `main`, nothing else: no test builds, no CI artifacts, no builds from a branch.
  Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, roll back
  with a release: revert on `main`, release the next number, and say in the bad release's notes that it
  is withdrawn and which one to use.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## Talking with Erez

His rule for the whole family (from the hub, 18 Oct 2026): no progress or in-between messages; write
to him only when he has to decide something, with a short context and the decision. Analysis and
findings: the information, your assessment, and what he should do, nothing more. Messages to the
hub stay complete, with the evidence.

## EWeb

- **The design is `docs/DESIGN.md`:** the owner's decisions, what was measured, and the plan. Keep it
  true: when a measurement or a decision changes, change it there in the same commit.
- **NetSurf's code or fresh code** (the owner's rule): each part uses NetSurf's version unless fresh
  code written for a fast 68030 is faster on the page set and at least as standards-compliant. Port
  NetSurf's part first, measure it, and only then write a replacement.
- **`third_party/netsurf/`** holds four NetSurf libraries (MIT) at the commits its README lists. Don't
  edit them on their own: a change also goes in `third_party/netsurf/patches/` as a patch against
  upstream (git am applies it; the owner takes no part in NetSurf's development, so nothing is sent
  to NetSurf) and in its README with the reason, so an update can carry it.
  Their generated sources are made in `build/gen` (perl, gperf), never committed. NetSurf's own core
  is GPL-2.0-only: taking any of it is the owner's call (DESIGN.md, Open questions).
- **The Atari build is freestanding**, on `common/`'s runtime; `atari/include/` and
  `atari/libc_extra.c` add only what NetSurf's libraries need beyond it. Every build (Linux too) leaves
  out iconv, so the code measured is the code the Falcon runs.
- **Measuring** (tests/bench): `tests/bench/pages.sh` fetches the page set into `build/pages`;
  `make bench68k` and `tools/count68k.sh` (QEMU with plugins, from `tools/build-qemu.sh`) count
  68030 instructions; `tests/bench/measure.py` does the whole set; `tests/bench/hatari.py` times
  `NSBENCH.TTP` on an emulated 32 MHz Falcon (EmuTOS's `etos1024k.img`).
- **Before pushing:** `make` and `make test` (AddressSanitizer and UBSan on the sample and news pages, the
  Hebrew pages in every Hebrew encoding, `tests/hebrew`, and the input stream, `tests/input`); after a
  change to the parser or its build, `tests/html5lib/fetch.sh` and `make h5test` (Hubbub against the
  Web Platform Tests' tree-construction tests, here and as 68030 code), and after one to libparserutils'
  input, `make hebtest68k` too.
- **EWeb's network code** is `src/net/` (URL parser, HTTP client, gzip) on `common/`'s connections and TLS,
  with `src/plat.h` (`atari/plat_tos.c`, `tests/host/plat_posix.c`); `tests/net/ewget.c` is `EWGET.TTP`.
  After changing it: `make test` (nettest, the fuzz seeds and finds), `make itest`, `make urltest`
  (`tests/url/fetch.sh` first) after a change to `url.c`, and `make hatarinet` for the Atari side; run
  `make fuzz` for a while after a change to a parser, and keep what it finds in `tests/fuzz/found`.
  The handshake's RSA and elliptic curves run on the DSP through `common/` (`ui/dsp.c`, `ui/dsp_ec.c`,
  `src/ecdsp.c`, `dsp/ec.a56`): change those in AERA-Atari and test them there (`common/tests/ec`).
  `make clean` deletes `build/etos1024k.img` (EmuTOS 1.4's 1024k image, from SourceForge), which the
  Hatari rigs need.
- **The browser** is `EWEB.PRG`: `ui/` (its GEM side, after EFTP's: the window and toolbar in
  `win.c`, the page on the screen in every mode through ST RAM in `screen.c`, `main.c`'s menus, keys,
  `EWEB.INF` next to the program and the idle loop that lets Esc stop a load), on `src/browse`
  (addresses, loading, history, EWeb's own pages), `src/doc` (Hubbub's tree, the UA sheet, HTML's
  hints), `src/layout` (fresh: boxes, lines, tables, a display list) and `src/gfx` (`EWEB.FNT`,
  drawing into memory). It is a program only (no accessory). `tests/host/ewrender.c` draws a page or
  URL as EWeb would (a PNG, or `-d` the display list). After changing the layout or the document:
  `make test` (`tests/layout`: when a list changes on purpose, look at the pages with ewrender, then
  `tests/layout/run.py EWRENDER FNT --update`); after the GEM side, `make hatarigui` (Hatari, Xvfb,
  xdotool), and look at its screenshots in `build/gui`; after anything that reads or writes files,
  `make hatarifiles` (each case's opens and lookups against its budget: the guidelines' 6.5).
- **`EWEB.FNT`** is made by `tools/mkfont.py` from DejaVu with the Pillow in `tools/requirements.txt`
  (pinned: its FreeType draws the glyphs, and `tests/layout`'s lists depend on them).
- **The help file** is `docs/GUIDE.md` (`tools/mkhelp.py`, EFTP's) with the family's front page
  (`docs/front.json`, `common/tools/helpfront.py`); `make help HCP=...`. Its window picture
  `docs/help/WINDOW.IMG` (and `docs/screenshot.png`) is a Hatari capture of the start page in 16
  colours (`helpfront.py thumb`): take it again when the window changes.
- **A release** carries `EWEB.PRG`, `EWEB.FNT`, `EWEB.HYP`, the icon (`icons/`, `tools/icon/make_rsc.py`),
  `NSBENCH.TTP` (no FPU instructions, so any Falcon runs it), `NSBENCH.TXT`
  (`docs/NSBENCH.TXT`, CR/LF in the zip), `SAMPLE.HTM`, and `NEWS.HTM` with `NEWS.CSS`, and
  `EWGET.TTP` with `EWGET.TXT`, `CACERT.PEM` and `ROOTS.DAT`, from `make dist`.
  `tests/bench/mkpage.py` makes the news page, the same bytes every time; when it changes, measure the
  shipped files again and change their figures in DESIGN.md.
- `tests/html5lib/h5test.c` is Hubbub's test runner with a new `main()`, and stays MIT.
- **EWeb's CSS engine** is `src/css/` (`docs/CSS.md`): it replaced LibCSS on speed and standards
  (DESIGN.md). After changing it, `make test` (its cases, `tests/css/cases.txt`, worked out from the
  specifications) and `make csscmp` (against LibCSS on the page set: no difference may be left
  without a reason); after a change meant for speed, count it (`tools/count68k.sh`,
  `tests/bench/measure.py -E ewcss`) and change DESIGN.md's figures. `EC_COMPAT_*` options exist
  only for csscmp.
