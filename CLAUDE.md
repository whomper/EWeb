# Notes for working on EWeb

EWeb is the AERA family's web browser for the Atari Falcon, tuned for a DFB1 (68030 at 50 MHz, FPU,
128 MB of TT-RAM). There is no browser yet: the repo holds the design (`docs/DESIGN.md`) and the first
milestone's measuring tools. The other AERA apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST) are
meant to look and behave as one family, and EWeb will join them.

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

## EWeb

- **The design is `docs/DESIGN.md`:** the owner's decisions, what was measured, and the plan. Keep it
  true: when a measurement or a decision changes, change it there in the same commit.
- **NetSurf's code or fresh code** (the owner's rule): each part uses NetSurf's version unless fresh
  code written for a fast 68030 is faster on the page set and at least as standards-compliant. Port
  NetSurf's part first, measure it, and only then write a replacement.
- **`third_party/netsurf/`** holds four NetSurf libraries (MIT) at the commits its README lists. Don't
  edit them on their own: a change also goes in `third_party/netsurf/patches/` as a patch against
  upstream (ready to send to NetSurf) and in its README with the reason, so an update can carry it.
  Their generated sources are made in `build/gen` (perl, gperf), never committed. NetSurf's own core
  is GPL-2.0-only: taking any of it is the owner's call (DESIGN.md, Open questions).
- **The Atari build is freestanding**, on `common/`'s runtime; `atari/include/` and
  `atari/libc_extra.c` add only what NetSurf's libraries need beyond it. Every build (Linux too) leaves
  out iconv, so the code measured is the code the Falcon runs.
- **Measuring** (tests/bench): `tests/bench/pages.sh` fetches the page set into `build/pages`;
  `make bench68k` and `tools/count68k.sh` (QEMU with plugins, from `tools/build-qemu.sh`) count
  68030 instructions; `tests/bench/measure.py` does the whole set; `tests/bench/hatari.py` times
  `NSBENCH.TTP` on an emulated 32 MHz Falcon (EmuTOS's `etos1024k.img`).
- **Before pushing:** `make` and `make test` (AddressSanitizer and UBSan on the sample page, and the
  Hebrew pages in every Hebrew encoding, `tests/hebrew`); after a
  change to the parser or its build, `tests/html5lib/fetch.sh` and `make h5test` (Hubbub against the
  Web Platform Tests' tree-construction tests, here and as 68030 code).
- `tests/html5lib/h5test.c` is Hubbub's test runner with a new `main()`, and stays MIT.
