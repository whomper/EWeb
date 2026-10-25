# Notes for working on EWeb

EWeb is the AERA family's web browser for the Atari Falcon, tuned for a DFB1 (68030 at 50 MHz, FPU,
128 MB of TT-RAM): `EWEB.PRG`, a browser of the document web (text and pictures; forms come next), with the design
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
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  - this repo's checks pass;
  - the rigs on his TOS pass, where this file asks for them;
  - the hub's family checks pass.

  Then give Erez the link in one line, with anything he has to do, and send the hub the link. Roll
  back a bad release the same way. Decisions that are his (design, Pi packages, ports, a change to
  what a user sees that wasn't asked for) still go to him first.
- **Testing with another AERA program uses its latest release**, what Erez runs, never a build of its
  `main`: `common/tools/getrelease.py APP DIR FILE...` fetches it. Only for a change both programs are
  still making, test against the other's `main` at a named commit, say so, and test again against its
  release when it is out.
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
- **Better than every other Atari browser, and the fastest code** (the owner's rule, 19 Oct 2026):
  each part is measured (68030 instructions on the page set, then the emulated Falcon and his DFB1)
  and made as fast as the machine allows. The DSP takes any work it does faster than the 68030, and
  the code is tuned for the 68030; so there are two releases, `EWEB.PRG` (68030) and the ST edition
  `EWEBST.PRG` (68000). DESIGN.md, Decisions.
- **NetSurf's code or fresh code** (the owner's rule): each part uses NetSurf's version unless fresh
  code written for a fast 68030 is faster on the page set and at least as standards-compliant. Port
  NetSurf's part first, measure it, and only then write a replacement.
- **`third_party/netsurf/`** holds five NetSurf libraries (MIT) at the commits its README lists. Don't
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
  68030 instructions, `tools/prof68k.py` shows where they go function by function, and
  `build/m68k/docparse` is the HTML parse as `EWEB.PRG` does it (on the Atari's heap); `tests/bench/measure.py` does the whole set; `drawbench -m` prints the heap (the Atari's allocator) after parsing, styling and layout, and its peak; `tests/bench/hatari.py` times
  `NSBENCH.TTP` on an emulated 32 MHz Falcon; `tests/bench/picbench.py` times a picture decoded
  and dithered as EWeb does it: `PICBENCH.TTP` (as `EWEB.PRG`, on the 68030 and with the DSP) on
  the Falcon, `PICBST.TTP` (as `EWEBST.PRG`) on the cycle-exact 8 MHz ST.
- **Before pushing:** `make` and `make test` (AddressSanitizer and UBSan on the sample and news pages, the
  Hebrew pages in every Hebrew encoding, `tests/hebrew`, and the input stream, `tests/input`); after a
  change to the parser or its build, `tests/html5lib/fetch.sh` and `make h5test` (EWeb's parser and
  Hubbub against the Web Platform Tests' tree-construction tests, here and as 68030 code: EWeb's
  must keep all but the six scripted ones), and after one to libparserutils' input, `make hebtest68k`
  too.
- **EWeb's network code** is `src/net/` (URL parser, HTTP client, gzip) on `common/`'s connections and TLS,
  with `src/plat.h` (`atari/plat_tos.c`, `tests/host/plat_posix.c`); `tests/net/ewget.c` is `EWGET.TTP`.
  After changing it: `make test` (nettest, the fuzz seeds and finds), `make itest`, `make urltest`
  (`tests/url/fetch.sh` first) after a change to `url.c`, and `make hatarinet` for the Atari side; run
  `make fuzz` for a while after a change to a parser, and keep what it finds in `tests/fuzz/found`.
  The handshake's RSA and elliptic curves run on the DSP through `common/` (`ui/dsp.c`, `ui/dsp_ec.c`,
  `src/ecdsp.c`, `dsp/ec.a56`): change those in AERA-Atari and test them there (`common/tests/ec`).
  The Pi gateway (Settings > Secure: `FALCON=0`, `GATE=`, `GATEPORT=`; EWGET `-g`, ewrender `-W`) is AERA-Gateway's `webgate.py`, whose
  protocol EWeb's session owns (AERA-Gateway's owner session commits it): `make itest` and `make
  hatarinet` run it from EWeb's read-only `gateway/` copy (`tools/sync-gateway.sh`; `WEBGATE=` for another).
  Its `--browser` (snapshots: View > Through the Pi's browser, `SNAPSHOT=`, ewrender `-R`; live view:
  `http://webgate/live`) is tested by `make itest` with a stand-in for webgate-ext.
  Every Hatari rig boots the family's machines (`common/tools/machines.py`): Erez's TOS 4.04 from
  `~/.aera/tos` when it is there (never in git, never sent anywhere), else EmuTOS 1.4; before asking
  Erez for a release, run the GEM rigs on his TOS and say so (the hub, 19 Oct 2026).
