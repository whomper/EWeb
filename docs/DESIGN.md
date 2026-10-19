# EWeb design

EWeb is a web browser for the Atari Falcon with a DFB1 accelerator, part of the AERA family. This
page holds the decisions made so far, what the Falcon was measured to do, the design that follows from
it, and how EWeb gets past CAPTCHAs.

## Decisions (16 Oct 2026)

| Question | Your decision | What it means |
|---|---|---|
| Hardware | A **DFB1** at **50 MHz** with an **FPU** and **128 MB of TT-RAM**, plus the Falcon's own **DSP** | The machine EWeb is tuned and measured on. It uses the FPU for JavaScript's maths and the DSP for heavy number work (below). |
| Network | A **NetUSBee** | An NE2000-style Ethernet adapter on the cartridge port. About 300 KB/s at best (one report, measured under Linux, not STinG); EFTP on your Falcon gives the real figure. |
| Gateway | **An option, as in the other apps** | EWeb talks to sites itself by default. AERA-Gateway's `web` service is the alternative, and is the only way past CAPTCHAs (below). |
| Machines | **Every Falcon; the most from a fast one** | One program, in a build with the FPU and one without, that checks at start what the Falcon has (accelerator, TT-RAM, FPU) and uses it. Tuned for your DFB1. |
| Code | **NetSurf's code where we can't do better; fresh code where we can** | Each part (HTML parser, CSS engine, DOM, layout, image decoders) uses NetSurf's version unless fresh code written for a fast 68030 is faster and at least as standards-compliant. How it's decided is below. The GEM front end and everything Atari-side are ours, on AERA-Atari's `common/`. |
| JavaScript | **QuickJS** | The best fit measured: modern syntax, small, portable C, MIT licence. |
| Hebrew | **Supported** | Hebrew pages in every Hebrew encoding, right to left, mixed with English. It makes layout fresh code: NetSurf's reorders no text (below). |
| System | **Plain TOS with STinG** | EWeb is one program with its own event loop: STinG connections that don't block, pages loaded in small steps between GEM events, the program and its data in TT-RAM when there is some. There are no threads and no memory protection, so the code has to be careful with memory. |

## Why not a full modern browser

A browser that runs today's web the way Chrome or Firefox does, entirely on the Falcon, isn't
realistic. More work won't change that: the 68030 is roughly 1,000 to 5,000 times too slow for the way
modern sites are built. A good browser for the "document web" (text, links, images and forms) is
realistic, with the gateway for the rest.

### What was measured

QuickJS (2026-06-04, commit `84e723f`), the best small JavaScript engine for this machine, was built
for the 68030 with FPU (`m68k-linux-gnu-gcc` 13, `-O2 -m68030 -m68881`) and run on QEMU's 68030
(`qemu-m68k -cpu m68030`, one instruction per block). Every executed instruction was counted, less the
1.14 million the engine takes to start.

The counts are exact. The times assume the DFB1 runs 5–8 million instructions a second at 50 MHz: an
emulated 32 MHz Falcon with TT-RAM ran NetSurf's code at 4.0–5.3 million (see the first milestone,
below), and the 68030's 256-byte caches hurt an interpreter. Your DFB1 ran NetSurf's code at 5.2 million
(below), so expect the low end of each range. The last column is Chrome's engine (V8, in Node 22) on one 2.8 GHz Xeon core, from cold.

| Workload | 68030 instructions | DFB1 at 50 MHz | V8 |
|---|---|---|---|
| Parse a 146 KB JSON API response | 16 M | 2.0–3.2 s | 0.65 ms |
| Compile jQuery 3.7.1 (87 KB) | 65 M | 8.1–13 s | 3.7 ms |
| Compile react-dom 18.3.1 (132 KB) | 103 M | 13–21 s | 5.8 ms |
| Compile three.js 0.160 (670 KB) | 361 M | 45–72 s | |
| Load React and render a 100-story front page (1,709 elements, HTML only, no layout) | 171 M | 21–34 s | 22 ms |

What follows from it:
- **JavaScript is the wall.** Compiling minified JavaScript costs about 540–780 instructions per byte.
  The median web page ships about 0.6 MB of compressed JavaScript (Web Almanac 2024), well over 1 MB
  unpacked. Just compiling it would take roughly **2–5 minutes, before any of it runs**, and every click
  re-renders through the same slow path.
- **Memory is not the problem.** The engine's heap stayed under about 3 MB in these tests. 128 MB of
  TT-RAM is generous for a lean browser.
- **The FPU helps less than you might hope.** All JavaScript numbers are floating point, so it speeds
  up script maths. Layout and image decoding can use integer arithmetic, so it doesn't change the
  picture.
- **The DSP is the more useful helper.** `common/` already uses it for RSA in TLS, and since 18 Oct
  2026 for the handshake's elliptic curves too (below). EWeb's candidates
  to move there are scaling and dithering images and the colour conversion in JPEG decoding, each
  measured against the 68030 first.
- **Engine choice.** QuickJS understands modern JavaScript syntax. Duktape, the engine NetSurf uses,
  stops at ES5, so modern code bundles don't even parse. The engines in real browsers rely on
  just-in-time compilers that have no 68k version.

What HTML and CSS cost is in the next section.

## What NetSurf's HTML and CSS cost (first milestone, 16 Oct 2026)

NetSurf's HTML parser (Hubbub) and CSS engine (LibCSS) were ported to plain TOS on `common/`, and
`nsbench` (in `tests/bench`) measures them page by page: it parses the page into a tree, parses the
stylesheets, then selects and composes every element's style the way NetSurf does. As
`NSBENCH.TTP` it is 740 KB, of which NetSurf's code is about 700 KB.

**The page set:** 258 real saved pages (the htmlparser-benchmark corpus: BBC, NYT, GitHub and
others, 13–396 KB; the median page is 84 KB with 743 elements). The corpus keeps no linked
stylesheets, so two real ones stand in for them: Pico (82 KB, rules on elements and attributes) and
Bootstrap (233 KB, rules on classes). Each page's own `<style>` and `style=""` count too.

**How:** every phase's 68030 instructions were counted under QEMU (`tests/bench/measure.py`), and the
times use 5–8 million instructions a second, as above.

| Phase (per page) | Median | Time | 90th percentile | Time |
|---|---|---|---|---|
| HTML parse | 15.9 M | 2.0–3.2 s | 31.3 M | 3.9–6.3 s |
| CSS parse, Pico (once per site, if kept) | 33.0 M | 4.1–6.6 s | 36.0 M | 4.5–7.2 s |
| CSS parse, Bootstrap | 103 M | 13–21 s | 105 M | 13–21 s |
| Style for every element, Pico | 50.7 M | 6.3–10 s | 105 M | 13–21 s |
| Style for every element, Bootstrap | 42.3 M | 5.3–8.5 s | 87.4 M | 11–17 s |
| **All three, Pico, first visit** | **99.9 M** | **12–20 s** | **168 M** | **21–34 s** |
| **HTML and styles, Pico already parsed** | **65.8 M** | **8–13 s** | **132 M** | **16–26 s** |

Layout, drawing, images and the network come on top; they aren't measured yet.

(Corrected 17 Oct 2026: `nsbench` didn't yet do what NetSurf does when the tree builder finds a
`<meta charset>` that the first look missed, which is to parse the page again in that charset, so 101
of the pages were read as Windows-1252. Measured that way, NetSurf's HTML parse costs 17.7 M at the
median and 32.4 M at the 90th percentile. The UTF-8 copy below brings it to 12.8 and 24.4 M.)

**Cross-check on an emulated Falcon** (`tests/bench/hatari.py`: Hatari 2.4.1, EmuTOS 1.4, a 68030
at 32 MHz, Hatari's fastest, with a 68882 and TT-RAM, cycle-exact), one BBC News page (100 KB, 862
elements) with Pico:

