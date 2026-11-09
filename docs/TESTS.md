<!-- SPDX-License-Identifier: GPL-2.0-or-later
     Copyright (C) 2026 AERA | Erez Yaary -->
# EWeb's tests

Every check, what it covers and how to run it. `make test` is the first; the others are
for the parts they name (CLAUDE.md says which to run after which change).

- `make test`: nsbench with both CSS engines on `tests/bench/sample.html` and
  `NEWS.HTM`, the Hebrew pages in every Hebrew encoding (`tests/hebrew`), the
  CSS engine's cases (`tests/css/cases.txt`), csscmp on the test pages, and
  libparserutils' input stream (`tests/input`: documents in random
  chunks, valid UTF-8 and not, against the WHATWG UTF-8 decoder), with
  AddressSanitizer and UBSan. `make hebtest68k` runs the Hebrew pages and the
  input stream as 68030 code.
- `make csscmp`: EWeb's CSS engine against LibCSS on the whole page set: every
  difference must be one of LibCSS's known gaps.
- `make h5test`: EWeb's HTML parser and Hubbub against the Web Platform Tests'
  tree-construction tests, here (EWeb's with AddressSanitizer and UBSan) and
  as 68030 code under QEMU: EWeb's passes all but the six that need scripts.
- `make test` also runs the HTTP client against servers played by
  `common/tests/fuzz/fuzz_net.c` (`tests/net/nettest.c`: lengths, chunks,
  gzip and deflate, redirects, kept connections, and what a hostile server
  might send), the cookies (`tests/net/cookies.txt`, from RFC 6265bis), the
  bookmarks (`tests/browse/bmtest.c`: other browsers' files read, adding,
  removing, the file written at each change), the history
  (`tests/browse/hgtest.c`: the newest 1000, a place on a page another
  address, the file written at quit and read again), the disk cache on a clock of
  its own (`tests/browse/dctest.c`: how long an answer stays fresh, what may
  not be kept, asking again and a 304, the pack a ring of the cache's size),
  pictures that come as the Gateway's pens (`tests/browse/pentest.c`), the fuzz
  targets' seeds and past finds (`tests/fuzz`), and the scripts' test pages
  (`tests/js`: the DOM, events, forms, `document.write`, timers, computed
  styles, a page left for another, errors and the budget), and a page's encoding
  (`tests/browse/enctest.c`: the HTML Standard's sniffing for pages that
  name none, the default by the address, the Encoding Standard's labels,
  links' queries in the page's encoding).
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
  file dropped on it, with the release's `CACERT.PEM` and `ROOTS.DAT`
  (twice: the second time `ROOTS.DAT` knows `CACERT.PEM`'s date), and
  through AERA-Gateway's webgate (TLS 1.3 from the Gateway to the server).
- `common/tests/ec`: the DSP's elliptic curves against BearSSL (AERA-Atari's
  test, run there).
- `make test` also sends forms (`tests/forms/cases.txt`, worked out from the
  HTML Standard: the entry list; urlencoded, multipart and plain text; the
  page's character set; Return in a field; typing, reset), and lays out sixteen
  pages (`tests/layout`: text, tables, floats, inline blocks, flexbox, grid,
  forms, pictures and more; and `tests/bench/sample.html`) and compares their
  display lists with the ones kept, and the Hebrew test page in each of its
  encodings, and pages nested deeper than the stack holds
  (`tests/layout/deep.py`: cut, not crashed), with AddressSanitizer and
  UBSan; `make itest` loads a site from
  `tests/net/server.py` in the browser over HTTP and HTTPS (its stylesheet,
  its links, a redirect, an anchor, a page in windows-1255, Back and Forward
  from the pages kept, EWeb's own page for a server that isn't there) and
  saves (`/site/files.html`: a file that isn't a page, a link's file
  streamed to the disk, one that isn't there, a link's page, the page
  shown).
- `make hatarigui` (`tests/hatari/gui_test.py build/gui`; name groups
  after it to run only those): `browse`, `EWEB.PRG` on an emulated Falcon
  through FAKESTNG, the mouse moved by xdotool: a page and its stylesheet,
  a link clicked, Undo, an address typed with an anchor, a long page
  scrolled and Back from memory, a secure page, Esc stopping a download, a
  page of pictures (scrolled while they come, the throbber turning), forms
  (a field typed into and sent with Return, Back to it, Tab, a box ticked
  with Space and by its label, a login sent with POST), saving (^S,
  Alternate-click on a link, a file EWeb doesn't show), bookmarks (^D, ^B,
  Remove) and ^Q; `scripts`, the Falcon with its 68882: a page's tabs
  clicked, an `onclick`'s alert answered, from `EWEBJS.OVL`, and the page
  without it; `screens`, a page and a page of
  pictures from the disk on the Falcon in 256 colours (EWeb's own, and the
  system's) and true colour and on the TT in 16 colours and monochrome, the
  pictures read back from the cache the next time, and the Settings (^,); `st`, `EWEBST.PRG` on a 4 MB
  1040 ST (TOS 1.04) in monochrome and medium resolution, a
  secure page without the gateway, then a secure page and its pictures, as
  the Gateway's pens, through AERA-Gateway's webgate; `gateway`, `EWEB.PRG` on
  the Falcon through webgate with the family's accessories loaded, the
  pictures as JPEG and PNG and as the Gateway's pens; `falcon`, a secure page
  whose pictures come from another secure host, TLS and the JPEGs on the
  DSP; `falcon4`, `EWEB.PRG` on the stock 4 MB Falcon (no FPU, no TT RAM): a
  177 KB page loaded and loaded again, and the pictures' page; `deep`, a page nested 1000 deep on the Falcon and the 1040 ST, cut,
  not crashed; `refresh`, pages that send you on, one that waits while
  Esc keeps it, and one that sends itself on again and again, stopped;
  `details`, a summary clicked open and closed on the Falcon and the 1040
  ST, and an address whose #fragment is in a closed details.
  `screens` and `st` also type a search address longer than its field
  into the Settings (on the ST with Shift+arrows at its ends) and check
  that it is saved whole.
- `make itest` also loads a page of pictures (`tests/net/server.py`'s
  `/site/pics.html`): each decoded at the size shown, the page laid out
  again for the one whose size it didn't give, the missing one failed; a
  background from the sheet's folder and a `srcset`'s choice
  (`/site/bg.html`), a WebP decoded (a big one left out; the ST edition
  fetches none), and a picture on its own; the disk
  cache (`/site/cache/`: a fresh page shown with no request, the next run
  too; a 304; a POST forgetting the page it went to; a kept page shown when
  its server can't be reached, but not a must-revalidate one; decoded
  pictures read back); forms (`/site/form.html`) sent with GET and POST,
  urlencoded and multipart, a POST answered with 303, and Back to the form
  as it was filled, with no field, query or cookie kept in the log;
  cookies (`/site/cookie/`: set and sent back, the lasting one kept in a
  file for the next run, a 303's, another site's picture's refused); and
  scripts (`/site/script.html`: a file's script, `fetch` and
  `XMLHttpRequest`, `document.cookie` without the HttpOnly cookie). Then
  the same through AERA-Gateway's webgate (`gateway/`, or `WEBGATE=`): TLS
  1.3 from the Gateway, a redirect, a big chunked answer, a certificate it can't
  check refused, pictures made smaller and WebP made JPEG, pictures as the
  Gateway's pens (the same to the pixel, and read back from the disk cache), a
  login's POST and its cookie; and the Gateway's browser (`--browser`, with a
  stand-in for webgate-ext): a snapshot and the live view. The decoders'
  own tests (PngSuite, libjpeg-turbo's and libnsgif's pictures against
  Pillow, the 68030 build under QEMU, the DSP's JPEG and dither against
  the 68030's, and the Gateway's pens against the Atari's own code) are
  AERA-Atari's `common/tests/pic` (`make test`, `make test68k`, `make
  testdsp`, `make pens` there).
- `make hatarifiles`: what `EWEB.PRG` and `EWGET.TTP` do on the disk, from
  Hatari's GEMDOS trace (the guidelines' 6.5): the opens and lookups of
  each case (start, a page with pictures, five pages, back and forward, the
  page size changed, secure pages, five minutes idle, quit, a second start
  with a secure page and quit, `EWGET.TTP` fetching 1 MB) against its
  budget. Apart from the first page (the cookies and the cache's index
  read) and the first secure page (the certificates and the seed), pages,
  history and idle touch nothing, and a quit with nothing changed writes
  nothing. It starts from an `EWEB.INF` as 0.26 wrote it and checks that the one saved has an empty line after each setting (the
  guidelines' 6.1).
- `make urltest`: the URL parser against the URL Standard's tests (Web
  Platform Tests, `tests/url/fetch.sh`), for http and https.
- `make fuzz`: the HTTP client, the URL parser, the cookies, the HTML parser and
  the bookmark file under libFuzzer.
- `tests/bench/measure.py [-E ewcss]`: 68030 instructions per phase for the
  whole page set; `tests/bench/hatari.py [--stock]`: `NSBENCH.TTP` timed on an
  emulated Falcon; `tests/bench/picbench.py`: a picture timed as `EWEB.PRG` and
  `EWEBST.PRG` decode it, on the emulated Falcon and ST (`make
  binary/PICBENCH.TTP binary/PICBST.TTP` first). The Hatari rigs boot the
  family's machines (`common/tools/machines.py`): the owner's own TOS when
  `~/.aera/tos` has it (1.04 on the STs, 4.04 on the Falcon, 3.06 on the
  TT), else EmuTOS 1.4.
- `ITEST_PORTS=N make itest` moves the servers' ports up by N,
  and `GUI_SLOT=N make hatarigui` gives the rigs their own ports and X
  display, so two trees can run them at once.