- **The browser** is `EWEB.PRG`: `ui/` (its GEM side, after EFTP's: the window and toolbar in
  `win.c` with the throbber (`ui/throbber.h` from `tools/mkthrobber.py`) and the icons (`ui/tbicons.h`
  from `tools/mktbicons.py`: each as it is, greyed and lit under the mouse), the page on the screen in every mode through ST RAM in `screen.c`, `main.c`'s menus, keys,
  `EWEB.INF` next to the program and the idle loop that lets Esc stop a load), on `src/browse`
  (addresses, loading, history, EWeb's own pages), `src/doc` (EWeb's HTML parser `html.c`, the
  document's memory, the UA sheet, HTML's hints), `src/layout` (fresh: boxes, lines, floats, tables, flexbox, grid, a display list) and `src/gfx` (`EWEB.FNT`,
  drawing into memory). It is a program only (no accessory). `tests/host/ewrender.c` draws a page or
  URL as EWeb would (a PNG, or `-d` the display list). After changing the layout or the document:
  `make test` (`tests/layout`: when a list changes on purpose, look at the pages with ewrender, then
  `tests/layout/run.py EWRENDER FNT --update`); after the GEM side, `make hatarigui` (Hatari, Xvfb,
  xdotool), and look at its screenshots in `build/gui`; after anything that reads or writes files,
  `make hatarifiles` (each case's opens and lookups against its budget: the guidelines' 6.5).
  **Dialogs** have the AES's 3D look where it has one (the guidelines' 5.6: common's `dlg_3d`,
  called by `d_end` in `ui/dialogs.c`; Settings > General > 3D dialogs, `DIALOGS3D=`): after
  changing them, `make hatarigui` (its `dialogs` group: TOS 4.04 and EmuTOS both ways, the ST's
  TOS 1.04 with the tick greyed). **A box's scroll bars** (overflow auto or scroll) are the AES's
  (the guidelines' 5.1: common's `panebar`, as wide and high as `pb_width`/`pb_height`,
  `LAYOUT_OPT.sbar`; drawn by `draw_box_bars` in `ui/win.c` over the page's copy, pressed through
  `pb_click`; ewrender `-a W,H`): after changing them, `make test` and `make hatarigui` (its
  `boxes` group).
- **Forms** are `src/doc/form.c` (each control's state, kept with the document; what a form sends,
  after the HTML Standard), drawn from that state by `src/gfx/canvas.c` (DL_CTL items, Hebrew right
  to left), sent by `br_submit` (`src/browse`), edited in `ui/win.c`. After changing them: `make test`
  (`tests/forms/cases.txt`, worked out from the standard), `make itest` (`/site/form.html`), `make
  hatarigui`.