| Phase | Emulated Falcon, 32 MHz | 68030 instructions | Rate |
|---|---|---|---|
| HTML parse | 4.15 s | 22.1 M | 5.3 M/s |
| CSS parse | 7.29 s | 32.9 M | 4.5 M/s |
| Style for every element | 14.58 s | 58.9 M | 4.0 M/s |

The QEMU count is the Linux build's (glibc's `malloc`, not `common/`'s), so the rates are close, not
exact. Scaled to 50 MHz they give about 6–8 M/s, if the DFB1's memory keeps up.

**On your DFB1** (NSBENCH 0.1, `SAMPLE.HTM`, 16 Oct 2026), against Hatari running the same program:

| Machine | HTML | CSS | Styles | All | Against a stock Falcon |
|---|---|---|---|---|---|
| Your Falcon, DFB1 at 50 MHz | 59 ms | 362 ms | 122 ms | 543 ms | 2.4× |
| Emulated: 68030 at 32 MHz, TT-RAM | 63 ms | 427 ms | 133 ms | 623 ms | 2.1× |
| Emulated: stock Falcon, 16 MHz, ST-RAM | 138 ms | 905 ms | 270 ms | 1,313 ms | 1× |

The run is about 3.3 M instructions, so your DFB1 ran about 6 M a second: inside the 5–8 M assumed,
and the estimates above stand. It is only 1.15 times the emulated 32 MHz machine, though, where the
clocks would give 1.56. NSBENCH 0.2 reports what decides that: whether the program and its memory
are in TT-RAM or ST-RAM, the 68030's caches, a loop's speed and each RAM's read speed. (The page
measured 1,116 bytes on the Falcon, not 1,088: its line ends were converted once more on the way,
which changes nothing that matters.)

**A page to compare with:** from 0.2, the release has `NEWS.HTM` and `NEWS.CSS`, a made-up news front
page (72 KB, 816 elements, with a Hebrew section) and its stylesheet (83 KB), written by
`tests/bench/mkpage.py` to cost what the page set's median does. As shipped:

| | HTML | CSS | Styles | All |
|---|---|---|---|---|
| 68030 instructions | 15.7 M | 49.0 M | 46.7 M | 111 M |
| Emulated: 68030 at 32 MHz, TT-RAM | 3.08 s | 11.19 s | 10.43 s | 24.7 s |
| Emulated: stock Falcon, 16 MHz, ST-RAM | 6.53 s | 23.15 s | 21.06 s | 50.7 s |

| Your Falcon, DFB1 at 50 MHz, caches on | 2.47 s | 9.37 s | 9.46 s | 21.3 s |
| Your Falcon, DFB1 at 50 MHz, caches off | 3.39 s | 12.23 s | 12.99 s | 28.6 s |

**NSBENCH 0.2 on your DFB1** (16 Oct 2026). The first run found the 68030's caches **off** (CACR
0000): the loop ran 2.7 million rounds a second and the page took 28.6 s, 3.9 M instructions a second.
With both caches on, with burst (CACR 3111), the loop ran 6.1 million, just under the 6.2–8.3 million
Motorola's timings give at 50 MHz, so the CPU runs at its clock. The page took 21.3 s: 5.2 M
instructions a second, 2.4 times a stock Falcon and 1.16 times the emulated 32 MHz one. The caches
made the loop 2.25 times faster but the page only 1.34 times: NetSurf's code and data are far larger
than the 256-byte caches, so it mostly runs from TT-RAM, which reads at 20,000 KB/s (Hatari's reads at
24,200; ST-RAM 5,000 on your Falcon). The program and its heap were in TT-RAM.

So **plan on about 5 M instructions a second** for this kind of code on your DFB1, and a median page
costs **21 s** to parse and style with NetSurf's libraries: 2.5 s of HTML and **19 s of CSS**, nine
tenths of it. CSS is what has to get faster (below). A browser at a usable speed wants that page in
2–3 s, so the CSS work has to cost about a tenth of what LibCSS's does, or be done once and kept.

