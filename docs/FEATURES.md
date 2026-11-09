<!-- SPDX-License-Identifier: GPL-2.0-or-later
     Copyright (C) 2026 AERA | Erez Yaary -->
# EWeb's features in full

The README's Highlights, at length (as the README had them until 9 Nov 2026). The guide,
[`GUIDE.md`](GUIDE.md), says how to use each.

- **`EWEB.PRG`**: GEM windows, as a browser has them (^N for another; a
  link meant for a new window opens one): Back, Forward,
  Reload, Home, Search and Stop, the address, the AERA throbber turning
  while a page comes, and a status bar whose line fills as it loads; links
  followed with a click, scrolling with the sliders and the keys (a page
  wider than the window sideways too), 50 pages of
  history, the page's text to the clipboard, larger and smaller text. Words
  typed in the address go to FrogFind, the search engine made for vintage
  computers (Settings > General for another). A page that sends you on
  (`<meta http-equiv=refresh>`, the Refresh header) goes on when its time
  comes, as the status line says; Esc stays.
- **EWeb's own engine**: EWeb's HTML parser (the HTML Standard's, nearly three
  times as fast as NetSurf's on the 68030), EWeb's CSS engine
  (about a tenth of LibCSS's time on the 68030, [`docs/CSS.md`](docs/CSS.md);
  a sheet for print or another width, by its `media`, left out)
  and fresh layout code: blocks, lines, lists, floats, inline blocks,
  tables (their borders collapsing too), flexbox and grid, boxes that hide
  what overflows them or scroll it with bars of their own, colours (translucent ones and opacity too, mixed with what is under
  them in every screen mode), borders, background pictures, colour
  gradients (linear, radial and conic, dithered to the screen's colours;
  the ST edition draws those that run straight down or across, in the
  screen's own colours, and any other in its colours' average), the old
  centring (`<center>`, `align`), and Hebrew right to left, in every Hebrew
  encoding (visual Hebrew, ISO-8859-8, shown in the order it is stored, as
  browsers show it): a page that names none is read as browsers read it
  (UTF-8 when it is, Windows-1255 on Israeli sites), and View > Encoding
  reads one again in another. The parts a page folds away (`<details>`)
  open and close with a click. Frames and iframes, each a page of its own
  in a box that scrolls, their links opening where the page says.
- **Pictures**: JPEG (libjpeg-turbo, every kind), PNG with transparency and
  GIF, and SVG drawings, files and those written into a page (EWeb's own
  reader and anti-aliased rasteriser, `src/svg`, in whole numbers for the
  68030), fetched after the page is shown (you read and scroll meanwhile),
  decoded straight at the size shown, dithered to the screen's colours in
  monochrome, 4, 16 and 256 colours (on a Falcon the DSP decodes the JPEGs
  and, in 4 colours or more, dithers them, about eight times as quick as the
  68030); of a picture offered in several sizes and forms (`srcset`,
  `<picture>`), the one the screen needs; off with one menu item on a slow
  line.
  The decoders are the family's (AERA-Atari's `common/src/pic.c`).
- **Forms**: fields, boxes, lists and buttons, typed into (Hebrew too,
  right to left), Tab from one to the next and Return to send; sent with
  GET or POST, urlencoded, multipart or plain text, in the page's own
  character set, as the HTML Standard has browsers send them (a file field sends
  the file chosen, since 0.73).
- **A little JavaScript** (QuickJS, on a lean DOM of EWeb's own document):
  menus, tabs, form checks, alerts, timers and `fetch`, for the sites you
  choose (Options > Run this site's scripts), within a budget of 200 KB and
  a few seconds a page; past it, or with scripts off, the page's
  `<noscript>` version. It needs an FPU (a 68881 or 68882, as on the DFB1):
  without one, and in the ST edition, pages are shown without scripts.
- **Cookies**, as RFC 6265bis has them, those meant to last kept in
  `COOKIES.TXT` (written once, at quit; their values encrypted, as the
  family keeps passwords); none for other sites' pictures and
  stylesheets; Options > Forget cookies.
- **Bookmarks**: a Bookmarks menu (^D adds the page shown, ^B shows them all
  as a page), kept in `BOOKMARK.HTM` in the bookmark file other browsers
  import and export.
- **Find in page**: Edit > Find (^F), ^G and Shift-^G: the words marked
  and brought into view, case aside, Hebrew as it is read.
- **Selecting and copying**: drag over the text, double-click a word or
  Select all (^A); ^C copies it to the GEM clipboard, Hebrew as it is
  read, and the selection keeps its text when the page is laid out again.
- **History and visited links**: View > History (^Y) lists the pages opened,
  newest first; links to them are drawn in their visited colour (colours
  only, and never on a page whose scripts run); the newest 1000 kept in
  `HISTORY.TXT` (written once, at quit); Options > Forget history.
- **A disk cache**, as HTTP has browsers keep one: pages, stylesheets and
  pictures kept in a ring of 4 MB pieces (`CACHE\`) from one run to the next,
  asked again with their ETag or date when they say so, and pictures kept
  decoded for the screen (a page of pictures in a quarter of a second
  instead of twenty); a kept page shown when its site can't be reached;
  Settings > General for its size, Options > Empty the cache.
- **Downloads and saving**: the page shown as it came (^S); a link's file
  (Alternate-click, as Safari's Option-click, or a link marked as a
  download), a file EWeb doesn't show (a ZIP, a program, a PDF) or one its
  site says to save, opened: saved where you say, straight to the disk as it
  comes, of any size, in a download box with its name, a bar by its size and
  Cancel;
  under the name the site gives it (RFC 6266), made to fit a TOS disk,
  archives as they came.
- **EWeb's own fonts** (`EWEB.FNT`): DejaVu Sans, Serif and Mono in four
  styles and twelve sizes, the same on every Atari, with or without GDOS:
  Latin (Vietnamese and Romanian too), Hebrew, Greek, Cyrillic and the
  common symbols, each alphabet read only when a page shows it.
- **Every screen**: monochrome, 4, 16 and 256 colours and the Falcon's true
  colour, through ST-RAM so that it runs from TT-RAM; in 256 colours EWeb's
  own palette, with the pale tints pages need (a setting keeps the
  system's).
- **Secure sites itself**: TLS 1.2 on the Atari, the arithmetic on the DSP
  (a Direct connection); or through the Gateway, AERA-Gateway's web service on a
  Raspberry Pi (TLS 1.3 sites too).
- **The family's ways**: the About box, menus, shortcuts, right-click menus,
  VA_START and drag and drop, the help file, iconify, the crash report; a
  mail address (`mailto:`) goes to EMail.
- And measuring tools, made from the source (Building) for the tests and
  not in the release: `NSBENCH.TTP` times the HTML parsers and both CSS
  engines on the Atari, `PICBENCH.TTP` a JPEG decoded and dithered on the
  68030 and with the DSP, `EWGET.TTP` fetches a page and times it.
  [`docs/DESIGN.md`](docs/DESIGN.md) has what was measured, the decisions
  and the plan.