- **Cookies** are `src/net/cookie.c` (RFC 6265bis; hooked into `http.c` by `src/browse`), kept in
  `COOKIES.TXT` next to `EWEB.PRG`, values encrypted with `EWEB.KEY` (common's `secret.c`): read
  once when needed, written once at quit (6.5). EWEB.PRG's log has no query, cookie or field (7.3). After changing
  them: `make test` (`tests/net/cookies.txt`), `make fuzz` for a while (`fuzz_cookie`), `make itest`
  (`/site/cookie/`), `make hatarifiles`.
- **Bookmarks** are `src/browse/bookmark.c` (`BOOKMARK.HTM` next to `EWEB.PRG`, Netscape's bookmark
  file; read once a start for the Bookmarks menu, one safe save at each change), listed on
  `about:bookmarks` (`src/browse`). After changing them: `make test` (`tests/browse/bmtest.c`),
  `make hatarigui`, `make hatarifiles`. **The history** is `src/browse/history.c` (`HISTORY.TXT`
  next to `EWEB.PRG`: read at start for the home page's list, else when a page's links are first
  marked; written once at quit; the newest
  1000), listed on `about:history`; **visited links** are `src/browse/visited.c` (each link joined
  by `url_join_quick` where it can, else parsed; colours only, `src/css`; none on a page whose
  scripts run). After changing them: `make test` (`tests/browse/hgtest.c`, `tests/layout/visited.py`,
  the CSS cases), `make fuzz` for a while (`fuzz_history`, and `fuzz_url`, which checks the quick
  join against the parse), `make hatarigui`, `make hatarifiles`. **Find in page** is
  `src/layout/find.c` (the page's runs as one folded string, a place a run and a byte), marked by
  the canvas (`canvas.mark`) and driven by `ui/win.c` (`win_find`, `win_mark`): after changing it,
  `make test` (`tests/layout/find.py`, ewrender `-G`/`-U`) and `make hatarigui`. **Selecting and
  copying** are `find.c` too (`page_pos_at`, `page_range`, `page_copy_text`; a selection kept by
  `page_text_index` through a relayout), dragged in `ui/win.c` (`select_text`; Find's words are the
  selection): after changing them, `make test` (`tests/layout/select.py`, ewrender `-x`/`-j`) and
  `make hatarigui`. **Esc while a page is made**: the parser, the CSS engine and the layout ask
  `doc_poll` / `ec_poll` now and then, set by `src/browse`'s `build` to GEM's turn and Esc; after
  changing them, `make test` (`tests/layout/stop.py`, ewrender `-I STAGE:N`) and `make hatarigui`
  (its deep group). **Line breaks inside words** (UAX #14's, for what pages have: `lb_inword`,
  `word_pieces` in `src/layout/layout.c`, kept out of the plain words' loop, which `drawbench`
  counts): after changing them, `make test` (`tests/layout/breaks.html`). **Hebrew mixed with numbers
  and Latin**: `bidi_levels` (UAX #9 with a paragraph's words for its characters, its embeddings:
  `bidi_mark` controls from `unicode-bidi` and the text) and `visual_order` (a line by those
  levels), `word_pieces` cutting words where directions meet, `mirrored` in `src/gfx/canvas.c`,
  `dir=auto` in `src/doc/doc.c` (`auto_rtl`): after changing them, `make test`
  (`tests/layout/bidi.html`, `tests/layout/embed.py`, the Hebrew pages). **A page drawn while it loads**: a preview, once a load, of its start with the
  sheets EWeb has (`preview`, `br_preview_step` in `src/browse`; `shown()` in `ui/win.c`): after
  changing it, `make test` and `make hatarigui` (browse: `/site/slow.html`). **The home page** is `src/browse/home.html` (AERA's look; `tools/mkhome.py`
  puts its pictures in and makes `build/gen/home.h`, and writes the page filled in for ewrender
  and drawbench; the wordmark and the AERA logo in `src/browse/home/` are made once from AERA's
  brand files with `--assets`), filled in by `home_page` in `src/browse/browse.c`: after changing
  it, look at it with ewrender in 256 and 16 colours and 320 wide, and `make hatarigui` (browse:
  home). **Saving** (File > Save page as, a link's file streamed to
  the disk, a download) is `src/browse` with `ui/main.c`: `make itest` (`/site/files.html`) and
  `make hatarigui` after changing it.
- **The disk cache** is `src/browse/dcache.c` (RFC 9111's freshness and validation, stale pages
  when cut off, a POST forgetting; pictures kept decoded for the screen by `img.c`; `CACHE\EWEB.PAK`
  and `EWEB.IDX` next to `EWEB.PRG`, the index read when first needed and written at quit, a job's
  answers added in one write, the pack a ring of the cache's size, each answer tagged),
  used by `fetch()` in `src/browse`. After changing it: `make test` (`tests/browse/dctest.c`),
  `make itest` (`/site/cache/`), `make hatarifiles`.
- **Pictures** are `src/browse/img.c` (each address once, its file kept, decoded at each size a
  page shows it, fetched after the page by `br_pictures_step`; which one an `<img>` shows, from
  `srcset`, `sizes` and `<picture>`, and background pictures as tiled items, are `src/layout`'s,
  drawn by `src/gfx/canvas.c`; `tests/layout/pictures.html`) on AERA-Atari's `common/src/pic.c`
  (JPEG with libjpeg-turbo, `-DPIC_LIBJPEG`; PNG; GIF with libnsgif; WebP with libwebp,
  `-DPIC_LIBWEBP`, `EWEB.PRG` only, up to `WEBP_MAX_PX` in `img.c`): change the decoders there
  and run its `common/tests/pic` (`fetch.sh`, `make test`, `make test68k`). On a Falcon a baseline
  JPEG is the DSP's, Huffman codes to pixels, and a progressive one's from libjpeg's coefficients
  (`common/dsp/jpeg.a56`, `common/atari/dspjpeg.c`;
  `PICDSP=`), and so is the dither to 3 pens or more (`common/dsp/dither.a56`, the very pens of
  the 68030's: `make testdsp` there compares them), made fast for the stock Falcon (the owner's
  rule, 20 Oct 2026): after changing them,
  `make testdsp` there too (Hatari's DSP core), and `PICBENCH.TTP` on the emulated stock Falcon
  (`tests/bench/picbench.py --machine Falcon-4:256`, `--profile` for where its time goes). `make test` has `tests/browse/trim.py` (a picture at two sizes
  in little memory, ewrender `-D`); `make itest` loads
  `/site/pics.html` and `/site/bg.html`; `make hatarigui` shows it over the network and from the disk in every mode.
  Through the Pi a picture may come as the screen's pens (DESIGN.md, Pens from the Pi): webgate runs
  AERA-Atari's `common/tools/pens` (`aerapens`, pic.c itself; its parity test `common/tests/pic/pens.py`,
  `make pens` there), EWeb asks in `fetch_picture` and unpacks in `img.c` (`tests/browse/pentest.c`);
  `make itest` checks them against pictures decoded here (ewrender `-E` sums each picture's pens, `-Q`
  asks for pens or not) when its webgate has them.
- **Frames and iframes** (DESIGN.md, Frames and iframes): each frame a document of its own on the
  page's (`br_frames` on `doc.frames` in `src/browse`: `frame_cb`, `frame_page_cb`, `frame_load`,
  fetched by `br_pictures_step`; `frame_target`, `frame_go`, the history's `BR_ENTRY.frames`), its
  page spliced into the page's by the layout (`LAYOUT_OPT.frame`, `frame_page`; `frame_box`,
  `splice`, `frameset_layout` in `src/layout/layout.c`; `page.kids`, `page.lfr`). After changing
  them: `make test` (`tests/layout/frameset.html`, `frames.html`, `tests/layout/frames.py`, ewrender
  `-M X,Y` clicks and `-M back`), `make itest` (`/site/fs.html`), `make hatarigui` (browse).
- **More windows** (DESIGN.md, More windows): each a browser of its own with win.c's state
  (`ewin` in `ui/ui.h`; `cur` the window worked on, `br` its browser; `wins`, `win_new`,
  `win_free`, `win_front`, messages to the window they name in `win_message`), made by `main.c`'s
  `new_window` (^N, `to_window` for `BR_WINDOW`: a link's or a form's target `_blank` or a name no
  frame has, `frame_target`), as many as `win_max`; `src/browse` keeps every browser's pictures
  (`br_register`, `shown_imgs`) and shares the kept pages. After changing them: `make test`
  (`tests/layout/frames.py`: `frames/targets.html`), `make hatarigui` (browse: ^N, ^W, `/site/blank.html`).
- **SVG** is `src/svg` (fresh, in whole numbers; DESIGN.md, SVG): `svgread.c` reads an `<svg>` from
  EWeb's document (a file parsed by EWeb's HTML parser), `raster.c` fills and strokes its paths,
  anti-aliased, `fixmath.c` the 16.16 maths, `svg.c` sizes a file, draws it as a `PIC` and writes a
  page's inline `<svg>` out as a file (`svg_markup`) for `img.c`, which keeps it as a picture
  (`svg_cb` in `src/browse`, `LAYOUT_OPT.svg`). NetSurf's libsvgtiny is the yardstick only
  (`tests/svg/tiny.c`, `svgrender -T`). After changing it: `make test` (`tests/svg/run.py`: each
  case in `tests/svg/cases` against librsvg's, kept in `tests/svg/expect.txt`; a new case's with
  `--update`, rsvg-convert and Pillow; `tests/layout/svg.html`), `make fuzz` for a while
  (`fuzz_svg`; keep what it finds in `tests/fuzz/found/svg`), and count it
  (`build/m68k/svgrender -n`, `tools/count68k.sh`, soft float as `EWEB.PRG`) when the change is
  meant for speed.
- **`EWEB.FNT`** is made by `tools/mkfont.py` from DejaVu with the Pillow in `tools/requirements.txt`
  (pinned: its FreeType draws the glyphs, and `tests/layout`'s lists depend on them).
- **The help file** is `docs/GUIDE.md` (`tools/mkhelp.py`, EFTP's) with the family's front page
  (`docs/front.json`, `common/tools/helpfront.py`); `make help HCP=...`. Its window picture
  `docs/help/WINDOW.IMG` is a Hatari capture of the start page in 16 colours (`helpfront.py
  thumb`): take it again when the window changes. **The README's picture** `docs/screenshot.png`
  (and every picture of EWeb on GitHub) is from the 256-colour Falcon showing a site of the modern
  web, not EWeb's own page (Erez, 25 Oct 2026): `tools/screenshot.py URL` (Hatari, a webgate here;
  yaary.music now), kept as a palette PNG of the screen's exact colours.
- **The ST edition** is `EWEBST.PRG` (`make binary/EWEBST.PRG`): the same sources as 68000 code
  (`-Os -fno-store-merging -fdisable-tree-bswap`, `-DEW_ST`), `tls_none.c`, no DSP, TJpgDec in place
  of libjpeg-turbo, `atari/ll68000.c`'s 64-bit arithmetic (libgcc's is 68020 code); check a build
  with `m68k-linux-gnu-objdump -d -m m68k:68000` for `.short` in code. It ran on a 1 MB ST up
  to 0.27; Erez dropped that target on 23 Oct 2026 (DESIGN.md, Decisions): its floor is the 4 MB
  1040 ST, with the family's accessories loaded beside it (AERA-Atari's
  `common/tests/family/acc_all.py ST --prg EWeb:EWEBST.PRG:EWEB.FNT`, before each release). `EW_ST`
  keeps the arenas' pieces small, and a page that doesn't fit gives back the kept pages and the
  fonts and is tried once more. `make hatarigui`'s `st` group runs it on the 1040 ST (TOS 1.04) and
  through the gateway.