**Standards:** Hubbub passes 1,139 of the 1,679 current tree-construction tests (the Web Platform
Tests' `html/syntax/parsing`, 15 Oct 2026; `make h5test`): 67.8%, and the same as 68030 code. 198
fragment tests don't apply (Hubbub has no fragment parsing). Without the two newest features,
`<template>` (121 failures) and processing instructions (84, a 2026 change), it passes 76.6%. The
rest are edge cases: script text cut short at the end of a file, `<ruby>`, newer elements such as
`<search>`.

**Where the time goes** (callgrind, the BBC page with Pico): selection is about half, close to a third
of that composing each element's style with its parent's; parsing the CSS is a quarter; parsing the
HTML a fifth. Two fifths of the HTML parse, and part of the CSS parse, is libparserutils turning UTF-8
into UTF-8, decoding and re-encoding every character (now copied: see Hubbub's input, below).
`nsbench`'s own tree code is under 2%.

**The first NetSurf-or-fresh calls:**
- **HTML parser: Hubbub stays.** It costs 2–3 seconds on a median page, and a fresh parser would have
  to match its 1,139 passing tests before it could count as better. Its first gain, copying UTF-8
  instead of decoding and re-encoding it, took 28% off on the page set (Hubbub's input, below).
  `<template>` needs adding.
- **CSS: fresh code, decided (17 Oct 2026).** Parsing and selection cost 3–9 times what the HTML
  does (19 of `NEWS.HTM`'s 21 s on your DFB1). EWeb's own engine (below) is 7–15 times faster on the
  page set and closer to today's specifications, so it replaces LibCSS; LibCSS stays in nsbench as
  the yardstick.
- **Layout: fresh code, decided by Hebrew.** NetSurf's layout uses `direction` only for default
  alignment and margins, and nothing in NetSurf reorders text, so a line mixing Hebrew and English
  would come out backwards. Fresh layout with the bidirectional algorithm built in is both more
  compliant and needed (see Hebrew, below).

## EWeb's own CSS engine (17 Oct 2026)

`src/css/`, described in [CSS.md](CSS.md): written for the 68030 from the start. It reads a rule's
declarations only when the rule first matches, skips `@media` blocks that don't apply, finds an
element's rules through its own atoms with no callbacks, rejects most descendant selectors with a
filter of the ancestors, shares styles between siblings and caches them between cousins, applies
each property once, and uses integer fixed point throughout. It computes the 83 properties EWeb
draws (LibCSS computes over a hundred, the spoken-media ones too), and has what LibCSS lacks:
`var()`, `calc()`, Selectors 4, Media Queries 4, `@supports`, `@layer`, CSS Color 4's syntax, `gap`,
`inset`, gradients.

**Speed.** 68030 instructions for the CSS (sheets and every element's style), counted under QEMU:

| | LibCSS | EWeb's engine | Fewer |
|---|---|---|---|
| `NEWS.HTM` with `NEWS.CSS` | 95.6 M | 8.7 M | 11× |
| The page set with Bootstrap, median page | 145.8 M | 9.6 M | 15× (10–20×) |
| The page set with Pico, median page | 84.4 M | 12.0 M | 7× (6–9×) |

And timed on Hatari (`NSBENCH.TTP -E both`, `NEWS.HTM`):

| Machine | LibCSS (css + styles) | EWeb's engine | Faster |
|---|---|---|---|
| Emulated: 68030 at 32 MHz, TT-RAM | 11.12 + 10.43 s | 0.65 + 1.34 s | 10.9× |
| Emulated: stock Falcon, 16 MHz, ST-RAM | 23.11 + 20.96 s | 1.39 + 2.96 s | 10.1× |

Your DFB1 ran LibCSS's part of `NEWS.HTM` in 18.8 s, 0.87 of the emulated 32 MHz machine's time,
so EWeb's engine should take about **1.7 s** there, and the whole page (with the HTML's 2.5 s)
about 4 s instead of 21. At the DFB1's 5.2 M instructions a second the page set's median page is
1.8 s with Bootstrap and 2.3 s with Pico (90th percentile 2.2 and 3.6 s). The HTML parser is now
the larger part; copying its UTF-8 input (next section) took 28% off it.

**On your DFB1** (NSBENCH 0.3, 17 Oct 2026; the caches were off again, CACR 0000): `NEWS.HTM`
with `NEWS.CSS` took LibCSS 12.21 + 12.99 s and EWeb's engine 0.76 + 1.55 s: **10.9 times
faster**, as on the emulators. With the HTML's 3.40 s the page took EWeb 5.7 s where it took
NetSurf's libraries 28.6 s. With the caches on it should be about 4 s.

**Standards.** `make csscmp` compares the two engines value by value on all 258 pages, each with
Bootstrap and then with Pico: 53.8 million values a run, every element, every property both have.
A difference counts only if it is one of LibCSS's known gaps, most of them found exactly: the page
is styled again with one of the engine's `EC_COMPAT_*` switches on, each leaving out one thing
LibCSS lacks, and the difference is put down to that only if the values then agree. What is left:

- With Bootstrap, 4.0 million differences, all LibCSS falling short: mostly CSS 3 initial values and
  computed-value rules (`min-width: auto`, no border width without a style, flex items blockified,
  `bolder` computed), then `calc()` (26,597 values), `text-align: start` (1,035), `text-decoration`
  with a style (76), and LibCSS's own px conversion, which rounds the pixels in a unit down and swaps
  `vw` and `vh` (`unit.c`; 267).
- With Pico, 4.2 million, the same kinds, with Selectors 4 (`:where()`, `:is()`, `:not()` of a list;
  207,694 values) the largest of the new ones.
- 171 and 137 values on three pages with no reason found by the tools. Each was traced by hand, and
  in each EWeb's value is the right one: LibCSS doesn't end a bad `url(` where CSS Syntax 3 does; on
  two pages LibCSS leaves out style attributes, and on one it misses a rule that plainly matches,
  both only in the whole page (the cause is inside LibCSS's selection). Separately, the engine's
  switch for style attributes finds about 118 values a run where LibCSS left one out.

`make test` runs 39 cases worked out from the specifications (`tests/css/cases.txt`: the cascade,
selectors, shorthands, values, `var()`, `@media`, `@supports`, `@layer`, error recovery, sharing),
with AddressSanitizer and UBSan, and the engine ran clean under them on every page of the set.

**A LibCSS fault, for the record:** `css_unit__px_per_unit()` in LibCSS's `select/unit.c` uses the
viewport's width for `vh` and its height for `vw`. (EWeb takes no part in NetSurf's development: its
changes to NetSurf's libraries stay here.)

**Next for the engine:** logical properties (`margin-inline-start` and the rest, which Hebrew pages
need), keeping parsed sheets per site (a site's stylesheet is parsed once, not on every page), and
Pico's cost per element (11,000 instructions against Bootstrap's 3,100: `:where()` and custom
properties).

## Hubbub's input: UTF-8 copied (17 Oct 2026)

Hubbub reads the page through libparserutils, which turned every character into UCS-4 and back into
UTF-8, even when the page was UTF-8 already. Now valid UTF-8 is copied, checked as it goes
(`third_party/netsurf/patches`). What isn't UTF-8 becomes U+FFFD as the
WHATWG Encoding Standard says, one for each broken sequence, wherever the page is split into chunks.
libparserutils' own decoder didn't do that: it let through 5- and 6-byte forms and code points past
U+10FFFF, and at a chunk boundary it could swallow the rest of the chunk or drop a character. Two
bugs were found on the way and fixed. A document that ended 64n+1 characters into its last chunk lost
its last character, and the input was never flushed at the end. Hubbub's first look for a
`<meta charset>` now reads 1,024 bytes, as the HTML standard says, not 512.

`nsbench` now does what NetSurf does with a `<meta charset>` found while parsing: it parses the page
again in that charset. Before, it kept going in Windows-1252, and 101 of the 258 pages were measured
in the wrong charset.

**Speed**, the HTML parse in 68030 instructions:

| | NetSurf's libparserutils | Copying UTF-8 | Fewer |
|---|---|---|---|
| `NEWS.HTM` | 15.76 M | 11.03 M | 30% |
| The page set, median page | 17.7 M | 12.8 M | 28% |
| The page set, 90th percentile | 32.4 M | 24.4 M | 25% |

From 216 instructions per byte of HTML to 154 at the median. The 62 pages that gain less than 5%
aren't UTF-8 (Windows-1252, ISO-8859-1 or no charset given) and still go through the old decoder.
Copying costs six 68030 instructions a byte of ASCII. LibCSS reads its sheets the same way, so its
parse is about 15% cheaper too: Bootstrap from 103 to 87.5 M, Pico from 33.0 to 27.4 M. The comparison
with EWeb's engine above is with NetSurf's libparserutils, as released.

Timed on Hatari, `NEWS.HTM`'s HTML took 2.29 s on the emulated 32 MHz machine, down from 3.08, and
4.99 s on the emulated stock Falcon, down from 6.53. On your DFB1 that should be about 2.5 s with the
caches off, down from 3.40, and about 1.8 s with them on. The whole page with EWeb's CSS engine would
then take about 4.8 s with the caches off and about 3.5 s with them on.

**Standards:** Hubbub's tree-construction tests are unchanged (1,139 of 1,679, here and as 68030
code). `tests/input` feeds libparserutils documents in random chunks, valid UTF-8 and not, and
checks the output against a plain version of the WHATWG decoder. It also checks every length around
the 64-character and 4,096-byte limits, here and as 68030 code (`make test`, `make hebtest68k`).
NetSurf's libparserutils fails 5,999 of its 10,420 cases. All 258 pages ran clean under
AddressSanitizer and UBSan.

**Next for the HTML parser:** the tokeniser appends an attribute's value to a buffer one character
at a time, a function call and a `memcpy` each. That is now the largest cost in the parse (17% on
`NEWS.HTM`). Pages in Windows-1252 and Windows-1255 could copy their ASCII the same way UTF-8 is
copied now.

## Fetching pages: EWeb's HTTP client (17 Oct 2026)

`src/net/` fetches a page straight from the Atari: the URL parser, the HTTP/1.1 client and gzip, on
`common/`'s connections and TLS (BearSSL, RSA on the DSP), through STinG or MiNTnet. NetSurf fetches
through libcurl and its own core (GPL-2.0-only), neither of which is in the port; the AERA family
already has an HTTP client on `common/` (ECal's and EContact's, for CalDAV and CardDAV), so EWeb's
grew from that one. When it has settled, it can move into `common/` for those apps too.

What it does:
- **Addresses** as the WHATWG URL Standard parses them: links resolved against the page, `.` and `..`,
  backslashes, IPv4 and IPv6 addresses, percent-encoding as UTF-8, and names that aren't ASCII
  (Hebrew ones too) in punycode, with the part of UTS #46's mapping pages meet.
- **HTTP/1.1:** answers by length, in chunks, or up to the close; gzip and deflate unpacked; redirects
  (up to 20, a POST answered with 301, 302 or 303 becoming a GET, as browsers do); the charset of the
  answer from Content-Type; four connections kept open (a page, its stylesheets and images come from
  two or three hosts), and a request that finds its kept connection closed goes again on a new one.
- **Limits** on everything a server says (the AERA guidelines, 7.1): a header line 16 KB, all of them
  64 KB and 200 lines, lengths and chunk sizes checked, a body 8 MB unpacked, a gzip bomb stopped at the
  limit.
- **How long each part took**, for `EWGET.TTP`, the tool that fetches a page and times it (in the next
  release, with `CACERT.PEM` and `ROOTS.DAT`).

**On an emulated Falcon** (Hatari, a 68030 at 32 MHz with TT-RAM and the DSP emulated, `EWGET.TTP`
through `FAKESTNG.PRG`, a stand-in for STinG over the serial port, against a server on the same
computer):

| | Time |
|---|---|
| The root certificates, from `ROOTS.DAT` (121 authorities) | 0.30 s, once |
| TLS 1.2 handshake: ECDHE-RSA, ChaCha20-Poly1305, the signature on the DSP | 1.75 s |
| The next request over the kept connection: no handshake | 0 s |
| Unpacking a 36 KB page (4 KB gzip) | 0.13 s |

The serial line runs at about 20 KB a second, so the times for the body aren't the network's. On your
DFB1, `EWGET.TTP` will say what a real connection costs. A new connection to a server seen before
resumes its TLS session, which leaves out the key exchange (tested on this computer).

**Standards and safety:**
- **URLs:** all 448 of the URL Standard's tests (the Web Platform Tests' `urltestdata.json`) whose
  answer is an http or https URL, or a failure for one, pass (`make urltest`). The 7 with a NUL in them
  are left out: a C string ends there, and HTML turns NUL into U+FFFD before an href gets here.
- **HTTP:** 102 cases in `make test` (`tests/net/nettest.c`), with `common/`'s `fuzz_net.c` playing the
  server in pieces of 1 to 97 bytes. They cover the lengths, chunks, gzip, redirects and kept
  connections, and the lengths, lines, chunks and bombs a hostile server might send.
- **Over TCP and TLS:** `make itest`, against servers on this computer with a test authority. It checks
  pages plain and packed, a kept connection, a resumed session, redirects from HTTP to HTTPS, and that
  a certificate for another name, or one nobody signed, is refused. `make hatarinet` does it on the
  emulated Falcon, and with the release's certificates.
- **Fuzzing:** `make fuzz` (libFuzzer, AddressSanitizer, UBSan) on the HTTP client and the URL parser.
  It found one fault within a minute, now fixed and kept in `tests/fuzz/found`: a long name that isn't
  ASCII could write past the buffer its punycode went into. After that, ten minutes of each found
  nothing (5.6 million server answers, 27.9 million addresses).

`common/` came up to date from AERA-Atari for this (`tools/sync-common.sh`): EWeb's copy predated
`tos_ms()`, the millisecond timer that stays right where timer C runs fast.

## The TLS handshake: elliptic curves on the DSP (18 Oct 2026)

**On your DFB1** `EWGET.TTP` 0.5 fetched `https://en.wikipedia.org` (a redirect to the Main Page, 50 KB
gzip, 254 KB unpacked) in 99.4 s the first time and 2.5 s the second, over the kept connection (body
65 KB/s, unpacking 1.2 s). The handshake took 85.7 s of the first. Wikipedia turned down both hellos
that asked for an RSA certificate, and the third, which took its ECDSA one, left BearSSL to do the
elliptic curves on the 68030. Counted on a 68030 (`tools/count68k.sh`, BearSSL as `EWGET.TTP` builds
it):

| BearSSL on the 68030 (`br_ec_all_m31`) | Instructions |
|---|---|
| X25519 key exchange (both halves) | 8.2 M |
| ECDSA P-256 signature check | 21 M, of which 2.2 M are 1/s modulo the order |
| ECDSA P-384 signature check (BearSSL's P-384 code is generic) | 119 M, of which 6.2 M are 1/s |
| RSA-2048 signature check (the DSP does it, 0.1 s) | 7.4 M |

The P-384 check, of the authority's signature on Wikipedia's certificate, was most of the 86 s. Asking
for RSA isn't a way round it: more and more sites have only ECDSA certificates (Let's Encrypt's default
since 2022).

**So the DSP does the curves now** (AERA-Atari's `common/`, opt-in, which EWeb takes):
`dsp/ec.a56` keeps 128 numbers modulo the curve's prime on the DSP and multiplies (Montgomery, a column
of 23-bit digit products a time, one product an instruction), adds and subtracts them, a 24-bit command
word each; `src/ecdsp.c` is BearSSL's `br_ec_impl` with the formulas as streams of those commands
(P-256, P-384, P-521 in Jacobian coordinates with 4-bit windows; Curve25519 by RFC 7748's ladder), and
checks a whole ECDSA signature there, 1/s modulo the order included. A curve it doesn't know, or a DSP
another program holds, goes back to BearSSL. Counted on Hatari's DSP core (the DSP's own time, 32 MHz):

| On the DSP | Time |
|---|---|
| X25519 (each half), and the self-test the first time | 0.23 s |
| ECDSA P-256 check | 0.40 s |
| ECDSA P-384 check | 0.78 s |
| ECDSA P-521 check | 1.7 s |

A handshake like Wikipedia's (X25519, a P-256 signature, a P-384 one, an RSA one) should take about 2 s
instead of 86. With the curves this cheap, the first hello offers ECDSA as well as RSA, so a server
with only ECDSA isn't connected to three times. In the Hatari rig (`make hatarinet`, a P-256 key signed
with P-384) the handshake takes 1.1 s, but Hatari runs the DSP more than twice as fast as a real one
when the 68030 is set to 32 MHz, so that figure is too good. The product is mostly overhead around the
columns (2,240 cycles for P-256, of which 620 are the products themselves): there is room to make it
faster if a measurement on the DFB1 says it matters.

**The report** hid about 10 s of that first fetch: with the redirect served over the kept connection,
`connect` wasn't shown, and the two refused hellos were counted as connecting. `connect` is now shown
whenever a connection was made, and the TLS line counts the refused tries and says how many there were
(`(3 tries)`).

Checked: `common/tests/ec` (902 checks against BearSSL on a C model of `ec.a56`, the same through
`ec.a56` itself on Hatari's DSP core), `make itest` with an ECDSA-only server (the third try, on this
computer, which has no DSP), `make hatarinet` with it (the curves on the emulated DSP, ECDSA at the
first try).

**On your DFB1** `EWGET.TTP` 0.6 fetched `https://www.wikipedia.org` (29 KB gzip) in 21.1 s, the
handshake 12.8 s of it instead of 85.7, ECDSA at the first try, with the 68030's caches off:

| On the DSP, on the DFB1 | Time |
|---|---|
| ECDSA P-384 check (three of them in Wikipedia's chain) | 0.94 s, 0.96 s |
| ECDSA P-256 check | 0.44 s |
| X25519 (each half) | 0.24 s |

That is a fifth more than the counts above for P-384, a tenth for P-256; the first P-384 check took 1.7 s because its time held the
self-test and the line about it. The DSP's 4.5 s were a third of the handshake. Its log (`-l`) took
about 3.5 s more: each line of a handshake was written out at once, opening and closing the file, about
half a second a line on that disk (the two X25519 halves, 240 ms each, are 760 ms apart). The rest,
about 4.8 s, mostly before the first signature check, the log didn't explain.

So in 0.7 the handshake's lines stay in the log's buffer until it ends (AERA-Atari's `src/conn.c`),
the self-test's time is its own, and `src/tls.c` logs the handshake's steps: the random seed (read and
written in `SEED.DAT`), the server's first answer after the hello, the server's certificates (how many,
their bytes, and the time spent decoding and checking them against the time they took to arrive), and
how much of the handshake was waiting for the network. `EWGET.TTP` finds `EWGET.CMD` whatever the
desktop passes when nothing is typed (a space, or ARGV), in its own folder when it knows it, takes a
`.CMD` file dropped on it, and says where it looked when there is none: the desktop's box takes only a
few dozen characters.

**On your DFB1 with 0.7** and the caches on, the same fetch took 12.8 s (the handshake 7.6 s; with
the log, 14.4 s and 8.5 s). The download went from 29 to 163 KB/s and unpacking from 930 to 675 ms with
the caches. The handshake's steps, from the log:

| Step | Time |
|---|---|
| The random seed: `SEED.DAT` read, and saved the safe way (a new file, the old one deleted, a rename) | 2.9 s |
| The server's first answer after the hello | 0.1 s |
| The server's 4 certificates (4,115 bytes): three P-384 checks on the DSP (2.8 s), its self-test (0.25 s), decoding and hashing them on the 68030 (about 0.9 s) | 4.3 s |
| The P-256 signature, X25519 both ways, the end of the handshake | 1.2 s |

Waiting for the network was 0.26 s of it all. Counted on a 68030 (`tools/count68k.sh`, a chain shaped
like Wikipedia's, four certificates and three ECDSA signatures, 2.5 KB): BearSSL takes 5.8 million
instructions for it, the signature arithmetic aside, of which 3.8 million are spent finding each
certificate's issuer among the 121 root certificates: it hashes every root's name again for each
certificate.

So in 0.8 (AERA-Atari's `src/tls.c`):

- **`SEED.DAT`** is written once a session, over the old one in place (`PF_UPDATE`, an app's
  `plat.h` opts in): no new file, no rename, no search of the FAT. Later handshakes in a session go on
  from a seed kept in memory.
- **The root certificates' names** are hashed once: the X.509 engine's name hash is ours to choose,
  and `tls.c`'s remembers a root's. The chain above takes 3.5 million instructions the first time in a
  session and 2.2 million after.
- **Signatures already checked are remembered** (`SIGS.DAT`, 64 of them, about 2 KB): a SHA-256 of
  the curve, the key, the hash of the signed data and the signature, kept only after the check
  succeeded. A certificate's signature is the same bytes every time it is sent, so an authority's
  signature on its intermediate, or on a site's certificate until it is renewed, is checked once:
  after the first visit Wikipedia's three P-384 checks (2.8 s) go. BearSSL still decides everything
  else: names, dates, uses, and the chain up to a root certificate, so a remembered signature from an
  authority that isn't trusted changes nothing (`make itest` checks it). The server's signature on its
  key exchange is new every time and isn't looked up. ECDSA only: RSA takes 0.1 s on the DSP.

**On your DFB1 with 0.8** (the caches on, the log on): the first visit's handshake took 8.0 s, of
which about 1.9 s was making `SIGS.DAT` (once); the seed took 0.55 s instead of 2.9, and decoding the
certificates 0.73 to 0.77 s instead of 0.92. The second visit knew all three P-384 signatures: the
handshake took 3.15 s (8.5 s in 0.7), and the whole fetch 9.6 s:

| Second visit, 0.8 | Time |
|---|---|
| Finding the host and connecting | 1.1 s |
| Reading the root certificates | 1.5 s |
| The handshake: the DSP 1.2 s (self-test 0.25, P-256 0.44, X25519 0.48), the certificates 0.73 s, the seed 0.55 s, the network 0.21 s | 3.15 s |
| The log written out after the handshake (`-l` only) | 1.0 s |
| From the request to the answer's first line | 1.0 s |
| The page (29 KB) and unpacking it | 2.0 s |

So in 0.9: **`ROOTS.DAT` is told by `CACERT.PEM`'s date.** Most of the 1.5 s was reading all 186 KB of
`CACERT.PEM` for its checksum, every run, to be sure `ROOTS.DAT` was made from it. `ROOTS.DAT` (now
layout 2) also keeps the file's date: a release's has none, so the first run checks the checksum as
before and then writes the date in, in place; later runs compare the size and the date only (an app
opts in with `tls_file_stamp`). On Hatari's Falcon: 285 ms, then 25. **The answer's second** gets
two lines in the log: when its first bytes came and when they were decrypted, after the request; the
server answered the hello in 0.1 s, so the 1.0 s wait is the server's own time or ours, and the next
log will say which.

**On your DFB1 with 0.9:** three runs of 7.2 to 8.7 s (9.6 s with 0.8): the root certificates
80 to 550 ms instead of 1.5 s. The second of wait for the answer isn't the server's: its first bytes
came 100 to 110 ms after the request, but the first 15 KB record took 0.9 to 1.55 s more to arrive,
while the next 15 KB took 0.17 s; how STinG takes data in at a transfer's start is the next thing to
look at.

**Crashes after EWGET ends** (18 Oct 2026): with ECal, EContact and EHypView loaded, the machine hung
after EWGET's last key, with 2 or 3 bombs or none, though not every time; and one run's connection
log had 450 bytes of binary where five lines should be. The accessories install no vectors and can't
run while EWGET does (the hub read their code). The disk is an SD card on SCSI with HDDRIVER 13.2:
DMA, which reaches ST-RAM only, while EWGET and its buffers are in TT-RAM. HDDRIVER is meant to
handle that, but a sector moved to or from the wrong memory would explain both the junk and a
corrupted ST-RAM (where TOS, the desktop and the accessories live). So from 0.10 EWGET moves every
read or write of a sector or more from TT-RAM through 8 KB of its own in ST-RAM (`atari/plat_tos.c`),
says so on its "files" line with the `_FRB` cookie, and has the family's crash recorder
(`common/atari/crash.c`): a crash in EWGET is written to `CRASH.LOG`; one in the system after it
ended is found in TOS's record at the next start.

**Faster arithmetic on the DSP** is the next step for the handshake: after the above a repeat visit
leaves the DSP the key exchange's signature (0.44 s for P-256) and X25519 (0.48 s both ways), and a
first visit the certificates' checks too. The product (`dsp/ec.a56`) is mostly the overhead around
its columns.

## Showing pages: fonts, layout, drawing (18 Oct 2026)

Your decisions (18 Oct 2026): **fresh layout code** (NetSurf's core is GPL-2.0-only and reorders no
text), **EWeb's own bitmap fonts** drawn from DejaVu, the same on every Falcon with or without GDOS,
and **a text browser first**: text, links, lists and tables, with pictures and forms in later
releases.

- **Fonts** (`tools/mkfont.py`, `src/gfx/font.c`): `EWEB.FNT` holds DejaVu Sans, Serif and Sans
  Mono, each regular, bold, italic and bold italic, at 9 to 32 pixels (12 sizes): 144 faces of 395
  characters (Latin-1, Latin Extended-A, Hebrew, punctuation and signs), 1.8 MB unpacked; each face
  is DEFLATE'd in the file, 705 KB in all (Erez's decision, 19 Oct 2026: packed, every size kept). FreeType draws them
  without smoothing, with its hinted advances, so the spacing matches the pixels; letters a face
  lacks (DejaVu Serif has no Hebrew) are Sans's. EWeb opens the file once at start and keeps it
  open: its directory is read then, and each face the first time a page uses it, unpacked by the
  HTTP client's inflate (the start page's five faces: its layout 40 ms -> 365 ms on the emulated
  32 MHz Falcon, the first time only). Hebrew vowel points take no room and are not
  drawn yet.
- **The document** (`src/doc`): Hubbub's tree as EWeb's nodes, each carrying the CSS engine's node,
  with a UA stylesheet from the HTML Standard's rendering section. HTML's attributes that stand for
  CSS (`bgcolor`, `width`, `align`, `border`, `cellpadding`, `<font>`...) become presentational
  hints, a new part of EWeb's CSS engine: author rules of specificity zero before every sheet, as
  the standard has them.
- **The layout** (`src/layout`, fresh): blocks with collapsing margins, padding, borders and
  backgrounds, widths with `min`/`max` and `box-sizing`, `margin: auto` centring; lines broken at
  spaces (a word too long for its line is split), `text-align` (justify too), `line-height`,
  `white-space`, `text-transform`, `text-decoration`, sub and superscripts; lists with every common
  marker and `start`, `reversed` and `value`; `::before` and `::after` text; tables with shared column
  widths, `colspan`, `border-spacing`, shrinking to fit, and flex rows side by side; Hebrew words
  reordered right to left within each line (a word-level form of the bidirectional algorithm), with
  right-to-left paragraphs aligned right. Pictures show their `alt` text; form controls are drawn as
  boxes. Not yet: floats, positioning, `rowspan`, vertical alignment in cells, pictures, forms.
- **The display list and the canvas** (`src/gfx`): the layout gives fills and text runs in device
  pixels (a CSS pixel is 0.8 of the Falcon's at the start); the canvas draws them into memory, a
  byte a pixel for the planar modes or 16-bit for true colour, which the GEM side copies to the
  screen through ST-RAM (the screen can't be fed from TT-RAM).
- **Browsing** (`src/browse`): what an address typed means (a URL, a file, or words to search for),
  the page and its stylesheets over HTTP and HTTPS or from the disk, a memory cache (4 MB) for Back
  and for a site's stylesheets, 50 pages of history with where each was scrolled, a page's anchors,
  and EWeb's own pages (the start page; "can't open", with the reason).
- **The window** (`EWEB.PRG`, `ui/`, after EFTP's): a toolbar (Back, Forward, Reload or Stop, Home,
  the address, typed into in place), the page under it with the window's slider, the status on the
  info line (a link's address under the mouse, what a load is doing, how a secure page came), the
  family's menu bar, About box, shortcuts, right-click menus, VA_START and drag and drop, help,
  iconify and crash report. The page is drawn into memory a band of 32 rows at a time and copied
  to the screen from ST-RAM: in the planar modes each colour is the screen's nearest (EWeb sets no
  pens), the bytes made planes and `vr_trnfm`'d; on a monochrome screen fills are greys dithered
  from black and white; in true colour the canvas is the screen's own pixels. Scrolling moves what
  is on the screen and draws only the new rows. While a page loads, the network's waits keep the
  window alive, and Esc, Undo or Stop stop the load, the page before staying.
- **First timings,** on the emulated Falcon (Hatari 2.4.1, EmuTOS 1.4, a 68030 at 32 MHz with
  TT-RAM, 640x480 in 16 colours; `EWEB.PRG` logs each page's phases), saved pages from the disk
  without their stylesheets:

  | Page | Size | Total | Parse | Style | Layout | Drawn |
  |---|---|---|---|---|---|---|
  | BBC News (4,965 px tall, 121 links) | 100 KB | 4.35 s | 2.68 s | 0.16 s | 0.83 s | 0.48 s |
  | WSJ (10,000 px, 260 links) | 186 KB | 9.49 s | 7.08 s | 0.31 s | 1.50 s | 0.48 s |

  The parse is Hubbub's, as measured before (4.15 s on the same BBC page before its UTF-8 copy): its
  tokeniser is the next thing to make faster. "Drawn" is the first screen in 16 colours.
- **Checked:** every page of the page set (258) laid out and drawn with AddressSanitizer and UBSan,
  with no fault; `tests/layout` keeps the display lists of three pages (`make test`), and the Hebrew
  test page lays out the same in each encoding; `make itest` loads a page and its stylesheet over
  HTTP and HTTPS from the test server, follows a redirect and an anchor, and shows EWeb's own page for
  a server that isn't there and for a file that isn't a page. `tests/host/ewrender.c` draws a page or
  URL into a PNG as the Falcon would. `make hatarigui` runs `EWEB.PRG` on emulated Falcons and TTs:
  a site over the network through FAKESTNG, a link clicked with the mouse, Undo, an address typed
  with an anchor, a secure page (the TLS handshake on the emulated 68030 and DSP), Esc stopping a
  download, and a page in 256 colours, true colour, TT medium and TT monochrome.
- **The disk** (the guidelines' 6.5, Erez's rule of 18 Oct 2026: every open costs real time on his
  disks). Measured with Hatari's GEMDOS trace (`make hatarifiles`, `tests/hatari/disk_files.py`),
  opens (`Fopen` and `Fcreate`) and lookups (`Fsfirst`) per case:

  | Case | 0.11 | Now |
  |---|---|---|
  | Start | 5 opens, 1 lookup (`EWEB.INF`; `EWEB.FNT` 4 times) | 2, 0 (`EWEB.INF`, `EWEB.FNT`) |
  | A page with a face not used before | 1 open (`EWEB.FNT`) | 0 |
  | 10 pages, a page with 10 pictures, back 4 and forward 4, 5 minutes idle | 0 | 0 |
  | The page size changed (^+ ^- ^0) | 0 (saved at quit) | a safe save each time |
  | Quit, nothing changed | 2 opens, 1 lookup, a safe save | 0 |
  | First secure page of a session | 3 opens, 5 lookups | 3, 1 (`ROOTS.DAT`; `SEED.DAT` read, then written over) |
  | The same, the first session after installing | 5 opens, 7 lookups, 61 reads | 5, 1, 31 |
  | `EWGET.TTP`, 1 MB | 4 opens, 2 lookups, the file made once | 4, 1 (`CRASH.LOG`: a report to show?) |

  Pages, stylesheets, history and Back go through memory (the cache), never the disk. `EWEB.FNT`
  stays open while EWeb runs, a face read with one seek. A file read whole is opened and its size
  taken with a seek (`pf_size_open`), not looked up first. `EWEB.INF` is read once and kept: a setting
  changed by the user (the page size, the home page) is saved then, the window's place at quit if it
  moved, and nothing is written when nothing changed. The secure connections' files (`CACERT.PEM`,
  `ROOTS.DAT`, `SEED.DAT`) are `common/`'s (`tls_file_info`: `CACERT.PEM` looked up once; the seed
  read, then written over in place, with no look at the folder). Reads and writes go
  through 8 KB in ST-RAM (`atari/plat_tos.c`) until it is known whether the Falcon's crashes come
  from DMA to and from TT-RAM; then 32 KB pieces.

## The design

### Every Falcon, fastest on an accelerated one

- **Two builds from one source,** as the family has ST editions: one uses the FPU, for Falcons that
  have one; the other doesn't, and runs JavaScript's maths in software, several times slower. Nothing
  else in EWeb needs floating point.
- **At start,** EWeb reads the system cookies (`_CPU`, `_FPU`, `_MCH`), asks TOS for TT-RAM, and times
  a short loop to see how fast the CPU is. It checks that the 68030's caches are on and says so if they
  aren't: your DFB1 ran with them off at first, which made it a quarter slower.
- **With TT-RAM,** the program, the pages, the caches and decoded images live there. Only the screen
  stays in ST-RAM.
- **Defaults follow the speed:** how large images are decoded, how big the caches are, and whether
  JavaScript is offered. On a stock 16 MHz Falcon images are decoded smaller and JavaScript is off.
- **The DSP is in every Falcon,** so the work it takes on helps all of them.

### NetSurf's code or fresh code

NetSurf's version of each part is ported first and measured. Fresh code is written only where the
measurements show a cost it can remove: by using the 68030's and the DSP's strengths and TT-RAM, or by
leaving out what EWeb never needs.

1. **Port** NetSurf's part to plain TOS on `common/`, adding the C library calls it needs.
2. **Measure** it on a fixed set of saved pages (a long Wikipedia article, the Hacker News front page,
   an Atari-Forum thread, a lite news page): instructions on the emulated 68030, as in the
   measurements above, then a timed run on your Falcon.
3. **Check compliance** with the standard tests: html5lib-tests for the HTML parser, a chosen part of
   the Web Platform Tests for CSS and layout.
4. **Replace** NetSurf's part with fresh code only when the fresh code is faster on the page set and at
   least as compliant. When the two are close, NetSurf's stays: it is already written and tested.

| Part | NetSurf's version | Licence |
|---|---|---|
| HTML5 parser | Hubbub, with libparserutils | MIT |
| CSS engine | LibCSS, with libwapcaplet | MIT |
| DOM | libdom | MIT |
| GIF and BMP decoders | libnsgif, libnsbmp | MIT |
| Layout and page handling | NetSurf's own core | GPL-2.0-only |

The MIT parts fit EWeb's GPL-2.0-or-later licence. NetSurf's core is GPL version 2 only: if any of it
is used, EWeb as a whole program becomes GPL-2.0-only, while EWeb's own files keep "or later". NetSurf
also has an Atari front end, under the same terms; EWeb's front end is ours, built on `common/` to the
AERA guidelines.

### 1. Direct: EWeb's own engine on the Falcon

- **Networking:** HTTP/1.1 and TLS 1.2 through `common/` (BearSSL, the connection code, DNS), with
  gzip, cookies, redirects and a disk cache.
- **HTML and CSS:** an HTML5 parser and a CSS engine, NetSurf's or fresh by the rule below, with
  selectors, media queries, CSS variables and flexbox.
- **A narrow screen:** EWeb tells sites its screen is about 640 px wide, so responsive sites send their
  simpler mobile layout, which also fits the Falcon's screen.
- **Images:** GIF, PNG and JPEG. JPEGs decode at 1/2, 1/4 or 1/8 size, which is much faster. EWeb asks
  servers for JPEG and PNG only, so image servers don't send AVIF or WebP.
- **First-party content only by default:** no third-party scripts, ads, trackers or web fonts. This also
  saves a slow TLS handshake for each extra domain.
- **The rest:** forms, bookmarks, history, Hebrew (below), and a GEM interface that follows the AERA
  guidelines.
- **Sites that should work well:** Wikipedia, Hacker News, Atari-Forum and other phpBB forums, blogs,
  documentation, text-only news (lite.cnn.com, text.npr.org), DuckDuckGo's HTML version, FrogFind and
  GitHub READMEs.

What to expect: pages load in seconds to tens of seconds, and scrolling moves in steps. The screen sits
in ST-RAM on the original 16 MHz bus, so redrawing it is slow even with the accelerator.

### Hebrew

Your decision: EWeb reads and shows Hebrew. The HTML parser stays NetSurf's: it doesn't care what
language the text is in, and `dir`, `lang`, `<bdi>` and `<bdo>` reach the tree as they are. Hebrew
lives in the other parts:

- **Decoding** (NetSurf's libparserutils): UTF-8, Windows-1255 and ISO-8859-8, with no iconv.
  ISO-8859-8-I, the logical-order label some older Israeli sites use, was read as Windows-1252, its
  Hebrew turned into Latin letters; one line fixes it (`third_party/netsurf/patches`), with the web's labels "logical" and "visual". `tests/hebrew` checks the same page in all
  six labels, on this computer and as 68030 code (`make test`, `make hebtest68k`).
- **CSS** (LibCSS): `direction` and `unicode-bidi` are there. EWeb's UA sheet adds the HTML rules for
  `dir="rtl"`, `dir="ltr"`, `<bdi>` and `<bdo>`; `dir="auto"` is layout's, from the first strong
  letter.
- **Layout and drawing** (fresh code): today Hebrew words are put right to left within each line
  after it is broken, across links and bold, and right-to-left blocks align right and put list
  markers on the right (above). Still to come: the full Unicode bidirectional algorithm (UAX #9),
  with numbers kept left to right, brackets mirrored and the levels `dir`, `<bdi>` and `<bdo>` set.
  `common/bidi.c` (from ClaudeST) handles one line of Atari-charset text, so EWeb needs the fuller
  algorithm, which can move to `common/` for every app.
- **Visual Hebrew:** a page labelled plain ISO-8859-8 (or "visual") is already in display order, so
  layout shows it as it is, without reordering. EWeb knows which kind it has from the page's charset.
- **Fonts:** EWeb's own fonts have the Hebrew letters (DejaVu's), so pages don't go through the Atari
  character set. Vowel points (niqqud) take no room and aren't drawn yet.
- **Typing:** Hebrew in forms comes with the keyboard handling.

### 2. A little JavaScript on the Falcon

- QuickJS (MIT licence), carried in the repo the way `common/` carries BearSSL. Its code is about
  800 KB for the 68030 at `-O2`.
- Off by default; switched on per site.
- A lean DOM: selectors, events, classes, `innerHTML`, timers and `fetch`.
- A hard budget of roughly 100–200 KB of script and a few seconds per page. Beyond that the page falls
  back to its `<noscript>` version.
- Enough for menus, tabs, form checks and small widgets. Not enough for frameworks.

### 3. Gateway: AERA-Gateway's `web` service

A new service, `aera-gateway install web`, replacing the WRP and Macproxy containers you set up by hand.
Like the other gateways it takes plain connections from the Atari and goes out over HTTPS, and it keeps
`aeragate.py`'s rules: home network only, Atari only.

- **A real browser on the Pi.** An ordinary Chromium runs on a virtual screen on the Pi. It isn't
  headless, isn't driven by an automation protocol, keeps its own saved profile, and goes out over your
  home internet connection.
- **Snapshots.** A small extension inside that browser reads each finished page, after its JavaScript
  has run, and the gateway sends EWeb a simplified copy: HTML, the CSS that EWeb understands, and images
  resized for the Falcon.
- **Your input.** Clicks and typing on the Falcon go back to the Pi and reach the browser as ordinary
  mouse and keyboard input.
- **Pictures where a snapshot can't work.** Maps, charts and canvas are sent as pictures, as WRP does
  today.
- **What the gateway also covers:** TLS 1.3-only servers, AVIF and WebP images, brotli compression and
  web fonts.
- **Hardware:** plan on a Pi 4 or 5 with 4 GB of RAM or more.
- **Your sessions:** the browser profile holds your logins, so it stays on the Pi and the service stays
  reachable from the Atari only.

## CAPTCHAs

CAPTCHAs are the biggest problem with CAB and the gateway today, so EWeb has to handle them.

### Why they fail with CAB today

WRP runs `chromedp/headless-shell` in `-m html` mode, which causes two problems:

1. **There is no live widget.** In HTML mode, CAB gets a simplified copy of the page with the scripts
   removed. A CAPTCHA is a live script inside a frame from Google, hCaptcha or Cloudflare. The copy has
   no live widget, so nothing you click ever reaches it. Macproxy never runs scripts at all.
2. **The browser looks like a bot.** Underneath is a headless Chromium driven by an automation protocol,
   which is exactly what these checks are built to stop. Even in WRP's picture mode, they tend to block
   it or keep asking.

### Why the Falcon can't run them itself

The checks are large scripts, deliberately obfuscated, that probe canvas, WebGL, timing and other
features a Falcon engine won't have. reCAPTCHA's script alone is several hundred KB; at the measured
rate, just compiling it would take roughly half a minute to a minute, and a reCAPTCHA answer expires
two minutes after it is issued. So a real modern browser has to run the check, and you have to answer
it.

### How EWeb gets past them

The Falcon becomes the screen, mouse and keyboard of the gateway's real browser for the moment of the
check. A real browser with a person at it is exactly what a CAPTCHA is designed to let through, so this
doesn't rely on tricking anything.

1. **Spotting a check.** The gateway recognises Cloudflare's check pages, which carry a
   `cf-mitigated: challenge` header, and frames from reCAPTCHA, hCaptcha, Turnstile, Arkose and AWS WAF.
   For anything it misses, a key in EWeb opens live view by hand.
2. **Live view.** EWeb opens a window showing that part of the Pi's screen as a picture. Only the parts
   that change are sent. In 256-colour modes they are dithered to a fixed palette, so the desktop's
   colours stay as they are. Your mouse movement, clicks, drags, press-and-holds and keys go back as they
   happen, as with a remote desktop. A zoom key enlarges one image tile.
3. **Audio option.** When an image puzzle is hard to read in 256 colours, the audio challenge plays
   through the Falcon's DMA sound and you type what you hear. A 10-second clip at 8-bit, 12.5 kHz is
   about 125 KB.
4. **Afterwards.** The page goes back to normal snapshot view, and the site stays on the gateway's
   browser for the rest of the visit, because the browser that passed the check has to be the one that
   carries on. The saved profile keeps the pass, so the next visit usually doesn't ask again.

| Check | What you do on the Falcon |
|---|---|
| Cloudflare "Just a moment…" or Turnstile | Usually nothing; sometimes one click |
| reCAPTCHA or hCaptcha tick box | One click |
| Image grids | Click tiles in live view, or use the audio option |
| Press-and-hold, sliders, puzzle pieces | Hold or drag with the Falcon mouse |
| Invisible checks (reCAPTCHA v3) | Nothing; your real mouse movement is passed on |

**In direct mode,** when EWeb hits a check it says so and offers to open that site through the gateway
with one key. Without a gateway, sites behind a CAPTCHA can't work, because no browser on a 68030 can
pass them.

**CAB gets it too.** For CAB, live view is a clickable picture page, the kind WRP's picture mode makes
for old browsers. It's slower, but it gets you past CAPTCHAs in CAB long before EWeb is ready.

### What we won't do

- No CAPTCHA-solving services and no automatic solving.
- No disguising the browser.
- No moving a pass from the Pi's browser to the Falcon's own connection.

These break site terms, sites actively fight them, and this design doesn't need them.

### Limits

- **Some sites will still refuse** or ask more than once, especially banks and ticket sellers with
  strict scoring.
- **Speed:** live view has to beat the two-minute limit. At the NetUSBee's 300 KB/s or so, a whole
  reCAPTCHA picture (about 230 KB before compression) arrives in under a second, and later updates
  send only the tiles that change. Expect a second or two per update, decoding included; to be
  measured.
- **Google sign-in (optional):** signing into Google in the gateway's browser usually means fewer
  reCAPTCHAs, but that profile then holds your Google login.

## What works and what doesn't

| Kind of site or feature | Direct, on the Falcon | Through the gateway |
|---|---|---|
| Server-rendered sites (Wikipedia, forums, blogs, docs) | Yes | Yes |
| News sites | Lite and text editions; main sites only partly | Readable |
| Search | DuckDuckGo HTML, Mojeek, FrogFind | Google and Bing results readable |
| Light JavaScript (menus, form checks) | Yes, within the budget | Yes |
| Sites behind a CAPTCHA | No; EWeb offers the gateway | Yes, you answer it in live view |
| Web apps: Gmail, X, Facebook, Instagram, Discord, Google Docs and Maps | No | Reading and simple actions; slow and fragile |
| Video and audio streaming (YouTube, Netflix, Spotify), DRM | No | No; this would be a separate transcoding project |
| WebGL/WebGPU, WebAssembly, canvas games, video calls (WebRTC) | No | A still picture at best |

**Out of scope:**
- Running modern JavaScript frameworks or web apps on the Falcon itself.
- Media streaming, DRM, WebRTC, Web Audio, WebGL/WebGPU, WebAssembly, service workers, offline web apps
  and push notifications.
- Passkeys.
- Pixel-perfect modern CSS: Grid (maybe later), animations, transitions, transforms, filters, web and
  icon fonts, colour emoji, and scripts that need glyph shaping such as Arabic. Hebrew is fine.
- TLS 1.3 on the Falcon (BearSSL stops at 1.2; the gateway covers it), and smooth scrolling.

## Other Atari browsers

**HighWire** (github.com/neilrackett/atarist-highwire), revived by Neil Rackett in August 2026
(0.4.8, 1 Oct 2026), is the browser Atari users have today: a complete GEM program with layout,
tables, forms, frames, images, bookmarks, a cache and cookies, for every Atari from a 68000 ST to a
FireBee, without GDOS. Read from its source (17 Oct 2026; not yet measured):

- **HTML:** its own one-pass tag scanner that turns tags straight into words and boxes, with none of
  HTML5's parsing rules.
- **CSS:** about 57 properties at CSS 1 and 2's level, no flexbox; selectors of a type, one class or
  id and descendants (attribute selectors and most pseudo-classes drop the rule); `@media` for
  `screen` and `all` only; no `var()`, `calc()`, `@supports` or `@layer`. Every tag walks the whole
  list of rules (for the tag, each class and the id) comparing names as strings, so its cost grows
  with rules times elements: light on old pages, heavy on a framework's sheet such as Bootstrap's.
- **No Hebrew** (no Windows-1255 or ISO-8859-8, no bidirectional text), **no JavaScript**, and
  **https only through a proxy** that ends TLS for it.
- **Licence:** GPL-3.0-or-later since 0.4.6; code from before the change is also under its old
  zlib-style licence. EWeb is GPL-2.0-or-later, so it could take from the old code only.

Worth taking as ideas: dithering through a table of the screen's own colours, decoding a JPEG
straight at a half, quarter or eighth when it is shown that small, and laying out once per burst
of arriving images.

**Head to head on your DFB1** (17 Oct 2026, `NEWS.HTM` with `NEWS.CSS` from the disk, the caches
off for both): HighWire 0.4.8 logged **7.18 s** for its parse of the page, which reads the page,
loads and applies the part of `NEWS.CSS` it understands, and builds its boxes; laying them out
and drawing come after, and are not in that figure. EWeb took **5.7 s** for a full HTML5 parse
(3.40 s) and the whole stylesheet with every element's computed style (2.31 s). So EWeb is the
faster of the two already, while doing the whole job: HighWire leaves out what it doesn't know of
`NEWS.CSS` (its custom properties, flex layout, attribute selectors and most pseudo-classes, and
its media queries' conditions). The figures aren't quite the same work: HighWire's includes
reading `NEWS.CSS` from the disk and making boxes, EWeb's neither. The page's images were left
out of both (their paths lead nowhere on the Atari).

## Order of work

1. **The gateway's `web` service, in AERA-Gateway,** with live view for CAB. This gets you past
   CAPTCHAs first, before EWeb exists.
2. **EWeb's first milestone:** port Hubbub and LibCSS, fetch a page directly, parse its HTML and lay
   out the text on the real Falcon, and measure what HTML and CSS cost there. That makes the first
   NetSurf-or-fresh calls. *Done so far (16 Oct 2026):* the port, the measurements on emulated
   68030s and the first calls (above), and `NSBENCH.TTP` in release v0.1 for your Falcon (it runs on
   an emulated stock Falcon too: 16 MHz, no FPU, no TT-RAM), timed on your DFB1 with `SAMPLE.HTM`
   (above), and NSBENCH 0.2's machine report and `NEWS.HTM` on your DFB1: 21.3 s with the caches on,
   nine tenths of it CSS; then EWeb's own CSS engine, about ten times faster, timed on your DFB1 in
   NSBENCH 0.3 (above), and Hubbub's UTF-8 input copied, 28% off the HTML parse; then EWeb's HTTP
   client and `EWGET.TTP` (above), tested on an emulated Falcon, and Wikipedia fetched on your DFB1
   (18 Oct 2026), whose 86-second handshake moved the elliptic curves to the DSP (above), timed again
   on your DFB1 (v0.7 to v0.9); then EWeb's fonts, the document, the layout, browsing and the GEM
   window (above): `EWEB.PRG`, the first text browser.
3. **The document web:** images, forms, floats and positioning, bookmarks, a faster HTML parse.
4. **Gateway mode in EWeb:** snapshots and live view, over EWeb's own protocol.
5. **JavaScript:** QuickJS with the lean DOM.

## Open questions

- **Licence, if NetSurf's core wins a part:** EWeb as a whole program would then be GPL-2.0-only
  (its own files keep "or later"). Decide when it comes up.

## Sources

- [DFB1 repository and wiki (dh219)](https://github.com/dh219/DFB/wiki)
- [Web Almanac 2024: page weight](https://almanac.httparchive.org/en/2024/page-weight)
- [NetSurf on Wikipedia (Duktape, licence)](https://en.wikipedia.org/wiki/NetSurf)
- [Debian QuickJS package, built for m68k](https://packages.debian.org/sid/quickjs)
- [NetUSBee (hardware.atari.org)](https://hardware.atari.org/netusbee/index.htm)
- [debian-68k: NetUSBee throughput, about 300 KB/s](https://lists.debian.org/debian-68k/2026/07/msg00018.html)
- [htmlparser-benchmark (the page set)](https://www.npmjs.com/package/htmlparser-benchmark)
- [HighWire (Neil Rackett's revival)](https://github.com/neilrackett/atarist-highwire)
- [Web Platform Tests: html/syntax/parsing](https://github.com/web-platform-tests/wpt/tree/master/html/syntax/parsing)
- [Cloudflare: detect a challenge page response](https://developers.cloudflare.com/cloudflare-challenges/challenge-types/challenge-pages/detect-response)
- [Google reCAPTCHA: verifying the user's response (token valid for two minutes)](https://developers.google.com/recaptcha/docs/verify/)