- **A release** carries what EWeb needs and nothing else (Erez, 24 Oct 2026): `EWEB.PRG`,
  `EWEBJS.OVL`, `EWEBST.PRG`, `EWEB.FNT`, `EWEB.HYP`, `CACERT.PEM` and `ROOTS.DAT`, and in the zip
  the icon (`icons/`: `make icons`, from the family's drawing `common/icons/EWEB.txt`), from `make
  dist`. The measuring tools (`NSBENCH.TTP`, `PICBENCH.TTP`, `EWGET.TTP`) and their pages are made
  by `make` for the tests and DESIGN.md's figures, not shipped (when Erez is to run one on his
  Atari, it goes in that release, and its notes say so). `tests/bench/mkpage.py` makes the
  news page and `tests/bench/mkphoto.py` the photo, the same bytes every time; when they change,
  measure them again and change their figures in DESIGN.md.
- **EWeb's HTML parser** is `src/doc/html.c` (fresh, after the HTML Standard's 13.2; DESIGN.md): it
  replaced Hubbub on speed and standards, and Hubbub stays only in nsbench and `make h5test` as the
  yardstick. Its named character references are `src/doc/entities.txt` (`tools/mkentities.py`
  makes `build/gen/entities.h`). After changing it: `make test`, `make h5test`, `make fuzz` for a
  while (`fuzz_html`; keep what it finds in `tests/fuzz/found/html`), and count it
  (`tools/count68k.sh build/m68k/docparse`, `tools/prof68k.py`) when the change is meant for speed.
  `ewh5test -f PAGE` prints a page's tree in the tests' format.
- `tests/html5lib/h5test.c` is Hubbub's test runner with a new `main()`, and stays MIT.
- **Scripts** are `src/js/js.c` (QuickJS from `third_party/quickjs`, kept as `third_party/netsurf`
  is: patches in its `patches/` with the README; a runtime per document, `doc.js`; the lean DOM on
  EWeb's own nodes; the budget), run by `src/browse` where Settings > Scripts says (`RUNSCRIPTS=`: on no
  site, the sites in `SCRIPTS=`, every site but those in `NOSCRIPTS=`; Options > Run this site's
  scripts; `br_scripts_site`, `tests/browse/sitetest.c`). On the Atari QuickJS and `third_party/libm` (musl's maths) are built with
  `-m68881` and run only with an FPU (the `_FPU` cookie); with `js.c` they are `EWEBJS.OVL`, a file
  of their own that `EWEB.PRG` loads when a site's scripts first run (`atari/js_ovl.c`, made by
  `atari/ovl.ld` and `tools/mkovl.py`; DESIGN.md, One EWEB.PRG for every Falcon), so `EWEB.PRG`
  has no FPU instruction at all (`m68k-linux-gnu-objdump -d`); the ST edition has `js_none.c`. After changing them: `make test`
  (`tests/js/run.py`, ewrender `-J`, `-K ID` clicks, `-T MS` timers), `make itest`
  (`/site/script.html`), `make hatarigui` (its `scripts` group, the Falcon with its 68882, with and without `EWEBJS.OVL`).
  `EWEB.PRG` runs on the stock 4 MB Falcon (Erez, 23 Oct 2026): `make hatarigui`'s `falcon4` group.
- **EWeb's CSS engine** is `src/css/` (`docs/CSS.md`): it replaced LibCSS on speed and standards
  (DESIGN.md). After changing it, `make test` (its cases, `tests/css/cases.txt`, worked out from the
  specifications) and `make csscmp` (against LibCSS on the page set: no difference may be left
  without a reason); after a change meant for speed, count it (`tools/count68k.sh`,
  `tests/bench/measure.py -E ewcss`) and change DESIGN.md's figures. `EC_COMPAT_*` options exist
  only for csscmp.
