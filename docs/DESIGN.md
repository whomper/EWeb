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
| Machines | **Every Falcon; the most from a fast one** | One program that checks at start what the Falcon has (accelerator, TT-RAM, FPU) and uses it, its JavaScript (which needs the FPU) in a file of its own (23 Oct 2026: one `EWEB.PRG` for every Falcon, the stock 4 MB one too). Tuned for your DFB1. |
| Code | **NetSurf's code where we can't do better; fresh code where we can** | Each part (HTML parser, CSS engine, DOM, layout, image decoders) uses NetSurf's version unless fresh code written for a fast 68030 is faster and at least as standards-compliant. How it's decided is below. The GEM front end and everything Atari-side are ours, on AERA-Atari's `common/`. |
| JavaScript | **QuickJS** | The best fit measured: modern syntax, small, portable C, MIT licence. |
| Hebrew | **Supported** | Hebrew pages in every Hebrew encoding, right to left, mixed with English. It makes layout fresh code: NetSurf's reorders no text (below). |
| Speed | **Better than every other Atari browser, and the fastest code** (19 Oct 2026) | EWeb has to outdo the other Atari browsers (below) on what it shows and on how fast it shows it, and each part's code has to be the fastest that can be written for the machine. A part is measured in 68030 instructions on the page set and on the emulated Falcon, and timed on your DFB1; where something faster can be written, it is. NetSurf's code stays only where nothing faster beats it. |
| DSP and 68030 | **The DSP for any work it does faster; code tuned for the 68030** (19 Oct 2026) | Beyond the TLS handshake's sums, the DSP takes whatever it does faster than the 68030 (decoding pictures, for one). EWeb's code is tuned for the 68030 (its 32-bit multiply and divide, longs at any address, its caches). So the releases are two: `EWEB.PRG`, 68030 code, and an ST edition, `EWEBST.PRG`, 68000 code (the plan, below). |
| 256 colours | **EWeb's own palette, with a setting** (23 Oct 2026) | TOS's 256 colours have no pale tints, so pastel backgrounds, translucent colours and pictures come out grey or coarse. EWeb sets a palette of its own while it runs in 256 colours, and gives TOS's back when it quits, with a setting to keep TOS's (EWeb's own 256 colours, below). |
| ST memory | **1 MB is no longer a target** (23 Oct 2026) | EWeb's ST edition only (the family's rule for the others stays) no longer has to fit a 1 MB ST (the hub's rule of 20 Oct 2026, kept up to 0.27). Its buffers are the Falcon's again and its rigs dropped the 1 MB machine (The ST edition, below); it is tested on the 1040 ST with 4 MB, with the family's accessories loaded too, and no 2 MB machine is added for now. |
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
- **HTML parser: Hubbub stayed, then fresh code, decided (19 Oct 2026).** Hubbub was made faster
  first (its UTF-8 input copied, then runs of characters at once, below); then EWeb's own parser
  passed 1,673 of the tests to Hubbub's 1,139 and took 44% off the parse on the page set (EWeb's
  own HTML parser, below), so it replaces Hubbub in `EWEB.PRG`; Hubbub stays in nsbench and
  `make h5test` as the yardstick.
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

**Next for the HTML parser:** taken up on 19 Oct 2026 (below).

## The HTML parse: runs of characters at once (19 Oct 2026)

Profiled function by function on the 68030 (`tools/prof68k.py`, QEMU's hot-block plugin), most of
the parse was paid per character: in the data state, tag and attribute names and attribute values,
Hubbub's tokeniser took one character a turn (a call to the state, a peek at the input, a few tests
and an append to a buffer). Three changes to NetSurf's code (`third_party/netsurf/patches`):

- **Runs at once:** where a state would only collect a character, it collects the run up to the
  next character it treats specially (a table of those for each state), in one append. The input
  stream hands over what it has decoded (`parserutils_inputstream_avail`), whole characters only.
- **UTF-8 copied four bytes at a time**, each run of ASCII with one `memcpy`.
- **Single-byte charsets through a table:** a page in Windows-1252, ISO-8859-8, Windows-1255 and
  the like went through two codecs a character at a time, a third of its parse. Its bytes 0x80-0xFF
  are now looked up in a table of their UTF-8, made from the charset's codec, and its ASCII is
  copied as UTF-8's is.

The parse is now measured as `EWEB.PRG` does it: `build/m68k/docparse` makes EWeb's own document
(`doc_parse`) on the Atari's heap (`common/atari/libc.c`, through `tests/bench/atheap.c`), not on
glibc's. In 68030 instructions:

| | Before | After | Fewer |
|---|---|---|---|
| `NEWS.HTM` | 10.07 M | 6.41 M | 36% |
| The page set, median page | 11.50 M | 7.58 M | 34% |
| The page set, 90th percentile | 22.1 M | 13.2 M | 41% |
| The page set, all 258 pages | 3,493 M | 2,087 M | 40% |

From 140 instructions a byte of HTML to 90 at the median. Every page gains at least 25%; the 63
pages in a single-byte charset gain another 29% (median) from the table.

**Standards:** the documents are the same. The tree-construction tests are unchanged (1,139 of
1,679, here and as 68030 code), the 258 pages' display lists are byte for byte what they were
(`ewrender -d`), and `tests/input` now also feeds nine single-byte charsets in random chunks and
checks them against their codecs (`make test`, `make hebtest68k`).

**What is left** on `NEWS.HTM`: Hubbub's tokeniser and tree builder 36%, the heap 17% (Hubbub and
the document take blocks for tokens, nodes and attributes), EWeb's document 12%, `memcpy` 9%,
libparserutils 8%, interning names for the CSS engine 6%. Hubbub's tree reaches EWeb's document
through a callback for each node and attribute, and its tokens are copied twice on the way. By
the owner's rule (the fastest code; Decisions) a parser written for EWeb's document is next: the
HTML Standard's tokeniser and tree construction writing straight into EWeb's nodes, out of one
arena. It replaces Hubbub only if it is faster on the page set and passes at least the tests
Hubbub passes; `<template>` and the 2026 changes Hubbub lacks would count for it. (Done the same day:
EWeb's own HTML parser, below.)

## EWeb's own HTML parser (19 Oct 2026)

After the runs above, what was left of the parse was Hubbub's design: a callback for each node and
attribute, every token copied twice, and the heap taking a fifth of the time. `src/doc/html.c` is
the HTML Standard's parser (13.2, as of October 2026) written for EWeb's document and the 68030:

- **The page is decoded once**, before the tokenizer, into one buffer: UTF-8 checked as the WHATWG
  decoder does, four bytes of ASCII at a time; single-byte charsets through a table made from
  libparserutils' codecs; UTF-16. Newlines are normalised on the way, and the buffer ends in a 0
  that every scanning loop stops at, so a loop is a load and a test.
- **The tokenizer takes runs**: text up to the next character that matters in its state, a tag or
  attribute name, an attribute value. Text goes to the tree as pointers into the page.
- **The tree builder writes EWeb's nodes directly**, out of the document's memory: large blocks
  freed all at once (`doc_alloc`), no reference counts, no callbacks. An element's name is its atom
  for the CSS engine, looked up once a document for the 128 names the parser knows.
- **Everything the Standard does now**: `<template>` and its contents, processing instructions
  (2026), the customizable `<select>` (2025: options, `<hr>` and `<selectedcontent>`), `<search>`,
  foreign content, the fragment case (for the tests now, `innerHTML` later), the encoding sniffing
  algorithm with its prescan and a `<meta charset>` that changes the encoding.

**Standards** (`make h5test`, the Web Platform Tests' tree-construction tests of 15 Oct 2026):

| | Documents | Fragments |
|---|---|---|
| Hubbub | 1,139 of 1,679 (67.8%) | none (no fragment parsing) |
| EWeb's parser | 1,673 of 1,679 (99.6%) | 195 of 195 |

The six it fails need a script engine (`scripted_*.dat`: a script that moves nodes as the page is
parsed). The same as 68030 code. On the page set, each of the 258 pages comes out as the same tree
as html5lib's (1.1, Python), but for the comments after `</html>` its serialiser leaves out; two
pages' display lists change from Hubbub's, both for the better (ZDNet's comment thread, which
Hubbub dropped; a form field one pixel to the side). `make fuzz` has it (`fuzz_html`: the charset,
scripting and a fragment's context from the first byte, and a check that the tree holds
together); its first find, an empty DOCTYPE at the very start, is in `tests/fuzz/found/html`.

**Speed**, the parse into EWeb's document (`build/m68k/docparse`, on the Atari's heap) in 68030
instructions:

| | NetSurf's, as released | Hubbub made faster | EWeb's parser |
|---|---|---|---|
| `NEWS.HTM` | 10.07 M | 6.41 M | 3.67 M |
| The page set, median page | 11.50 M | 7.58 M | 3.94 M |
| The page set, 90th percentile | 22.1 M | 13.2 M | 7.57 M |
| The page set, all 258 pages | 3,493 M | 2,087 M | 1,163 M |

From 140 instructions a byte of HTML to 48 at the median: 44% off Hubbub made faster (19% on the
page that gains least, 63% on the most), and three times as fast as NetSurf's code as released. The document's own work after the parse (the
presentational hints, now looking attributes up by atom) is in these figures. `EWEB.PRG` is 189 KB
smaller without Hubbub (715 KB).

**On the emulated 32 MHz Falcon** (`make hatarigui`), `EWEB.PRG`'s log gives the test server's
long page (36 KB of words, one text node, where both parsers mostly scan text) 195 ms against
Hubbub's 205; a small page 90-105 ms with either. Most of that is not the HTML: what the log calls
the parse also covers the status line and EWeb's UA stylesheet, which was parsed again for every
page (about 320 K instructions). It is now parsed once a program run (below).
`EWEB.PRG` is built freestanding, where a `memcpy` is a call, so the parser's small copies are
the compiler's own moves.

**What is left** on `NEWS.HTM`: the tokenizer about a fifth, the decoder a tenth (both near what a
loop over the bytes costs), then copies into the document, interning names for the CSS engine, and
clearing new nodes, a few percent each. The first page also looks up a few charset names in
libparserutils' list (about 50 K instructions), once a program run.

## The UA stylesheet once a program run (19 Oct 2026)

EWeb's UA stylesheet (the HTML Standard's rendering rules) was parsed into each page's CSS
context, so every page paid for it again. Now the first page parses it once into a context of its
own (`ec_share`: every block parsed, its names marked shared), and each page's
context is made on it (`ec_create_on`). A page's own rules on a name the base already has go into
the page's own table beside the base's, so the base is never written after it is built and stays
the same however many pages are shown. Selecting styles takes the base's rules and the page's
together, in the cascade's order.

| | Before | Once a run |
|---|---|---|
| A small page after the first (68030 instructions, `docparse -s`) | 341 K | 45 K |
| The first page of a run (builds the base) | 341 K | 503 K |
| A small page on the emulated 32 MHz Falcon (`make hatarigui`, the log's parse) | 90-105 ms | 10-40 ms |
| The test server's long page, the same | 195 ms | 115 ms |
| The first page of a run, the same | 90-105 ms | 140 ms |

The Hatari figures are the same on EmuTOS 1.4 and on Erez's TOS 4.04 (`common/tools/machines.py`
takes it from `~/.aera/tos`). Checked: every page of the set gives the same display list alone and
after another page (after `NEWS.HTM` with its 83 KB stylesheet too), the kept layouts are
unchanged, and `make csscmp` finds the same differences as before.

## Fetching pages: EWeb's HTTP client (17 Oct 2026)

`src/net/` fetches a page straight from the Atari: the URL parser, the HTTP/1.1 client and gzip, on
`common/`'s connections and TLS (BearSSL, RSA on the DSP), through STinG or MiNTnet. NetSurf fetches
through libcurl and its own core (GPL-2.0-only), neither of which is in the port; the AERA family
already has an HTTP client on `common/` (ECal's and EContact's, for CalDAV and CardDAV), so EWeb's
grew from that one. When it has settled, it can move into `common/` for those apps too.

What it does:
- **Addresses** as the WHATWG URL Standard parses them: links resolved against the page, `.` and `..`,
  backslashes, IPv4 and IPv6 addresses, percent-encoding as UTF-8 (but a query in its page's own
  encoding: Encodings, below), and names that aren't ASCII
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

Since EWeb 0.40 (common's `tls.c` of TLS.OVL 1.1, 25 Oct 2026) the server's certificates are kept
while the handshake runs and checked after it, before any data, with the same checks and the same
words on failure: a TT without the DSP took 17 s on Gmail's chain inside the handshake, and Google
dropped it. The log says "kept, to be checked after the handshake", then the check's time. And
(TLS.OVL 1.2) the retry with every cipher follows only a real refusal, an alert before any
certificate or a close with no answer: a close after the server's certificates no longer brings a
second full handshake (Gmail on EMail's TT). Since 1.3 a line after the handshake says where its
work went: "the key exchange: x25519, N ms; the server's key: RSA 2048 bits, its signature N ms".

**Without a DSP** (a TT, or a Falcon whose DSP failed its self-test) RSA's check and BearSSL's i31
multiplication (the key exchange, the prime curves) run in 68030 assembly since 0.40, as in TLS.OVL
1.4: common's `rsa030.c` with `atari/mont030.S`, and `tlsovl/i31_montmul_030.c` with
`mont31_030.S` in place of BearSSL's `i31_montmul.c`, in `EWEB.PRG` and `EWGET.TTP` (the ST
edition has no TLS; since 26 Oct the family's `TLS.OVL` carries them all, below). On the emulated TT (32 MHz, `make hatarinet`'s "on a TT"), a handshake with
the test server's RSA-2048 certificate: 5.0 s with 0.39, now 2.3 s (the key exchange 1,045 ms, the
signature 250 ms "on the 68030", the chain 450 ms), and 2.0 s the next time (the chain's signature
remembered in `SIGS.DAT`).

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
ended is found in TOS's record at the next start. Erez's runs of 0.11 (19 Oct 2026): HDDRIVER sets
`_FRB` (it bounces TT-RAM itself), four runs without junk, and the bombs still came, after EWGET had
ended (a white screen, no `CRASH.LOG`): not the files' DMA. The family's rule since (the guidelines'
6.5): with `_FRB`, TT-RAM goes to and from the disk as it is, in 32 KB pieces; without, through
32 KB of ST-RAM.

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
  Mono, each regular, bold, italic and bold italic, at 9 to 32 pixels (12 sizes): 144 faces of 398
  characters (Latin-1, Latin Extended-A, Hebrew, punctuation and signs), 1.8 MB unpacked; each face
  is DEFLATE'd in the file, 705 KB in all (Erez's decision, 19 Oct 2026: packed, every size kept).
  Since 24 Oct 2026 (Erez's choice, A: not Armenian, Georgian or the rarer symbols) each face has
  four more blocks of characters, `EWFNT3`: more Latin (Latin Extended-B, Vietnamese, Welsh, the
  combining accents, which take no room and are drawn over the letter before), 398 characters;
  Greek, 135; Cyrillic, 120; the common symbols (arrows, mathematics, currencies, fractions,
  circled numbers, shapes, check marks, card suits), 270. A block is read and unpacked the first
  time a page shows one of its characters, its place in the file read then (one more seek and
  read), so a page without them costs what it did; a face's blocks average 13.4, 4.5, 4.1 and
  8.2 KB unpacked, given back with the face (`font_drop`). At start the other blocks' codes
  (4.6 KB) and each face's pointers to its blocks (2.9 KB) take 7.5 KB more. The file is 2.7 MB
  (the blocks 769, 317, 270 and 644 KB packed); the encodings these letters need (Windows-1251, ISO-8859-5, Windows-1253,
  ISO-8859-7) are in View > Encoding, and the default for Cyrillic's and Greek's top-level
  domains is Firefox's (`br_fallback`). FreeType draws them
  without smoothing, with its hinted advances, so the spacing matches the pixels; letters a face
  lacks (DejaVu Serif has no Hebrew) are Sans's. EWeb opens the file once at start and keeps it
  open: its directory is read then, and each face the first time a page uses it, unpacked by the
  HTTP client's inflate (the first page's faces: the old start page's five took its layout from
  40 ms to 365 ms on the emulated 32 MHz Falcon, the first time only; the home page uses four, those
  most pages use). `font_get` keeps the face it picked for each family, style and size (24 Oct
  2026): it went through all 144 for every run of text, a tenth of the home page's layout (1.99 M
  68030 instructions to 1.80 M). Text is measured from each face's advances of ASCII, read where
  they are in it through one table of their places (`font_width` and `font_advance` went through
  `font_glyph` and a `memset` for every character; a copy beside each face took 14 KB, which the
  stock Falcon's pages need), and the layout looks a page's lists of font names through once
  (`font_of` matched up to 26 names in each, at every run of text): a 127 KB page of the page set
  laid out in 12.1 M instructions, from 13.9 M, half of it unpacking the eight faces it uses, the
  first time. Hebrew vowel points take no room and are not drawn yet.
- **The document** (`src/doc`): the HTML parser's tree as EWeb's nodes, each carrying the CSS engine's node,
  with a UA stylesheet from the HTML Standard's rendering section. HTML's attributes that stand for
  CSS (`bgcolor`, `width`, `align`, `border`, `cellpadding`, `<font>`...) become presentational
  hints, a new part of EWeb's CSS engine: author rules of specificity zero before every sheet, as
  the standard has them.
- **The layout** (`src/layout`, fresh): blocks with collapsing margins, padding, borders and
  backgrounds, widths with `min`/`max` and `box-sizing`, `margin: auto` centring; lines broken at
  spaces (a word too long for its line is split), `text-align` (justify too), `line-height`,
  `white-space`, `text-transform`, `text-decoration`, sub and superscripts; lists with every common
  marker and `start`, `reversed` and `value`; `::before` and `::after` text; tables with shared column
  widths, `colspan`, `border-spacing`, and shrinking to fit (flex rows, side by side, until flexbox had its own, below); Hebrew words
  reordered right to left within each line (UAX #9 with the line's words for its characters), with
  right-to-left paragraphs aligned right. Pictures and forms (below, 19 Oct 2026). Since 19 Oct 2026 (your order: after pictures, the layout's gaps):
  - **floats**, left and right, in each block formatting context (the page, a cell, a float, a box
    with `overflow` other than visible, absolute boxes): each at its width (asked for, or its widest
    within the room), as far over as it fits beside those already there, else below them; lines
    beside them are shortened (moved below when not even their first word fits), `clear` goes
    below them, a table or a box of its own goes beside them rather than under, and a context holds
    its floats. HTML's `align=left|right` on `<img>` and `<table>` floats them (and `hspace`,
    `vspace`); a floated or `display: block` picture is a block of its own size (`margin: auto`
    centres it);
  - **positioning**: `relative` moves a box and keeps its place; `absolute` takes it out of the
    flow, at `left`, `right` and `top` in its nearest positioned ancestor (the page for `fixed`:
    EWeb has no viewport of its own), else where it would have been; text meant for screen readers
    only (a box of a pixel hiding its overflow, or put far off the page) isn't drawn (and since
    23 Oct 2026 a box hiding what overflows it clips it: below). Since 24 Oct 2026 positioned
    boxes are drawn over what isn't, by `z-index` (CSS 2.1, Appendix E, `draw_order` in the
    layout): a stacking context's own items in the document's order, then its positioned boxes,
    `auto` and 0 in the document's order, then those above by `z-index`; one with a `z-index` is
    a context of its own, drawn whole in its turn; one below 0 stays where it is, under what
    follows it (as before). A click goes to what is drawn on top. Before, everything was drawn in
    the document's order, and demozoo.org's login menu (`position: absolute; z-index: 10`, open
    without its script) ran into the column below it, both its words and the column's showing.
    The list itself stays in the document's order, for find, selecting and copying; the order
    drawn in is the page's ranges of it (`page.seg`, ewrender `-d`'s "drawn:"), and the strips'
    index (`sidx`) is in that order. And a link laid out as a block (a card, a menu's button:
    `display: block`, `inline-block`, a flex item) is the link all over its box, as in a browser,
    not only on its words and pictures: an item not drawn, its border box (`link_area`, ewrender
    `-d`'s "fill ... #00000000 link N"), as a box with `pointer-events: none` has none;
  - **tables**: `rowspan` (a cell takes its columns in the rows below; the last of them grows to
    it), every cell as tall as its row with its background and borders, `vertical-align` (middle by
    default, top, bottom) in cells, and columns too wide for the room keep their longest word,
    sharing what is left by how much more each would take;
  - **flexbox** (21 Oct 2026, its own algorithm now, CSS Flexbox 1's 9.2 to 9.9; it had shared table
    rows' code): each item's base size (`flex-basis`, its width, else its widest), shared out by
    `flex-grow` or taken back by `flex-shrink` (weighted by the base), each within its `min-` and
    `max-width` (an automatic minimum is its longest word), frozen and shared again as the
    specification does; `flex-wrap` (and `wrap-reverse`) into lines, `justify-content` (start, end,
    centre, the three spaces) and auto margins along a line, `align-items` and `align-self`
    (stretch, start, end, centre, baseline) across, `align-content` across the lines of a container
    of a set height, `row-gap` and `column-gap`, `order`, `row-reverse`; `flex-direction: column` (and
    `column-reverse`) lays its items down, `flex-grow` sharing a set height's room. An element in a
    flex container is an item of its own; its text goes in an anonymous one;
  - **grid** (21 Oct 2026, a first form of CSS Grid 1): `grid-template-columns` and `-rows` (lengths,
    percentages, `fr`, `auto`, `min-content`, `max-content`, `minmax()`, `fit-content()`, `repeat()`
    with a count, `auto-fill` and `auto-fit`), `grid-template-areas` and `grid-area`, `grid-row`,
    `grid-column` and their longhands (numbers, negative ones from the end, `span`), auto-placement
    row by row or column by column (`grid-auto-flow`, sparse), `grid-auto-rows` and `-columns`,
    gaps, `justify-items`, `justify-self`, `align-items`, `align-self` and `justify-content`.
    Columns are sized from the width: lengths first, content sizes from the items of one column (an
    item over several makes room for its longest word), then each grown to its limit, `fr` sharing
    what is left, else the `auto` columns; rows are as tall as their items need, and `fr` rows share
    a set height's room. Not taken: named lines (other than areas'), `dense`, `subgrid`, `masonry`,
    `grid` and `grid-template`'s areas form. The CSS engine's 13 new properties cost 0.2 to 0.6% more
    68030 instructions on the page set's sheets.
  Background pictures and `srcset` (Pictures, below). `tests/layout/floats.html`, `flex.html`,
  `grid.html` and `pictures.html` keep them all; the page set laid out with AddressSanitizer and
  UBSan, no fault.
- **The display list and the canvas** (`src/gfx`): the layout gives fills and text runs in device
  pixels (a CSS pixel is 0.8 of the Falcon's at the start); the canvas draws them into memory, a
  byte a pixel for the planar modes or 16-bit for true colour, which the GEM side copies to the
  screen through ST-RAM (the screen can't be fed from TT-RAM).
  Since 19 Oct 2026 (your report: text appeared on the screen line by line) the part to draw is
  drawn whole in memory and copied to the screen in one go, nothing seen half drawn (buffers for
  the screen's height when 256 KB of ST RAM stay free after them, else 32 rows at a time as
  before); the display list is indexed in strips of 256 rows, so drawing the window, or the rows a
  scroll brings in, looks only at the items there; the planes are made 8 pixels at a time from
  tables, 16 pixels alike (most of a page is its background) as all-set or all-clear words, written
  in the screen's own interleaved planes (`vr_trnfm` only for screens of another format); glyphs and
  true colour's fills a row at a time. A page drawn, on the emulated 32 MHz Falcon (the window,
  `make hatarigui`'s pages): 16 colours 410-455 ms to 310-365 ms (a long page, 850 to 760), 256
  colours 585-625 to 560-570, true colour 120-140 to 105-130; the TT in 16 colours 440 to 350, its
  1280 x 960 monochrome 2.6-3.6 s to 1.9-2.0 s. Translucent items are mixed with what is under them
  since 22 Oct 2026 (Translucent colours and opacity, below).
- **Browsing** (`src/browse`): what an address typed means (a URL, a file, or words to search for),
  the page and its stylesheets over HTTP and HTTPS or from the disk, a memory cache (4 MB) for Back
  and for a site's stylesheets (a page that came after a redirect under both addresses), 50 pages
  of history with where each was scrolled, a page's anchors, and EWeb's own pages (the home page,
  below; "can't open", with the reason). The pages nearest in the history stay laid out (Erez's request,
  19 Oct 2026): Back and Forward show them at once, laid out again only for another window width,
  within a quarter of the free TT RAM up to 24 MB (else an eighth of the free ST RAM up to 2 MB),
  a page counted as eight times its source; the farthest go first. In the GUI rig a Back went from
  about 1 s (fetch from the cache, parse, style, layout) to 0.45 s, all of it drawing.
- **The home page** (`about:start`, Erez's request of 24 Oct 2026: "nice visuals, the EWeb logo
  and great links out of the box"; simpler the same day, a row a group, no boxes): AERA's look,
  black with light copper labels, the EWeb globe (the family's drawing) and wordmark (Oxanium), the
  AERA logo at the foot. `src/browse/home.html` is made into C strings at build time by
  `tools/mkhome.py`, its pictures put in as `data:` addresses (2.4 KB of them), and EWeb fills in
  the greeting and the date by the clock, a search box (`SEARCH=`: the words go through
  `about:search`), the five pages opened last and the first six bookmarks, and a tip a day. Its
  links: search (FrogFind, DuckDuckGo Lite, Wiby), news in text (68k.news, NPR, CNN Lite, Hacker
  News), reading, Atari (Atari-Forum, DHS, Demozoo, Pouët, Atarimania), the early web, and AERA.
  It reads HISTORY.TXT at start, which the first page from the network read before; its pictures
  are decoded each time, not looked for in the disk cache (no `data:` picture is), which would
  open it at every start and write it at every quit. On the
  emulated Falcon in 256 colours it is shown in 2.0 s (the old start page 1.6 s), most of it the
  first page's work that any page pays: the UA sheet, the fonts; 4.1 M 68030 instructions to style,
  lay out and draw.
- **The window** (`EWEB.PRG`, `ui/`, after EFTP's): a toolbar of one row (your layout, 21 Oct 2026,
  a size larger the same day: the buttons as pictures alone at the left, 30 x 25, Back, Forward,
  Reload, Home, Search and Stop, each saying its name on the status bar under the mouse; in the
  platinum look since 24 Oct 2026, your choice of three drawn: graphite shading, light at the top
  with a dark edge, Stop alone red; under the mouse an icon wakes, lit in AERA's copper (your idea,
  the same day), and one that can't be used is greyed. `tools/mktbicons.py` draws them 16 times
  bigger and reduces them, in three editions: 256 colours and true colour, shaded (each colour the
  screen's nearest when first drawn, so EWeb's palette and TOS's both show copper); the 16 system
  colours, the shading dithered 2 x 2 and lit in gold; black and white, a white fill with a black
  edge, greyed dotted and lit inverted, as GEM shows a chosen object. Each icon is a picture with
  holes, made into the screen's own form with its mask (ST RAM: 29 KB for the 18 in 256
  colours, 58 KB in true colour, 14 KB in 16 colours, 4 KB in black and white; kept, each made
  once, only where there is TT RAM: on a machine of ST RAM alone, a 4 MB Falcon or an ST, made
  each time it is drawn, so that memory goes to the pages) and drawn as two
  blits, the mask clearing its pixels and the picture ORed in, the same in every mode; the address beside them, typed into in place; the
  throbber, below, at the right end), the page under it with the window's slider, a status bar at the
  foot (your choice of 21 Oct 2026, design C of four drawn: a link's address under the mouse, what a
  load is doing, how a secure page came; and along its foot a line two or three pixels high that
  fills from left to right as a load comes, each part in its phase's colour, the darker system
  colours that read on the grey: connecting blue, the page cyan, its stylesheets magenta, building
  it yellow, its pictures green; black with 4 colours or 2. Each phase has its stretch of the line
  (0-8%, 8-41%, 41-55%, 55-64%, 64-100%); within one it goes on with the bytes that come (never to
  the end, as how much will come isn't known) or with the pictures done; once full it goes a moment
  later. GEM's info line takes only text, so the window has none and draws its own), the
  family's menu bar, About box, shortcuts, right-click menus, VA_START and drag and drop, help,
  iconify and crash report. The page is drawn into memory a band of 32 rows at a time and copied
  to the screen from ST-RAM: in the planar modes each colour is the screen's nearest (EWeb sets no
  pens), the bytes made planes and `vr_trnfm`'d; on a monochrome screen fills are greys dithered
  from black and white; in true colour the canvas is the screen's own pixels. Scrolling moves what
  is on the screen and draws only the new rows. While a page loads, the network's waits keep the
  window alive, and Esc, Undo or Stop stop the load, the page before staying.
- **Dialogs** (the guidelines' 5.6, your decision of 24 Oct 2026: the family's look, with a
  setting): where the AES draws 3D objects every dialog and alert is in its 3D look, by common's
  `dlg_3d` (EWeb's own trees in `ui/dialogs.c`'s `d_end`; the About box and alerts in common's
  code): the box on the 3D grey, buttons raised, fields white. That is TOS 4,
  MagiC, XaAES, and EmuTOS 1.4 too (its AES has TOS 4's 3D objects); TOS 1 to 3 stay flat.
  Settings > General > 3D dialogs (`DIALOGS3D=1`; 0: flat everywhere), greyed where there's no 3D
  look. `make hatarigui`'s `dialogs` group shows the Settings and Find on TOS 4.04 and EmuTOS
  both ways, and greyed on the ST's TOS 1.04.
- **First timings,** on the emulated Falcon (Hatari 2.4.1, EmuTOS 1.4, a 68030 at 32 MHz with
  TT-RAM, 640x480 in 16 colours; `EWEB.PRG` logs each page's phases), saved pages from the disk
  without their stylesheets:

  | Page | Size | Total | Parse | Style | Layout | Drawn |
  |---|---|---|---|---|---|---|
  | BBC News (4,965 px tall, 121 links) | 100 KB | 4.35 s | 2.68 s | 0.16 s | 0.83 s | 0.48 s |
  | WSJ (10,000 px, 260 links) | 186 KB | 9.49 s | 7.08 s | 0.31 s | 1.50 s | 0.48 s |

  The parse was Hubbub's, as measured before (4.15 s on the same BBC page before its UTF-8 copy);
  EWeb's own parser has replaced it since (below). "Drawn" is the first screen in 16 colours.
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
  | The text size changed (^+ ^- ^0) | 0 (saved at quit) | a safe save each time |
  | Quit, nothing changed | 2 opens, 1 lookup, a safe save | 0 |
  | First secure page of a session | 3 opens, 5 lookups | 3, 1 (`ROOTS.DAT`; `SEED.DAT` read, then written over) |
  | The same, the first session after installing | 5 opens, 7 lookups, 61 reads | 5, 1, 31 |
  | `EWGET.TTP`, 1 MB | 4 opens, 2 lookups, the file made once | 4, 1 (`CRASH.LOG`: a report to show?) |

  Pages, stylesheets, history and Back go through memory (the cache), never the disk. `EWEB.FNT`
  stays open while EWeb runs, a face read with one seek. A file read whole is opened and its size
  taken with a seek (`pf_size_open`), not looked up first. `EWEB.INF` is read once and kept: a setting
  changed by the user (the text size, the home page) is saved then, the window's place at quit if it
  moved, and nothing is written when nothing changed. The secure connections' files (`CACERT.PEM`,
  `ROOTS.DAT`, `SEED.DAT`) are `common/`'s (`tls_file_info`: `CACERT.PEM` looked up once; the seed
  read, then written over in place, with no look at the folder). Reads and writes from TT-RAM go
  in 32 KB pieces (`atari/plat_tos.c`): as they are where the disk driver sets `_FRB`, else
  through 32 KB in ST-RAM; a 1 MB download is 32 writes (it was 128 of 8 KB). `EWGET.TTP`'s log
  (`-l`) is written once a run, not at each milestone.

## Pictures (19 Oct 2026)

Your decisions (19 Oct 2026): **pictures first, then the layout's gaps** (floats, positioning,
`rowspan`), and **the picture code shared** in AERA-Atari's `common/` rather than copied from
EContact: `common/src/pic.c` and its decoders, which EContact's photos can move to.

- **How a page gets them** (`src/browse/img.c`, `br_pictures_step`): the page is shown as soon as
  its text is laid out; its pictures come after, those in the window first, then below, then above,
  one a step of the main loop, each drawn as it comes. The window scrolls meanwhile (its arrows,
  slider, Up, Down, Space, ClrHome); anything else (a link clicked, a menu, typing) stops the
  picture coming, is done, and the pictures go on after it; Esc and Stop end them for the page.
  Each address is kept once, with its file (so that another width or page size decodes it again
  without fetching) and each size it is decoded at; their memory is counted (an eighth of the free
  TT RAM up to 16 MB, else a sixteenth of the free ST RAM up to 1 MB), and
  past it the pictures no page shows go first, then the decoded pixels of those only kept pages
  show, never those at a size a page shows now (`img_size_shown`: a picture a page shows at two
  sizes that don't fit together was decoded at each in turn, for ever, until 25 Oct 2026; a
  yaary.music portrait on a host's 8 MB). A page kept for Back keeps its pictures.
- **Their size on the page** (`src/layout`): CSS `width` and `height` (HTML's `width` and `height`
  attributes are hints for them), else the file's own size, one from the other by the
  proportions, within `max-width` and `max-height`; `vertical-align: middle` and `bottom` too.
  Before a picture's file has come, the attributes' proportions stand in, or with neither its alt
  text: the page is laid out again when such a picture comes with other proportions, at most once
  in two seconds and when the last has come (in the GUI rig: 0.4 to 0.7 s on the emulated Falcon
  for the test page). A picture not there yet is framed, its alt text in it. `data-src` and the
  like stand in for a `src` that is missing or a `data:` placeholder; `data:` pictures are decoded
  from the address itself.
- **Decoding** (`common/src/pic.c`): straight to the size shown, never whole at the file's size
  (but an interlaced PNG and a GIF, four bytes a pixel while they decode): a shrinking picture is
  averaged, each pixel weighted by its alpha, an enlarged one repeats its pixels. Kept as the
  Falcon's 16-bit colour with an alpha only when it has one; in true colour drawn as it is, alpha
  mixed; in the modes with a palette dithered once (Floyd-Steinberg, serpentine) to the screen's
  own pens, a colour wanted past the cube's edges finding the pen that lies that way (light blue
  on 16 colours would otherwise come out grey); monochrome the same with black and white.
- **JPEG: NetSurf's choice, libjpeg-turbo, over EContact's TJpgDec.** Both measured (68030
  instructions under QEMU, `common/tests/pic`; the times at your DFB1's 5.2 million a second):

  | A JPEG | shown at | TJpgDec | libjpeg-turbo 3.1.4 |
  |---|---|---|---|
  | 480 x 320 photo | its size | 26 M (5.0 s) | 19 M (3.7 s) |
  | the same | 192 x 128 | 26 M (5.0 s) | 8.5 M (1.6 s) |
  | 1589 x 1043 photo | 596 x 391 | 297 M (57 s) | 61 M (12 s) |
  | 320 x 240, progressive | 128 x 96 | 0.85 M, 8x8 blocks only | 7.1 M, the real picture |

  libjpeg-turbo's IDCT unpacks straight at n/8 of the size (TJpgDec's 1/2 and 1/4 do the whole
  IDCT, then average), and it reads every JPEG a page has: progressive, arithmetic, CMYK. It costs 89 KB of program. Taken as its decoder only, 8-bit, without
  the floating-point IDCT (AERA-Atari's `third_party/libjpeg-turbo/AERA.md`). TJpgDec stays in
  `common/` for the 68000 programs.
- **JPEG made faster on the 68030 (20 Oct 2026):** libjpeg's fast integer IDCT (`JDCT_IFAST`)
  and no copies: a picture shown at the size it unpacks to gets its 5-6-5 pixels straight from
  libjpeg (patched to give the machine's own byte order), any other has its RGB rows go to the
  scaler as they are. The decoder's own functions on the 480 x 320 photo (`tools/prof68k.py`):
  22.6 M instructions at its size before, 16.7 M after (3.2 s on the DFB1); shown at 192 x 128,
  10.2 M to 8.9 M. Against a decode in floating point the photo is 38.2 dB (39.2 with the exact
  IDCT; libjpeg's merged upsampling would save more but makes colour fringes, 31.7 dB, so it isn't
  used). The DSP's part is below.
- **JPEG made faster on the ST's 68000 (20 Oct 2026):** TJpgDec's transform is now the same AAN
  transform in 16-bit arithmetic (each product one `muls.w`; the 68000 has no 32-bit multiply,
  and GCC called a routine for each), at 1/2 and 1/4 scale the coefficients are folded into a
  4 x 4 or 2 x 2 block and only that is transformed (the exact average of each square, which the
  scaler took before), and the colours come from tables, each chroma sample's terms once for the
  pixels sharing it (AERA-Atari's `third_party/tjpgd/README.txt`). The scaler takes RGB rows,
  divides through a table of reciprocals (`mulu.w`) and copies a row shown at its size; black
  and white has a dither of its own. Its error against libjpeg-turbo's exact decode went down
  (4:2:0, mean 2.83 to 2.64). `PICBST.TTP` (`tests/bench/picbench.c`, run in Hatari's
  cycle-exact 8 MHz ST by `tests/bench/picbench.py`) times each step:

  | The 480 x 320 photo on an 8 MHz ST | before | after |
  |---|---|---|
  | decoded, shown at 192 x 128 | 18.1 s | 12.8 s |
  | dithered to black and white, 192 x 128 | 5.7 s | 2.1 s |
  | decoded at its size | | 24.8 s |
  | dithered at its size | | 12.8 s |

  In `EWEBST.PRG` the progressive 320 x 240 JPEG (jpegdc's 8 x 8 averages) went from 13.7 s to
  5.0 s on the page: while a picture decodes, GEM gets its turn ten times a second and without
  waiting for its timer (each turn had waited a clock tick, hundreds of them), and the throbber
  is copied, not made again (its section, below).
- **JPEG on the DSP (20 Oct 2026; the owner's rules: the DSP takes what it does faster, and it is
  made fast for the stock Falcon, "as I want others to enjoy this", not for his DFB1X):** the
  first version (0.16) had the 68030 unpack the Huffman codes and the DSP transform and make the
  colours. Erez's `PICBENCH.LOG`s showed it was the 68030's half that a stock Falcon waited on
  (8.49 s on the 68030 alone, 2.57 s with the DSP; at 192 x 128, 5.88 s and 3.11 s), and the
  DSP's that a DFB1X did (2.11 s and 1.10 s). So now the DSP (`common/dsp/jpeg.a56`) decodes a
  baseline JPEG itself: `pic.c` lets libjpeg read the headers and sends the scan's bytes as the DSP
  asks for them, two to a 16-bit port write. The DSP unpacks the Huffman codes (for an AC
  coefficient a 9-bit table, made on the DSP, gives its run, value and length at once when the
  code and value fit, as libjpeg-turbo's fast path does), dequantises, transforms each block
  straight to n/8 of its size (each pixel the mean of the ones it covers; only the rows and
  columns a block uses, a block of its DC alone filled at once), makes the colours (the chroma
  repeated over the luma it covers; each channel's bits from a table at Y + its term, libjpeg's
  way, the tables made on the DSP for 5-6-5 or R G B) and sends each MCU's pixels from its host
  interrupt while it decodes the next, so the 68030 reads one while the DSP works on the other.
  The 68030 is left copying bytes in and pixels out. A picture shown smaller the DSP shrinks too
  (21 Oct 2026; the scaler's centre mapping): each source pixel's R, G and B summed into the box of
  the pixel shown it falls in (one long word a box: X R << 12 + G, Y B), the boxes that go on into
  the next MCU or row of MCUs carried, the rest sent as their means, rounded as the scaler does: the
  same bytes as the DSP's R G B rows through the scaler (the tests compare them), with only the
  pixels shown coming back. Boxes of more than 16 pixels, or tables over 4096 words, leave it to the
  scaler. Making that oracle showed the R G B tables' red one below for many reds (×$7FFFFF,
  rounded), taking one from green and blue; the tables are exact now. The first such JPEG checks it against
  libjpeg's own decode of a known picture; a DSP that gets it wrong, or stops answering, is left
  alone. CMYK JPEGs stay with libjpeg. A progressive (or arithmetic-coded) one (21 Oct 2026) has
  libjpeg read its scans into coefficients on the 68030 (`jpeg_read_coefficients`), and the DSP
  does the rest from them, each block's coefficients sent in zigzag order to the last other than
  0: the transform, colours and upsampling were two thirds of libjpeg's work on it. The photo saved
  progressive on Hatari's stock Falcon: 6.78 s on the 68030, 4.38 s with the DSP; shown at 192 x
  128, 5.21 s and 4.21 s. What is left is libjpeg reading the scans (its refinement passes most)
  and the coefficients' trip to the DSP; the scans can't go to the DSP, which hasn't the memory for
  a picture's coefficients. Against Pillow: closer
  than libjpeg's fast transform for 4:4:4 and grey (its transform is exact), a little further for
  4:2:0 (it repeats the chroma where libjpeg blends it). The scaler sums a JPEG's rows in 16 bits
  (a third of the memory the five longs of a picture with alpha take), the same pixels a third
  sooner. `PICBENCH.TTP` on Hatari, the release's `PHOTO.JPG` (`tests/bench/mkphoto.py`: 480 x
  320, 24 KB, a grain as a camera's):

  | The photo | stock Falcon (16 MHz): 68030 | with the DSP | 32 MHz: 68030 | with the DSP |
  |---|---|---|---|---|
  | at its size, 0.16 | 4.56 s | 1.40 s | 2.69 s | 0.73 s |
  | at its size, now | 4.64 s | 0.58 s | 2.72 s | 0.28 s |
  | shown at 192 x 128 (4/8, then the scaler), 0.16 | - | - | 1.71 s | 0.89 s |
  | shown at 192 x 128, 0.17 | 3.07 s | 0.69 s | 1.65 s | 0.35 s |
  | shown at 192 x 128, the DSP shrinking it | 3.07 s | 0.45 s | | |
  | a 1024 x 683 photo shown at 600 x 400 (5/8, then shrunk) | 18.5 s | 2.0 s | | |

  Hatari runs a stock Falcon about 1.8 times as fast as Erez's (4.56 s against 8.49 s on the
  68030, 1.40 s against 2.57 s with the DSP), so his should take about 1.1 s for the photo with
  the DSP. What is left: the 68030 reading each pixel through the host port (a status read and
  two byte reads a word) and the DSP's own work: the Huffman codes, the transform, and shrinking
  (about 135 cycles a pixel shown and 95 a source pixel, in external memory). `PICDSP=0` (Settings > General) leaves it all to the 68030.
- **PNG** is fresh (`common/src/png.c`, on the family's inflate: NetSurf uses libpng, a library
  too big for what it adds here): every colour type and depth, interlaced, `tRNS`; an 800 x 600
  PNG decoded at its size, 44 M instructions (8.5 s), shown at 604 x 453, 59 M. **GIF** is
  NetSurf's libnsgif, its first frame (a 48 x 48 GIF, 0.2 M).
- **Dithering** costs 150 to 210 instructions a pixel on the 68030 (a 192 x 128 picture, 1 s; 16
  and 256 colours alike, the nearest pens found among a few); true colour has none. Black and white
  (two pens) has a loop of its own, on the 68030.
- **Dithering on the DSP (21 Oct 2026; the owner's rule: the DSP takes what it does faster):** for
  3 pens or more (a Falcon's 4, 16 and 256 colours) `common/dsp/dither.a56` gives the very pens the
  68030's dither gives, to the bit (`make testdsp` dithers every test picture both ways, 4, 16 and
  256 colours, 432 times; the first picture checks it on a known one, some of it clear, and a DSP
  that differs or stops answering is left alone). The colour each pixel wants is made in the
  accumulator, as (c - 128) x 2^15 and the error's sixteenths below, so that its limiter clamps it to
  -128..383 as the 68030 does; its pen comes from a near table (32 x 32 x 32 cells, three to a word,
  each worked out the first time it is wanted by the 68030's own search, kept while the program and
  the palette stay); the next row's errors are written over the row's own as each is done. The
  68030 sends the palette's tables once, then each row's pixels as they are, and reads the row
  before's pens back three to a word while the DSP dithers the next (its host receive interrupt
  takes the row into a ring of two). Hatari 2.4.1 raises no receive interrupt for a word sent to a
  DSP that had none waiting (its "direct transfer"; later Hatari does), so the program takes such a
  word itself while it waits: on Hatari the rows don't overlap the dither, on a Falcon they do. The
  DSP takes 174 cycles a pixel (5.4 us); `PICBENCH.TTP` on Hatari's stock Falcon:

  | The photo dithered | 68030 | with the DSP |
  |---|---|---|
  | at its size (480 x 320), 16 colours | 9.6 s | 1.14 s |
  | shown at 192 x 128, 256 colours | 1.74 s | 0.23 s |

  Pictures under 1024 pixels or wider than 1280 stay on the 68030 (loading the program and the
  tables costs more than it saves). `PICDSP=0` leaves it to the 68030 too.
- **EWeb asks for** `image/jpeg,image/png,image/svg+xml,image/gif`, so servers that would send WebP
  or AVIF send those. SVG is EWeb's own since 25 Oct 2026 (SVG, below). Not yet: animation. CSS gradients are drawn since 22 Oct 2026 (CSS gradients,
  below). `<input type=image>` shows its picture (forms, below).
- **Which picture** (21 Oct 2026, the HTML Standard's 4.8.4.3): a `<picture>`'s first `<source>`
  whose `type` EWeb reads (none, JPEG, PNG or GIF: a WebP or AVIF one is passed over) and whose
  `media` holds (the CSS engine's media queries, `ec_media_ok`), else the `<img>`'s own `srcset`
  with its `src` as 1x. Of a `srcset` EWeb takes the candidate nearest the screen's density at or
  above it (the zoom: 0.8 device pixels to a CSS pixel at 100%), the least to fetch that still
  looks sharp, else the densest; `w` descriptors are measured against `sizes` (its first entry
  whose media condition holds; lengths, `vw`, `em`, `calc()`, `min()`, `max()` and `clamp()`;
  `ec_sizes`), else the window's width. A picture chosen for 2x is shown at half its file's size.
  `data-srcset` and `data-sizes` stand in as `data-src` does, and a `data:` candidate (a
  placeholder) only when there is nothing else.
- **What isn't fetched, and the Pi's pictures** (21 Oct 2026; your yaary.music in Falcon mode took
  minutes for its pictures: 13 MB of them, 2560-pixel JPEGs, PNGs of up to 1.9 MB, and 1.4 MB of
  WebP that EWeb fetched only to find it couldn't read). In Falcon mode a picture whose address
  ends in `.avif`, `.svgz`, `.jxl`, `.heic` or `.tif` (and `.webp` in the ST edition) isn't
  fetched at all, and any picture's answer whose type is one of those (or text: a site's "not
  found") is left unread after its headers (`HTTPREQ.refuse`, its connection closed).
  **WebP in `EWEB.PRG`** (Erez's option C, 24 Oct 2026): it decodes a WebP itself, with Google's
  libwebp 1.6.0 (its decoder only, in AERA-Atari's `common/`, `-DPIC_LIBWEBP`; `common/README.md`),
  lossy or lossless, with alpha, of up to 1,048,576 pixels (`WEBP_MAX_PX` in `img.c`: a bigger one
  is left out, its place framed; an animated one too). A lossy WebP takes about 193 68030
  instructions a pixel at any size it is shown (a JPEG 96, or 32 shown at a third, as libjpeg
  unpacks it smaller), so a megapixel is about 40 s on the DFB1 and a 400 x 300 picture 5 s; it
  comes to the scaler a band of rows at a time, never whole; a lossless one is whole while it is
  decoded (8 bytes a pixel). It adds 93 KB to `EWEB.PRG` (1,036,033 bytes to 1,129,192), and no FPU
  instruction. EWeb still asks for JPEG, PNG and GIF, and a `<picture>`'s WebP source is still
  passed over for its JPEG one, which the DSP decodes quicker; through the Pi a WebP still comes
  as JPEG. The ST edition has none of it (TJpgDec only). Through the Pi gateway each picture
  is asked for with `X-Gateway-Picture: fit=WIDTH` (the page's width, device pixels): webgate
  decodes one wider than that, or of a kind the Atari can't read, with Pillow, upright as its EXIF
  says, and sends a baseline JPEG no wider (quality 80; a PNG of 256 colours when it has
  transparency or was a GIF or palette PNG), its own size written in it, "webgate natural WxH" in
  the JPEG's first comment or under the keyword `webgate` in a PNG's tEXt before its pixels; EWeb
  (`img.c`) lays it out at that size, so the page is the same either way, and the disk cache keeps
  it with the size in it. A JPEG, PNG or GIF the page can show as it is comes as it is.
  Of yaary.music's pictures, an 865 KB photo (2560 x 1920) comes as 72 KB, 608 x 456, and a
  1.8 MB PNG with transparency as 30 KB (measured with webgate's `make_picture`). Without Pillow
  on the Pi, pictures pass as they came. The site itself can do more: its pictures have no
  `srcset`, which WordPress makes for its uploads, and EWeb would take the smallest sharp one.
- **Pens from the Pi, measured** (21 Oct 2026, for the hub: could the Pi decode in the Atari's
  place?). Four pictures (the photo, a banner, a palette PNG, a test card) at the page's width,
  each as webgate sends it now (a: the Atari decodes and dithers it) and as the screen's pens made
  on the Pi (b: a byte a pixel, deflated; c: packed at the screen's depth, deflated; d: packed, not
  compressed), the Atari's time from `PICBENCH.TTP`/`PICBST.TTP` in Hatari (a .Z file is inflated),
  d's estimated (a copy). The four together, the Atari's seconds and the bytes:

  | machine | a | b | c | d |
  |---|---|---|---|---|
  | ST mono (EWEBST) | 113.0 s, 79 KB | 9.6 s, 38 KB | 6.3 s, 33 KB | ~0.2 s, 46 KB |
  | ST low, 16 colours | 88.0 s, 43 KB | 8.2 s, 39 KB | 7.6 s, 37 KB | ~0.2 s, 91 KB |
  | ST medium, 4 colours | 211.5 s, 79 KB | 6.3 s, 27 KB | 5.1 s, 25 KB | ~0.2 s, 46 KB |
  | stock Falcon, 16 colours (DSP) | 6.6 s, 79 KB | 4.1 s, 75 KB | 3.9 s, 73 KB | ~0.2 s, 186 KB |
  | stock Falcon, 256 colours (DSP) | 6.7 s, 79 KB | 7.7 s, 147 KB | 7.7 s, 147 KB | ~0.2 s, 372 KB |

  With the transfer at 20, 65 and 160 KB/s, d is the fastest on an ST at any speed above 7 KB/s
  (2.5 s in place of 117 s for the four at 20 KB/s in mono); on the stock Falcon d above 30 KB/s at
  16 colours (c below) and above 45 KB/s at 256 (a below). Inflating on the Atari is what makes b
  and c slow: about 5 KB of deflate a second on the 8 MHz ST, 20 on the 16 MHz 68030. The DSP's
  dither of a PNG was timed on the 68030 by picbench (its DSP check was a JPEG's) until this
  measurement: 0.6 s, not 5.7, as EWEB.PRG does it.
- **Pens from the Pi, built** (21 Oct 2026, your go-ahead): webgate makes a picture into the
  screen's pens with AERA-Atari's `aerapens` (`common/tools/pens`): `common/src/pic.c` itself,
  both of EWeb's builds in one program (libjpeg-turbo as EWEB.PRG decodes, TJpgDec as EWEBST.PRG;
  `-fsigned-char -fwrapv`), so the pens are the ones the Atari would make to the bit:
  `common/tests/pic/pens.py` runs it (here, and arm64 and armhf under QEMU) against the same code
  as 68030 and 68000 code under qemu-m68k (5088 runs, all the same). A pure-Python dither was
  measured first (AERA-Gateway's rule is standard Python): 0.5 s here for a 400-pixel photo, about
  2 s on a Pi 4, slower than the Falcon's DSP; aerapens takes 25 ms here. AERA-Gateway builds it on
  the Pi from its `pens/common/` copy and systemd runs one a connection on
  `/run/webgate-pens.sock`, as its own user with 64 MB, 2 s of CPU and nothing to open; it takes
  pictures of 16 million pixels at most (from the header, before anything is made) and makes 4
  million at most; its answer names the AERA-Atari commit and the build (`X-Gateway-Pens`).
  Fuzzed with libFuzzer, ASan and UBSan (`make fuzzpens` there), which found a GIF whose frames
  make its canvas far bigger than its header: pic.c now caps it (on the Atari w x h x 4 could
  wrap). EWeb's side (`src/browse`):
  - **When:** always through the gateway, but in true colour (your decision, 21 Oct 2026, after
    0.24 on the DFB1: the Falcon decoding the Pi's JPEGs took seconds a picture and slowed
    everything; `PENS=1`, as EWeb starts). `PENS=-1` is the measured choice: the ST edition and a
    68030 without the DSP always, with the DSP when answers come at 30 KB/s or more (16 colours or
    fewer) or 45 KB/s (256), from the last answer of 8 KB or more (`browser.rate`); `PENS=0` never
    (`pens_wanted`; ewrender `-Q`).
  - **The request:** `X-Gateway-Picture: fit=W; size=WxH; pens=N; pal=CRC; ed=lj|tj`, `size=` the
    size it is shown when the layout knows it (its width and height given), else the Pi makes it
    no wider than the page and a picture shown at another size is asked for again at that size
    (`img_refetch`). `pal=` is the CRC-32 of the screen's colours: webgate keeps the last 16 sent
    to `http://webgate/palette` (POST, 2 to 256 of R G B), and answers 409 with
    `X-Gateway-Palette: need` for others before it fetches anything; EWeb sends them and asks
    again.
  - **The answer:** `application/x-aera-pens`, `EWPN`: 1, the bits a pen, flags (1: a mask), N - 1;
    width, height, the picture's own width and height; the colours' CRC; the pens a row at a time,
    packed, high bits first; then a bit a pixel where it is shown (alpha 128 or more, where the
    canvas draws a pen). Its length is checked against its head before anything is made of it.
    `img.c` only unpacks it (`pentest`). A WebP, AVIF or a photo its EXIF says is turned is made
    a PNG on the Pi first (upright, nothing lost), then pens.
  - **The cache:** the answer is kept in the disk cache under its address and the pens asked for
    (`#pens=N,CRC,WxH`), so another screen's or size's pens are never taken for these; a mode
    change (other colours) asks the Pi again, as does a window of another width. The text size
    changes nothing.
  - **Without aerapens** on the Pi, webgate answers as before (JPEG or PNG), and EWeb takes
    either: nothing breaks while the Pi hasn't it. `make itest` checks the pens through webgate are
    the pens decoded here, to the pixel, and the cache by the pens asked for (aerapens behind a
    socket as on the Pi: `tests/net/penssock.py`); `make hatarigui`'s ST case takes them through
    the gateway on the emulated ST.
- **The text size** (Larger text, Smaller text, ^+ ^- ^0; `ZOOM=`, per cent) changes the text
  alone since 21 Oct 2026 (before, the whole page): fonts and line heights (`LAYOUT_OPT.text`), not
  lengths or pictures, so the pictures keep their size and the pixels decoded for it, the page its
  width in CSS pixels (the same `srcset` choices and media queries), and nothing is fetched or
  decoded again.
- **Background pictures** (21 Oct 2026, CSS Backgrounds 3): the first layer's `url()`, over the
  border box, at `background-position` in the padding box, `background-size` (`cover`,
  `contain`, lengths, percentages, `auto`) and `background-repeat`; the root's or the body's over
  the whole canvas (since 22 Oct 2026 its tiles go on below a page shorter than the window, as CSS
  Backgrounds 3, 2.11.2 has them; before, the window was white there). Each is one item in the
  display list (`DLF_TILE`: the item is where it is drawn, the tile where the first one goes and its
  size), its picture decoded once at the tile's size, and the canvas draws the tiles where the item
  and the window meet: an opaque picture's first rows of tiles, the rest copied from the rows a tile
  above. Laid out before its file has come, it is drawn when the page is laid out again with its
  size. A sheet's `url()` is its own address's (`ec_sheet_at`), not the page's. Not taken: more
  layers (the top one is drawn: CSS gradients, below), `background-attachment: fixed` (drawn as
  `scroll`), `background-origin`, `space` and `round` (drawn as `repeat`), pictures in inline boxes'
  backgrounds, `list-style-image`. `background-clip` stops the picture at the box it names since 22
  Oct 2026, as it stops the colour.
- **Off on a slow line:** View > Show pictures (`PICTURES=` in `EWEB.INF`): pages show their
  pictures' alt text instead.
- **Checked:** `common/tests/pic` (PngSuite, libjpeg-turbo's and libnsgif's test pictures and more
  made from them, against Pillow with both JPEG decoders, cut short and damaged under
  AddressSanitizer and UBSan, and the same bytes from the 68030 builds); `make itest` (a page of
  pictures over HTTP: sizes, the page laid out again, a missing one, a picture on its own); `make
  hatarigui` (the page of pictures over the network, scrolled while they come, and from the disk in
  256 colours, true colour, TT medium and TT monochrome). The rig found a fault in the family's
  FAKESTNG: a connection's number used again at once (a page stopped with Esc, then the next) got
  the last of the old connection's data; fixed in AERA-Atari.

## Forms (19 Oct 2026)

Your order (19 Oct 2026): after pictures and the layout's gaps, forms, then cookies. NetSurf's own
forms are part of its core (GPL-2.0-only, and laid out by its layout), so this is fresh code, after
the HTML Standard:

- **State** (`src/doc/form.c`): each control's, kept with the document, so that laying the page
  out again keeps what was typed and Back shows a form as it was left (a kept page; one fetched
  again starts afresh, as browsers do without a script). A control belongs to its `form`
  attribute's form, else the form the parser had open (the HTML Standard's form element pointer,
  for forms left open across a table's cells), else the form around it. Values as the page gives them (`value`, a textarea's
  text, `checked`, `selected`; a list with nothing chosen shows its first option that can be);
  `disabled` (and a disabled fieldset's, but its first legend's), `readonly`, `maxlength`; reset.
- **Drawing:** the layout gives each control its size (a field from `size`, a textarea from `cols`
  and `rows`, a list as wide as its widest option, so that choosing moves nothing) and a display list
  item of its own (`DL_CTL`) that the canvas draws from the state: typing draws the field again,
  not the page. A field scrolls sideways to show the caret, a textarea wraps and scrolls its lines.
  Their text runs right to left where it is Hebrew, by a short form of the bidirectional algorithm
  (the first strong letter gives the direction; numbers, with their points, commas and signs, stay
  left to right; brackets mirrored), and the caret goes where the letter is on the screen; this is
  the fuller algorithm that page text still lacks (Hebrew, above), and can move to `common/`.
  `<input type=image>` is its picture; a `<label>`'s words stand for its control.
- **Sending** (the standard's form submission): the entry list (the button that sent it, a picture
  button's `name.x` and `name.y`, `_charset_`, `dirname`, unticked boxes and disabled controls
  left out), then `application/x-www-form-urlencoded`, `multipart/form-data` (a file field sends no
  file: EWeb can't upload yet) or `text/plain`, line breaks as CR LF; `formaction`, `formmethod`,
  `formenctype`; a dialog's form sends nothing. The text goes in the page's character set (a letter
  it lacks as `&#N;`, as browsers do; `accept-charset` as the standard picks from it: Encodings,
  below), so Windows-1255 sites get
  their Hebrew as they expect. GET puts it in the address, POST through the HTTP client (a 301, 302
  or 303 answer fetched with GET; a POST's answer isn't cached; Back to it, once it isn't kept,
  fetches its address with GET rather than sending again). Return in a field sends the form by its
  first submit button, or without one when the field is the form's only one (implicit submission).
- **EWEB.PRG** (`ui/win.c`): a click puts the caret in a field (the pointer a text cursor over
  one); Tab and Shift-Tab go from control to control (then to the address); Left, Right, ClrHome,
  Up and Down in a textarea, Backspace, Delete; ^C, ^X, ^V; the Atari's keys (Hebrew too) typed as
  UTF-8. Boxes and round buttons by click or Space; a list's options in the family's pop-up, a
  screenful at a time (Earlier, Later).
- **Checked:** `tests/forms/cases.txt` (33 cases worked out from the standard: what each kind of
  control sends, the encodings, the charsets, implicit submission, typing, reset; `make test`, with
  AddressSanitizer and UBSan); `make itest` (`/site/form.html` over HTTP: GET, POST urlencoded and
  multipart, a POST answered with 303, Back to the filled form); `make hatarigui` (a field typed
  into and sent with Return on the emulated Falcon, Back, Tab, Space and a label's words on a box,
  a login sent with POST); `tests/layout/forms.html`; the page set (1,580 controls in its 258
  pages) laid out and drawn with AddressSanitizer and UBSan, no fault.

## Cookies (19 Oct 2026)

Kept on the disk (your decision, 19 Oct 2026). `src/net/cookie.c`, fresh code after RFC 6265bis
(NetSurf's cookies are in its GPL-2.0-only core):

- **Set-Cookie** (the parsing algorithm, the cookie-date algorithm): Expires (two-digit years, the
  dates servers really write, a day the month doesn't have refused) and Max-Age (which wins);
  Domain (a host and its subdomains; refused when the host isn't in it, or when it is a public
  suffix: `com`, `co.il`, `co.uk`... from a short list, as EWeb has no Public Suffix List); Path
  (else the request's directory); Secure only from https, and a plain page can't replace a secure
  cookie; HttpOnly; SameSite (None needs Secure); the `__Secure-` and `__Host-` prefixes. Every
  answer's cookies are taken, a redirect's too (a login answered with 303).
- **Cookie:** the host's own and its domains', on matching paths, Secure ones only over https,
  longer paths first, then the older. SameSite: Strict only from the same site; Lax (and no
  SameSite) also on a page opened from another site with GET; None everywhere a page goes.
- **Other sites' cookies:** a picture or stylesheet from another site (a tracker's, an ad's)
  neither sets nor gets cookies; a site is its host's last two labels, three under a listed
  suffix.
- **On the disk** (the guidelines' 6.5): `COOKIES.TXT` next to `EWEB.PRG`, one line a cookie (domain,
  path, expiry, name, value and the flags, tab-separated), the values encrypted as the apps keep
  passwords (7.2: common's `secret.c`, XTEA with the key in `EWEB.KEY`, made the first time; a
  cookie is often a login; `secret.c` now takes 4,096 characters), read once, when the first page
  needs cookies; written once, at quit, in one safe save, and only when a lasting cookie changed (none
  that last: the file goes). Cookies for the session never go to the disk. A reset loses what
  changed since the start (a site asks you to log in again). Options > Forget cookies empties the
  store and removes the file at once. 3,000 cookies at most, 60 a domain (the least used go).
- **Checked:** `tests/net/cookies.txt` (30 cases worked out from the RFC, `make test`, with
  AddressSanitizer and UBSan); `tests/fuzz/fuzz_cookie.c` (Set-Cookie lines from the fuzz input,
  and the file written, read and written again the same; what it found, in `tests/fuzz/found`);
  `make itest` (set and sent back; the lasting one kept for the next run, encrypted, the session
  one not; a 303's cookie on the GET after it; another site's picture setting none).
- **The logs** (7.3): EWEB.PRG's has no page's query (what was typed into a form or a search);
  the HTTP client's (EWGET's `-l`, CRASH.LOG) has each request's path with its query cut to
  `?...`, and never a cookie, a field or a body (`make itest` sends a login with GET and POST, its
  cookie set, and looks for them in the log).

## Saving (19 Oct 2026)

File > Save page as (^S) writes the page shown as it came (the server's bytes, in its own
encoding), kept with the page for that (a ninth more memory per page kept for Back). A link's
file (Alternate-click, as Safari's Option-click, or its right-click menu) goes through the HTTP
client's sink to the disk as it comes: gathered 256 KB at a time in memory (TT RAM first), the file
open for the job and written a piece at a time (the guidelines' 6.5), so a download can be bigger
than the memory, and Esc leaves nothing behind. A file EWeb doesn't show, opened as a page, is
already in memory (up to 8 MB): EWeb asks where to save it and writes it in one safe save; the
page shown stays. The name offered is 8.3 from the address (`INDEX.HTM` for `/`, the type's
ending when the address has none); from 22 Oct 2026 the name the server or the link gives comes
first, and a link's file is asked for once its answer has begun (Downloads, below). `make itest`
and `make hatarigui` save each kind.

## Bookmarks (19 Oct 2026)

`BOOKMARK.HTM`, next to `EWEB.PRG`, is the bookmark file browsers have shared since Netscape (a
`<DT><A HREF>` for each, UTF-8), so one exported from another browser is read (folders left out,
bookmarklets and `place:` queries too) and EWeb's opens elsewhere. It is read once a start, for
the Bookmarks menu (the first twenty, the menu bar made again when they change), and written in
one safe save at each change (6.5). `about:bookmarks` lists them all, each with a Remove link that
names it by its place and a hash of its address, so a list shown before another change can't
remove the wrong one, and the list shown again takes the place of the one clicked (Back doesn't
remove it twice). `tests/browse/bmtest.c` (in `make test`) reads a Firefox export and checks
what EWeb writes reads back the same; `make hatarigui` adds, lists and removes one.

## The disk cache (19 Oct 2026)

What servers send (pages, stylesheets, pictures) is kept on the disk from one run to the next, as
HTTP caching (RFC 9111) has a browser keep it: used as it is for as long as the answer said it
stays fresh (`max-age`, else `Expires` less `Date`, else a tenth of the time since
`Last-Modified`, a day at most, less its `Age`; a year at most), then asked for again with its
`ETag` or date, a 304 keeping what is there. Not kept: `no-store`, `Vary: *`, a page that sets
a cookie (it may be one person's; a picture or stylesheet that does is kept, as browsers' own
caches keep them, since 21 Oct 2026), one with a `Refresh` header (the index keeps no headers, so
from the cache it wouldn't go on; since 22 Oct 2026), one with nothing to go by, and one bigger
than a quarter of the cache (an eighth until 21 Oct 2026). Back and Forward take a page from it
however old it is, as browsers do; Reload asks the server again (a 304 when it is unchanged).

On the disk (the guidelines' 6.5): `CACHE\EWEB.PAK` holds the answers one after the other and
`CACHE\EWEB.IDX` the index, read in one open the first time the cache is needed and written at quit.
What comes is gathered in memory and added to the pack in one write when a sixteenth of the cache
has gathered, or at quit: browsing page after page opens nothing (a reset loses only what is
fetched again). The pack is opened when a job (a page and its stylesheets, a page's pictures)
first reads from it, and closed when the job ends; its head is checked against the index the first
time a run opens it. The pack's first bytes and the index carry the same number, so an index from
before a reset doesn't read another pack; what a job added after the last quit is lost then, and
fetched again. Options > Empty the cache forgets it all, the files too.

**The pack a ring (21 Oct 2026).** Until then, at quit a pack grown past its size, or half of it
left over from answers replaced, was written again with the answers used last (three quarters of
its size): after a page of 45 pictures, whose decoded pixels are kept too, that was reading and
writing some 6 MB, and quitting took you 20 to 30 s. Now the pack is a ring of the cache's size:
what a job brings goes in one write at the ring's place (`AT`, in the index's third form, `EWDC
3`), back to its start when the write wouldn't fit, and what it lands on is forgotten (the newest
kept, not the used last). Quit writes only what has gathered and the index. Each answer in the
pack carries a tag, its key's hash and its length, checked when it is read: an index from before
a reset may name what the ring has written over since, and that is forgotten rather than shown.
An index of the older forms is read as none (its pack is written over). The same day: a pack made
new was opened for writing only, so what a job put in it and asked for again before the job ended
(a page's pictures, decoded at another size) failed to read and was forgotten; it is opened again
for reading and writing once made.

**Smarter (20 Oct 2026)**, the owner's ask: a cache that knows how current what it holds is, and
holds pictures ready to show.

- **What may be shown stale is the server's to say.** `must-revalidate`, `proxy-revalidate` and
  `no-cache` mark an answer that is never shown out of date; `immutable` (RFC 8246) one that Reload
  doesn't ask about while it is fresh (a site's versioned scripts and pictures). The marks are kept
  in the index (its second form, `EWDC 2`, a number more a line; the first is still read), and a 304
  with a `Cache-Control` of its own changes them.
- **`Vary`** keeps an answer only when it varies on what EWeb always sends the same
  (`Accept-Encoding`, `Accept`, `Accept-Language`, `User-Agent`); on anything else (a cookie) it isn't kept.
- **Cut off** (the server can't be reached, RFC 9111 4.2.4), a page kept is shown from the cache
  however old, and the info line says so: *From the cache: the server couldn't be reached, so it
  may be out of date.* Not one marked never to be shown stale, and never for a POST.
- **A POST forgets** what was kept for its address (RFC 9111 4.4), so the next look asks the
  server.
- **Pictures as the screen shows them.** Decoding is what takes the time on the Atari (a 480×320
  JPEG 3.1 s on the emulated Falcon, 800×600 PNG 15.4 s), so the pixels made for the screen are
  kept too: at each size a page shows a picture at, for each palette (the 256-colour pens, 16, mono,
  true colour), under a key made of two sums of the file, its length, the size and the palette, so
  the same picture from another address, or from the disk, finds them. Only for 64×64 and over (a
  smaller one decodes in about the time it takes to read). They are EWeb's own data in the pack,
  fresh for a year, until the ring comes round to them. The picture is fetched as before
  (fresh or asked again), so a changed picture gets new sums, and is decoded again.
- **The size** is `CACHE=` in `EWEB.INF`, Settings > General's Disk cache, MB (64 at most; 0:
  none); without it 32 MB (8 until 21 Oct 2026: a page of 45 photos and their decoded pixels took
  more than that, so its first pictures were gone by its last, and gone again on the next visit),
  2 MB on a machine without TT RAM.

**Measured** on the emulated Falcon in 256 colours (`make hatarigui`, `PICS.HTM` from the disk,
then the next run on the same disk): the five pictures took 20.7 s to decode and dither, and 0.25 s
from the cache (the small animated GIF, decoded each time, 0.16 s of it): the 800×600 PNG 15.4 s
against 0.07 s.

`tests/browse/dctest.c` (in `make test`) checks the rules on a clock of its own, a reset between a
job and quit, the ring (never bigger than the cache, the oldest written over, and after a reset
nothing written over read), a new pack read back in the same job, `Vary` and the marks; `make itest`
shows a page from it with no request, and a 304, a POST forgetting, a page shown cut off and a
`must-revalidate` one not, and the pictures read back; `make hatarigui` the pictures read back on
the next run; `make hatarifiles` counts its opens.

## Gateway mode, first step: webgate fetches (20 Oct 2026)

AERA-Gateway's `web` service, `webgate.py` (EWeb's protocol, in AERA-Gateway as the family's other
gateways are), is an HTTP proxy for EWeb on the Pi: EWeb, with the Pi gateway set (Settings > Secure:
`GATE=`, `GATEPORT=`, and Falcon mode off, `FALCON=0`; EWGET
`-g`), sends every request to it in plain HTTP in proxy form (`GET https://host/path HTTP/1.1`),
and the Pi makes the connection onwards with aeragate's rules (TLS 1.2 or 1.3, the certificate and
name checked, never back into the home network). It passes the answer back as it comes: the
server's gzip or deflate as they are (only those are asked for: less to carry over a serial line,
and EWeb unpacks them), a long answer a piece at a time (chunked when its length isn't known),
redirects, cookies and error pages as they came (EWeb follows and keeps them itself), and
`X-Gateway-TLS` saying how the Pi reached the server, which EWeb's info line shows ("TLS 1.3, ...,
through the gateway"). What it can't do is answered 502, 403 or 504 with a line starting
"webgate:", shown on EWeb's page. Port 8280. It gives the ST edition the secure web, and the Falcon
the sites that take TLS 1.3 only; direct stays the Falcon's default. Tested: webgate's own unit
tests (in AERA-Gateway), `make itest`'s gateway cases (TLS 1.3, a redirect, a big chunked answer, a
refused certificate, a gateway that isn't there, a page with its pictures, a login's POST and
cookie) and EWGET through it in Hatari (`make hatarinet`), from EWeb's copy of AERA-Gateway's
`gateway/` (`tools/sync-gateway.sh`). It is in AERA-Gateway 1.2 (`aera-gateway install web`, port
8280 assigned to EWeb). Next: snapshots (Chromium on the Pi), live view, and the ST edition.

## Gateway mode: the Pi's browser, snapshots and live view (20 Oct 2026)

webgate's `--browser` (AERA-Gateway 1.2 has it; the recipe leaves it out until Erez decides on
Chromium and Xvfb for the Pi) hands an address to an ordinary Chromium on the Pi with webgate-ext
loaded, no automation protocol: the extension long-polls webgate on 127.0.0.1:8281, opens the page,
and once it has been quiet for 800 ms (8 s at most) sends it back as plain HTML: no scripts or
handlers, each canvas as a picture, the fields as filled, a `<base>` so that EWeb fetches the
stylesheets and pictures through webgate as usual. EWeb asks for that with `X-Gateway-Snapshot: 1`
on a page's GET (View > Through the Pi's browser, `SNAPSHOT=`), not looked for in or kept in the
disk cache; the answer's `X-Gateway-Snapshot-URL` becomes the page's address, and the info line says
"a snapshot from the Pi's browser". Live view (View > Live view) is webgate's own page,
`http://webgate/live?url=...`: the browser's screen as a 608-pixel JPEG in a picture button (EWeb's
forms send its x and y), a field to type into and buttons for keys; webgate turns them into real
input with xdotool and answers 303 back to the page. The home network rule (7.3) holds for the
browser: webgate gives it only http and https addresses outside it, and the extension blocks
requests into it as far as an address shows it (the owner session's review). Tested: webgate's
unit tests with a stand-in extension and xdotool, a real Chromium in Xvfb (click, type, a script
run, "As a page"; the home network refused and blocked), and `make itest`'s two cases through
`ewrender -R` and the live page with a stand-in extension. Measured here: Chromium 597 MB on the
disk, 385 MB of memory with one simple page; Erez's Pi is probably a Pi 4.

## The ST edition, EWEBST.PRG (20 Oct 2026)

`EWEBST.PRG` is the same sources as `EWEB.PRG` built as 68000 code (`-fno-store-merging
-fdisable-tree-bswap`, as every 68000 build of the family), with common's `tls_none.c` in place of
TLS (BearSSL's 150 KB left out) and no DSP; secure sites come through AERA-Gateway's webgate, the
Pi gateway of Settings > Secure. The cross compiler's libgcc has its 64-bit multiply and divide
only as 68020 code (an illegal instruction on an ST), so `atari/ll68000.c` has 68000 ones (16-bit
multiplies, common's 32-bit routines, shift and subtract), matched against native arithmetic on a
million random pairs; they belong in common next to its 32-bit ones. TOS 1.x has no `Mxalloc`: the
memory is sized from `Malloc(-1)` there.

**It ran on a 1 MB ST up to 0.27** (the hub's rule for ST editions, 20 Oct 2026; no longer a target
since 23 Oct 2026, Decisions). TOS 1.04 there leaves a
program 940,906 bytes (919 KB, measured with a probe in Hatari); the first build needed 1015 KB to
load (590 KB of code, 23 KB of data, 401 KB of BSS). Now 655 KB (384 KB of code, 23 of data, 249 of
BSS):

| Change | Saved |
|---|---|
| TJpgDec (and jpegdc for what it won't take) in place of libjpeg-turbo | 92 KB of code |
| `-Os` | 115 KB of code |
| `EW_ST`: the CSS engine's prelude 1024 tokens, 512 hits, a 256-entry style cache, 256 values and declarations, 4 levels of `:not()`, 1024 names | 128 KB of BSS |
| `EW_ST`: the HTTP client's read buffer 8 KB (2 KB since the downloads' names, below), the stack 48 KB | 24 KB of BSS (6 KB more for the 2 KB buffer) |

At run time the arenas take small pieces on the ST (the CSS engine 4 KB, its first ones from 512
bytes, the document and the layout 8 KB, not 64 and 16), and so does the display list (128 items at first, 5.5 KB, not 1024,
45 KB; since 22 Oct 2026, Details and summary), as a context's empty pieces were most of a small
page's memory; a font face is unpacked into its exact size (in every edition: before, its buffer
doubled as it grew). A page that doesn't fit gives back the pages kept for Back, the one shown with
its source (since 24 Oct 2026: before, a page loaded again had its source twice, 130 KB more for
the stock Falcon's big page), the pictures not in use and the fonts' glyphs (read again when used),
and is built once more (and the document's and the CSS engine's arenas take a smaller piece, down
to what is asked, when their 64 KB one can't be had: since 24 Oct 2026, when the stock Falcon's big
page failed on the last free pieces of its heap, with 70 KB free in it but no 64 KB together); if it
still doesn't fit, EWeb says so on a page of its own, which is small. Running out while styling it
counts too (since 22 Oct 2026, `ec_select()` says so): before, the elements left without a style
made a page that laid out to nothing, which translucency's 3 KB more code brought about on the
pictures page. Pictures may take a sixteenth of the free RAM (12 KB on the 1 MB ST, so few come;
the photo is refused for memory).

Measured in Hatari (8 MHz, TOS 1.04), the tables page from the disk:

| Machine | Free at start | Page | Parse | Layout | Drawing |
|---|---|---|---|---|---|
| 1 MB ST, monochrome | 196 KB | 10.9 s | 1.4 s | 2.8 s | 6.2 s |
| 4 MB ST, monochrome | 2.8 MB | 11.1 s | 1.5 s | 2.9 s | 6.3 s |
| 4 MB ST, medium resolution | 2.8 MB | 6.8 s | 1.5 s | 2.9 s | 1.9 s |

On the 1 MB ST the pictures page then shows too (9.3 s), after the tables page is given back. The
`-Os` build is about 15% slower than the `-O2` one was on the 4 MB ST (9.5 s for the tables page);
the 1 MB ST comes first. Drawing in monochrome is the most of it; JPEG on 16-bit arithmetic came
on 20 Oct 2026 (Pictures, above), drawing is for later.

## The throbber (20 Oct 2026)

Erez's choice of six animations of the AERA mark's pyramid A: **Orbit**, made more of at his
asking. A star goes round the A on a tilted ellipse with a short trail, behind it at the back of its
round, lighting the A's edge as it passes in front, over a field of 22 faint stars that pulse
slowly (never brighter than the A's shadow side, so they don't take the eye). It goes round while a
page or its pictures come, a frame each 110 ms from the network's waits and the decoders' polls;
at rest it is the A alone.

`tools/mkthrobber.py` draws it at 8 times the size and puts it on the Falcon's own colours, written
as `ui/throbber.h`: 16 frames of 30 x 30, the toolbar's height (30 x 15 on the 200-line screens;
40 x 40 until the toolbar became one row, 21 Oct 2026, when black and white lost its dither and
its star field, which left no A at that size) in three editions,
TOS 4.04's 256-colour palette for 256 colours and true colour (gold, bronze and copper from its hue
wheel, an ordered dither so the still A doesn't shimmer as the star moves), its 16 system colours,
and black and white (monochrome and 4 colours). Each frame is deflated on its own (80 to 380
bytes). A frame is unpacked the first time it is shown and made into the screen's own form in ST
RAM (`scr_rgb_dev`, `ui/screen.c`), then copied from there (`vro_cpyfm`): making one took 150 ms
on an 8 MHz ST, longer than a frame lasts, and slowed a picture's decoding by as much. The 16
frames take 3.8 KB in monochrome, 30 KB in 256 colours; without the memory, each frame is drawn
as it comes, as the page is (`scr_rgb`). The
editions are 17 KB of `EWEB.PRG` and 5.5 KB of `EWEBST.PRG` (no 256-colour or 480-line 16-colour
one there; with its code, the ST edition grew by 7 KB).

**The 1 MB ST** had 188 KB free at the start with the picture cache, and showed the pictures page
blank (190.9 KB was enough before it): it is at the edge, and each addition counts. The cookie
file's buffers for encrypting and decrypting values (12 KB) are now taken only while they are read
or written, and the planes table for 256 colours (8 KB) only on such a screen: 203.6 KB free
without the throbber, about 197 KB with it, and the pictures page shows again.

**On the 4 MB ST since 23 Oct 2026** (the 1 MB ST dropped, Decisions): what the 1 MB ST couldn't
spare is the Falcon's again. The stack is 256 KB (not 48; its floor 16 KB, not 8), the stylesheet's
limits 4,096 tokens a prelude, 512 values and declarations and 8 levels of `:not()` (not 1,024, 256
and 4), the cascade's 2,048 hits and 1,024 kept styles (not 512 and 256), the names 2,048, the HTTP
client's copies 16 KB (not 2), gradients 64 stops; and `-webkit-gradient()` is the ST edition's
too. `EWEBST.PRG` now needs 1,036 KB to load (472 KB of code, 23 of data, 541 of BSS, from 703 KB),
and the 1040 ST has 2,432 KB free at the start (2,787 before); the family's accessories loaded
beside it pass (`acc_all.py ST --prg EWeb:EWEBST.PRG:EWEB.FNT`). The pages time as before (the
tables page 12.8 s in monochrome, 7.1 s in medium resolution; styling 20 ms slower on each page,
the larger cache to clear). A page 1000 deep is shown cut at the floor (4,892 pixels of it), where
the 1 MB ST couldn't show it, and six tables nested in each other lay out whole (they laid out to
nothing on the 1 MB ST). The arenas' small pieces stay: they cost 0.08% of the instructions, and a
page that takes only what it needs leaves more of the ST to the pages kept for Back and the
pictures.

## A little JavaScript: QuickJS on EWeb's own document (20 Oct 2026)

The design's part 2 (below): QuickJS (`third_party/quickjs`, 2026-06-04, one small patch for a
freestanding build) runs a page's scripts on a lean DOM in `src/js/js.c`, fresh code: the objects
scripts see are EWeb's own document nodes, made into objects only when a script first asks for one
(one object a node), so there is one tree, styled and laid out as before. Selectors (`querySelector`,
`matches`, `closest`) are EWeb's CSS engine's, `innerHTML` and `document.write` EWeb's HTML parser's
(a fragment parsed into the document's own memory and atoms). NetSurf's DOM (libdom) was not ported
first, against the rule: its whole point would be to sit under Duktape, which can't parse today's
scripts (ES5 only), and EWeb's document, built for speed, is not libdom's tree; to be weighed if
the DOM grows past "lean".

- **What scripts get:** nodes, elements and text; `getElementById`, `querySelector(All)`, the
  `getElementsBy...` (arrays, not live lists); creating, inserting, moving and removing nodes;
  attributes, `id`, `className`, `classList`, `style` (the `style` attribute's declarations) and
  `getComputedStyle` (the CSS engine's computed values); `textContent`, `innerHTML`, `outerHTML`,
  `insertAdjacentHTML`; form controls' `value`, `checked`, `selectedIndex`, `options`, a form's
  `elements`, `submit()` and `reset()`; events with capture and bubbling, `addEventListener`
  (`once`), `on...` properties and attributes (`return false` too), `preventDefault`,
  `CustomEvent`, `dispatchEvent`; `setTimeout`, `setInterval`, `requestAnimationFrame`, promises;
  `location` (and setting it), `history.pushState`, `navigator`, `document.cookie` (no HttpOnly
  cookie seen, set or replaced: RFC 6265bis), `alert`, `confirm`, `prompt`; `fetch` and
  `XMLHttpRequest` (made when a page first uses them, from a few lines of JavaScript, as are
  `localStorage` and `sessionStorage`, kept for the page only); `getBoundingClientRect` and the
  `offset...` sizes from the layout (each element's block, kept with the page for this, and the
  element of each text, picture and control: also how a click finds its element).
- **When they run:** after the page is parsed and its stylesheets have come, in document order, as
  if each were deferred; what `document.write` writes goes in after its script, and a script that
  writes is run in its turn. Then `DOMContentLoaded` and `load`, and the timers due. A script that
  changed the document has it styled and laid out again (after its event or timer, not at each
  change). Clicks, typing, ticks and choices in forms, and a form sent are events; a click's default
  (a link followed, a form sent) waits for its listeners, which may cancel it. Modules aren't run
  (no loader); their `nomodule` fallbacks are.
- **The budget** (the design: 100-200 KB, a few seconds; since 26 Oct 2026, Erez's choice, pages of
  jQuery and its friends, their scripts kept compiled: below): 1 MB of script (or of its compiled
  code) a page and 15 seconds for its scripts, `DOMContentLoaded` and `load` (the time a script
  waits for the network or for an answer to an alert not counted, nor compiling or reading its
  code), 2 seconds for each event or timer after, 8 MB of heap and 96 KB of the 128 KB stack. A page over its budget is parsed again and shown without scripts, with its
  `<noscript>` parts. An event or timer over its 2 seconds is stopped; the page stays.
- **Off by default, on per site** (the design): Options > Run this site's scripts, kept in
  `EWEB.INF` as `SCRIPTS=`, the host names. Since 24 Oct 2026 a setting too (Erez, who found
  choosing each site a chore; his choice of the default): Settings > Scripts, `RUNSCRIPTS=`,
  on no site (0), on the sites chosen (1, as it starts), on every site but those left out (2,
  `NOSCRIPTS=`, the same menu item unticked). On every site, a site whose scripts were too much
  (`JS_TOO_MUCH`) is left out until EWeb quits (the newest 16, `br_scripts_too_much`), so its
  pages aren't made twice; the menu item tries it again. A page with scripts pays for them even
  when it needs none: it shows once they have come and run, without a preview, and QuickJS's
  start (below) is 0.6-0.9 s on the DFB1 for each; so the sites chosen stays the default. A page whose site isn't chosen is parsed as before; one
  with no script and no `on...` attribute gets no runtime either (QuickJS's start costs 1.14
  million 68030 instructions).
- **`<noscript>`** is shown when scripts don't run: the HTML Standard hides it only with scripting.
  EWeb's UA sheet hid it always, so pages lost their no-script versions; now a page whose scripts
  run parses it as text and hides it, and every other page shows it.
- **Only with an FPU** (a change to the design's "a build without the FPU runs JavaScript's maths in
  software"): QuickJS is built with `-m68881`, as every JavaScript number is a double and libgcc's
  soft-float routines for the 68000 family are position-independent code that EWeb's freestanding
  link can't take. `EWEB.PRG` runs scripts when the `_FPU` cookie says there is one (the DFB1's
  68882, a TT's, a Falcon with an FPU card), and has no FPU instruction outside `src/js`, QuickJS and
  its maths, so it runs as before on a Falcon without one. A soft-float build (EWeb's own double
  routines) is possible later if Falcons without an FPU should have scripts. The maths are musl's
  (`third_party/libm`), the C library calls QuickJS needs beyond common's `atari/js_libc.c`.
  The ST edition has none of it (`js_none.c`; 1.8 KB more than before, for the layout's and forms'
  shared changes).
- **Size:** 874 KB of `EWEB.PRG`'s 1.62 MB of code: QuickJS 764 KB (the engine 667, Unicode's
  tables 55, regular expressions 26, number formatting 11, the rest 4), musl's maths 41 KB, the DOM
  and the rest of `src/js` 69 KB (the design's estimate for QuickJS was about 800 KB).

Tested by `make test` (`tests/js/run.py`: pages whose scripts check themselves and whose results
are looked for in the display list, through ewrender `-J` with clicks `-K` and timers `-T`, under
AddressSanitizer with QuickJS's own assertions on, which find any object EWeb leaks), `make itest`
(a script from a file, `fetch`, `XMLHttpRequest` and `document.cookie` against the test server, an
HttpOnly cookie kept from them) and `make hatarigui`'s `scripts` group (EWEB.PRG on the emulated
Falcon with its 68882: tabs a script makes, one clicked, an `onclick` alert answered). There the
tabs page's scripts (329 bytes) took 740 ms on the emulated 32 MHz Falcon: most of it the runtime
QuickJS makes for each page with scripts and the prelude it compiles, the cost every such page
pays. Counted (`build/m68k/jsbench`, `tools/count68k.sh`, `tools/prof68k.py`): a page with an empty
handler costs 4.3 million 68030 instructions (about 0.6-0.9 s on the DFB1), the test page `basics.html`
5.1 million; the runtime's start and end are most of it, spread over QuickJS's atoms and properties
(each 3-6%) as it builds JavaScript's standard objects, not the prelude. The saving to try next is
one runtime and context kept for the pages of a site, or made once and copied.

## The stack (21 Oct 2026)

EWeb bombed on Erez's Falcon on Wikipedia's Falcon article, through the gateway and in Falcon mode,
while pictures came or the page scrolled: 2, 3, 4 or 11 bombs, different each time, and only a cold
boot brought the machine back. Measured in Hatari on TOS 4.04: the stack (crt0's, in the BSS, right
after the program's data) held 128 KB, and the layout took about 940 bytes of it for each level a
page is nested, as the box tree is laid out recursively (tables and flex boxes more, measured again
at each level). A page 100 blocks deep took 114 KB; 150 never showed. Nothing stopped the stack at
its end: it went on into the program's data, and what bombed afterwards depended on what it had
overwritten. A page laid out again when a picture arrives sits a little deeper still.

What EWeb does now:
- **A floor.** `ec_stack_floor` (`src/css/ecss.h`), set by `main()` 16 KB above the stack's end (8 KB
  on the ST): room for the deepest call below a recursion, worked out from GCC's call graph
  (`-fcallgraph-info=su`): 1.5 KB under the layout, and 5 KB under it for a picture's address. Every
  walk that follows a page's depth checks it (`EC_DEEP()`): the layout (blocks, tables, flex, grid,
  floats, inline content, measuring, the box fix-up), the parser's clone, the forms' text, the
  scripts' serialising, cloning and text, the stylesheet's nested `:is()` and `@supports`. What is
  deeper is left out; the log says `deep`, the status line "Part of this page is nested too deeply to
  show." The cascade stops 14 KB above the floor, as more goes under each element it styles (12.5 KB:
  `cascade_style`, a `var()` or `grid-template-areas` value) while its own recursion takes 176 bytes
  a level; the rest of the subtree takes the deepest element's style. The document's own walks
  (`finish`, the sheets, anchors) go without recursion.
- **A larger stack for `EWEB.PRG`:** 256 KB. The ST edition kept 48 KB while the 1 MB ST had no more
  to give (64 KB, or the big buffers made static, which take the same memory for good, left it
  unable to show a page); 256 KB too since 23 Oct 2026 (The ST edition, above). `grid_layout` is kept out of `layout_block` (it was inlined, and every level paid
  for its locals): 736 bytes a level instead of 944.

Measured again on the emulated Falcon: 25 blocks deep 37 KB, 100 deep 91 KB, 150 deep 127 KB, 300 deep
234 KB, all whole; 1000 deep cut at 247 KB, the 16 KB under the floor untouched; 40 nested tables and
40 nested flex boxes whole. On the 1 MB ST: 25 deep whole (37 KB), 40 deep cut at 42 KB; deeper pages
need more memory for their styles than the ST has, and EWeb says it can't show them. The ST edition
before this went to 49,149 bytes of its 49,152 on a page 60 deep, and a page 300 deep never showed.
`make test` runs `tests/layout/deep.py` (each kind 3000 deep, cut, under AddressSanitizer with
ewrender `-S`, and 60 deep, whole). The crash catcher (`common/atari/crash.c`) now also takes a fault
in the program's own supervisor code, a wild jump while it runs and line F, so a crash leaves
`CRASH.LOG` even when only a cold boot recovers the machine.

## Refresh: `<meta http-equiv=refresh>` and the `Refresh` header (22 Oct 2026)

A page that sends you on with a `<meta http-equiv=refresh>` or the `Refresh` header went nowhere:
EWeb read neither. Pages that have moved do it, and DuckDuckGo's HTML results lead to the site
through a page of theirs (`/l/?uddg=`) believed to forward this way (not yet seen from here: the
audit couldn't fetch one), so a result clicked would show a blank page. Now EWeb does as the HTML
Standard says (4.2.5.3, the Refresh state and the shared declarative refresh steps):
- **Which:** the page's `Refresh` header first (the document is made with it), else the first
  `<meta http-equiv=refresh>` the parser puts in whose content is one. The standard runs its steps
  as each `<meta>` is inserted, so the parser notes each such meta then (`src/doc/html.c`, the
  in-head rules every `<meta>` goes through; `doc.refresh`, at most 16): its content as it is then
  (a script changing it after changes nothing), whether a `<base href>` was in by then, and in the
  order they went in, which a table's foster parenting can make other than the tree's. No element
  pays for it: one in a `<template>` isn't in the document, one in `<noscript>` is there only when
  the site's scripts don't run (the parser keeps `<noscript>` as text when they do), and
  innerHTML's don't count. The content is parsed step by step as the standard does it
  (`doc_refresh_parse`: the time, its fraction ignored, `;` or `,`, `url=` with or without quotes);
  the address is resolved against the `<base>` if one was in when the meta went in (one after it
  doesn't count). One that is `javascript:`, or doesn't parse, is passed over for the next.
- **When:** the standard's "completely loaded", the page with its pictures, for a refresh of the
  page itself or a place on it, as Chrome does: a page that reloads itself every ten seconds shows
  its pictures each time, however slowly they come (from 22 Oct 2026, review: the countdown began
  when the page was shown, and a picture slower than it was cut short, and fetched again from its
  start, each time). A refresh to another page counts from when the page is shown, without waiting
  for the pictures of the page being left: one that goes on at once fetches none, and one that goes
  on in five seconds goes even while its pictures still come (the one coming is cut short). The GEM
  side's idle loop (both editions) wakes for it as for a script's timer.
- **How:** in the page's place in the history (the standard's "replace"), so Back goes to the page
  before it, not to one that sends you on again. The page itself is fetched anew (`NAV_REFRESH`:
  its stylesheets as any page's, not again as Reload asks for them; Chrome validates the page
  only), which a page that refreshes itself is for; a place on it (`url=#top`, or no address on a
  page shown at a fragment) is scrolled to, the page staying, as the standard and Chrome do (until
  the review it was loaded again, five times, and then stopped as a loop). Back and Forward don't
  start a page's refresh again, as the standard allows ("do nothing"); Reload does. An address
  EWeb doesn't open on its own (`mailto:`, `data:`, a file from a page on the web) leaves the page.
- **Its pictures, as they are now:** a page fetched anew for its refresh asks for its pictures
  again as their HTTP freshness says, as browsers do for a document made anew (`img_recheck`, from
  the layout of a `NAV_REFRESH` page): one sent `no-store`, or out of date, comes again from the
  server (a 304 when it is unchanged), one still fresh is read from the disk cache, and a file from
  the disk again; meanwhile the picture EWeb has is shown, and one that came the same isn't decoded
  again. A picture opened on its own with a `Refresh` header (a camera's JPEG) is the page itself,
  and changes with it. Until the review's second round (22 Oct 2026) `img.c` kept each picture by
  its address, so a camera's picture or a weather map stayed the first one for ever while the page
  around it came again. Reload (^R) still shows the pictures EWeb has, as before: asking for them
  all again would read every picture of the page again from the disk cache or the network.
- **What you see:** "Going on to example.com in 5 s: Esc stays here." (or "This page loads again in
  30 min: Esc stops it.") on the status line, with the time left, whenever nothing else is said
  there (a picture's line, a button's or a link's name), so it comes back after them; Esc, Stop
  (enabled meanwhile) or any other load cancel it, and EWeb says "Staying on this page." Stop then
  stops only that, not the requests of the page's scripts. While the address field has the keyboard
  it waits, so that what is typed there isn't lost. A page that goes on at once, again and again
  (itself, or two that send each other on), stops after five in a row (`BR_REFRESH_RUN`): "This page
  sends EWeb on at once again and again: it stays here." Browsers go on for ever. That is what the
  status line says at rest until the next load, so it comes back after the pictures of the page it
  stops on (until the review's second round it was said once, and the pictures' lines took its
  place: two pages sending each other on fetch no pictures until the last).
- **The caches:** a page with a `Refresh` header isn't kept, in memory or on the disk, as the header
  wouldn't come with it. Through the Pi webgate passes the header on as the server sent it; the Pi's
  browser (snapshots, live view) follows a refresh itself.

Measured: 68030 instructions for EWeb's document (`build/m68k/docparse`) on the page set, 1,328.0 M
before and 1,328.3 M after (+0.02%; the median page +912 instructions, the most +4,942: each
`<meta>`'s attributes looked at for `http-equiv`). Noting the meta in `doc.c`'s walk after the parse
instead cost +0.12%, a test on every element; that is why the parser does it. The code that works
out the refresh after the page is built is kept out of `load_post` (`noinline`): inlined, its 8 KB
of addresses sat on the stack under the whole load, and the ST edition's 48 KB ran out under a
stylesheet fetched through the gateway (3 bombs, `make hatarigui`'s `st` group); `load_post`'s frame
is as before, 4,252 bytes on the ST.
`tests/browse/rftest.c` (`make test`) has the Web Platform Tests' 73 parsing cases, each address
resolved as the test does, and which `<meta>`s count; `make itest` the pages of `/site/refresh/`
(the header before the meta, a `<base>` before and after, `<noscript>` with scripts and without, a
`javascript:` address, a script changing the content, a table's order, the loop, a page refreshing
itself with its stylesheet asked for once and after its slow picture, a place on the page, Back,
the disk cache, the header through webgate, and a camera's page whose pictures are asked for again:
no-store each time, one kept an hour once, a no-cache one a 304); `make hatarigui`'s `refresh` group
the status line (again after the pictures), the timer, Undo, Esc, the address field and the loops
(itself, its picture read again but not decoded again; two pages sending each other on) on the
Falcon. The status line is read from what `win_status` logs at each change (`status ...`), not
worked out from the browser's state.

## Stylesheets by their media, type and set (22 Oct 2026)

A `<link rel=stylesheet>`'s or a `<style>`'s `media` was looked at only as text: taken when
"screen" or "all" appeared anywhere in it, in lower case, so `media="screen and (max-width: 600px)"`
was applied at any width, `(min-width: 40em)` never, `not screen` and `small` always, `SCREEN` never.
Now `doc_sheets` (`src/doc/doc.c`) does as the HTML Standard says (4.2.4, 4.2.6) and CSSOM's
"add a CSS style sheet":
- **Media:** the attribute "matches the environment" by the CSS engine's own media queries
  (`ec_media_ok`, Media Queries 4: types, `not` and `only`, features, ranges, three-valued logic),
  against the very screen `@media` blocks see (the window's width in CSS pixels, 16-bit colour,
  and so on); none, empty or white space is all. `screen`, `all` and `print`, nine in ten of the
  page set's 673 media attributes, are told without the engine's tokens.
- **Type:** a `<style>`'s none, empty or `text/css` in any case; a `<link>`'s none, empty (as
  browsers take it) or a MIME type whose essence is `text/css` (`Text/CSS; charset=utf-8` too).
  Others aren't CSS: not read, not fetched.
- **Not applied:** `rel="alternate stylesheet"` (only when chosen, and EWeb has no menu to choose
  with), a `<link disabled>`, one whose `href` is empty (it would fetch the page itself), and a sheet
  of another set: the first sheet with a `title` that isn't an alternative names the preferred set,
  and a later one with another title is left out, as browsers do. An alternative whose title is the
  preferred set's is applied (CSSOM's "enable a CSS style sheet set"; Gecko and Blink do so), though
  the set be named by a sheet after it: then the rest of the page is looked through for the name,
  which only a page with an alternative before its first titled sheet costs. One with no title is
  never applied, as in Gecko. None on the page set is left out by its set.
- **Where:** a `<link rel=stylesheet>` in the body applies as one in the head, in document order.
- **What isn't fetched:** a sheet whose media doesn't hold isn't fetched at all, whether it never
  can (print, speech, `not all`) or holds only at another width (the standard lets a browser fetch
  only the sheets it applies). On the page set 11 of the 673 go the other way at 760 CSS pixels
  (`ewrender`'s width), all linked: sheets for screens 768 px wide and more, or 480 px and less.
- **The window sized:** media queries are evaluated again when the screen changes (Media Queries 4,
  2), so a sheet not fetched for its width is fetched when the window comes to one it holds at. As
  the CSS engine evaluates media queries (a sheet's own and every `@media` in the sheets) it keeps
  the widths at which they would all go the way they went (`ec_media_span`: each comparison with
  the width narrows them; an aspect ratio or `vw` in a query keeps only that width). A resize
  within them (most of them: a page's breakpoints are few) lays the same styles out again, its
  `<source media>` and `sizes` at the new width (`ec_set_media`). One outside them adds the page's
  sheets again (`ec_drop_sheets`, then `doc_sheets` for the new width: those fetched before come
  from the cache, those that hold now for the first time over the network, without a word on the
  status line), styles the page and lays it out: the document, its form fields and its scripts
  stay. A window sized while a request is under way (GEM's messages come as it waits) fetches no
  sheet inside it: inside a picture's the page is laid out with the styles it has, inside a sheet's
  (the sheets being added again) not at all, and the rest comes after it, as for a picture's size.
  So a page opened narrow gets its sheets for 768 px and up when the window is fulled.
- **Memory:** the sheets taken out give theirs back. The CSS engine keeps a page's sheets (their
  text, rules and declarations) in an arena of their own, which `ec_drop_sheets` frees, and what a
  style attribute or a `var()` makes as the page is styled goes with the styles; so the sheets
  added again take what they took, however often the window crosses a breakpoint or a script
  changes them. Before, each time took about 2.4 to 2.7 times the sheets' text again, kept until
  the page went: Bootstrap 5.3 (233 KB) took 676 KB on a Falcon and 548 KB more at each crossing,
  20 KB of the news page's sheet 59 KB on an ST and 52 KB more, so a page that fitted a 1 MB ST
  lost its styles after a few crossings; now 676 KB and 59 KB each time. A context's third arena,
  in the ST edition's 4 KB pieces, took a small page 4 KB more, and the 1 MB ST's rig no longer
  fitted the pictures page with the tables page still shown (`make hatarigui`'s `st`: its styles
  ran out of memory, and it came out empty); so there an arena's first pieces are smaller, from
  512 bytes, each as large as those before it together: a page with a small `<style>` takes 1 KB
  for its names and sheet where it took 4 KB, the UA sheet 38 KB where it took 40 KB. On the
  Falcon they stay 64 KB: the pieces more cost +0.08% of the page set's instructions. When the
  sheets don't fit even so (the engine says so: `ec_sheets_short`), the pages kept for Back go and
  they are added once more, as for a page that doesn't fit. The log has a line each time the sheets
  are added again, and why.
- **Scripts:** with scripts run, each stylesheet `<link>`'s `load` event comes after
  `DOMContentLoaded` (and one preloaded as a style), when a handler may hear it, so the async-CSS
  pattern works: `media=print onload="this.media='all'"` and `rel=preload as=style
  onload="this.rel='stylesheet'"` (the Filament Group's loadCSS, WP Rocket, Autoptimize, Jetpack
  Boost), whose `<noscript>` copy is text when scripts run. A print sheet isn't fetched until a
  handler makes it hold, so its `load` comes as if it had been. When a script changed the page's
  sheets (`doc_sheets_key`: the elements that are sheets, their attributes and a `<style>`'s
  text), they are added again before the page is styled, and after a click or a timer too (a theme
  a button turns on by `link.disabled = false`). Only the sheets count: a script that adds a
  preconnect, a DNS prefetch or a preload of a script, or gives the icon another address, leaves
  them as they are (each time they are added again they are all parsed again: Bootstrap's 233 KB
  are about 6.9 M instructions on a 68030, some 1.3 s on the DFB1, so a script that rewrites a
  `<style>` on a timer costs that each time, but no more memory). `media` is a property of
  elements now, as `rel` was.

Measured: 68030 instructions for EWeb's document styled with its `<style>`s
(`build/m68k/docparse -s`) on the page set, 1,575.7 M before and 1,575.1 M after (-0.04%; the
median page -2,464, the most +5,896: two media queries the engine evaluates). Looking each
attribute up on its own (`dn_attr`, six looks a `<link>`) cost +0.13% (+7,004 the median page),
more than the media queries; the sheet's attributes are now found in one pass. With the widths
kept, the alternatives' set and the scripts' key: 1,575.4 M (+0.02%, the median page +86; pages
with no media query or title move by up to 0.1 M either way as the document's memory lies
otherwise). With the sheets' memory an arena of their own, measured again in one run: 1,575.2 M
before, 1,575.8 M after (+0.04%, the median page +2.7 K, every page within 55 K); the media
attribute's text test, in that run, 1,575.9 M (so -0.01% in all). `make test` has
`tests/layout/media.html` (print, a wide and a narrow screen, `em`, `SCREEN`, `not screen`,
`small`, an empty media, a list, orientation and colour, alternate (of another set, of the
preferred one named after it, untitled), disabled, types, two sets, one linked in the body),
`tests/layout/resize.html` (built at 760 CSS px, the window made 500, 760 and 500: the list as a
page built at 500 has it), `tests/css/cases.txt`'s widths (`@span`, `@width`) and memory (`@again`:
the sheets, a style attribute and `var()` added again 50 times take no more),
`tests/js/sheets.html` (the async-CSS pattern, a load listener, a print sheet's load, a theme a
click turns on, a `<style>` a script adds) and `tests/js/links.html` (a preconnect, a DNS prefetch,
a preload of a script and an icon a script adds or changes, at once, after a click and after a
timer: the sheets aren't added again); `make itest` `/site/media/page.html` at two widths, and at
608 px sized to 400 and back, and `/site/media/pics.html` sized while its picture comes
(`ewrender -Y`), which sheets the server was asked for; `make hatarigui`'s `st` the 1 MB ST's
pictures page with the tables page still shown.

## Encodings: pages that name none, and a choice by hand (22 Oct 2026)

A page that named no encoding (no BOM, no charset in Content-Type, no `<meta>` in its first 1,024
bytes) was read as Windows-1252, so an unlabelled UTF-8 page showed `Ã©` for `é` and Hebrew as
Latin letters, and so did a text file; a text file on the disk was read as a page (its `<` a tag);
nothing let the user say better; and a link's query was sent in UTF-8 whatever the page's encoding.
Now EWeb does as the HTML Standard and the URL Standard say:
- **Labels** (`enc_of`, `html_encoding`): the Encoding Standard's. libparserutils' list of names
  lacks some for the encodings it decodes (`cp1255`, `x-cp1255`, `iso-8859-8-e`, `csiso88598e`,
  `l9`, the `x-cp125N`s...), has ISO-8859-1 and -9 as encodings of their own and `utf-16` as
  big-endian, so a table of EWeb's is looked at first (by its first letter, then the name): every
  label of the encodings EWeb decodes comes to the standard's encoding (`tests/browse/enctest.c`
  has all 160), `utf-16` and its kin to UTF-16LE, UTF-8 in a `<meta>`. A page whose `<meta>` or
  HTTP answer said `cp1255`, and a form's `accept-charset="x-cp1255"`, were taken as naming none.
- **Sniffing** (13.2.3.2, `src/doc/html.c`): a BOM, then the encoding chosen by hand, then the
  HTTP answer's, then the prescan (all certain but the prescan's, as before); then, when nothing
  named one, UTF-8 if the bytes are (step 8, whose note singles UTF-8 out), else the locale's
  default (step 9), both tentative, so a `<meta>` further on still says. The detection is the UTF-8
  decoder itself: an unlabelled page is read as UTF-8, and the first byte UTF-8 can't have stops
  it and the page is read again in the default (in a Windows-1255 or -1252 page mostly its first
  letter beyond ASCII, so little is read twice). A page that is ASCII throughout is the same text in
  either and takes the default's name, which its forms and links use. A sequence cut short at the
  end counts as UTF-8 when one came before it (more may have been coming). A first try scanned
  the bytes before decoding them: 11 instructions a long on the 68030, +2.75 an ASCII byte.
- **The default** (`br_fallback`, `src/browse`): the standard lets it come from the address, as
  Firefox does from the top-level domain: Windows-1255 for Israel's `.il` (the owner's sites and
  the older Israeli ones that send no charset), Windows-1252 for the rest and for files (a file has
  no address to tell by). Since 24 Oct 2026, with `EWEB.FNT`'s Greek and Cyrillic, Firefox's for
  those domains too: Windows-1251 for .ru, .ua, .by, .bg, .rs, .mk and the like, ISO-8859-7 for
  .gr and .cy; Arabic's would show letters `EWEB.FNT` hasn't got.
- **Text files** (`text/plain`, and `.TXT` on the disk, which was read as a page) follow the same
  steps but the prescan, and are made as the standard's page load processing model for text files
  says: a `<pre>` and a line feed it drops (so the file's own first stays), then the PLAINTEXT
  state, all the rest text (`html_opts.plaintext`), where EWeb used to escape the file into a page
  of its own and read that in Windows-1252.
- **By hand** (View > Encoding..., `br_encoding`): the page shown read again from its source as it
  came (`b->src`, kept for Save page), its stylesheets from the cache of any age, nothing fetched.
  The choice is the standard's step 2, after the BOM and before the HTTP answer's; it stays with the
  page (a byte of its history entry, and the HTTP answer's charset while it is kept: Reload, Back
  and Forward to it, a refresh of itself, a place on it) until another page is loaded; Auto goes
  back to the page's own. TOS's AES has no submenus, so
  the item opens a popup at the mouse (the shared `popup()`, Esc closes it with nothing chosen),
  Auto first with what it found, the choice ticked. The list (`br_encodings`) is those EWeb decodes
  whose letters `EWEB.FNT` has: UTF-8, Windows-1252 (ISO-8859-1 is its label in the Encoding
  Standard, so one item for both; likewise ISO-8859-9 is Windows-1254's), ISO-8859-15,
  Windows-1255, ISO-8859-8-I, ISO-8859-8, Windows-1250, ISO-8859-2, Windows-1254, Windows-1257,
  ISO-8859-13, ISO-8859-4, ISO-8859-10 and ISO-8859-3 (the visual one laid out as it is stored:
  Hebrew, above). Greek, Cyrillic, Arabic and Thai would show as boxes whatever the encoding, so
  they aren't offered. The ticked item's tick stands in the first of the two spaces the others
  start with, as the shared popup draws it (one cell to the left of them at first).
- **Addresses in the page's encoding** (the URL Standard's query state, "percent-encode after
  encoding"; HTML's "encoding-parsing a URL"): a link's, an `<img>`'s, a `<link>`ed stylesheet's, a
  `<base>`'s, a form's action, a refresh's and a script's address (`<script src>`,
  XMLHttpRequest, `location`) put its query in the page's encoding, a character the encoding lacks
  as `&#N;` percent-encoded (`%26%23N%3B`); U+FFFD too (`%26%2365533%3B`: the table holds it for
  each byte an encoding leaves out, so it isn't looked for there). The path and the fragment stay
  UTF-8, and so do CSS's `url()`s, in a sheet, a `<style>` or a `style` attribute alike (CSS parses
  them without an encoding; the layout tells the picture callback which it has, as the last two
  reach it still relative), and `fetch()`'s (the Fetch Standard's Request parses its URL in UTF-8;
  `__ew_fetch` is told which asks).
  `url_parse_enc` (`src/net/url.c`) takes the page's encoding as a table of its bytes 0x80-0xFF
  (`html_enc_table`, from libparserutils' codec, made once for each encoding met); a page in
  UTF-8 or UTF-16 sends UTF-8, as the standard's output encoding says.
- **The single-byte decoders** (libparserutils' codecs, `third_party/netsurf/patches`, 0005) are
  the Encoding Standard's indexes, which browsers decode with. They had U+FFFD for the ISO-8859
  encodings' bytes 0x80-0x9F (the indexes' C1 controls), the Windows code pages' unassigned bytes
  there (C1 controls too) and Windows-1255's 0xCA (U+05BA, a Hebrew point, drawn as nothing as
  the others are): in a page's text, and in its links' and forms' queries, which sent `&#65533;`
  where browsers send the byte. A C1 control is drawn as `?`, as any character `EWEB.FNT` hasn't
  got (CSS Text has it visible). Only the bytes the indexes leave out are U+FFFD now.
- **Forms** pick their encoding as the standard's "pick an encoding for the form" says: the first
  of `accept-charset`'s labels (split at white space) that names an encoding EWeb has, none naming
  one meaning UTF-8 (EWeb looked for "utf-8" anywhere in it, and kept the page's otherwise), else
  the page's; `_charset_` is the encoding's name as the standard has it. U+FFFD goes as `&#65533;`,
  as in a query.

Measured: 68030 instructions for EWeb's document (`build/m68k/docparse`, which gives no HTTP
charset, so every page is sniffed) on the page set, 1,328.3 M before and 1,279.6 M after (-3.7%;
the median page 4.57 M, now 4.36 M). The 187 pages whose prescan finds their encoding are as they
were (+0.01% the median, at most +0.2%, in the CSS engine's functions: the document's memory lies
otherwise; at first +0.67%: the text file's `<pre>` sent through `dispatch()` took it out of the
parse's loop, a call a token, and now goes to the insertion mode itself). Of the 71 it doesn't,
the 23 in ASCII are 11.0% quicker (the median), read by the UTF-8 decoder, which copies ASCII four
bytes at a time, where the Windows-1252 table first counted every byte; 42 with UTF-8 in them are
9.1% to 24.6% quicker (their `<meta>` past the prescan says UTF-8, which they are now read as from
the start, where they were parsed again); and six are 11.6% to 17.9% slower: UTF-8 whose late
`<meta>` says ISO-8859-1, so they are parsed twice, as the standard says (and end in Windows-1252,
as before). The review's fixes (the labels' table, looked at by its first letter) leave the page set
at 1,278.9 M, from 1,279.4 M for the same commit's binary counted the same way (-0.04%; each
page within 0.2%).

The ST edition is 4.4 KB larger (its code, the history's choice, the menu's list, the labels), and the 1
MB ST has 3.5 KB less free at start (185,754 bytes, from 189,282). That is close to its edge: a
first version, with the popup's labels kept in memory (1.6 KB) and the decoder called from two
places, left 184,704 and no longer fitted `make hatarigui`'s pictures page with the tables page
shown; so the labels are on the stack while the popup is up, the table for a page's queries is
made only when a page in a legacy encoding comes, and the decoder is called from one place. On the web most pages come with a charset in Content-Type,
which none of this touches. `make test` has `tests/browse/enctest.c` (the sniffing's steps in each
order that matters, UTF-8 at every place of a run of ASCII from even and odd addresses, overlong
forms, surrogates, a sequence cut short; text files; `br_fallback`'s addresses; every label of the
encodings EWeb decodes; View > Encoding's list; each single-byte decoder against the Encoding
Standard's index; the Web Platform Tests' `url/resources/percent-encoding.json` for UTF-8,
Windows-1252 and ISO-8859-2, Hebrew's queries, U+FFFD and bytes that aren't UTF-8; the detection
against a plain validator on 20,000 random pages), `tests/forms/cases.txt` (`accept-charset`, its
labels, U+FFFD, U+05BA and a C1 control in Windows-1255), `tests/layout/run.py` (the Hebrew page the
same in six encodings, two with no label: UTF-8 found, Windows-1255 chosen by hand; visual Hebrew
drawn as the same text in logical order; a text file); `make itest` `/site/enc/` (UTF-8 unlabelled;
Windows-1255 unlabelled as `localhost`, as `hebrew.il` through `EW_HOSTS`, the host build's name
table, and chosen by hand, kept for Back; its link's query and its form in Windows-1255; a text
file; the queries a page's pictures (one with 0xCA and 0x81), sheet, CSS `url()`s, `fetch()` and
XMLHttpRequest send, as the server got them); `make hatarigui`'s `browse` View > Encoding... on the
emulated Falcon; `make fuzz`'s `fuzz_html` (text files, the default) and `fuzz_url` (a query in an
encoding of a byte a character).

## Downloads: their names, attachments and gzip files (22 Oct 2026)

A file was named from its address alone and handled by its type alone: `download.php?id=123` was
offered as `DOWNLOAD.HTM` (`.PHP` a page's ending), a GitHub release's file (which redirects to a
storage address that isn't its name) lost its name, a text file its server says to save
(`Content-Disposition: attachment`) was shown, `<a download>` was a plain link, and a `.tar.gz`
served as Apache serves one (`Content-Type: application/x-gzip`, `Content-Encoding: x-gzip`) was
unpacked and saved as a tar under its `.GZ` name. Now:
- **Content-Disposition** (RFC 6266, with RFC 8187's `filename*`; `http_disposition` in
  `src/net/http.c`): read after 4.1's grammar, with spaces wherever its implied LWS lets them;
  `filename*` before `filename` (4.3), in UTF-8 or ISO-8859-1 (what RFC 8187 asks a recipient to
  know); `filename` read as ISO-8859-1. A header that isn't RFC 6266's (a quoted type, a value with
  a space or a comma in it or text after its quotes, a parameter twice, no type) is ignored whole,
  as tc2231 (the test suite RFC 6266 was written with) has it; a `filename*` that isn't RFC 8187's
  (no charset or another one, quoted, a bad `%`, bytes that aren't UTF-8) is passed over for
  `filename`, so the header still says attachment. An empty parameter (`attachment;
  filename=a.zip;`, common from PHP) is passed over, as browsers take it: tc2231 and the strict
  grammar would throw such a header away, and nothing in it can be misread. `attachment`, or a
  type EWeb doesn't know (4.2), on a 2xx answer saves the file whatever its type; on an error's
  answer (a 403) the page is shown, as tc2231's `attonly403` has it. The name counts with `inline`
  too, when the type is one EWeb saves. An answer whose header says attachment, or names a file
  EWeb saves (`inline; filename="Manual.pdf"`), isn't kept in the memory or disk cache, which keep
  no headers (as one with a Refresh header isn't): from there it would lose the name or the
  attachment. A page or text file that says only `inline`, or names itself (a name never used), is
  kept as without the header; pictures and stylesheets with one (CDNs send `inline; filename=`) are
  kept as before.
- **`<a download>`** (the HTML Standard, 4.6.5, "download the hyperlink"): a link with the
  attribute to the page's own origin is saved, not followed, through the HTTP client's sink as
  Alternate-click saves one (so it may be bigger than the memory), however it is activated: a
  click, or a script's `a.click()` (common for `a.download = name; a.click()`: `src/js` keeps the
  link's attribute with the address it sends the browser to, `js_navigation_download`, and
  `src/browse` saves it as a click on the link). A page whose script clicks one as it loads ("your
  download starts now") is shown first, then its file asked for: a load can come to `BR_SAVE`
  (or `BR_DOWNLOAD`, a mail address, a stop) after showing a page on the way, and `br.shows`, which
  counts the pages shown, tells the front end to draw it. One to another origin is
  followed as a link, as Chrome and Firefox do (its server may still say attachment). The name is
  the standard's "suggested filename": Content-Disposition's when it says attachment, else the
  attribute's when the answer (after redirects) is from the page's origin, else
  Content-Disposition's inline one, else the address's. The layout keeps each link's attribute
  after its href in `page.links` (`page_link_download`): one more attribute looked at for each
  `<a href>`, nothing for other elements.
- **When the name is asked:** a link's file (Alternate-click, the link's menu, `<a download>`) has
  its name only once its server answers, so the file selector now comes when the answer's headers
  are in and its body not yet (`HTTPREQ.head`, called by the HTTP client for a 2xx answer). The
  connection waits meanwhile (STinG holds what comes, and the server waits for its window), and
  the file is made only then: an answer that is an error leaves no empty file behind (until now
  the file was made first, and removed), and Cancel ends the request (`BRS_CANCELLED`, nothing
  said). A server waits only so long: nginx's `send_timeout` and Apache's `Timeout` end a
  connection that takes nothing for 60 s, so a file bigger than STinG's window was lost when the
  selector stayed up longer (choosing a folder, a floppy). So when the connection is lost once the
  selector has been up, the file is asked for once more, into the file chosen, with no selector;
  a link that gives its file only once answers that with an error, which is said. `make itest`
  holds a 4 MB file's selector for 3 s against a server that gives up after a second
  (`ewrender -Z`).
- **The TOS name** (`br_file_name`): of a name given, its last part only (RFC 6266, 4.3: no path,
  no `..\`, no drive), its base cut to 8 and its ending to 3, in capitals, with only what every
  GEMDOS takes (A to Z, 0 to 9, `_`, `-`), as Save page as names a page; a page's ending (`.html`,
  `.php`) is `.HTM` when it is a page, any other is kept (`DATA.PHP` for an octet-stream so named);
  a name with no ending takes the type's (`README.TXT` for text, `LICENSE` for an octet-stream);
  `.tar.gz` is `.TGZ`; a name with nothing a TOS name can hold (a Hebrew one) gives its ending to
  the address's base (`GET.ZIP`). From an address, a script's ending on what isn't a page gives
  way to the type's (`DOWNLOAD.ZIP` for a ZIP from `download.php`, `DOWNLOAD.PHP` before).
- **gzip files**, as browsers save them (Firefox keeps the encoding of a gzip type, and of a
  download whose name ends in `.gz`): an answer with `Content-Encoding: gzip` (`x-gzip` too) that
  is a gzip file (`application/gzip`, `application/x-gzip`, `application/x-tar+gzip`, or a name,
  Content-Disposition's else the address's, ending in `.gz` or `.tgz`) and is to be saved
  (attachment, or a type EWeb doesn't show) is kept as it came: the page's `HTTPREQ.head` sets
  `rs->packed`, and the HTTP client doesn't unpack it. A page sent packed whose name ends in `.gz`
  (`text/html`) is unpacked and shown, as before. A link's file streamed to the disk asks for
  nothing packed, and what a server packs anyway was always saved as it came.

Not done: a file EWeb doesn't show, opened by a plain click, still comes into memory (up to 8 MB)
before the file selector; streaming it to the disk too is the audit's other download item. A
`data:` link with `download` isn't saved (EWeb opens no `data:` page).

Measured: nothing here runs on a hot path but the layout's look at a link's attributes, which now
finds `href` and `download` in one pass where it looked for `href` twice; there is no 68030 count of
the layout, so it was counted on this computer (callgrind, `layout_page` alone, 20 pages of the page
set: the ten with the most links, 591 to 474, and ten about the median's 149): 223.77 M x86
instructions before, 223.21 M after (-0.25%; each page -0.07% to -0.37%). A first version that
looked for `download` on its own cost +0.30% (about 100 instructions a link). The ST edition's
`load_post` frame is 4 bytes larger (4,272), `fetch`'s 4 (5,636); the hook that asks where a link's
file goes takes 272 bytes, under a request. Its code and the browser's name for a file made
`EWEBST.PRG` 4.3 KB larger, and the 1 MB ST, with 182,538 bytes free at start, no longer fitted
`make hatarigui`'s pictures page with the tables page shown (the edge the encodings found, above).
So the HTTP client's read buffer is 2 KB on the ST edition, from 8 KB (16 KB on the Falcon): it
sizes only a sink's pieces and each copy into memory, since what comes waits in the connection's own
buffer, so a page or a file comes in more, smaller copies (some two thousand instructions more for a
50 KB page, which the network's pace hides). The 1 MB ST now has 186,888 bytes free at start, and
the `st` group passes.

`tests/browse/dltest.c` (`make test`) has RFC 6266's four examples and tc2231's cases by their names
there (inline, attachment, the extra parameters, the extension types, the character sets, RFC 2231's
continuations, the fallback, RFC 2047), and a few more (spaces and tabs, a charset other than the
two, a language, a value cut short, a NUL encoded, a name cut to its room at a character), and the
TOS names (each after a call that leaves an ending on the stack, which a name with none of its own
mustn't take); `nettest` the hook (a gzip file kept packed, unpacked when not asked, a request
stopped there and its connection gone, none for a 404, with a sink); `tests/fuzz/fuzz_disposition.c`
(`make fuzz`: values at every room for the name, which must come out whole UTF-8; 38 M runs in 300
s, nothing found); `make itest` `/site/dl/` (attachment, not shown and not kept by the disk cache;
filename* before filename; a gzip file by its type and by its name, saved as they came, also
streamed; `notes.html.gz` shown; an inline name, a 403 and a header that isn't RFC 6266's shown;
`<a download>` named, empty, with the server's name first, to another origin followed with and without
attachment, and clicked by a script; a `.tar.gz` as Apache sends one, known by its address alone; a
page that says `inline` kept by the disk cache, a file named by `inline` not; a file asked for again
after the file selector outlasted its server); `make hatarigui`'s `browse` (the file selector
offering the server's name, and an `<a download>` clicked, on the emulated Falcon).

## Translucent colours and opacity (22 Oct 2026)

The CSS engine kept every colour's alpha (`rgba()`, `hsla()`, `#rrggbbaa`, `#rgba`, `transparent`)
and computed `opacity`, but the layout threw both away: a translucent background or border was
painted solid (Bootstrap's card caps, `rgba(0,0,0,.03)`, and Pico's table stripes came out dark),
`color: transparent` text was drawn black, a translucent overlay hid what was under it, and
`opacity: 0` content (fading menus, hidden overlays, inputs laid invisibly over a styled button)
showed. Now, after CSS Color 4:

- **Alpha** is kept from the computed colour to the screen: the display list's colour is
  `0xAARRGGBB`, for backgrounds, the four borders, text and its decorations, form controls' text and
  frames, list markers and pictures. Alpha 0 draws nothing. Two inline backgrounds one in another
  make one colour at layout time (source-over, Compositing and Blending 1), so a run is still one
  fill; a translucent canvas background is shown on white, as browsers do, and painted once (the
  root's or the body's box, whichever gave the canvas its colour, doesn't paint it again: CSS
  Backgrounds 3, The Canvas Background). A block's background colour stops at the box
  `background-clip` names (border, padding or content box), which shows with translucent borders
  (Bootstrap's modals and menus: `padding-box`).
- **Text of a transparent colour that the page shows another way**, with a paint EWeb doesn't draw,
  is drawn in that paint's colour rather than not at all (browsers show it; before translucency
  EWeb drew it black), the first it has of: a background through the text (`background-clip: text`,
  Tailwind's gradient headings: the gradient's first colour that shows, or the background colour;
  a picture, or a gradient EWeb can't compute: the text's own colour, solid), its outline
  (`-webkit-text-stroke`) and its shadow (`text-shadow`, the first one's colour: recoloured text,
  an input's hidden caret). The background and the outline are what the glyphs show; a shadow is
  under them, offset, blurred, often faint, and inherited, so taken first it drew gradient headings
  with a soft shadow at its 10% black (in 16 colours not at all). `background-clip: text` never
  leaves the text undrawn: pages don't hide text so, and its gradient often fails to compute here
  (Tailwind 4 makes it of custom properties that only `@property`, which EWeb doesn't read, starts:
  its `var()` is invalid at computed-value time and the image `none`). For this the CSS engine now
  computes `background-clip` (and `-webkit-background-clip`, and the `background` shorthand's box),
  `text-shadow` (the first shadow's colour), `-webkit-text-stroke-width` and `-color`, and keeps a
  gradient's first colour; none of them is drawn otherwise. Text a picture replaces (`color:
  transparent` over a `url()` background) stays undrawn. A legacy colour attribute of `transparent`
  (`<font color>`, `<body text>`) is turned down, as the HTML Standard's rules for parsing a legacy
  colour value say, so the text keeps the colour it inherits.
- **Opacity** (Transparency: the opacity property) isn't inherited, but applies to the element and
  everything in it as a group, clamped to 0..1. EWeb multiplies it into each item the group draws
  rather than drawing the group off screen and mixing it in. Your rules decide it (the fastest code,
  and the ST edition on a 1 MB ST): an off-screen group needs a buffer the element's size (the whole
  page for `body`, more than a 1 MB ST has) and a second pass over its pixels; multiplied in, it
  costs the layout nothing and the drawing below is all there is. It is the group's look wherever
  the group's items don't lie over one another (text on the page, borders, pictures, controls);
  where they do (text on its own background), the background shows through the text a little more
  than in a browser. **Opacity 0 draws nothing**, and its items stay in the list: it keeps its room,
  and its links, controls and scripts' clicks still work, as CSS has it (unlike `visibility:
  hidden`), unless it has `pointer-events: none` (CSS UI 4, inherited), as hidden menus, dropdowns
  and modals nearly always do (Tailwind's `opacity-0 pointer-events-none`): then a click goes through
  its items to what is under them. The CSS engine computes `pointer-events`; the layout marks such
  items (`DLF_NOHIT`, ewrender `-d`'s "nohit") and the hit tests (a link, a control, a script's
  click) pass them by. Opacity makes no stacking context (CSS draws an element of opacity below 1
  as if it were positioned with `z-index: 0`; EWeb draws it where it is).
- **What lifts an opacity of 0** that EWeb doesn't run or draw (`own_opacity()` in the layout).
  Pages ship content at opacity 0 and have something reveal it; drawn as CSS has it, that content
  would be missing for good. EWeb draws such an element whole:
  - one with an animation (`animation-name` or the `animation` shorthand: the engine keeps whether
    there is one): EWeb runs no animations and reads no `@keyframes`, and an element of opacity 0
    with an animation is nearly always faded in (`animation: fadeIn .8s forwards`, whose
    fill-mode keeps the last keyframe); so it is drawn as its animation leaves it, taken to be in
    full;
  - an `<img>` whose picture EWeb took from `data-src`, `data-srcset` and the like (as it does when
    `src` is missing or a placeholder, in place of the lazy loader's script): the script would have
    lifted its opacity too (lazysizes' `.lazyload { opacity: 0 }`);
  - elements AOS (`data-aos`) and Webflow (`data-w-id`) fade in with their scripts as the page is
    scrolled to them: EWeb's own scripts are off unless a site is in `SCRIPTS=`, and its lean DOM
    has no scrolling events or IntersectionObserver for them anyway;
  - a checkbox or radio button of its own opacity under 1/16: a page that hides the native control
    draws its own from generated content (`label::before` with no text: Bootstrap 4's custom
    controls), which EWeb doesn't draw, so without its control the box's state couldn't be seen.
    Its own opacity only: one inside a group of opacity 0 stays hidden.

  Other content a page's script reveals stays hidden while scripts are off, as in a browser with
  JavaScript off (a reveal-on-scroll class of a site's own has no marker EWeb could know). This is
  the most common way opacity removes content, and EWeb's default is scripts off: the markers above
  are the common ones, kept short so as not to show what pages really hide.
- **Form controls**: a control's text (its value, a button's label, a list's choice, the caret,
  whose `caret-color` is the text's) is drawn at its colour's alpha; the rest of it (its frame, a
  box's tick and a button's dot, a placeholder, the keyboard's frame) at the control's opacity, as
  browsers draw those in colours of their own, so `color: transparent` on a control or an ancestor
  doesn't hide a checkbox's state or a radio button.
- **Drawing** (`src/gfx/canvas.c`): an item less than solid is mixed with what is already drawn
  under it, in every mode, rather than with the colour the layout knows is behind it (the cheaper
  way the audit offered): only reading back shows what is under a translucent cover, text and
  pictures included, and it is nearly as quick.
  - True colour: each pixel read back and mixed in 32nds (the 5-6-5 parts spread out in a long, one
    multiply), two pixels a long, a long as the last one taking the last one's result (what is under
    is mostly one colour), a pixel as the last one likewise.
  - 4, 16 and 256 colours: a pen's colour comes from the screen's own palette (`ui/screen.c` reads
    it from the VDI already), mixed, and the screen's nearest pen taken, once per pen for each
    colour and alpha; then a pixel is a look-up in a table of 256 (`mix_to[]`, an array the 68030's
    code indexes in one instruction, not through a pointer), four pixels a long where they are
    alike. What each colour's pens became is kept for the last four colours (two in the ST
    edition): a change of colour takes out of the table only the pens the last one put in and puts
    back those the next one had, so runs in a few translucent colours one after another (Material's
    `.87` titles beside `.54` text, muted text beside links) neither empty the whole table nor work
    the pens out again (the review's 3,000 runs alternating two translucent colours, 16 colours:
    22.46 M instructions of drawing to 20.90 M, one colour 21.48 M to 20.79 M).
  - Monochrome: a translucent fill's mix is patterned as solid fills are (the greys of black and
    white, fixed to the page); text takes the nearest of black and white.
  - Pictures at an opacity: mixed per pixel in true colour; with a palette, an ordered pattern (4 x
    4, fixed to the page) draws the share of their pixels the opacity says, as mixing two pens has
    no cheap table (65,536 pairs).
  - The ST edition draws the same way; it keeps 16 pens for each of two colours (its screens have
    no more; with more, what isn't kept is worked out again), 0.6 KB in all (the Falcon's 2.6 KB).
  - Each depth has its own loops for a cover (8 bits a look-up a pixel, 16 bits a mix), and a
    glyph's mixing loop takes its depth once a glyph: the dearest case below, a translucent cover
    over a whole page, 16 colours 36.7 M to 31.1 M instructions, true colour 65.1 M to 56.9 M.
- **Limits:** a mix is the screen's nearest pen, so on a screen of few colours a tint of a few per
  cent vanishes (as near as makes no difference), TOS's 256 colours have no pale pink for half red
  on light grey (a grey is nearest), and with 4 colours half black on white is black. Group opacity
  is approximate where a group's items overlap (above). Text at a low alpha may vanish on a
  monochrome screen.
- **Measured** with `build/m68k/drawbench` (new, `tests/bench/drawbench.c`: a page laid out and
  drawn as `EWEB.PRG` does, 400 rows at a time, at 8 bits in 16 colours or at 16, counted with
  `tools/count68k.sh`), before and after, on 29 pages of the page set alone and with each of its two
  sheets (87): the layout 0.34% fewer instructions (the CSS engine, the same code in both, differed
  by up to 1% a page between the two builds: no more than the noise), the drawing 0.04% fewer in 16
  colours and 0.02% fewer in true colour (pages with translucent items up to 1.3% and 2.5% more, one
  8.6% fewer as what has no alpha isn't drawn now); solid text's glyph loops are kept apart from the
  mixing ones, so they cost what they did. The dearest case, a translucent cover over a whole page
  (the news page without its sheet, 608 x 8,000): 16 colours 21.0 M to 36.7 M instructions (3.2 more
  a pixel), true colour 44.9 M to 65.1 M (4.2 more), so a screenful (608 x 400) of cover costs 0.8 M
  and 1.0 M more, 0.15 to 0.2 s on the DFB1; since each depth has its own loops, 31.1 M and 56.9 M
  (2.1 and 2.5 more a pixel, a screenful 0.5 M and 0.6 M). All
  its text at alpha .75: 19.3 M to 21.3 M (16 colours), 33.1 M to 35.5 M (true colour); everything in
  it at opacity .5: 21.5 M and 35.6 M (19.3 M and 33.1 M solid). The review's fixes (what shows
  text of a transparent colour, `pointer-events`, what lifts an opacity of 0, the new properties),
  on 12 page-set pages alone and with Bootstrap (24 runs): the styling 0.20% more instructions (six
  properties more, Bootstrap's `pointer-events`, `text-shadow`, `animation` and `background-clip`
  now read and cascaded, a style 30 bytes larger), the layout 0.40% more, the drawing the same
  (+0.002%). In all, `EWEB.PRG`'s code grew 8.3 KB and its memory 2.6 KB (the mixing tables), the ST
  edition's 6.0 KB and 0.6 KB: on the 1 MB ST the pictures page now needs the tables page given
  back first (above, It runs on a 1 MB ST).
- **Tested:** `tests/css/cases.txt` (the colours' alpha, `transparent`, opacity's clamping, a
  percentage, `calc()`, inheritance; `background-clip` and its shorthands, a gradient's first
  colour, `text-shadow`, `-webkit-text-stroke`, `pointer-events` and its inheritance,
  `animation-name` and `animation`); `tests/layout/alpha.html`'s display list (ewrender `-d` shows
  a colour less than solid as `#rrggbbaa`, a picture's and a control's opacity as `alpha`, and
  "nohit"; the page has gradient, shadow and outlined text (and both under a faint shadow, and a
  gradient that doesn't compute), a replaced one, a legacy colour of
  `transparent`, an animated and an AOS paragraph, a lazy picture, a custom checkbox, a veil a click
  goes through and `background-clip: padding-box`); `tests/layout/alpha.py` (`make test`) reads
  back the pixels ewrender draws in true colour and in 2, 4, 16 and 256 colours (its new `-p`):
  opacity 0 draws nothing, fills, text and text under a cover come out half way, a picture at
  opacity .5 is half way to white (or half its pixels), text of a transparent colour is drawn in its
  gradient's, shadow's or outline's colour (the gradient's or the outline's over a faint shadow's,
  one EWeb can't compute in the text's own colour) and not where a picture replaces it, a
  checkbox's tick shows in a transparent colour and at its own opacity 0 but not in a group of
  opacity 0, a translucent root background is one colour down the window, a click (`-J -K`) on a
  link under a veil of `pointer-events: none` follows it, and the page drawn in bands at 8 and 16
  bits under AddressSanitizer; `make hatarigui`'s screens group shows the page on the Falcon in 256 colours
  and true colour and on the TT in 16 colours and monochrome, and its st group in `EWEBST.PRG` on
  the 1040 ST in monochrome and medium resolution.

## CSS gradients (22 Oct 2026)

The CSS engine took gradients as valid and kept their first colour alone, and the layout drew
nothing for them: a header or a button with white text on a dark gradient came out white on white
(and as the `background` shorthand resets the colour, a fallback colour written before the gradient
was gone too). Now, after CSS Images 3 and 4:

- **Kept whole** (the CSS engine, `ec_grad` in `ecss.h`): `linear-gradient()`, `radial-gradient()`,
  `conic-gradient()` and their `repeating-` forms, in `background-image` and the `background`
  shorthand: a linear one's direction (an angle in `deg`, `grad`, `rad` or `turn`, a unitless 0, or
  `to` a side or a corner), a radial one's shape (`circle`, `ellipse`), size (the four extent
  keywords, one length, two lengths or percentages) and centre (`at` any position), a conic one's
  `from` and centre, and the colour stops with up to two positions each (lengths and percentages,
  `calc()` of the two; a conic one's angles and percentages) and the hints between them. Lengths in
  `em`, `rem`, `vw` and the like are computed with the style, as other lengths are; `currentcolor`
  becomes the element's colour in the layout. An interpolation method (`in oklab`, `in hsl longer
  hue`: Tailwind 4 writes one) is read, and the gradient drawn in sRGB, as browsers draw one between
  sRGB colours with none named; EWeb's colours are all sRGB, so only a named Oklab or polar method
  comes out a little different. A gradient that isn't valid (a hint first, last or two in a row,
  three positions, a circle's size in per cent, an unknown method) makes its declaration invalid, so
  a fallback before it stays; one stop alone is valid (CSS Images 4). `tests/css/cases.txt` prints
  each as CSS writes it.
- **Math functions** (CSS Values 4, 10, which allows them wherever an angle or a length goes):
  an angle may be `calc()`, `min()`, `max()` or `clamp()` of angles (`calc(var(--a) * 1deg)`),
  worked out as the sheet is read (a conic gradient's stop a percentage of the turn too); a
  position or a radius `min()`, `max()` or `clamp()` of one unit is worked out (`min(10px, 20px)`,
  `max(1em, 2em)`), and so is one inside `calc()` (`calc(100% - min(10px, 20px))`). One of px and
  % together (`min(10%, 5px)`) can't be until the gradient line's length is known: such a gradient
  is valid all the same, kept as its colours alone (`approx`) and drawn as their average, never as
  nothing (and the `background` shorthand's colour stays with it); so is one of more stops than
  EWeb keeps (64, 32 in the ST edition: their first ones' average) or of more relative lengths.
  Before (the review of 22 Oct 2026), each of these made its declaration invalid. An angle is kept
  within a turn (`10000turn` is `0deg`), so neither the parser's arithmetic nor the canvas's can
  overflow, and so is a length in `vw`, `vh`, `vmin` or `vmax` within the fixed point (`1e9vw` is
  the most a length can be, 2,097,152px; before the second review its product overflowed, as
  `width: 1e9vw`'s did before gradients: undefined behaviour, and a stop in the wrong place). The
  ST edition, short of memory, works no `min()` out, inside `calc()` or not: such a gradient is its
  average, the shorthand's colour kept with it (850 bytes less; before the second review one inside
  `calc()` made the declaration invalid there, and white text meant for a dark gradient came out
  on white).
- **`-webkit-` forms** (the Compatibility Standard): `-webkit-linear-gradient()` and
  `-webkit-repeating-linear-gradient()` (a side it starts from, not `to` one; an angle from east,
  counter-clockwise) are kept as their modern forms, and `-webkit-gradient(linear, ...)` (WebKit's
  first: two points and `from()`, `to()`, `color-stop()`) as `linear-gradient()` to the side its
  line runs to, its stops placed on that line between the points; one across a corner and
  `-webkit-gradient(radial, ...)` as their average. Of the page set's 16 pages with gradients 7
  have only these (old Bootstrap's buttons and bars: 28 `-webkit-linear-gradient(top, ...)` and 18
  `-webkit-gradient(linear, left top, left bottom, ...)`), which drew nothing before. The ST
  edition takes `-webkit-linear-gradient()` but not `-webkit-gradient()` (2 KB of its code).
- **One layer, the top one.** The `background` shorthand kept its first layer already;
  `background-image` of several layers, invalid to EWeb before (and so dropped), now keeps its first
  too (csscmp's new reason: LibCSS drops such a declaration). More layers would need a list in each
  of the six background longhands (four hidden properties more, a style 20 bytes larger) and the
  layout's table and flex stretching to take several items for a box's background: not cheap. So a
  gradient over a picture (often a translucent darkening for a hero picture's white text) shows the
  gradient over the background colour, and a picture over a gradient the picture.
- **Laid out** as a background picture is: one item (`DL_GRAD`, the gradient with its lengths in
  device px) and a tile. A gradient has no size of its own, so its tile is the positioning area's
  size (the padding box) or `background-size`'s, placed by `background-position` and repeated by
  `background-repeat` (a stripe pattern of `linear-gradient(...) 0 0 / 100% 8px`); the root's or the
  body's over the whole canvas, its tiles placed as for the root and going on below a page shorter
  than the window (CSS Backgrounds 3, 2.11.2; the canvas draws that item, `page.cbg`, down to the
  window's foot). A box laid out with no height and stretched afterwards (a table row's
  cell, a flex line's item) takes its gradient's height with it. `background-clip` now stops
  pictures and gradients, not only the colour, at the box it names. An inline's gradient (a
  `<span>`'s) is its stops' average over its colour, a run being one fill; an inline-block's is
  its own box's, as a block's (Inline blocks, below). Text painted with a gradient (`background-clip: text`) keeps the
  gradient's first colour that shows (Translucent colours, above): an average with a transparent
  stop would be too faint to read.
- **Drawn** (`src/gfx/canvas.c`): each pixel's place along the gradient line (linear: from the
  angle, or at right angles to the diagonal for a corner), the ray (radial: the ellipse's distance)
  or the turn (conic) picks an entry of a ramp: the gradient's colours, mixed premultiplied (CSS
  Images 4, 3.5: red to `transparent` stays red), at 256 points at most, no more than one a pixel
  and no more than its colours need (two levels of 255 an entry where they change most; a sharp edge
  or a hint all there can be), and the colours before and after the stops as entries of their own,
  so that a sharp edge at either end falls where it is; a hint bends the mix by its power curve
  (fixed-point log and exponent tables). The stops are fixed up as CSS Images 3, 3.4.3 says
  (unplaced ones spread evenly, none before the one before it). An entry is its colour made for the
  screen as a 4 x 4 pattern on the canvas's own ordered thresholds, fixed to the page as the fills'
  greys are:
  - **Palette modes: an ordered dither, not the pictures' Floyd-Steinberg.** Floyd-Steinberg
    (`common/src/pic.c` and the DSP's) carries each pixel's error to the next: drawn a band at a
    time, as the window is (32 rows when memory is short), or scrolled, a gradient would show seams
    between bands, and keeping its pens as a picture's are kept would take a megabyte for a body's
    gradient. So the thresholds are the canvas's own, those monochrome fills and pictures at an
    opacity use already, and what is new is how an entry's 16 pens are chosen, so that together they
    make its colour (Knoll's pattern dither, 30 lines: each the screen's nearest, `ui/screen.c`'s
    cached `pen()`, to the colour plus the error the pens before it left, sorted by brightness, so
    light and dark spread evenly over the thresholds); on a monochrome screen they are the fills'
    grey pattern.
  - **True colour** has 16 5-6-5 values for each entry a step apart, so that a gentle gradient
    (`#fff` to `#eee`, two 5-bit steps) isn't drawn in bands.
  - **Fast where it can be:** a linear gradient at right angles is drawn a row at a time (one entry:
    a long of its four pens at a time, four longs a turn of a loop counted down) or a column at a
    time (the first four rows worked out, the others copied from four rows up); any other a pixel at
    a time, an entry index stepped along each row (a shift, a mask and a look-up: runs of one entry,
    each written a long at a time, measured a quarter slower for entries 15 px wide), the pixels
    before and after its span as runs of the first and last colours; a radial one from each pixel's
    square distance (a 16-bit multiply) against the squares of the entry's thresholds, the next
    pixel's entry the same or one either side, the pixels past its last stop as runs; a conic one
    from each pixel's offsets by a division and an eighth of a turn of atan's table (to a quarter of
    an entry), each row in two spans, either side of the centre. Each depth has its own loops, four
    pixels a turn.
  - **Tiles** (a pattern of dots or checks, `0 0 / 16px 16px`): a solid gradient's are drawn only as
    far as its tiles and the 4 x 4 pattern come round together (across and down: 52 px for tiles of
    13, a 16 px CSS tile at 0.8), and copied on from there, each copy across twice the last; a
    translucent one's (most dot patterns: dots on `transparent`) have their entries worked out once
    for one tile (64 KB at most), each row then drawn from them, a tile's row copied across; a
    linear gradient whose rows are each one entry is drawn across all its tiles at once. Before (the
    review of 22 Oct 2026) each tile was worked out on its own, its set-up (64-bit divisions) for
    each, 50 to 60 instructions a pixel for 16 px dots.
  - **A translucent gradient** at right angles is as cheap as a translucent fill: a row of one entry
    is its four pens put in a long at a time through a mask of where its alpha is above the
    thresholds (8 bits), or mixed as a translucent fill is (16 bits: a run of pixels as the last
    ones takes their result); one across has its row's entries worked out once for all its rows; a
    row of entries is drawn in runs of one entry at 16 bits, its transparent ones passed over, each
    entry's last pixel under it and what that became kept from run to run (what is under is mostly
    one colour, so an entry is mixed once, not at each of its runs a few pixels wide); and one
    across keeps that for each column (`mix_cols16`: its rows are alike, so a pixel is a load, a
    compare and a store, as a hero picture's scrim `linear-gradient(to right, rgba(0,0,0,.7),
    transparent)` is drawn). Before, each pixel was mixed on its own, 22 to 29 instructions a pixel;
    after the first review a fade across was still 25 a pixel in true colour (each run of an entry,
    a few pixels, mixed again), now 10.9.
  - **A ramp is kept** for the last four gradients drawn, with the screen it was made for, so a band
    of the window, a scroll or a page of like buttons doesn't work it out again (up to 10 KB each).
    Making one is most of a small gradient's cost (an entry's 16 pens are 16 of `pen()`'s look-ups,
    some 2,000 instructions an entry): so its entries are as many as its colours need, not one a
    pixel, and a header from `#2c3e50` to `#4ca1af` 600 px wide is 50 of them, not 256. On a page of
    typical gradients (a header, a hero at 135deg, three buttons, two cards with a sheen) that took
    the drawing from 4.54 M to 3.20 M instructions in 16 colours and from 4.83 M to 4.50 M in true
    colour (with two solid stops' mix worked out without a division). In 256 colours `pen()` costs
    more the first time a colour is met (`ui/screen.c` looks through all 256 pens, then keeps the
    answer for its 12-bit colour): the dithers' colours wander, so the first gradients drawn after
    the start fill much of that cache, up to 11 M instructions once (the test page of 22 gradients:
    17.6 M to draw, 11.3 M of them `pen()`; the page above 5.2 M; in Hatari's Falcon the test page
    took 3.5 s the first time). A faster nearest pen there (for later) would save that.
  - **Translucent** stops or a box's opacity: true colour mixes each pixel with what is under it (in
    32nds, as translucent fills are); with a palette an entry is drawn where its alpha is above the
    transposed thresholds, as pictures at an opacity are.
  - **Where it can't be drawn**, its stops' average (premultiplied), never nothing: a repeating
    gradient of no length (CSS Images 3, 3.6 says so), one whose stops can't be placed (`approx`,
    above), a ramp's memory not there, a radial gradient thousands of entries off its tile.
  - **A radial gradient of no size** is drawn as CSS Images 3, 3.2.3 says: a circle of no radius as
    one a 256th of a px round (its stops' percentages all 0, those in px where they are), an ellipse
    of no width as one a 256th of a px wide and as tall as can be (a linear gradient across,
    mirrored at its centre), one of no height its last colour (a repeating one's average). Before,
    any of them was its last colour, so `radial-gradient(circle closest-side at 0 0, red 20px, blue
    40px)` was all blue.
- **The ST edition draws a linear gradient at right angles, and any other in its colours' average.**
  The Falcon's drawing is about 11 KB of 68000 code (linear ones alone 7 KB). The 1 MB ST had
  190,090 bytes free at the start before this change, and its tables page needs between 177,886 and
  181,758 (it failed with the first and showed with the second: builds padded for the purpose, in
  Hatari); with all the Falcon's drawing it had 177,886, and the tables page no longer showed. So
  the ST edition has a drawing of its own, 2 KB: a gradient to a side, or at an angle a multiple of
  90deg (most of the web's: a header's, a button's), a row or a column at a time, each row's colour
  from the same stops as the Falcon's (`grad_place`, `stop_colour`) and drawn as a fill is (the grey
  pattern in monochrome, so a gradient is a ramp of 17 greys; the nearest pen in 4 and 16 colours,
  so it is a few bands); rows the screen shows alike are one fill (a solid colour's grey level in
  monochrome, its 5-6-5 value in true colour, else its 12-bit colour, as `pen()` keeps them), in a
  colour of that alone (the grey's or the 5-6-5 value's own colour, or the 12-bit colour with each
  nibble twice, so black and white stay so), and the colour between two stops is worked out in 32
  bits (a 64-bit division is a loop of 64 turns on the 68000). Before the second review (22 Oct
  2026) a run of rows was drawn in its first row's colour; a run starts where a band of the window
  or a scroll's new rows start, so in monochrome a gentle gradient's greys changed with them (the 1
  MB ST draws in bands of 32 rows): seams, now gone (`tests/layout/gradients.py` draws tall gentle
  gradients in bands from anywhere). A hint mixes its stops evenly (its power curve would be 700
  bytes more). Any other gradient is its stops' average, drawn as a fill. On the 1040 ST in
  monochrome (8 MHz, Hatari) the test page's window of 22 gradients is drawn in 13.6 s, against
  12.5 s with every gradient its average (a fill a row: 16.4 s). In all `EWEBST.PRG` grew 6.7 KB
  (the gradients the CSS engine keeps 3.7 KB, the layout's items 1.1 KB, the drawing 2 KB), and the
  1 MB ST has 185,068 bytes free at the start and shows the tables and the pictures pages as before.
  After the review (22 Oct 2026) it grew 1.1 KB more (angles in `calc()`, `min()` and the like, the
  averages and `-webkit-linear-gradient()` in the CSS engine, the canvas's background on down the
  window; it leaves out `-webkit-gradient()` and working `min()` out, 2.8 KB), and the 1 MB ST has
  184,204 bytes free at the start and shows the tables and the pictures pages as before. After the
  second review it grew 714 bytes more (the rows' keys, `min()` inside `calc()` taken as lost,
  lengths in `vw` kept within 32 bits), and the 1 MB ST has 183,670 bytes free at the start and
  shows the tables and the pictures pages as before. The rest of the Falcon's drawing (dithered,
  diagonal, radial, conic) could be had for about 9 KB more if the ST's memory is found elsewhere:
  **the owner's call**.
- **Limits:** one layer; `background-origin` (the padding box always) and `background-attachment:
  fixed` (a body's fixed gradient is spread over the whole page, not the window); a position or a
  radius `min()` of px and % together (its average, above); colours in `oklch()`, `lab()`,
  `color-mix()` and the like, which EWeb's colours don't read anywhere (such a stop makes the
  gradient invalid, as it makes `background-color` invalid, so a fallback before it stays); sRGB
  interpolation; a sharp edge inside a gradient longer than 256 px within half an entry of its place
  (on a 600 px one, a pixel); a conic gradient's angles to a quarter of an entry; `list-style-image`
  gradients aren't drawn (the marker of its type is); the radial and conic per-pixel work is the
  68030's (the DSP could take the distances and the angles: for later).
- **Measured** (`build/m68k/drawbench`, the whole page drawn 400 rows at a time, counted with
  `tools/count68k.sh`), a page of one 608 x 3,200 px gradient, instructions a pixel at 8 bits (16
  colours) and in true colour, against a solid fill's 0.88 and 6.04: to bottom 1.51 and 4.24, to
  right 1.47 and 4.16, 45deg 10.2 and 12.4, repeating stripes at -45deg 9.4 and 12.0, radial 27.5
  and 29.7, conic 35.7 and 36.0, a translucent fade over a colour (to bottom) 22.9 and 34.7. So a
  screenful (608 x 400) of radial gradient is 6.7 M instructions, about 1.3 s on the DFB1 at the
  rate measured for translucency above; a vertical one 0.4 M. The first versions took 39 and 55
  instructions a pixel for radial and conic, 18 for 45deg and 2.04 for to bottom: each row's entries
  were worked out into a buffer, then drawn; now the pens go straight in, the radial and conic rows
  are functions of their own (their variables in registers), a conic row is split at its centre, a
  run of longs is a loop counted down, four a turn, and a ramp has the entries its colours need
  (above). On the page set, before and after, the same 29 pages as for translucency, alone and with
  each sheet (87 runs; its pages and sheets hold only six different gradients, two of them the old
  `linear-gradient(top, ...)` that isn't valid): the styling 0.05% more instructions, the layout
  0.04% more, the drawing 0.01% more in 16 colours and the same in true colour.
  `tests/bench/measure.py -E ewcss` on all 258 pages: the CSS parse 0.05% more with either sheet,
  the selection 0.2% more with Bootstrap (a `background-image` of two layers through `var()`, as its
  `.form-select` has, is read now where the first comma made it invalid before) and the same with
  Pico (0.01%). `EWEB.PRG`'s code grew 28.1 KB (the drawing 22.1 KB: each depth its own loops, four
  pixels a turn) and its memory 2.6 KB (the four ramps' stops), and up to 40 KB while a page with
  gradients is drawn (the ramps' entries). After the review (22 Oct 2026), the translucent fade to
  bottom is 2.54 and 7.32 a pixel (against a translucent fill's 2.98 and 7.06; the review's code
  measured again: 22.9 and 32.1), one across 9.1 and 28.5 (40.9 and 58.5). On the review's page of
  one 608 x 960 px box (583,680 pixels, its page included: 2.1 and 5.3 a pixel with no gradient), 16
  CSS px dots (`radial-gradient(#999 1px, #fff 1px) 0 0 / 16px 16px`) are 3.4 and 7.0 a pixel (58.0
  and 61.0 before), the same dots on `transparent` 9.5 and 12.6 (61.0 and 61.6), checks
  (`repeating-conic-gradient(#ccc 0 25%, #fff 0 50%) 0 0 / 16px 16px`) 4.3 and 7.2 (50.7 and 50.9);
  each drawn as before to the pixel (every mode and slice compared with the review's drawing). The
  review's changes to the CSS engine cost nothing measurable: the news page styled 0.16% more
  instructions, less than moving the engine's code does (a function never called, 7 KB, put in
  `value.c`: 2.2% more), the sample page with Bootstrap 0.05%. `EWEB.PRG`'s code grew 6.8 KB more.
  Its memory: up to 64 KB more while translucent tiles are drawn (a tile's entries). After the
  second review (22 Oct 2026) the translucent fade across is 8.4 and 14.2 a pixel (9.1 and 28.5
  before it), on the 608 x 960 box 10.9 a pixel more than the page alone in true colour (25.1
  before); at 16 bits a translucent radial gradient 46.2 (49.0), a conic one 49.8 (52.5), one at
  45deg 31.3 (34.2), the dots on `transparent` 12.6 as before; each drawn as before to the pixel
  (263 random pages of translucent gradients over colours, gradients and stripes, in every mode and
  three slices); `EWEB.PRG`'s code 1 KB more, its memory 6 bytes an entry of each ramp and 6 a
  column of a fade across. The CSS engine's arithmetic kept within 32 bits costs nothing
  measurable (the news page styled 0.002% fewer instructions, the sample page 0.002% more).
  Counting, the program's path matters: QEMU puts it on the stack, and a longer one moved the news
  page's count by 1.1% (the buffers on the stack aligned otherwise), so the two programs compared
  are run from paths of one length.
- **Tested:** `tests/css/cases.txt` (what is kept of 18 gradients, 24 that aren't valid, the layers,
  14 with math functions, one of more stops than are kept, 12 `-webkit-` ones, and huge lengths in
  `vw` and the like, kept within the fixed point);
  `tests/layout/gradients.html` and its list (ewrender `-d` shows a gradient item, its tile and the
  gradient as the display list has it, `averaged` for one kept as its average);
  `tests/layout/gradients.py` in `make test` (318 checks of pixels read back in true colour and 2,
  4, 16 and 256 colours: ends and middles, angles, sides and corners, sharp edges at their pixel, a
  hint's bend, radial centres, corners, circles and ones of no size, repeating periods and one of no
  length, conic quarters, translucency and opacity, background-size, -position, -repeat and -clip,
  stretched boxes, the root's gradient and on below a short page, an inline's average, averages of
  stops that can't be placed, calc() angles, `-webkit-` forms, small tiles each alike, translucent
  fades, and every page drawn in slices of 13 rows the same as whole; the test page in bands at 8
  and 16 bits under AddressSanitizer; in 4 colours brightness, as in 2: before the review its checks
  there were counted and not made; huge lengths drawn with no undefined behaviour, as UBSan's report
  stops it); and since the review 239 more of the ST edition's drawing, through
  `build/asanst/ewrender` (ewrender built `-DEW_ST`, now in the Makefile and `make test`: rows and
  columns each one colour, ramps each way, stripes, averages, one with `min()` inside `calc()`,
  slices, and since the second review tall gentle gradients in bands of 32 rows from anywhere the
  same as whole, which the drawing before it failed in three of the four modes). While it was made:
  7,000 random pages of gradients (valid and not) drawn under AddressSanitizer and UBSan at every
  depth and in slices, and 1,500 on its final code, with no fault, and 1,500 through the ST
  edition's drawing; after the review 700 more, each in every mode of both editions and every fifth
  in bands (they found two overflows, `fx_mul`'s of a huge `calc()` product and the ST's of a tile
  hundreds of millions of px tall, both fixed), and libFuzzer's `fuzz_html` four minutes from them
  (574,549 pages); every tiled and translucent page of the review drawn the same to the pixel as
  before in every mode and slice; after the second review the 263 pages above through the Falcon's
  drawing and 41 through the ST's, under AddressSanitizer and UBSan with no fault, and
  `fuzz_html` 150 s (1,045,378 pages); `make csscmp` (no new difference without a reason, and none
  at all from the second review's changes; the page set's
  `-webkit-` gradients, valid now, are 20 and 25 values under "gradients as images", as LibCSS has
  none); `make hatarigui`'s screens group shows the page on the Falcon in 256 colours and true
  colour and on the TT in 16 colours and monochrome, its st group on the 1 MB ST and the 1040 ST;
  `make itest` (background pictures, which `background-clip` now stops too, and the canvas's now
  drawn on down the window).

## Legacy centring and HTML's `align` (22 Oct 2026)

Old pages, and many forums and banks, centre with `<center>` and `align`, and EWeb centred only
their lines: `<center><table width=600>`, `<td align=center><table>` and `<div align=center>`
round a box of a set width came out at the left, the text in them centred. Now, after the HTML
Standard's rendering section (15.3.3 flow content, 15.3.8 tables, 15.3.11 hr, 15.4.3 pictures)
and as Gecko and Blink do it:

- **HTML's own `text-align` values** (the CSS engine): `-webkit-left`, `-webkit-right` and
  `-webkit-center` (Gecko's `-moz-` names read as the same), Blink's names for the standard's "align
  descendants". They inherit as any `text-align` does, align the lines as `left`, `right` and
  `center`, and also place the blocks inside (below). The UA sheet gives `<center>` and `caption`
  `-webkit-center`; `align` (left, right, center or middle) on `div`, `p` (as both engines),
  `thead`, `tbody`, `tfoot`, `tr`, `td` and `th` is a hint of the legacy value; `justify` stays
  plain `justify`, as in both engines (the standard aligns the blocks to the left); on headings and
  other elements `align` is plain `text-align`, as Blink has it. A sheet's own
  `text-align: -webkit-center`, which some sites use to centre boxes, works the same.
- **Where a block goes** (`room_left` in `src/layout`): the room a block in the flow leaves beside
  it (one with a width, a table shrunk to its columns, a picture as a block) goes by CSS 2.1,
  10.3.3: its auto margins share it (one auto margin alone now moves a table too, where before
  only both centred one); with none, the end margin takes it by the container's
  direction, so in a right-to-left container (Hebrew pages) a narrower block now sits at the right;
  unless the container's `text-align` is one of the legacy values, which centre it or put it left
  or right. The nearest such container wins, as `text-align` inherits; a block's own `text-align`
  doesn't move it (`<center><div style="width:300px; text-align:left">`: the box centred, its text
  at the left); floats and positioned boxes aren't moved. As in both engines, a block with an
  `align` of its own is still placed by its container's (the standard leaves it alone).
- **`th`** is centred only where its parent's `text-align` is still the initial one (15.3.8): the
  UA sheet's `-ec-center-or-inherit`, computed to `center` or to the parent's value, so
  `<tr align=right>` puts its `th`s at the right and `<tr align=left>` at the left.
- **Quirks mode** (no DOCTYPE, or an old one): a table starts its `text-align` afresh (15.3.8's
  quirks rule), a small UA sheet `doc_add_ua` adds for such a page, so in `<center><table>` the
  table is centred and the text in it starts at the left, as browsers show such pages. The rule's
  font, `line-height` and `white-space` parts (a table not inheriting its page's font size there)
  are left out: they would change many old pages' text and are a decision of their own.
- **Tables, rules and pictures:** `<table align=left|right>` floats and `align=center` centres by
  its margins (as before); `<hr align>` left, right or center by its margins (new); on `img` and
  an image button `align=left|right` floats (an `img` as before; an image button now too: it, and
  any form control a page floats, has a block of its own round it, as a floated picture has, the
  control drawing its own frame in it as on a line), `top` and `absbottom` go to the line's top
  and bottom (CSS 2.1, 10.8.2, the line grown to hold them, in a `sup` or `sub` too; before, `top`
  sat on the baseline), `middle` and `center` put the picture's middle on the baseline (the
  standard's, Blink's `-webkit-baseline-middle`; before CSS's `middle`, a few pixels higher),
  `texttop` hangs it from the text's top, `absmiddle` and `abscenter` are CSS's `middle`, and
  `bottom` and `baseline` sit on the baseline (HTML 3.2's meaning of bottom, and both engines').
  `iframe`, `object` and `embed` take the same hints (an iframe is shown since stage 5: Frames
  and iframes, below). A picture or
  a form control in a `sup` or `sub` now grows its line by the shift, as a word there does (before,
  a raised picture went over the line above). `valign` on rows and cells is `vertical-align`, the
  cells inheriting their row's (as before); `baseline` now puts the first lines of a row's cells on
  one baseline (CSS 2.1, 17.5.3; before, such a cell sat at the top; one taking rows below stays at
  its top). `<caption align=bottom>` is not done: it is `caption-side`, which EWeb doesn't have yet.
- **Floats in the text** (CSS 2.1, 9.5.1): a float met partway through a line, `<img align=right>`
  after a few words, a floated span or form control, goes at that line's top when it fits beside
  what is on the line (the line shorter beside it), else just below the line, and so does every
  float after it on that line, though it would fit (as Gecko and Blink do), and the text either
  side goes on in the same lines (`fixup`'s `text_float`, placed by `lines`). Before, the text was
  cut into two anonymous blocks at the float: the line broke there and the float sat a line lower.
  A float in an inline (`<a href><img align=left></a>`) no longer turns the inline into a block. A
  box's widest (a float's or a cell's width, a table's columns) counts floats side by side, beside
  the text of their line or one after another until one clears, as browsers do; before it took the
  widest of them. A float still starts no higher than one before it, and `<br clear>` is not done
  yet.
- **Measured** (`build/m68k/drawbench -P 1` and `-P 2`, `tools/count68k.sh`, the two programs run
  from paths of one length), the page set's 258 pages, the sample page and the news page: the layout
  0.02% more instructions (a page from 0.81% fewer to 0.38% more, the Atari heap's own variation
  between two builds among it; the first version 0.064%, 0.13% before the lines looked for pictures
  at a line's top or bottom only in text that has one, and the room beside a block was worked out
  only for one whose width isn't automatic; the floats in the text next to nothing: a float in the
  text measured once, and looked for only in text that has one), the styling 0.17% more (10.6 K a
  page: 5.9 K once a program run, the nine new names and the longer UA sheet, which `EWEB.PRG` reads
  once; 2.6 K for the quirks rule on a page in quirks mode; the rest within the cascade's own noise
  between two builds). `EWEB.PRG` grew 4.5 KB, `EWEBST.PRG` 3.0 KB (2.1 KB and 1.4 KB of it the
  first version's).
  29 pages of the page set lay out differently. Seven for the alignment: the tables and boxes in a
  `<div align=center>` centred (CNN, three of eWeek's, the Daily Bulletin, the Salt Lake Tribune's
  weather), and Ynet's side menu (quirks mode) with its pictures' alt text at the left of their
  cells, as browsers have it. 22 for the floats in the text, each looked at before and after:
  Wired's "View all", TIME's comments' likes, share buttons (JPL, NECN) and NECN's poll question on
  one line with the text they float beside, CBS Sports' "| Sign In Register" on one line rather than
  three, floated radio buttons, check boxes and fields beside their labels (PCMag, WebMD, Mother
  Jones), the Dallas Morning News's footer columns side by side, and the Guardian's boxes of floated
  numbers a few pixels wider.
- **Tested:** `tests/css/cases.txt` (the legacy values inherited and Gecko's names, a misspelt one
  not taken; `th`'s value under the initial, a legacy and a plain parent;
  `-webkit-baseline-middle`); `tests/layout/align.html` (each case above, in a standards-mode page:
  pictures at the top and bottom in `sup` and `sub`, image buttons floated, a row of baseline
  cells), `floats.html` (a float that fits on its line, one that doesn't, one in a link, a floated
  span, a cell's width with one, floats side by side, floated form controls) and `quirks.html`,
  their lists looked at with ewrender first; the changed pages looked at before and after; `make
  csscmp`: no new difference (the legacy values join `start`, `end` and `match-parent` under
  `EC_COMPAT_START`; the page set has none); 400 random pages of nested `center`, `align`, `valign`,
  widths, directions and legacy values drawn by both editions under AddressSanitizer and UBSan with
  no fault, and 400 more of floats in text, links, `sup`, `sub`, cells and lists (pictures, spans,
  tables, image buttons and form controls floated); `fuzz_html` 180 s (1,142,385 pages); `make
  h5test` (EWeb's parser 1,868 passed, the six scripted ones failed, here and on the 68030); `make
  itest`; `make hatarigui`'s browse group (its forms page among them) and st group (`EWEBST.PRG` on
  the 1 MB ST and the 1040 ST, the tables page with its centred table among them), on the real TOS
  (4.04 on the Falcon, 1.04 on the STs).

## Details and summary (22 Oct 2026)

FAQ pages, documentation, forums and GitHub-style READMEs fold their parts away in `<details>`,
and EWeb showed everything inside a closed one, with no sign that it folds. Now, after the HTML
Standard (4.11 the elements, 15.5.4 their rendering) and as browsers show them:

- **What shows** (`src/layout`): a details element's summary (its first `summary` child) always,
  the rest (elements and text) only when it has its `open` attribute: the layout builds only the
  summary's boxes of a closed one (the standard's two slots, the second "removed from the
  rendering"), so what is hidden costs no boxes and no layout (it is still styled, as scripts may
  ask for its style). One without a summary has one of
  the layout's own, a triangle and "Details", in its style. An open one's summary comes first,
  whatever is before it in the page (the summary's slot is the first; browsers draw it so).
- **The triangle** (the UA sheet, 15.5.4): `details > summary:first-of-type` is `display:
  list-item` with `list-style: disclosure-closed inside`, `disclosure-open` when its details is
  open (the standard's `counter-increment: list-item 0` left out: the layout doesn't number a
  summary). The CSS engine now knows the two counter styles (CSS Counter Styles 3, 6.1), and the
  layout draws them as ▸ and ▾ (◂ in a right-to-left one), three characters added to `EWEB.FNT`
  from DejaVu (`tools/mkfont.py`) rather than drawn: they are the standard's own, and
  pages use them as text too. A page's own markers work as in browsers: `list-style: none` (the
  usual way, Pico's and GitHub's) hides the triangle, `summary { display: flex }` has none, and
  `::before` or `[open]` rules draw their own; `::marker` and `::-webkit-details-marker` are read
  but not applied (as before).
- **`list-style-position: inside`**, which the summary's triangle needs, now works for every list:
  the marker is the first thing on the item's first line, followed by a space (its counter
  style's suffix; CSS Lists 3, 3.1) that is no place the line chooses to break at, though a first
  word that doesn't fit beside the marker goes on the next line, as any word that doesn't fit
  where the line has no space does (CSS Text 3 allows a break there, the item's `white-space`
  being `normal`), and the lines after it start under it; before, EWeb drew every marker outside.
  A `list-style-type` that is a string is now that string, inside or outside, where EWeb drew a
  bullet.
- **A click on the summary** opens or closes its details (4.11.2, the summary's activation
  behaviour): `ui/win.c` asks the page what is under the pointer after a form control and a link,
  as browsers do (a link in a summary is followed, not toggled), then `page_summary_at`, an
  invisible display-list item (`DL_SUM`) the size of the summary's box, so it moves with the box
  when a cell, a flex line or `position: relative` moves it, in both editions. A summary a page
  lays out inline (`display: inline`) has an item for each of its parts
  on the lines instead (one with `inline-block`, as GitHub's and Bootstrap's button-like
  `<summary class="btn">` are, is a block in its line with its box's item: Inline blocks, below) (the line's run of its items, as an inline background is drawn; no cost
  where there is none, `L.sums`). A summary with `display: contents` has no box, but a click on
  its text is one in the summary all the same: its blocks are marked, and each run of its inline
  boxes goes in an anonymous inline (laid out as its parent's run, as before) whose parts on the
  lines are its click areas. Where summaries hold each other (a details in a summary), the
  smallest under the pointer is the one clicked. `br_details`
  (`src/browse`) sets or removes `open` (`dn_set_open`, keeping the element's hints and the CSS
  engine's links, so no `doc_refresh` is needed), styles the page again (all of it, as after a
  script: the triangle, and a page's own `[open]` rules, which may reach any element after the
  details) and lays it out again, with the busy bee meanwhile; the window stays where it was. Out
  of memory (a 1 MB ST) it gives back what a page that doesn't fit does, in two steps: the pages
  kept for Back, the pictures no page shows and the fonts' glyphs, and it is tried once more; then
  the page shown (its tree kept), and it is tried a last time. If even that doesn't fit, the
  window is left empty and EWeb says so (an alert after a click, the status line after a
  `#fragment`). On the 1 MB ST this came to nothing at first: the display list's first piece was
  1024 items, 45 KB, most of a small page's 61 KB, and the second page beside the first took the
  last of the RAM, so a FAQ page of three details didn't open at all; the ST edition's first piece
  is now 128 items (and the line items' 64, not 256: 15 KB at the time), so that page costs 14 KB,
  and a click opens it at the first try, in 335 ms (`make hatarigui`'s `details` group runs it on
  the 1 MB ST too, from a page of its own: `tests/layout/details.html` doesn't fit there).
  Measured in Hatari on `tests/layout/details.html` (912 pixels tall): 145 ms on the emulated
  32 MHz Falcon, 1.7 s on the 1040 ST (8 MHz), against 375 ms and 3.6 s to style and lay out the
  page as it was opened; with the review's cases the page is 1,215 pixels tall, and a click takes
  190 ms and 2.25 s. Tab in EWeb moves only between form controls, not to links, and so not to a
  summary either: no key opens one yet.
- **A place in a closed details** (a link to `#id`, or an address with one): the details it is
  in are opened first and the page laid out again, then the window goes there (7.4.6.4, scroll
  to the fragment, runs 4.11.1's ancestor details revealing algorithm; Chrome and Firefox do
  it): `page_anchor_node` finds the element, `dn_folded` says a closed details hides it (its `y`
  is then from an earlier layout, or none, and is not used), `dn_reveal` opens them, innermost
  first, each closing its group's open one. A link on the page shown that does it says so
  (`BR_SAME` with `relaid`), and `EWEB.PRG` draws the page again there.
- **A group** (the `name` attribute, 4.11.1, the exclusive accordion): opening one closes the
  group's open one, and as a page is parsed a later open one of a group starts closed (eight
  groups a page are kept track of). A group is a tree's: a details not in the page (made by a
  script) or in a `<template>`'s contents is in none of the page's groups; a script's details
  put in the page closes when its group has an open one (its insertion steps).
- **Scripts** (`EWEB.PRG`): `open` is the attribute, as before; setting or removing it (a click,
  `open`, `setAttribute`, `toggleAttribute`, a summary's `click()`) queues a `toggle` event,
  fired after the script, with the timers, as the standard's task: one an element, the first old
  state kept if it changes again before it fires (`oldState`, `newState`; `ToggleEvent` for
  scripts' own, the two `""` when not given); a group's other one closed gets its own. A details
  open as the page (or `innerHTML`) is parsed has its toggle event too, `closed` to `open`, as the
  parser's setting `open` runs the same steps (browsers fire it as the page loads; pages fill a
  details' content from it). One the parser closes for its group gets none.
  `HTMLDetailsElement` is there for pages that test for it. The CSS engine matches `:open` (a
  details or dialog with `open`; Selectors 4, HTML 4.16.3), which it took for a pseudo-class that
  never matches.
- **Measured** (`build/m68k/drawbench -P 1` and `-P 2`, `tools/count68k.sh`, the two programs run
  from paths of one length), the page set's 258 pages, the sample page and the news page (none of
  them has a `details`): the layout 0.042% more instructions (3.5 K a page in the middle, a page
  from 0.07% fewer to 0.11% more; 0.092% in the first version, whose details called the element
  builder from a second place, so GCC stopped inlining it into `build`); the
  styling 0.47% more, 25.8 K of it once a program run (the two rules of the UA sheet, which
  `EWEB.PRG` reads once, and the new names), 1.7 K a page. The font's new characters make each face
  39 to 120 bytes larger, 7 K instructions a page more to unpack the faces it uses (once a run, as
  `EWEB.PRG` keeps them). `EWEB.PRG` grew 4.7 KB, `EWEBST.PRG` 2.7 KB, `EWEB.FNT` 5.4 KB. No page
  of the page set lays out differently (ewrender's display lists before and after). The review's
  fixes (inline summaries, a place in a closed details, the rest above) then made the layout
  0.88% fewer instructions on the same pages (0.84% a page in the middle, from 1.85% fewer to
  0.06% more), the styling the same: the line's text items (`ITEM`) carry the inline summary's
  details, which makes them 64 bytes on the 68030, not 60, and an index into them a shift; that
  alone is 1.03% fewer (13 of the pages, the old code with the record padded), so the fixes
  themselves cost about 0.13%. `EWEB.PRG` grew 1.4 KB more (6.1 KB in all), `EWEBST.PRG` 1.3 KB
  (4.0 KB). The second review's fixes (the 1 MB ST, a summary with `display: contents`) change no
  page of the set either (ewrender's display lists), and leave 16 of its pages 0.003% fewer
  instructions to the end of the layout (`drawbench -P 2`); `EWEB.PRG` 0.5 KB more (6.6 KB in
  all), `EWEBST.PRG` 0.5 KB (4.4 KB).
- **Tested:** `tests/layout/details.html` (closed, open, nested, without a summary, the summary not
  first and a second one, a heading in the summary, `list-style: none`, a flex summary, right to
  left, a group, inside markers, strings) and its list, looked at with ewrender first;
  `tests/layout/details.py` (make test: summaries clicked through ewrender `-K`, which now clicks
  a summary in both editions: opened, closed again to the very same list, a group, the ST edition
  the same as `EWEB.PRG`); `tests/js/details.html` (the toggle events, their order, coalescing, a
  group, `click()` on a summary's child, a cancelled click, a summary clicked as a user does);
  after the review (same day): the summary first in an open one, `inline-block` and `inline`
  summaries clicked, a details in a summary, a group with one in a template, an address and a
  link to a place in a closed details (`tests/layout/details.py`), the toggle events of details
  open as parsed, a detached one and one put in the page, `ToggleEvent`'s defaults, a user's click
  on a closed summary going to the summary, not its details, when the two are the same size
  (`page_node_at`: of two blocks the same size, the child), in `tests/js/details.html`;
  `tests/css/cases.txt` (`:open`, the disclosure styles); `make csscmp` (no new difference: the
  disclosure styles are left out under `EC_COMPAT_KEYWORDS`, `:open` under
  `EC_COMPAT_SELECTORS`); `make h5test` (EWeb's parser 1,868 passed, the six scripted ones failed,
  here and on the 68030); `fuzz_html` 180 s (984,644 pages); `make itest`; `make hatarigui`'s
  new `details` group (a summary clicked open and closed on the emulated Falcon and with
  `EWEBST.PRG` on the 1040 ST, on the real TOS; after the review also an address with a
  `#fragment` in a closed details, a summary clicked with the window scrolled down, which stays
  there, and a place on the same page in a closed details), and its `browse` and `scripts` groups.
  After the second review: the group runs `EWEBST.PRG` on the 1 MB ST too (TOS 1.04, a FAQ page of
  its own: a summary clicked open and closed twice, a page loaded with a `#fragment` in a closed
  details, the page before kept for Back, and a place on the same page in another); its last
  attempt (the page shown given back) and its failure (the window empty, the alert) were forced
  once in a build of their own and looked at; `tests/layout/details.py` clicks a summary with
  `display: contents`; `tests/js/details.html` listens to the template's details, so that its
  having no toggle event is tested.

## Size, speed and memory (23 Oct 2026)

Erez asked whether EWeb is getting heavy. `EWEB.PRG` 0.27 is 1,856 KB; from the link map, its code
and data:

| Part | Size |
|---|---|
| JavaScript: QuickJS, musl's maths, the lean DOM | 897 KB |
| GEM side: the window, menus, dialogs, `common/`'s building blocks | 144 KB |
| Pictures (`common/src/pic.c`: libjpeg-turbo, PNG, libnsgif, the dither) | 138 KB |
| The HTML parser and the document | 130 KB |
| The runtime and libparserutils' character sets | 127 KB |
| Layout and drawing | 113 KB |
| TLS (BearSSL) | 100 KB |
| The CSS engine | 95 KB |
| The network | 88 KB |
| Browsing | 63 KB |

and 576 KB of BSS (the stack 256 KB, the CSS engine's buffers 189 KB). `EWEBST.PRG` is 518 KB
(703 KB with its BSS): no JavaScript, no TLS.

**Speed has gone the other way.** The two pages of the first timings (above), from the disk on the
emulated Falcon (Hatari, a 68030 at 32 MHz with TT-RAM, TOS 4.04, 640x480 in 16 colours), each the
second time (the first time after a start adds about a second, the fonts unpacked):

| Page | 18 Oct 2026 | 23 Oct 2026 | Parse | Style | Layout | Drawn |
|---|---|---|---|---|---|---|
| BBC News (100 KB) | 4.35 s | 2.96 s | 1.00 s | 0.19 s | 1.23 s | 0.33 s |
| WSJ (186 KB) | 9.49 s | 4.16 s | 1.64 s | 0.34 s | 1.70 s | 0.35 s |

The parse is three to four times faster (EWeb's own parser); the layout costs 13% more on the WSJ
page for the features since, and more on the BBC page, which now shows half as much again (7,486
pixels tall, not 4,965).

**Memory is where the size tells.** The program and its BSS take 2.4 MB before a page. On a
**4 MB Falcon** (16 MHz, no TT-RAM, TOS 4.04) 803 KB is left at the start, and both pages are
refused for memory. A build without JavaScript (which needs an FPU, which that Falcon has not)
left 1.54 MB and showed them (8.4 s and 11.1 s; a 176 KB WSJ page still didn't fit). With the
family's three accessories loaded (ECal, EContact, EHypView: about 2.4 MB of a 4 MB Falcon, the
family's known limit) about 1.1 MB is left for a program: TOS won't start `EWEB.PRG` in any of
these forms, and `EWEBST.PRG` starts with 319 KB free. The rigs had never run `EWEB.PRG` on the
4 MB Falcon. Erez's answer: one `EWEB.PRG` for every Falcon (below).

## One EWEB.PRG for every Falcon (23 Oct 2026)

Erez's decision (Decisions): the stock 4 MB Falcon is a target of `EWEB.PRG`'s, with the same
program as his DFB1's. What it took:

- **JavaScript in a file of its own, `EWEBJS.OVL`** (`atari/js_ovl.h`): QuickJS, musl's maths and
  `src/js/js.c`, 919 KB, next to `EWEB.PRG`, loaded the first time a page with scripts (a `<script>`
  or an `on...` attribute: `doc_has_scripts`) is opened on a site whose scripts are on, and only with
  an FPU (a stock Falcon never loads it), into the heap (TT RAM first), and kept for the rest of the
  run; the log says where it went, for a crash in it. It is linked at 0 with what it takes from `EWEB.PRG` left undefined
  (`atari/ovl.ld`); `tools/mkovl.py` makes it a TOS program's image (TOS's 14,196 fixups) with an
  imports' part: the 851 places that call or read one of the 58 things it takes from `EWEB.PRG`
  (`malloc`, the document's calls, `ec_stack_floor`), each filled in when it is loaded from
  `js_imports[]` (`build/gen/js_imports.c`, from the same tool), and its text starts with the 13
  calls `EWEB.PRG` makes (`js.h`). As the overlay reads EWeb's structures, a hash of the headers
  both are made from and `VERSION` (`JS_ABI`), and a CRC of the imports' names, must match: one from
  another build is refused. The caches are cleared after it is loaded (the 68030's CACR, CPUSHA on a
  68040 or 68060). A page whose scripts can't run (the file missing, damaged or from another version,
  for the rest of the run; too big for the memory, until a page next needs it) is shown as with its
  scripts off, and the status line says why. A review (same day) checked every relocation of the
  file by loading it at another address in a model of the loader, and the loader's bounds against a
  damaged file.
  `EWEB.PRG` is now 947 KB (1,856 before), 1.50 MB with its BSS (2.40), and has no FPU
  instruction at all.
- **Drawing in bands when memory is short:** the window's buffers for the screen's height (460 KB
  in 16 colours at 640x480) only when 2 MB is left beside them for the pages; else 32 rows at a
  time, as on the ST (the drawing took as long).
- **A page that doesn't fit when it is read** (a page loaded again beside the one shown) gives back
  the pages kept for Back and the one shown and is read once more, as a page that doesn't fit when
  it is laid out already did; a form sent isn't sent again.

On the emulated stock Falcon (16 MHz, no FPU, 4 MB, TOS 4.04, 640x480 in 16 colours) 1,829 KB is
free at the start (803 KB before), and the pages of the first timings show from the disk: BBC News
(100 KB) in 8.4 s, the WSJ page (186 KB) in 10.7 s, a 176 KB WSJ page that didn't fit at all in
13.6 s, each loaded again too. `make hatarigui`'s `falcon4` group shows a page of long paragraphs
(about 130 KB, 2,256 elements, 300 links, the same bytes each time) and loads it again, and the
pictures' page, with no scripts (no FPU, `EWEBJS.OVL` never loaded); its `scripts` group runs them
from `EWEBJS.OVL` and shows a page without it. Such a page of 151 KB took 31 s, 26 of them its
layout: 73 million 68030 instructions, half of them measuring its text a glyph at a time, the next
thing to make faster. A page takes about 11 times its size in memory (2 MB for the 176 KB page;
about 1 KB an element), and that varies by a few per cent from one build to the next, as the
cascade's cache of styles is keyed by addresses in the heap: the 151 KB page loaded again in one
build and not in the next (128 KB more for its styles), so 150 KB is where a 4 MB Falcon's pages
end, and the rig's is smaller. With the family's
three accessories loaded, a 4 MB Falcon has about 1.1 MB left for a program, and `EWEB.PRG` needs
1.5 MB to load: there it takes 14 MB or TT RAM (or `EWEBST.PRG`).


## EWeb's own 256 colours (23 Oct 2026)

Erez's decision (Decisions): in 256 colours EWeb sets colours of its own while it runs
(`ui/screen.c`, `PALETTE=1`, Settings > General > EWeb's own colours in 256 colours). The pixel
values TOS's sixteen pens don't draw with (16 to 254: VDI pens 16 to 254, as `vq_scrninfo` or TOS's
order says) get a 6x6x6 cube (each part 0, 51, 102, 153, 204 or 255) and 23 greys between the cube's,
more of them light (212 to 253, for pages' backgrounds); the sixteen stay, so the window, menus,
dialogs and icons keep TOS's colours. The colours set are read back as every mode's are, so the rest
follows by itself: the canvas's nearest colours, the pictures' dither (the 68030's and the DSP's, as
pic.c's pens), the decoded pictures (`img_pal_changed`; those kept on the disk are tied to the
palette's CRC, so others are decoded again) and the Pi's pens (`send_colours` sends the colours,
webgate keeps them by their CRC: no change to the protocol). TOS's colours are given back at the end
(and on each way out after the start, a crash too); turning the setting off or on gives them back or
sets EWeb's at once, and the page is drawn again. With `PALETTE=0` the screen's colours are left as
they are, for another program's pictures beside EWeb's.

On the emulated Falcon in 256 colours the page of translucent colours (`tests/layout/alpha.html`)
shows its pale yellow background and a pink red, where TOS's colours drew both grey, and the
gradients' page and the pictures are smooth. `make hatarigui`'s `screens` group checks that the
Falcon in 256 colours starts in EWeb's colours and gives TOS's back at the end, and runs it with
`PALETTE=0`, its screenshots side by side.

## Inline blocks (23 Oct 2026)

Stage 2's first part. A box with `display: inline-block`, `inline-flex`, `inline-grid` or
`inline-table` in the flow (not floated or positioned) was laid out as an inline: its words joined
the line's, its width, height, padding and background on each run of them, so buttons, menu
entries and cards of every site lost their shape. Now (`src/layout/layout.c`) it is a block that
is one atom in its line, after CSS 2.1 (9.2.4, 10.3.9, 10.8.1) and CSS Display 3:

- **Built** as a block box marked `ib` (a link round it is its link, so a button in a link
  still goes there); the boxes round it stay inline, and it starts a float context of its own (its
  floats are its, and the floats round it don't push its lines). A flex or grid container's
  inline-block child is an item as any. Its inside has what the text round it passes on: the
  opacity of the spans it is in, and the label it is in (a click on its text ticks the label's
  checkbox). A label that is a block or an inline-block itself (Bootstrap makes every label one)
  now passes its control to its text too (`L.ctl_in`); before, only an inline label did.
- **Measured** as a float is (its widest and narrowest for the line's, a table cell's or a flex
  item's width), and **laid out** once the line's width is known, before the lines are broken:
  as a block at (0, 0), as wide as it asks or shrunk to fit as a float, its items and blocks
  noted beside the line's items (`IBX`: kept apart, as every word is an item and a bigger item
  costs every page; inside `inline_layout` a stack, each line's let go when they are placed).
- **Placed** by its line: its baseline its last line's in the flow (a float or a positioned box
  after it doesn't count; a line moved by a flex's or a cell's alignment takes its baseline
  with it), an inline flex's, grid's or table's its first (CSS Flexbox 8.5, Grid 11.5, CSS 2.1
  17.5.1), or its bottom margin edge if it has no line or hides what overflows it (10.8.1), where
  `vertical-align` puts it (baseline, a length,
  `middle`, `text-top`, `text-bottom`, and `top` and `bottom` by the line's edges as a picture's),
  then its display items, its blocks (for scripts) and its elements' places (for an address's
  `#name`; walked without recursion, as it may be deep already) moved there. A hidden one
  (`visibility: hidden`) is placed too: what is visible in it shows, and the line keeps its
  room. One too wide for what is left of a line goes to the next, as a word;
  right to left it takes its place among the Hebrew words.
- **Boxes laid out first and moved after** (an inline-block, a float, an absolutely positioned
  box: each laid out at (0, 0), then moved where it goes): an absolutely positioned box in one
  of them whose containing block is outside it (the inline-block isn't positioned; or it is
  `position: fixed`) is left until that box has moved, then placed from where it would have
  been (`L.later`, `place_later`), in the opacity and the label it was in: before, it was moved
  with it, away from where its `top` and `left` put it. This was so for floats too. Placed from
  a line, it is laid out with the line's items kept aside (as a float in the text is), and the
  list of those left for later is let go only above the range being placed. A float's and a
  positioned box's places for scripts (and for a click's target) now move with them, as an
  inline-block's do; before, they stayed at (0, 0).
- **Widths:** an absolutely positioned or fixed child is no part of its parent's widest
  (CSS 2.1, 10.3.5): an inline-block, a float or a cell holding a dropdown was as wide as the
  dropdown.
- **Visibility:** a block's background and borders aren't drawn when it is hidden (they were);
  its text never was.
- **Left as they were:** pictures and form controls (their own atoms already, with
  `display: inline-block` from the UA sheet), and `<svg>`, `<video>`, `<canvas>`, `<iframe>`,
  `<object>` and `<embed>`, which EWeb doesn't show; a span's background round an inline-block
  stops at its edges, and `::before` and `::after` with `display: inline-block` are still laid
  out as inline.

**Measured** (68030 instructions, `build/m68k/drawbench`, the page laid out and drawn at
640 pixels, against the same code without inline blocks): the test page `tests/layout/inline.html`
3.88 M to 4.13 M (+6.3%: each box is now laid out as a block); the two pages of the page set
that name `inline-block` most (41 times each), 18.74 M to 18.76 M and 45.76 M to 45.86 M (+0.1%,
+0.2%); the news page, whose sheet names it 16 times, and the WSJ and BBC pages, without any,
within half a percent either way, which is the noise between two builds of the same code
(`memcpy` and `memset` move with the heap's addresses: Size, speed and memory, above; the news
page measured 13.34 M and 13.41 M in two builds of one commit). The first version kept an inline block's places in
every item, a quarter more bytes for each word, and cost the WSJ page 1.1%; moved beside the
items, the cost went.

Tested by `tests/layout/inline.html` and its list (baseline, `middle`, `top`, `bottom`, links
round and as buttons, a menu, cards a third wide side by side, an empty one, nested, an inline
flex with text of its own beside a float, in a table cell, too wide for its line, right to
left), `tests/layout/details.txt` (an inline-block summary now has its border and padding),
`tests/layout/inline.py` on `inline_cases.html` (the review's cases, same day: a click on the
text of an inline-block, block and inline label; a hidden one; one in a translucent span; a box
positioned in one; the baselines of an inline table, an inline flex, one with a float after its
last line and one with a centred flex in it; an address's `#name` in one; and the second
round's: text after one holding a dropdown drawn once and in its place, a fixed box in a
positioned box in one, an inline-block whose line is only another, a label's positioned box),
and the page set
with and without Bootstrap's and Pico's sheets under AddressSanitizer and UBSan: no fault.

## Scrolling sideways (23 Oct 2026)

Stage 2's fourth part. A page wider than the window (a table or a picture with a width of its
own, a site laid out for a wide screen) was cut at the window's right edge. Now the layout notes
how far right what it draws goes (`page.right`: its widest item, the canvas's picture aside; at
most 16384 pixels; the window's width when the root or the body hides what overflows sideways,
as many sites do to keep a phone from scrolling that way), and the window (`ui/win.c`) has a
horizontal slider and arrows (the guidelines' "sliders where content scrolls"), and **Left** and
**Right** move the page a little sideways, with **Shift** a window's width, when no field has the
keyboard (there they move the cursor, as before). The page's column at the window's left
(`left`) goes into drawing and into every place a point on the screen becomes one on the page
and back (links, controls, a field's cursor, summaries, a script's click); what stays on the
screen is moved with `vro_cpyfm` and only the new columns are drawn, as up and down; a control
given the keyboard is brought into view sideways too. A new page starts at its left; one laid
out again keeps its place, within its new width.

Tested by `make hatarigui`'s `wide` group (the Falcon in 16 colours, TOS 4.04): a table 1400
pixels wide; Right moves it sideways (the log's `sideways LEFT of MAX`), Shift+Right to its end,
and a link that was off to the right, clicked where it is then, is followed.

## Boxes that hide what overflows them (23 Oct 2026)

Stage 2's fifth part, the first half of "boxes that scroll". A box with `overflow: hidden` or
`clip` showed all of what was in it: a carousel's slides past its edge, the rest of a teaser cut
to three lines, a menu folded to its first row, a picture cropped to a square. Now
(`src/layout/layout.c`, `src/gfx/canvas.c`; CSS Overflow 3, 3 and 3.1):

- **What it hides isn't drawn or clicked.** Such a box has a `DL_CLIP` item (its padding box,
  placed once its height is known), and every item made in it carries it (`dl_item.clip`, 1 +
  its place in `page.clips`); a box in another carries the outer one, so they chain
  (`page_clip`). The canvas draws a clipped item on a canvas of what is left of it (its patterns
  keep the page's place; a glyph's ink past its word's edge is drawn where the box shows it), and
  a click outside it, or a script's, isn't one on its link, control, summary or element. Each
  axis clips by its own value, and one `visible` beside one that isn't `visible` or `clip` is
  `auto`, which scrolls (Boxes that scroll, below). Its height is the one asked for when its
  `overflow-y` isn't `visible` (before, `auto` and `scroll` kept the height but cut nothing, and
  what was in them ran over the boxes below: a code block with `overflow-x: auto` and a
  `max-height` over the paragraph after it).
- **Positioned boxes:** an absolutely positioned box is within its containing block's clips and
  those round it, not those of a box between the two (a dropdown placed against a box outside
  the one hiding its overflow shows past it, as in every browser); a fixed box is within none.
- **Moved with its box:** an inline-block, a float or a positioned box laid out at (0, 0) and
  moved takes its clips with it, and a cell or a flex item stretched to its row's height
  stretches its clip too (the fifth of a block's border items, `NBD`).
- **The root's and the body's** are the window's: they clip nothing and keep no height for it
  (`body { overflow-x: hidden; height: 100vh }` would have cut a long page at a screen's
  height), and their `overflow-x: hidden` keeps the window from scrolling sideways (above). Rows
  and their groups have no `overflow` (CSS 2.1, 11.1.1): a row's clipped nothing of a cell
  over several rows.
- **Pictures' alt text:** a picture with no size yet (or shown as its alt text) whose alt text
  is wider than its line (a sentence of alt text in a narrow column) made the line, and the
  page, as wide as the text; now it is cut at the line's width, once, as it is laid out (form
  controls keep their widths). With it, and a page no more than 8 pixels wider than the window
  taken as the window's width (a width rounded up), the page set's pages wider than the window
  at 640 pixels went from 84 to 27 (tables and pictures of a width of their own: what
  scrolling sideways is for).
- **Left:** `text-overflow: ellipsis`,
  `overflow-clip-margin`, `clip-path` and the rounded clip of `border-radius` aren't done, and
  pictures and form controls draw within their boxes as they did. Text for screen readers only
  (a box of a pixel hiding what overflows it, or far off the page) still goes, with its clips.

**Measured** (68030 instructions, drawbench, against the `@import` commit): the five pages of
the page set that name `overflow: hidden` most (17 or 18 times) +0.09% to +0.21%, the news page
+0.09%, `tests/layout/tables.html` +0.12% (a box's two `overflow` values read, and each display
item 2 bytes more: 50 on the Falcon, 46 on the ST); `tests/layout/overflow.html` 1.9% fewer, as
what is hidden isn't drawn. The first version cost up to 0.47%: the alt text cut glyph by glyph
each time it was drawn (now once), the canvas's drawing of an item no longer inlined (called
from two places; one now), and a pass of its own over the items for `page.right` (now in the one
that sorts them into strips).

Tested by `tests/layout/overflow.html`, its list and `tests/layout/overflow.py` (no red pixel,
so no hidden text drawn, in `EWEB.PRG`'s edition and the ST's; a hidden link not followed when
clicked; a carousel's 2000-pixel track doesn't widen the page; a box positioned in one within it
when the box is its containing block, past it when that is outside; and the review's cases,
same day: a code block with `overflow-x: auto` and a `max-height`, the body's `overflow-x:
hidden` with a height, a button wider than its box, a script's click on a box over hidden text,
a row's `overflow`, italic text in a box hiding its overflow drawn as in one that doesn't),
`tests/layout/floats.txt`
(a float context hiding its overflow: its clip, drawn the same), the other lists unchanged,
`make hatarigui` (TOS 4.04, every group), and the page set with and without Bootstrap's and
Pico's sheets under AddressSanitizer and UBSan.

## Boxes that scroll (23 Oct 2026)

Stage 2's last part, the second half of "boxes that scroll" (`src/layout/layout.c`,
`src/gfx/canvas.c`, `ui/win.c`). A box with `overflow: auto` or `scroll` (or an axis `visible`
beside one that isn't `visible` or `clip`, which is `auto`) and more in it than it shows clips
it, as `hidden` does, and scrolls:

- **Laid out** (`scroller`): once what is in it is laid out, how far that goes right and down
  from the box (`extent`: the items with the box's clip in their chain, each cut to the boxes
  on the way that hide what overflows them) is kept (`page.scr`). When the page is done
  (`scrollers_place`: a row, a flex line or a grid may stretch the box after it was laid out),
  that, less the padding box, is how far it scrolls (a pixel or two, a glyph's box below its
  line, doesn't count), each bar taking its room from the other way; and each way it scrolls
  has a bar (`DL_SBAR`, over the padding box's right edge or foot, made after what is in the
  box so drawn over it, as see-through as the box), its thumb as long as the part shown is of
  the whole. It is as wide (down) and high (across) as the AES's own slider column and row
  (`LAYOUT_OPT.sbar`, `sbar_h`: panebar's `pb_width` and `pb_height`, 23 pixels on TOS 4.04 in
  VGA, 19 on TOS 1.04 in ST high; 10 where nothing says, as ewrender lays it out unless `-a`). A box whose height is its own (`auto`) with a bar across its foot is
  that much taller, as in browsers, so the bar covers none of its last line. A hidden box
  (`visibility: hidden`) has no bars.
- **Scrolled** (`page_scroll`): every item with the box's clip in its chain moves by the change
  (a box in it, and that box's bar, with them), and so do the elements' places kept for scripts
  (`page.blocks`, which now carry their clip too: a click on a place a box hides isn't one on
  the element there, nor is a script's); the strips are made again. Drawing and every hit test
  (links, controls, summaries, a script's click, a bar) then take the items where they are, with
  nothing more. A box scrolled stays so when the page is laid out again (a picture's size come,
  the window sized, Larger text): `LAYOUT_OPT.was`, the page before, by element (not a
  `::before`'s or `::after`'s box, which has none). A link to a `#name` in a box, and a control
  given the keyboard in one, scroll the boxes it is in to it (`page_reveal`) before the window
  goes there.
- **In the window:** a box's bars are the AES's (the guidelines' 5.1, your choice of 24 Oct
  2026, "AES look for pane bars"): common's `panebar`, AES objects drawn by `objc_draw`, so
  TOS's look, its 3D (5.6), or XaAES's and N.AES's themes. The canvas leaves them out
  (`canvas.aes_bars`); `ui/win.c` draws them with `pb_draw` over the page's copy on the screen,
  each within the boxes hiding what overflows that it is in and the page's area, and the square
  where a box's two meet with `pb_corner`. A press is `pb_click`'s: an arrow a line at a time
  (held, some 15 a second), the track a boxful (less a pixel), the slider dragged; the box alone
  is drawn again (the log's `box N scrolled to X,Y`). Until 0.33 they were EWeb's own, 10 pixels,
  a track and a thumb without arrows.
- **White space:** a line of `white-space: pre` or `nowrap` longer than its box was broken at
  its words, so a code block never needed to scroll; now nothing in it (a word, a picture, an
  inline-block, an inline's padding) starts a new line, as in every browser (CSS Text 3, 3): in
  a box that scrolls it scrolls, elsewhere it overflows. A text file is shown as browsers show
  one, `white-space: pre-wrap` (its long lines wrapped). Such a run counts whole in a table
  cell's, a flex item's or a float's narrowest (min-content), and a table is no narrower than
  its columns' narrowest (CSS 2.1, 17.5.2): before, a table given less room than that squeezed
  its columns below it and broke words in the middle (`oth` / `er`); now it is wider and the
  page scrolls sideways. That takes the page set's pages wider than the window at 640 pixels
  from 27 to 45, most of the new ones 975 to 1050 pixels (rows of pictures and links in a
  table); no list of `tests/layout` changed.
- **Left:** no keys for a box (Up and Down scroll the page); a script's `scrollTop` and
  `scrollLeft` aren't the box's; text that
  goes on under a vertical bar is reached by scrolling the box a bar's width sideways, as the
  box's lines aren't laid out again a bar narrower; the AES's bars are drawn over the page, so
  one under a positioned box with a higher `z-index` shows over it.

**Measured** (68030 instructions, drawbench, against the commit before): the page set's pages
that name `overflow` most and the news page +0.09% to +0.12% (each box's `overflow` looked at
once more, each picture's and inline-block's `white-space`; none of the page set's pages has a
box with a bar, as they come without their own sheets, nor with Bootstrap's or Pico's), one of
them 6.7% fewer (a box a pixel wide with `overflow: auto` now cuts the text that went down it a
letter a line), `tests/layout/scroll.html` 4% fewer than when all of each box showed.

Tested by `tests/layout/scroll.html`, its list and `tests/layout/scroll.py` (seven bars in six
boxes, none for a box whose content fits; a link at a box's foot not followed when clicked until
the box is scrolled to it, then followed; a box scrolled kept so when the page is laid out
again; a box in a box moved with the outer one; every box scrolled to its end and drawn, in
bands too, in both editions under the sanitizers), `tests/layout/overflow.py` (the code block
with a `max-height` scrolls in its box, the paragraph after it below), `make hatarigui`'s
`boxes` group (the Falcon in 16 colours, TOS 4.04, panebar's bars as wide as the log's `box
bars` says: the down arrow a line, clicks on the track below the slider take the box to its
end, the link brought into view clicked and followed, Back, the slider dragged to the top
scrolls it back; a box scrolling both ways, its right arrow and its track across to the far
end), and the page set with and without Bootstrap's and Pico's sheets under
AddressSanitizer and UBSan. A review (same day) found thirteen faults, each fixed and checked on
its page: bars and reach left as they were when a flex line or a row stretched the box, script
clicks and places not moved, a nowrap run spilling out of a table cell, text under a bar out
of reach, bars on hidden boxes, a `#name` link into a box, a box clipped one way counted as
endless, boxes above the page's top, floated controls losing their height, room for a bar not
made, `::before` boxes' scrolling given to another, text files no longer wrapped, and pictures
and inline-blocks in a nowrap run still wrapped.

## `@import` (23 Oct 2026)

Stage 2's third part. A sheet's `@import` was passed over, so a site whose sheets come through
one (a `main.css` of `@import`s, WordPress themes, many older sites) had only its HTML's look.
Now (CSS Cascade 4, 2) the CSS engine asks an importer for the sheet (`ec_set_importer`,
`docs/CSS.md`), and its rules go where the `@import` is, before the importing sheet's own: only
an `@import` before any rule counts (`@charset` and `@layer`'s statement may come first); its
media queries are an `@media`'s (evaluated again when the window is sized, as all are), its
`layer` or `layer()` puts it below the unlayered rules, its `supports()` is an `@supports`'s; at
most 32 a page, 4 deep, so a loop of sheets ends. EWeb's importer (`import_cb` in `src/browse`)
fetches it as a `<link>`'s sheet is (the disk cache, Esc, the status line's "stylesheet N...",
the address made absolute against the importing sheet's, a `<style>`'s against the page's), and
its `url()`s are against its own address. nsbench and csscmp give no importer, so their figures
and LibCSS's comparison are as they were (`make csscmp`: the same differences, all with a
reason).

Tested by `tests/css/cases.txt` (six cases from CSS Cascade 4: where its rules go, one after a
rule, media, a layer, `supports()`, a loop; `ectest` now takes a case's other sheets as
`--- file NAME`) and `make itest` (`/site/imp/page.html`: a linked sheet in a folder importing
one in a subfolder, which imports one two folders up and has a `url()` of its own folder's; a
sheet for print never asked for; one for a wide screen; one after a rule; a `<style>`'s
`@import`; each sheet asked for once).

## Tables: borders that collapse, cells over several columns (23 Oct 2026)

Stage 2's second part (`src/layout/layout.c`).

- **`border-collapse: collapse`** (CSS 2.1, 17.6.2), which most sites' tables ask for: before,
  EWeb laid them out with the UA sheet's 2 pixels between cells and each cell's own borders,
  so a grid of 1-pixel lines came out as double lines with gaps. Now there is no spacing and no
  table padding, and each edge between cells is drawn once, by the cell after it (its top and
  its left; the cells on the table's right and at its foot draw those too): the border that
  wins of those meeting there (17.6.2.1: `hidden` wins and hides them all, then the wider, then
  by style, `double` first; a cell before its row, its row before its group, its group before
  the table, and the cell further left or up on a tie). The table's own borders are its edge
  cells' (room for them kept in the edge columns, as when they are its own), and the rows' and
  groups' borders (a line under each row, a heavier one under the header: borders that only
  this model draws) are their cells'. `collapse_row` works out each row's edges before its
  cells are laid out (per column, the bottom of the cell above and the right of the cell
  beside, through cells over several rows and columns); a place with no cell still has the
  line under the cell above it and the table's border beside it, and a hidden cell
  (`visibility: hidden`) shows none of its own but leaves its neighbours' (`collapse_gaps`, once
  the row's height is known). A table collapses only when it has rows of its own (in it or in
  its row groups): one without keeps its border (normalize.css and Bootstrap set
  `border-collapse: collapse` on every table, and it is inherited by a `display: table` box in
  one). A cell's widest counts the right border of the cell before it, which may be the wider
  of the two on their shared edge. Colours as drawn: every style draws solid, as before.
- **Rows and their groups** have no borders, padding or margins in either model (CSS 2.1, 17.6.1
  and 8.3-8.4); EWeb drew a row's border in the separate model.
- **Cells over several columns** gave no width to their columns, so `colspan="2"` text was
  squeezed into two narrow columns (`two columns` became five lines). Now (17.5.2.2) each gives
  what it needs beyond its columns' widths and the spacing between them, shared by their widths,
  after the cells of one column; `columns()` notes them as it goes, so its second pass doesn't
  walk the rows again (64 a table; more, and it does).
- **Left:** columns' and column groups' borders aren't part of the collapsing; half of an edge
  cell's border doesn't go into the table's margin (the whole of it is inside); an anonymous
  cell (text straight in a row) draws none of the edges it would own.

**Measured** (68030 instructions, drawbench, against the inline-block commit): pages without
collapsing tables or spanning cells unchanged (WSJ +0.2%, BBC +0.03%: noise); the box scores
page of the page set, 35 cells over several columns, 36.90 M to 37.48 M (+1.6%: those cells
measured, as they must be; walking its rows twice made it +2.9% at first); `tests/layout/
tables.html` +0.5%. Six of the page set's 258 pages draw differently,
each by its spanning cells (their columns wider, "View all podcasts & RSS feeds" on one line
where it took three).

Tested by `tests/layout/collapse.html` and its list (a grid of single lines; a table border
wider than its cells'; rows and a header with borders of their own; a wider, a double and a
hidden cell; cells over two rows and two columns and a short row, from HTML's `border`
attribute; backgrounds up to the lines; separate for comparison; a separate table in a
collapsing one; and the review's (same day): a table with no rows, rows in a wrapper, a short
row in a bordered table, a wide shared border, a hidden cell), the same in the ST edition, the
other lists unchanged, and the page set with and without Bootstrap's and Pico's sheets (Pico
collapses every table) under AddressSanitizer and UBSan.

## Google's JavaScript wall (23 Oct 2026)

Erez's report: google.com, words typed into its field, and the next page said to turn on JavaScript
to keep searching, with Options > Run this site's scripts ticked. Since January 2025 Google shows
its results only to a browser that first runs a check of its own; without it, a `/search` page's
`<noscript>` goes on to `/httpservice/retry/enablejs`, the page that says so, and `/sorry/` is its
CAPTCHA. EWeb's little JavaScript (QuickJS on its lean DOM) doesn't pass the check, so with scripts
on or off a search ends there; making it pass would mean chasing a check made to tell the big
browsers from the rest, which changes as Google likes.

Erez's rule (23 Oct 2026): EWeb relies on the Pi as little as it needs to and is self-contained. So
EWeb answers the wall itself (`src/browse`, `google_wall`): a page from Google's search (its host
`google.TLD`, `www.` or not, `co.` and `com.` before a country's) that is the wall, the "turn on
JavaScript" page or the CAPTCHA, is shown as a page of EWeb's own in its place, which says why and
links to the same words with the search engine in Settings (`SEARCH=`, FrogFind by default: no
JavaScript, no Pi). The words are the `/search` address's `q`, the CAPTCHA's from the address it
goes on to (`continue=`), and the "turn on JavaScript" page's from the page before it. Only a Google
`/search` page is looked through for the wall, so other pages cost nothing. The Pi's browser is
named last, and only when the Pi gateway is set up.

Tested by `make itest` (`/search`, `/httpservice/retry/enablejs` and `/sorry/` as `www.google.com`
through `EW_HOSTS`, as Google sends them to a browser without its check: the page in their place and
its link, with scripts on and off, from the CAPTCHA's address and from the page before, with no
search engine set, and a `/search` with results left as it is).

## Direct connection (24 Oct 2026)

Erez's decision for the family (the guidelines' 7.4): what was "Falcon mode", TLS on the Atari
itself, is called **Direct connection** everywhere the user sees it (the Options menu, Settings'
Secure page, alerts, the guide, the README), since a TT, or any Atari with a 68020 or better, does it
too. `EWEB.INF` keeps `FALCON=`, its note naming the Direct connection, so settings carry over. The
other apps move their TLS into the family's `TLS.OVL` and drop their ST editions; EWeb keeps its TLS
built into `EWEB.PRG` and keeps `EWEBST.PRG`, the whole browser built for the 68000, so for EWeb
only the name changes. (The notes below from before 24 Oct say Falcon mode.)

## History and visited links (24 Oct 2026)

Stage 3's first part. **The history** is `src/browse/history.c`: each page shown (fetched, not
EWeb's own) with its title and when, the newest 1000 kept in `HISTORY.TXT` next to `EWEB.PRG` (a
line each, "when, address, title" between tabs, oldest first), read when first needed (the first
page fetched: EWeb's own pages, the start page among them, aren't marked) and written once at quit
when it changed (the guidelines' 6.5; `make hatarifiles`' budgets have the read and the write). `about:history`
lists them newest first under the day each was last shown, a hundred to a page, so a long history
fits a small machine. View > History is ^Y, Safari's key (the family takes a Mac app's; ^H is
Backspace's code), and Options > Forget history forgets them, the file at quit.

**Visited links** (Selectors 4, 8.2): before a page is styled, each of its links is looked for in
the history (`src/browse/visited.c`, `EC_NODE_VISITED`, part of the style cache's key), and `:visited`
and `:link` match as they say. As in the other browsers, what a page can learn from it is held back:

- a `:visited` rule sets colours only (`color`, `background-color`, the borders' colours,
  `-webkit-text-stroke-color`); its other declarations aren't taken;
- `:link` matches every link, as if none were visited; on a visited link (the element's own, or
  the one it is in) its colours aren't taken, which are `:visited`'s. So `a:link` with a background
  picture or `display: none` does the same to every link, and no request or layout tells;
- inside `:is()`, `:not()` and the like, `:visited` matches nothing and `:link` is `:any-link`, so
  `:not(:visited)` can't tell either;
- on a page whose scripts run, no link is visited at all: the lean DOM's `getComputedStyle` gives
  the style as it is (the big browsers' answers as if no link were visited), and such pages are
  few on a Falcon.

An address is kept with its `#fragment`, as the other browsers key theirs, so a page's table of
contents stays unvisited until a link in it is followed (and that jump is kept too). A page goes
into the history before it is styled, so its links to itself are visited at once. When the history
changes, a page kept for Back is styled again when it comes back, once the window is idle, so Back
stays immediate; only colours change, so nothing on it moves. EWeb's own pages aren't kept for
Back but made again, so `about:history` is as the history is. An address with a control character
in it (a tab or a line's end would forge lines of the file) or a user's name and password isn't
kept, and the history keeps to 256 KB of addresses and titles on the Falcon, 96 KB on the ST,
the oldest going first.

**What it costs** (68030 instructions, the page set, `drawbench -V 1000 -M`: a history of 1000
pages, the links marked before the page is styled, against the page laid out and drawn). Resolving
each link as following it does (`url_parse_enc`, two 4 KB URLs allocated each time) took **7.27%**,
9,240 instructions a link. Now:

- `url.c`'s percent-encode sets and forbidden host characters are tables, not a `strchr` a
  character (every address EWeb parses gains);
- `url_join_quick` joins the common links (printing ASCII that needs no encoding, no `.` or `..`
  segment, a plain host and no port) without a parse, and `url_origin_quick` tells their origin.
  `make fuzz`'s `fuzz_url` checks both against the parse on every input, and the page set's
  26,713 links agree;
- the history's hash is djb2 (a 68030's multiply takes 44 cycles);
- a link whose origin has no page in the history is answered without being joined.

That leaves **0.97%** (1,229 a link), 0.67% on the median page by links and 2.05% at the most, with
the page's own site in the history and other sites not; 1.50% (1,904 a link) when every link's
site is in it, the most it costs. A page without links, or with an empty history, costs nothing.
`:visited` rules are real rules now (they were dropped as never matching), so `a:visited`, the UA
sheet's and a page's, is tried on every link: styling takes 0.71% more on the page set with no
history at all, 0.19% of the page drawn.

In 16 colours a visited link's `#551a8b` is as near the dark blue pen as the dark magenta one and
takes the first, dark blue, beside the bright blue of a link not visited; in 256 colours and more
it is the standard's purple.

Tests: `tests/browse/hgtest.c` (the history: the newest 1000, a place on a page another address,
origins, the file written and read again, one written by hand), `fuzz_history` (HISTORY.TXT read,
written and read back the same), the CSS cases (`:visited` colours only, `:is()` and `:not()`, the
style cache, a style attribute after a visited link, `:link` on a visited link and what is in
it), `tests/layout/visited.py` (in both editions: to a page and Back with `:link` and `:visited`
rules that would move things, laid out word for word the same; a link to the page itself, the
history read at the next start, a place followed, a page whose scripts run, `about:history`),
`make itest` (the same over HTTP, and another origin's links), and `make hatarigui` (^Y, and
HISTORY.TXT written at quit). A review found `:link`'s leak, the page's links to itself, absolute
links on a `file:` page, control characters and passwords in addresses, a kept `about:history`,
and the history's size: all fixed before it went in.

## Find in page (24 Oct 2026)

Stage 3's second part. Edit > Find... (^F, the family's) asks for words, then marks the first from
the window's top down in yellow and brings it into view, a box that scrolls too
(`page_reveal_item`, by the boxes hiding what overflows that the run is in, so the ST edition has
it); ^G finds the next and Shift-^G the one before (Safari's keys, as the family takes a Mac
app's), going round; the status bar says "2 of 5". `src/layout/find.c` folds the page's text runs,
in the display list's order, into one string: ASCII's and Latin-1's capitals small, white space a
space, a space between two runs with a gap between them (a word space, a line's end) and none
between two parts of a word (a bold "Ata" and "ri"). A space in the words matches any white space,
a line's end among it. The layout puts a line's runs in the list in the order they are read (the
line's words in its bidi order, `visual_order`, place them on the screen; the list keeps the
text's: a right-to-left line's runs right to left, a number or Latin words in it left to right),
so the string is the text as it is read (full bidirectional text is stage 4). A place on the page
is a run and a byte; the words found are the selection (below), kept the same way when the page is
laid out again; on another page none is marked. Text no one sees (opacity 0) isn't found. The canvas draws the mark under the
run's part (`canvas.mark`), from the run's right in a right-to-left one.

What a search costs (68030 instructions, `drawbench -F`): 0.24 to 1.3 million on the page set's
three biggest pages, 1.3% to 2.9% of laying each out, about 12 instructions a byte of text (the
folding by a table, the first byte tested before a match is tried); a search is made at each ^G,
the text not kept. Tested by `tests/layout/find.py` in both editions (order and round, back, case,
across runs and lines, Hebrew as it is read, hidden text, a box that scrolls, the mark drawn where
the words are) and `make hatarigui` (^F, ^G, Shift-^G on `about:history`).

## Selecting and copying (24 Oct 2026)

Stage 3's third part. A drag over the page selects its text from one place to another, once the
mouse has moved by more than 3 pixels with the button down, so a click still follows a link and a
drag that starts on one selects; the window scrolls a line at a time while the mouse is above or
below it. A double click selects a word (letters, digits and any character past ASCII, on into a
run beside it: the bold "Ata" and "ri"; on a link a double click opens it, as before), Shift-click
takes the selection on to there, Edit > Select all (^A) selects all the text (the address keeps ^A
while it has the keyboard), and a click elsewhere lets it go. ^C copies the selection, else the
page's text as before; Find's words are the selection, marked yellow (a selection made by the
mouse light blue, `#99ccff`: grey in 16 colours, a light pattern in monochrome), so ^C copies
them and ^G goes on from it, as in other browsers.

`src/layout/find.c` has it all on the page's runs in the display list's order, which is the order
they are read (above). A selection is a stretch (`pg_range`) from one place to another, the first
earlier in the list: an item is in it between the two, so the canvas marks it with two compares
(`canvas.mark`). The place under the mouse (`page_pos_at`) is the nearest run, on the line before
one above or below, then the nearest to the side, not one hidden by a box hiding what overflows
it; the byte is the nearest gap between two characters, from the right in a right-to-left run,
never before a combining mark (an accent, a Hebrew point: it goes with its letter, whether
`EWEB.FNT` has it or not). A double click selects a word only on text, never the nearest far away.
A selection's ends are kept as places in the page's text, by the characters before them that
aren't white space (`page_text_index`): when the page is laid out again its lines end elsewhere
and its runs are others, but its text is the same, so `page_text_place` finds the same stretch
again. A stretch's text (`page_copy_text`) is its runs in order, nothing between two parts of a
word (a superscript's "2" in "mc2" too: runs whose heights overlap by more than half are on one
line), a space between two runs apart on a line, CR LF between lines and an empty line before a run
lower than two of its lines down (a paragraph's gap). A review found the first order wrong on
right-to-left lines with a number or Latin in them, which the layout's own order put right.

What it costs (68030 instructions, `drawbench -X`, on the page set's three biggest pages): the place under the mouse, at each of its moves, 20,000 to 50,000 (about 25 a display-list item: the distance down first, the clip of a box hiding what overflows only for the nearest; 1,911 items on the biggest list, under 5 ms on the DFB1; twice that at first); keeping a selection's ends when it is made, and placing them again after a layout, about 10 a byte of the text before them; copying about 5 a byte. While the mouse drags, only the
rows of the items between where the end was and where it is now are drawn again, and nothing is
looked for while the mouse is still. Tested by `tests/layout/select.py` in both editions (a drag either
way, beside a line and below the page, a double click's word, Hebrew as it is read, Select all
and the page's text, the same text laid out again at two other widths, the mark where it is
drawn; `select.html`'s right-to-left lines with a number and Latin, super- and subscripts, an accent
and a point with their letters, no word far from text) and `make hatarigui` (a drag over the files page's links: none followed, ^C's
`SCRAP.TXT` as ewrender's `-x` selects it; ^A and ^C on `about:history`).

## Esc while a page is made (24 Oct 2026)

Stage 3's fifth part. A long page took its parse, styling and layout without a turn for GEM: on
the page set's biggest pages 45 million 68030 instructions, about 4 s on the DFB1 and over 10 s
on a stock Falcon, with the window not redrawn and Esc heard only after. Now each stage asks
now and then: the parser every 64 tags (`doc_poll`, `src/doc/html.c`), the CSS engine every
32 elements (`ec_poll`, `src/css/select.c`, a hook of its own as the engine is built on its
headers only), the layout every 16 boxes made or laid out (`doc_poll`, `src/layout`); every 256
until 26 Oct 2026, when the stock machines showed a second and more between turns (Speed on the
stock machines, below). The hooks
are set only while `src/browse` builds a page (`build`); they are the pictures' poll
(`gem_turn`): GEM's turn ten times a second (the window redrawn, moved, topped; Esc, Undo, Stop),
and 0 once Esc has set `conn_stop`. Then the stage gives up as when out of memory (the parser's
longjmp, the engine's stop flag, the layout's `fail`), the new document goes, and the page shown
before stays, as when Esc stops a page coming: it was never let go, as a page is made beside the
one shown. A window sized meanwhile (it can't be while a page loads, but uniconified it might)
only notes the width, and the page is laid out at it when made (`br_building`).

What it costs: the counters, 0.10% to 0.15% of the three biggest pages' instructions with no hook
set (drawbench); the biggest asks 27 times while parsed, 16 styled, 35 laid out. Tested by
`tests/layout/stop.py` in both editions under the sanitizers (a page of 9,000 tags made here:
each stage asks, asking changes nothing, stopped at each stage's first turn and halfway the page
before stays, nothing left behind; ewrender `-I STAGE:N`), and `make hatarigui`'s deep group
(the Falcon: a page of 45,000 tags, Esc once the status says it is laid out: "stopped", the page
before shown, the heap back to what it was).

## A page drawn while it loads (24 Oct 2026)

Stage 3's fourth part. A page was shown only once all of it had come and been laid out: on a slow
line, or for a long page, the window kept the page before for seconds. Now a load may show a
preview first, once: the page's start (up to 24 KB of its text, cut where a tag begins) parsed,
styled and laid out as the page will be, without its scripts, drawn in the window in place of the
page shown until the page itself is (`br_preview_step`, `preview` in `src/browse`). It comes

- while the page comes: a second into its request, with 6 KB of its text there (the window's idle
  turn asks; a gzip page's bytes so far unpacked as far as they go: the inflate keeps what it
  unpacked before they ran out);
- or, for a long page (over 48 KB) that came quicker, once its stylesheets are in, before the
  whole is styled and laid out.

Its stylesheets are only those EWeb has (the memory cache and the disk cache, of any age: the
fetch says so with `pv_offline`); one that would have to be fetched means no preview, rather than
a page drawn unstyled for a moment (a site visited before has its sheets). The window draws and
scrolls the preview (`shown()` in `ui/win.c`); the mouse and keys on the page wait for the page,
as they did, the selection isn't marked on it, and its place isn't kept for the page shown before.
Then it goes (`loaded`): the page is drawn where the preview was scrolled to, unless it goes to an
anchor; after Esc, the page before again.

A preview takes memory beside the page: one is made only where both fit in half the RAM a machine
of ST RAM alone has free (`pv_room`: the cost a kept page is counted at, the page's and the
preview's, within four times `br_keep_max`), which with TT RAM any page does; a page whose length
isn't known yet counts as a long one (48 KB, or four times the gzip bytes come). On the stock 4 MB
Falcon a page of up to about 55 KB has one, and its big test page (130 KB, which takes 1.9 MB of
its 2.1 MB) none; the ST edition never makes one. A preview made as the page came, when the whole
page turns out not to fit beside it, goes before the page is read (the page shown is drawn again
until then), and a page that doesn't fit gives it back first (make_room). Tested by `make
hatarigui`'s browse group: the test server's `/site/slow.html` (66 KB, gzip, two parts 2.5 s apart)
logs a preview before it is shown; and its falcon4 group's big page, loaded and loaded again.

## Line breaks inside words (24 Oct 2026)

Stage 4's first part. A line ended only at a space (or, when a word was wider than the line, inside
it wherever it ran out); a soft hyphen (`&shy;`) was drawn as a hyphen wherever it was, a zero width
space as a missing character, and Chinese or Japanese, which has no spaces, broke only where it ran
out. Now a line may also end inside a word where UAX #14 (Unicode's line breaking) lets it, for what
pages have (`lb_inword` in `src/layout/layout.c`):

- after a hyphen or a slash between a letter or digit and a letter ("well-" "known", "and/" "or", a
  long address after each `/`), not before a digit ("2020-2021", "1/2") nor at a word's start;
- after an en dash ("10–" "20"), on either side of an em dash, not between two;
- on either side of an ideograph (Chinese, Japanese, Korean), but not before closing punctuation,
  small kana or the prolonged sound mark, nor after opening punctuation (the classes CL, CP, NS, EX,
  IS and OP: Japanese kinsoku), nor by a quotation mark (QU);
- at a zero width space, and at a soft hyphen: neither is drawn, and a line that ends at a soft
  hyphen shows a hyphen at its end (when it fits there; not on a right-to-left line).

None in `white-space: nowrap` or `pre`, where the soft hyphens still aren't drawn. A word with such
a place is made of pieces, each an item of the line marked with the place before it (`canbreak` 3,
or 4 at a soft hyphen), so the line breaker and the narrowest width (a table's or a grid's column)
take them as they take spaces: the grid test's `fit-content(80px)` column now breaks "fit-"
"content(80px)" and is the 80 px it asks for. The text's bytes go through a table (`lb_class`), in
the loop that copies a word, which sends only a word with a hyphen, a slash or a character that may
matter (U+0080 to U+00BF, from U+2000) to `word_pieces`; Latin, Greek, Cyrillic and Hebrew letters
are copied as they were. Measured (`drawbench`, the page set's 127 KB page): its layout 12.07 M
68030 instructions before, 12.12 M now (`word_pieces` 48 K of them, its 150 such words). Copying a
selection or finding words across a line ended at a soft hyphen has its hyphen in the text. Tested
by `tests/layout/breaks.html` in `make test`, and 150 pages of random bytes of those kinds under
AddressSanitizer.

## Hebrew with numbers and Latin (24 Oct 2026)

Stage 4's second part. Hebrew words were put right to left within a line and a neutral took its
neighbours' direction, but a word was drawn in one direction: "ב-2026" came out with its number
backwards ("6202-ב"), brackets in Hebrew pointed the wrong way, and a full stop after a number or
an English word at the end of a Hebrew sentence stayed on the wrong side. Now the line is ordered
by UAX #9 with its words for characters (`visual_order`): each word left to right (Latin), right to
left (Hebrew), a number (digits and what is between them: "3.50", "24/10/2026", "50%", "$5") or
neutral; a number after Latin is Latin (W7); a neutral between two of the same direction takes it,
a number counting as right to left (N1), else the line's (N2); the levels follow (I1, I2: a number
in Hebrew, and Latin on a right-to-left line, one above), and the runs at each level or above are
reversed from the highest (L2). A word drawn right to left (its level odd: `DLF_RTL` in its flags)
has its brackets and signs that point one way turned (L4: `mirrored` in `src/gfx/canvas.c`), so a
neutral word on a right-to-left line, "(" or "?!", is drawn so too. For that a word is cut where it
has to be (`word_pieces`, no line ending between the pieces): where Hebrew meets a number or Latin
("ב-" "2026", the hyphen with the Hebrew, as N1 has it beside a number; "ו-" "$5", a sign with its
number), and around punctuation beside letters of the other direction than the text's ("2026" ".",
"EWeb" "." and "(" "10" ")" in Hebrew; "שלום" "!" in English), so that UAX #9 puts it by the text
around it. The words' bytes say which need that (`lb_class`, the same loop as the line breaks'),
and also each word's direction, which `word_dir` worked out from each character before: the page
set's 127 KB page lays out in 12.01 M 68030 instructions, from 12.12 M; the Hebrew test page 1.4%
more than before. The embeddings came after (below). Tested by `tests/layout/bidi.html` in `make test` (the
results checked against the algorithm by hand), and the random pages above on lines of both
directions.

## Room on the stock 4 MB Falcon (24 Oct 2026)

`make hatarigui`'s `falcon4` group shows a page as big as most of the page set's (130 KB, 2,406
elements) on the stock 4 MB Falcon. 0.34 showed it with 5,872 bytes left; the embeddings' code (6 KB
more of `EWEB.PRG`) took that, and the page came out as "EWeb can't show this page". Measured with
`drawbench -m` (the heap after each stage, the Atari's allocator): parsed 974 KB, styled 1,176 KB,
laid out 1,941 KB with a peak of 2,016 KB. The peak was the display list growing: at 2,048 items it
was copied into one of 4,096, both there while it was, and 1,546 places were left empty after.
Now the list starts about as big as the page will want (an item an element and an eighth more:
`layout_page`), and what is left over is given back once the page is laid out: laid out 1,864 KB,
peak 1,880 KB (135 KB less), and the page shows with 130 KB left. A line item stays 64 bytes on
the 68030 (the embeddings' levels a byte array of the block's, `L.lv`, its controls in a gap item's
`dir`), as a block of tens of thousands of words has as many. Where the rest of a page's memory goes
(about 400 bytes an element before it is laid out) is the next section's.

## A page's memory (25 Oct 2026)

Before stage 5 (frames, iframes and more windows each keep a document of their own) the same
test page's memory, piece by piece (`drawbench -m` with each piece of the Atari's heap put down to
the code that asked for it), once it was laid out (1,864 KB):

| What | KB | |
|---|---|---|
| The document: 3,761 nodes of 118 bytes, their text, attributes | 606 | 1,355 of the nodes text, each a whole element's size |
| The layout's boxes (3,753 of 74 bytes) and the tables' column widths | 316 | kept with the page, though nothing used them once it was laid out |
| The CSS engine: five 64 KB pieces | 320 | the page's sheets, names and styles used 12 KB of its three, the UA sheet 36 KB of its two |
| The text runs, links, tiles | 140 | |
| The page's source (kept for Save page) | 128 | |
| The display list (2,550 items of 50 bytes) | 125 | |

Three changes, none of them to what a page shows (`tests/layout`'s lists are the same):

- **The boxes are let go once the page is laid out** (an arena of their own, `L.ba` in
  `layout_page`); the text runs, links, tiles and gradients the display list points at stay in the
  page's.
- **The CSS engine's arenas grow from 2 KB** (`EC_FIRST` in `src/css/core.c`, each piece as big as
  those before it together), as the ST edition's did from 512 bytes: a page's context now takes
  about what it needs, not 64 KB an arena.
- **A text node, a comment, a DOCTYPE or a processing instruction is 48 bytes**, not 118: an
  element's own part (its `ec_node`, attributes, classes, form and control) is at the end of a
  `dnode`, and `DN_SIZE` leaves it out of the others. `nattr` and `ncls` stay in every node, so
  `dn_attr` may ask any. In the checks' builds (AddressSanitizer) a short node is given a whole
  dnode with the rest poisoned (`DN_POISON`), so a read of an element's part in it is caught:
  `make test` caught one at once (the visited links' marking read every node's flags), reading
  the code another (a script's `cloneNode` copied a text node's name), and `fuzz_html` found
  nothing more in ten minutes (it found a media query's `em` overflowing instead, kept in
  `tests/fuzz/found/html`).

The test page: parsed 974 KB to 778 KB, styled 1,176 KB to 802 KB, laid out **1,864 KB to
1,162 KB**, and its peak (while it is laid out, the boxes there) 1,880 KB to 1,506 KB. On the page
set the memory a page keeps went from a median of 1,084 KB to 750 KB (the 90th percentile 1,663 KB
to 1,229 KB, the largest 4,116 KB to 3,138 KB), its peak from a median of 1,134 KB to 918 KB. The
page set's 68030 instructions (`drawbench`: parse, style, layout and drawing) went 0.05% down, each
page between 0.23% down and 0.35% up.

## Bidirectional embeddings (24 Oct 2026)

Stage 4's last part: what a page says of a part's direction. Before, a line was one level of text,
its direction the block's: `dir` on an inline element, `<bdi>`, `<bdo>`, CSS `unicode-bidi` and the
direction marks in the text (LRM, RLM, RLO...) did nothing, so a user name in Hebrew pulled the
colon and the number after it ("User 3 : name" for "User name: 3 posts") and a `<bdo dir=rtl>` was
drawn as it was stored.

- **The controls:** an inline element's `unicode-bidi` puts a control of no width on the line before
  and after it (CSS Writing Modes 3, 2.4.2: `embed` RLE or LRE, `isolate` RLI or LRI,
  `bidi-override` RLO or LRO, `isolate-override` both, `plaintext` FSI; then PDF or PDI): a gap item
  (its `dir` the control, `bidi_mark`) that draws nothing and no line ends before. The same controls in the
  text, and LRM and RLM, are cut out of their word as such marks (`word_pieces`, `bidi_char`); they
  are bytes from U+2000, which already go there.
- **The levels** are UAX #9's, worked out once a paragraph (a `<br>` or a newline kept ends one:
  B) rather than a line at a time (`bidi_levels`), into a byte an item kept beside the items
  while the block is laid out (`L.lv`; the items stay 64 bytes, as a big page has tens of
  thousands, and a stock 4 MB Falcon has room for no more): X1-X8 (the embedding stack, its overrides and
  isolates, the overflow counts), X9 (the embeddings' controls go), X10 (the level runs, joined at a
  valid isolate and its PDI into isolating run sequences, each with its sos and eos), then on each
  sequence W7, N1, N2, I1 and I2, a word for a character as before (W1-W6 are a word's own: "$5",
  "24/10/2026"; N0, brackets, isn't done). A line (`visual_order`) then has its spaces and controls
  at its end at the paragraph's level (L1) and its runs reversed by level (L2); a word at an odd
  level is drawn right to left (`DLF_RTL`), so a `<bdo dir=rtl>`'s English comes out mirrored as
  in a browser. A paragraph of `unicode-bidi: plaintext` (a `<pre dir=auto>`) takes its first strong
  letter's direction (P2, P3), and its lines are aligned by it.
- **The UA sheet** has the HTML Standard's rules (15.3.5): `[dir=rtl i]` and `[dir=ltr i]` set
  `direction`; `[dir]`, `<bdi>` and `<output>` isolate; `<bdo>` is `isolate-override`;
  `textarea` and `pre` of `dir=auto` are `plaintext`. `dir=auto`, and a `<bdi>` without a `dir`, take
  their direction from their first strong letter (3.2.6.4: passing over `<bdi>`, `<script>`,
  `<style>`, `<textarea>` and elements with a `dir` of their own), as a presentational hint
  (`auto_rtl` in `src/doc/doc.c`).
- **Faster where there is no right-to-left text:** the levels are worked out only for a block with
  Hebrew, a control, `direction: rtl` or `plaintext`, and a line of left-to-right text alone isn't
  ordered at all; before, every line was, with a buffer allocated for it.

- **Right-to-left rows** (the selection review's notes, 24 Oct 2026): a table, a flex row or a
  grid whose `direction` is `rtl` was laid out from the left. Now a table row's cells are each laid
  out where their mirror is (CSS 2.1, 17.5: the table's direction), its collapsed borders' fills
  mirrored once made (`mirror_items`); a flex row's main start is its right (`row-reverse` then
  from the left), a flex column's items to its right; a grid's columns from the right, an item's
  start its area's right.
- **Copying beside a block:** two flex items or table cells touching on a line were copied as one
  word ("onetwo"). The first run of each line box is marked (`DLF_LINE0`, DL_TEXT's use of
  `DLF_NOSIZE`'s bit), and a run that starts a line box is never part of the word before it
  (`touches` in `find.c`), so they copy and are found with a space between. Not an inline-block's,
  whose runs belong to its line. Left: an inline-block's text is copied before the line it is on
  (it is laid out, so listed, first), as before.

**Measured** (68030 instructions, `drawbench`, against 0.34): laying out the page set's three
biggest pages 1.1% to 1.3% fewer (395 KB: 14.65 M to 14.46 M), the Hebrew test page 1.8% more,
`tests/layout/bidi.html` 5.6% more; styling 0.3% to 1.6% more (the UA sheet's new rules for `dir`).
Tested by `tests/layout/embed.py` in `make test` (23 cases worked out from UAX #9 by hand: `<bdo>`
both ways, bidi-override, RLO and LRO in the text, `dir` on a span both ways, a plain span against a
`<bdi>` with the colon and the number after it, `dir=auto` both ways, RLM and LRM against neither, a
`<pre dir=auto>`'s paragraphs and their alignment, an embedding open across lines, a right-to-left
table with and without collapsed borders, flex row, `row-reverse` and grid) and
`tests/layout/select.py` (touching flex items and cells copied apart, a word's two runs together); every
`tests/layout` list unchanged, the Hebrew pages in every encoding.

## SVG (25 Oct 2026)

Stage 5's first part. NetSurf's part first, as the owner's rule says: **libsvgtiny**
(`third_party/netsurf/libsvgtiny`, 23 Oct 2026) reads an SVG into paths of lines and curves with
their fill and stroke, on NetSurf's libdom; ported onto EWeb's own document with a small libdom of
EWeb's (`tests/svg/dom.c`: a node a dnode, a file parsed by EWeb's HTML parser), its paths drawn by
the rasteriser below. Measured against librsvg (`rsvg-convert`) on 3,467 icons and logos (the
simple-icons set, CC0, commit `90646e4b`, not kept here), each at 48 x 48:

- **It drew 3,038 of them**: 429 failed on arcs whose flags are written without a space
  (`a2 2 0 11-.002 4.686`, which SVG allows and icon optimisers write), and a smooth curve
  repeated without its letter (`s1 2 3 4 5 6 7 8`) took the wrong control point, so a few hundred
  more came out garbled. It has no fill rule (even-odd), no opacity, no rounded rectangles, no
  `<style>`, no radial gradients; floats throughout, which `EWEB.PRG` has in software only.
- **EWeb's own reader** (`src/svg/svgread.c`, fresh, in whole numbers: `fixmath.c`) drew all
  3,467, on average 0.41 of 255 from librsvg's grey over white (libsvgtiny's: 1.29 on those it
  drew); 23 more than 2 apart, the worst 16.7 (fine lines' anti-aliasing). On the 68030 in soft
  float, as `EWEB.PRG` runs, on the icons both drew (`build/m68k/svgrender`, less an empty
  picture): **0.40 of libsvgtiny's instructions** at 48 x 48 (median 188 K against 435 K, 291
  icons of every fourth: the 68030 build of libsvgtiny stops with a bus error in its path parser
  on many that its host build draws; not looked into, it being the yardstick), 0.49 at
  192 x 192 (1.33 M against 2.97 M). So the fresh reader is `EWEB.PRG`'s, and libsvgtiny stays as
  the yardstick (`svgrender -T`).

What it reads: the shapes (path, rect with rounded corners, circle, ellipse, line, polyline,
polygon), every path command (arcs by their centre, a flat one, its radius over 32 times its
chord's half, from its chord and how far it bows, since 16.16 can't place a far centre), groups,
nested `<svg>`, `<use>` of an element or a `<symbol>`, the viewBox with preserveAspectRatio, the
transform attribute; fill and stroke as colours (CSS's: `ec_color_text`), `currentColor`, linear
and radial gradients (objectBoundingBox and user units, their `href` chain, spread, transform),
the opacities, the fill rule, stroke widths, caps, joins and the miter limit; presentation
attributes, the style attribute and the picture's own `<style>` (simple selectors). Not: text,
clipping paths, masks, filters, patterns, markers, dashes, animation. User units go to 8192 either
way (16.16 with room to add a few).

**The rasteriser** (`src/svg/raster.c`, fresh: NetSurf's front ends draw paths with the system's
calls) is FreeType's way in whole numbers: a point in 256ths of a pixel, each cell an edge passes
through keeping its cover and area, a row read from the left giving each pixel the exact area the
path covers in it (nonzero and even-odd), a band of 16 rows at a time so a big picture needs
little beyond its own pixels; curves halved with shifts alone until a piece strays under a fifth
of a pixel; strokes as each segment's quadrilateral with round, miter or bevel joins and butt,
round or square ends, the pieces wound alike so they join. Out comes a picture of the family's kind
(`PIC`: RGB 5-6-5 and an alpha), dithered as any other.

**In EWeb** an SVG file (an `<img>`, a CSS background, `pic_kind` or `svg_late` for one whose
`<svg>` comes after a long XML prolog) is an `img.c` picture like any other, drawn at each size a
page shows it (`svg_decode`, at most `img_max_px` pixels); EWeb asks for `image/svg+xml` and no
longer refuses `.svg` addresses. A page's inline `<svg>` is laid out as an `<img>`
(`LAYOUT_OPT.svg`): `browse.c` writes it out as an SVG file (`svg_markup`: its `color` its
currentColor, the CSS `color` it has; the elements elsewhere in the page that it names, a sprite
sheet's `<symbol>`, a gradient, in a `<defs>`) and keeps it as a picture under an address made from
it, so it is sized, drawn, kept and dithered as an `<img>`'s, and an icon a page shows twenty times
is one picture. Its size: its width and height, else its viewBox's proportions on 300 wide, else
300 x 150, CSS's as for any picture. A page's CSS doesn't reach into an inline SVG (EWeb's CSS
engine has no fill or stroke); its attributes and its own `<style>` do.

`EWEB.PRG` grew 55 KB, `EWEBST.PRG` 36 KB (both editions draw SVG). `make test` runs
`tests/svg/run.py` (18 cases, one a feature, against librsvg's pictures kept as greys in
`tests/svg/expect.txt`, at most 3 of 255 apart on average; the cases are 0.06 to 1.79 apart),
`tests/layout/svg.html`'s display list, and `fuzz_svg`'s seeds and finds (four, each fixed: a
`<style>` read past its end, and sums of numbers near 32 bits).

## Frames and iframes (25 Oct 2026)

Stage 5's second part. NetSurf's frames are its core's (its desktop's frames and its HTML
content's iframes: GPL-2.0-only, the owner's call, Open questions), so EWeb's are its own, built on
its layout's boxes that scroll:

- **Each frame is a document of its own** (a frameset's `<frame>`, an `<iframe>`), fetched after
  the page as its pictures are (`br_pictures_step`): a frameset's frames first (they are the
  page), then the page's pictures (and the frames'), then its iframes (an embedded player or a
  map is worth less than the page's pictures); one laid out smaller than 4 px (a counter's, a
  tracker's) never. It is parsed by EWeb's parser without scripts: a frame's don't run, and what
  most iframes hold now (players, adverts, comments) needs scripts the Falcon can't afford. Its
  sheets are fetched as a page's and it is styled for the frame's width. The frames hang on the
  page's document (`doc.frames`, `br_frames` in `src/browse`: at most 32, 12 in the ST edition,
  four deep), so a page kept for Back keeps its frames.
- **The layout splices a frame's page into the page's.** It asks for a frame's number when it
  meets one (`LAYOUT_OPT.frame`) and for the frame's document laid out at the size it gives it
  (`frame_page`). That page's items go into the page's display list in the order they are drawn,
  inside a clip of the frame's box that scrolls as a box with `overflow: auto` does (the AES's
  bars, panebar; laid out again beside its bar when it is taller than the frame, as a browser
  does). Its clips, boxes that scroll, links and controls become the page's, and the frame's page
  is kept with the page (`page.kids`: its text and tiles are in its arena). So drawing, clicks,
  scrolling, Find, selecting and copying work in a frame as they do in the page, with nothing new
  in the canvas or the window; on a frameset, which doesn't scroll, Up, Down, Space and ClrHome
  scroll the frame under the mouse.
- **An iframe** is a replaced box: 300 x 150 CSS px unless its width and height (attributes or
  CSS) say otherwise, each its own; `2px inset #767676` (`frameborder=0`: none); `srcdoc`;
  `scrolling=no`. **A frameset** fills the window (`LAYOUT_OPT.height`, laid out again when the
  window's height changes). Its rows and cols follow the HTML Standard's rules for a list of
  dimensions: the absolute first, then per cent, then the relative share what is left. Between
  its frames is a grey 3D border of 4 CSS px unless `border`, `framespacing` or `frameborder` say
  otherwise; framesets nest.
- **Where a link goes:** a frame's links are made absolute by its own address
  (`page_set_link`) and know their frame (`page.lfr`). A link's `target` (else its document's
  `<base target>`; a form's `target`, a button's `formtarget`) picks the frame: none or `_self`
  its own, `_parent`, `_top` the window, a name that frame. `_blank`, and a name no frame has,
  open another window (More windows, below). A frame's load is an
  entry in the history with the page's address and every frame's address then
  (`BR_ENTRY.frames`, "PATH URL" lines, a frame's path its place among its document's frames):
  Back to it loads only the frames that differ, and a page loaded again from the history (not
  kept) gets its frames as they were.
- **Not yet:** scripts in frames (and the page's scripts don't reach into them: a click in a
  frame is on its element), dragging a frameset's borders, `marginwidth` and `marginheight`, a
  frame's own refresh, HTML in `<object>` and `<embed>`.

`EWEB.PRG` grew 14 KB (1,212,238 bytes) and `EWEBST.PRG` 11 KB (638,818). A page without frames
is laid out as before (`tests/layout`'s lists are the same). Tests: `tests/layout/frameset.html`
and `frames.html` (their lists); `tests/layout/frames.py` (ewrender `-M X,Y` clicks and `-M back`:
links into frames and to the window, Back, a form, a details and Find in a frame, a frame whose
file isn't there; both editions); `make itest` (`/site/fs.html` over HTTP); `make hatarigui`
(browse: the frameset, its menu's link into the main frame, Undo).

## More windows (25 Oct 2026)

Stage 5's third part: `EWEB.PRG` opens more windows than one, as a browser does.

- **A window is a browser and its GEM state** (`ewin`, `ui/ui.h`): its page, history and address
  (`browser`, `src/browse`), and what `win.c` kept of its one window (its parts, where it is
  scrolled to, the address being typed, the control with the keyboard, the selection, its
  throbber's frame and status bar). `cur` is the window worked on and `br` its browser, so the
  code that worked on the one window works on whichever `cur` is: the one in front, made `cur` at
  each event (`win_front`), or the one an AES message names (`win_message`, for that message).
  What all the windows share stays shared: the settings, bookmarks, history, cookies, the disk
  cache, the pictures decoded (each address once, `img.c`), the throbber's and the toolbar's
  pictures in the screen's form.
- **One load at a time**, as before: it holds its window's browser, and while it runs other
  windows are redrawn, moved and topped but take no commands (the menus wait). Pictures come for
  the window in front first, then for the others (not an icon's); a page's timers and its
  refresh wait while its window is behind. A window closed while pictures come for it is closed
  after the picture (`put_off`).
- **New windows:** ^N (File > New window) with the home page, a little below and to the right of
  the one in front; Control-click on a link, or its menu's Open it in a new window; and a link's
  or a form's target (`frame_target`): `_blank`, or a name no frame has, is `BR_WINDOW`, the
  address in `br.elsewhere` and the name in `br.window`. The front end loads it in the window of
  that name if it opened one (`ewin.name`), else in a new one named so (`to_window`), else, with
  no more to be had, in the window itself. A form that posts to another window is answered in
  its own. ^W (File > Close window) closes the window in front; the last one quits (the
  guidelines' ^W). The window in front is the one whose place `WINDOW=` keeps.
- **Memory:** a window costs its state (14 KB, most of it its history and addresses) and its
  page. There may be eight beside TT RAM, four with 6 MB of ST RAM free at the start, else two
  (`win_max`: a 4 MB Falcon, a 1040 ST). The pages kept for Back share `br_keep_max` (each window's history its share), and
  the pictures trimmed keep what every window shows (`br_register`, `shown_imgs`).

`EWEB.PRG` grew 4 KB (1,216,504 bytes) and `EWEBST.PRG` 4 KB (642,548). Tests:
`tests/layout/frames.py` (`frames/targets.html`: `_blank`, a name, `_top`, a form's target;
ewrender shows another window's page in its stead, a `window` line), `make hatarigui` (browse:
^N, ^W, `/site/blank.html`'s `_blank` link and a named window).

## Pictures fetched at once (25 Oct 2026)

Stage 5's last part. A page's pictures came one after another: EWeb asked for one, the Pi's
gateway fetched it from its server and made it smaller or the screen's pens, EWeb drew it, then
asked for the next. With pens the Atari hardly decodes at all, so most of each picture's time was
the Pi's: its round trip to a server on the internet and its own decoding.

- **Where the time goes decides where the work is done.** On the Atari, fetching several at once
  would need a connection each and a client that reads them in turn (EWeb's is one request at a
  time, on its kept connection), and in Direct mode each new connection is a TLS handshake on the
  Falcon, dearer than what it would save; the Atari's line to the Pi is what it is either way. So
  the pictures are fetched at once on the Pi, where the waiting was, and EWeb's side stays one
  request at a time.
- **EWeb names a page's pictures to webgate when they begin** (`prefetch` in `src/browse`, from
  `br_pictures_step`, once a page shown): a POST to `http://webgate/prefetch`, a line each, the
  address as the request will have it (proxy form), its `X-Gateway-Picture` (`pic_ask`, the same
  function the request uses: pens at the size shown, or as wide as the page) and its cookies, after
  the page's address for the Referer; those in the window first, as `next_item` takes them, 64 at
  most. A picture the disk cache has, fresh or to be asked about (`dc_has`), isn't named, nor one
  to be asked for again. webgate fetches and makes them four at a time and answers EWeb's request
  for each with what it made, waiting for it if it is being made (one not begun yet is made by
  the request itself); what isn't asked for in three minutes goes. A webgate that doesn't know
  `/prefetch` answers otherwise: EWeb doesn't ask again that run, and its pictures come as before.
  The protocol is EWeb's (AERA-Gateway's owner session commits webgate's side: `Prefetch` in
  `webgate.py`, its tests in `test_webgate.py`), released in AERA-Gateway 1.10 (25 Oct 2026, with
  Erez's yes); EWeb's `gateway/` copy is 1.10's.
- **Measured**, ewrender through webgate on this machine: a page of 8 pictures each half a second
  in coming (`/site/lazy.html`, as a far server's) took 4.5 s, now 1.3 s; yaary.music's 17
  pictures through the internet 4.8 s, now 2.9 s. On the Pi, whose pens take longer than this
  machine's, the share saved is the larger.

While doing this, a picture shown at two sizes that didn't fit the pictures' memory together was
found to be decoded at each in turn for ever (Pictures, above; fixed: `img_size_shown`).

With the guidelines' 3.5 (dialogs in the AES's windows on MiNT and MagiC) and `common/`'s update
beside it, `EWEB.PRG` grew 9 KB from 0.38's (1,225,347 bytes) and `EWEBST.PRG` 6 KB (648,905). A webgate that knows `/prefetch` says each picture it fetches ahead
in its log, the Atari and the host only (7.3).

Tests: `make itest` (`/site/lazy.html`: with a webgate that makes pictures ahead, its 8 pictures
in under 3 s and "made ahead"; with one that doesn't, all 8 as before); webgate's
`test_webgate.py` (`PrefetchTest`).

## Layout speed, and a site of test forms (26 Oct 2026)

- **Words measured as they are read** (stage 6): the falcon4 rig's big page (131 KB, 2,406
  elements) laid out in 33.0 million 68030 instructions, now 27.3 (`build/m68k/drawbench -P 2`,
  `tools/count68k.sh`). An item (one for every word and space) is cleared by stores in line, not
  a call to `memset`; a word's width is summed from its face's table of ASCII advances
  (`font_widths`, made when the face is first used) as it is copied; a block of plain text's
  narrowest and widest are found in one pass and kept for the layout (`L.mc`: a table asks for
  each cell's in turn), but not a block with floats or inline blocks in it, whose measuring
  nests (`tests/layout/deep.py`); `len()`'s calc() and percentages are apart.
- **webdriveruniversity.com** (Erez's choice of a site of test forms, 26 Oct 2026), in Direct
  mode (EWeb's own TLS, against the release's roots) and compared with Chromium:
  - an absolutely placed box with `bottom` (a footer, a bar) is placed once its containing
    block's height is known (`bottom_place`, `L.bots`), and so is one with a `top` in % in a
    block of auto height; a positioned box of set height is the containing block's height for
    its children's % (`L.cbh`, `abs_fill` for one sized by `top` and `bottom`);
  - a control the page styles (a ground, a border, padding, a width or a height:
    `styled_ctl`) is a block of its own with the control's style, as an inline block on its
    line or a block; with a ground, that block is its frame; with a border and no ground, or
    only a size or padding, it keeps the control's own ground, and a sized or padded one its
    frame round the padding; a styled button has the page's padding in place of its own;
  - an auto-width item's % or calc() margins count in its width (`cell_pref`: radio buttons
    in a centred flex column broke at every letter);
  - scripts: `input.type` can be set (the control becomes its new kind, `form_retype`), and
    `document.implementation.createHTMLDocument` gives a document of detached nodes (jQuery
    asks for both as it starts). The site's pages bring 290-680 KB of script (jQuery,
    Bootstrap, jQuery UI): over the budget, so they are shown without it. With the budget
    lifted on the host the Popup page works (its alert, its modal, its link). On the 68030
    those three cost 356 million instructions (about 70 s on the DFB1; jQuery alone 79 M,
    15 s), over 90% of it QuickJS compiling them; read back as bytecode they cost about a
    twentieth (jQuery 68 M to compile, 3.8 M to read; jQuery UI 232 M and 7.0 M), and
    bytecode made on another machine reads on the 68030. Erez chose to keep them compiled
    (below).

Tests: `tests/layout/absbottom.html` (bottoms, % tops and heights, styled controls; its list
looked at against Chromium), `floats.txt` and `gradients.txt` changed for the better (a floated
field with its width and border; a bar at its box's bottom), `tests/js/domdoc.html`.

## Scripts kept compiled (26 Oct 2026)

Erez's choice (26 Oct 2026): a page's scripts kept as QuickJS's bytecode, compiled by the Pi when
EWeb goes through it, and by EWeb itself, once, in Direct mode. Compiling is nearly all of what a
library costs the 68030 (jQuery 3.4.1, 88 KB: 68 million instructions to compile, 3.8 million to
read back; jQuery UI, 509 KB: 232 and 7.0 million), so a page of them comes in a fifth of the time.

- **The code** (`src/js/js.c`): "EWQJS", the bytecode's tag on a line, then QuickJS's bytecode
  without its line numbers (a third of the size). The tag (`JS_GetBytecodeTag`: QuickJS's
  version, the format's, how many atoms and opcodes, the byte order) says which QuickJS can read
  it; with `CONFIG_EWEB` the bytecode and a regular expression's (a string among a script's
  constants, upstream in the order of the machine that compiled it) are big-endian on every
  machine, so the 68030 reads them as they are and the Pi's are the 68030's very bytes
  (third_party/quickjs, patch 0002; `make bc68k`).
- **Direct mode** (`run_kept`): a script of 4 KB or more (`JS_KEEP_MIN`), from a file or in the
  page, is looked for in the disk cache under its source's sums and length and the tag (EWeb's
  own data, `dc_put_own`: never stale, its key its contents); there, it is read; else compiled,
  kept and run. Compiling a big one takes the 68030 seconds: the parser asks every 256 tokens
  (`JS_SetParsePoll`, patch 0002) for the window's turn and Esc, which stops the page as Esc
  does while it is read. Esc stops a script that runs too (the budget's interrupt).
- **Through the Pi** (`jh_fetch_script` in `src/browse`): a `<script src>` is asked for with
  `X-Gateway-Script: TAG` and kept under its address and the tag; webgate fetches it unpacked
  and has AERA-Gateway's aerajsc compile it (EWeb's `tools/jsc/aerajsc.c` on EWeb's QuickJS,
  behind `/run/webgate-js.sock` as aerapens is), and answers `application/x-eweb-qjs` with the
  server's ETag and dates, so EWeb asks again with them and a 304 keeps the code. A webgate
  without aerajsc, or with another tag, or a script that doesn't compile: the script comes as it
  came, and EWeb compiles and keeps it itself. One compiled by another QuickJS is refused.
- **Only EWeb's code and the Pi's are run as code**: QuickJS doesn't check bytecode as it
  reads it, so bytes from anywhere else could do anything. EWeb runs its own (kept under keys
  no address has) and an answer of `application/x-eweb-qjs` that `fetch` let stand: asked for
  of the Pi with this build's tag, its `X-Gateway-Script` that tag, from a webgate that said at
  `http://webgate/scripts` that it compiles them (`gate_compiles`, once a gateway). Every
  other answer of that type is kept as `application/octet-stream`, and its bytes are read as a
  script's source (a SyntaxError). A webgate that answers `/scripts` drops the servers' own
  `X-Gateway-` headers; an older one (1.10 and before) passes them on, so it is never asked
  for compiled scripts and never believed. Direct mode takes no compiled code from the network
  at all, only its own. The link to webgate is plain HTTP on the home network (the guidelines'
  7.3), so whoever can answer for webgate there can hand EWeb compiled code: the same trust
  EWeb already gives the Pi for pages and pens.
- **Code kept by another build isn't read**: the tag ends with the sum of QuickJS's sources
  (`CONFIG_EWEB_SUM`, made by the Makefile and by `tools/jsc/Makefile` alike), and the cache's
  keys (EWeb's own and the Pi's answers') carry the tag; EWeb's own are also keyed by the
  script's source (its two sums and length), so a script that changed is compiled again. A new
  QuickJS, a new patch or a changed file there is a new tag, and what was kept before isn't
  looked for (`make bc68k` checks that tools/jsc's aerajsc has EWeb's tag and bytes).
- **Measured** (`build/m68k/jsbench -k DIR`, `tools/count68k.sh`; webdriveruniversity.com's
  Popup page: jQuery, Bootstrap and jQuery UI, 678 KB): 356 million 68030 instructions before
  (about 70 s on the DFB1), 299 million the first time in Direct mode (compiled without line
  numbers, and kept), 51.6 million after (about 10 s), of which about 13 million read the
  code and about 30 million are jQuery UI's own start; through the Pi the same, less the source's
  sums. The 5-second page budget couldn't hold that start: it is 15 seconds, compiling and
  reading not counted. The code: jQuery 95 KB, Bootstrap 34 KB, jQuery UI 216 KB.

Tests: `make test` (`tests/js/kept.py`: compiled and kept, read again, the same either way; Esc
while it compiles, `-I script:N`; a short script not kept; compiled code from elsewhere not
run as code); `make bc68k` (the Pi's code for `tests/js/kept/lib.js` the 68030's very
bytes, and run on the 68030 it says what the host says); `make itest` (`/site/kept.html`:
compiled once, a 304 and the code read the next time; through webgate with aerajsc, the Pi's
code kept as it came, and without it, compiled here; `/site/forged.html`, a server's own
"compiled code" with its own `X-Gateway-Script`, not run, directly or through webgate);
AERA-Gateway's `test_webgate.py` (`ScriptTest`: `/scripts`, the servers' `X-Gateway-` headers
dropped).

## Speed on the stock machines (26 Oct 2026)

Erez's rule for the family (the guidelines' Speed): the stock machines set the speed, an 8 MHz ST
and a stock Falcon, not the DFB1. `make hatarispeed` (`tests/hatari/speed.py`, on common's
`tests/speed`) does the user's actions on each and reads the longest stretch between EWeb's event
calls (`AESGAP`, from common's `gem.c` built with `AES_PROFILE=20` into `build/prof`), each named
by what EWeb was doing (`gap_event`, `gap_tag`, `gap_stage` in `ui/`). The first round, on Erez's
TOS (4.04 on the Falcon, 1.04 on the ST), before and after the fixes below (ms; the guidelines' time
in brackets; a page's load is a long job, held to Esc's 100 ms):

| action | Falcon-4:16 before | after | ST:mono before | after |
|---|---|---|---|---|
| the program started (2000) | 1080 | 400 | 4880 | 1720 |
| the home page shown | 2375 | 795 | 13630 | 3505 |
| the sample page loaded | 1505 | 510 | 10395 | 2570 |
| the news page loaded | 1565 | 1650 | 8065 | 7100 |
| a letter typed in the address (50) | 50 | 25 | 175 | 85 |
| a line scrolled (50) | 30 | <20 | 115 | 25 |
| a page scrolled (150) | 35 | <20 | 120 | 25 |
| Settings opened (150) | 165 | 160 | 430 | 430 |
| Settings closed: what it uncovers (300) | 430 | 155 | 5135 | 1085 |
| the pictures' page loaded | 1230 | 1185 | 8920 | 2540 |

Where the time went, and what changed:

- **The planes.** TOS's VDI doesn't answer `vq_scrninfo` (NVDI's and EdDI's), so every band of the
  page went through `vr_trnfm`, slow, and EWeb's own conversion stored each word through a
  multiply. `ui/screen.c` now asks `vr_trnfm` once at the start what it makes of two words; when
  they come back interleaved, as the Atari's own screens have them, EWeb makes them so itself, with
  running pointers and a run of 16 pixels alike (most of a page) taken from the last such run's
  words. On the Falcon a window's conversion went from about 900 to 310 ms.
- **The shades.** On a screen of few colours a colour it hasn't got is a pattern (`cv_fill`'s
  shade), and it was written a pixel at a time: on the ST in monochrome most of a page's drawing, 36
  million cycles (4.5 s) of a start that showed the sample page. It is now a long (four pixels) at a
  time (`fill_pattern`, `src/gfx/canvas.c`): the same pixels in 2, 4 and 16 colours, a page drawn in
  2.6 s on the ST rather than 7.5.
- **The start.** The window opened and showed the desktop inside it until the home page was
  made, then was drawn white a band at a time, then the page (on Erez's Falcon, the video of
  26 Oct, 8 s of the desktop in it). Its redraw is now handled before the first page is read, and
  with no page yet it is a VDI fill, at once.
- **The toolbar.** Its pictures were kept only beside TT RAM, so on a stock Falcon each draw
  unpacked and converted every icon again, and a button under the mouse showed each step: cleared,
  framed, a hole, then the icon (Erez's second video). Each button is now made whole in memory,
  for each of its looks (as it is, greyed, raised and lit, sunk: `scr_button_dev`), kept (4 to 30
  KB of ST RAM) and put on the screen in one copy; the toolbar's ground is filled around the
  buttons, not under them.
- **The menus.** `menu_update` runs after every key and click and set each of its 45 entries
  through the AES whether it had changed or not: most of a letter typed or a line scrolled on the
  ST. It now asks the AES only for what changed (`menu_set`).
- **The family's rules of the same day** (the guidelines' Speed > How, common at 0879983): the
  window's work area is put on a multiple of 8 across as it opens, moves, is sized or fulled
  (`wind_x8`), and the address's and the status line's text start on a byte (text off one costs
  TOS 1.04 seven times as much); dialogs, popups and the About box put back what they covered
  (common's `dlg_under`), with no redraw of the page under them, and what a dialog changes
  (Settings: text size, pictures, colours) EWeb draws again itself, as before; the four longest
  stretches go to the log at quit (`aes_slowest`, "the slowest: ..."). EWeb's own lists
  (bookmarks, the history) are pages, not GEM rows, so the rule of a row's state by a mark and
  blue is for their CSS, which has no bold rows.
- **The polls.** The parser, the CSS engine and the layout gave GEM its turn every 256 tags,
  elements or boxes, which on the stock machines was a second and more; now every 64, 32 and 16
  (`gem_turn` itself waits 100 ms between turns).

Still over, the next round, worst first: drawing a whole window on the stock Falcon (about 500 ms
of a page shown: the canvas a byte a pixel, then planes, in bands of 32 rows for want of memory);
the news page (84 KB and its 75 KB sheet) doesn't fit beside the 1.5 MB EWEB.PRG takes on a 4 MB
machine, and its 1.3 s file read and 0.45 s of styling between turns; a picture decoded in one
stretch (a 96 x 96 PNG takes 365 ms to decode and 360 to dither on the stock Falcon, the home
page's globe among them, decoded at each start as `data:` pictures aren't kept); a letter typed
on the ST (85 ms) and Settings opened (430). The start size (1.84 MB with the BSS and the stack)
is the plan in the hub's message of 26 Oct: TLS into the family's `TLS.OVL`, WebP and SVG into an
overlay of their own, the stack and the bigger static tables taken when needed.

**The start size, step 1: TLS in `TLS.OVL` (26 Oct 2026).** `EWEB.PRG` no longer carries
`common/`'s `tls.c`, BearSSL, the DSP's RSA and elliptic curves or the 68030 assembly for them:
it carries `common/atari/tls_ovl.c` and `common/ui/dsp_ovl.c` in their place, and the family's
`TLS.OVL` (1.8, 140 KB; 148 KB when loaded) beside it, read the first time a Direct connection makes
a secure one (through the Pi it never is). `EWGET.TTP` does the same, so `make hatarinet` measures
the code `EWEB.PRG` runs.

| `EWEB.PRG` | Before | After |
|---|---|---|
| The file | 1,251,503 bytes | 1,113,371 |
| Text | 1,201,228 | 1,066,720 |
| Data | 25,680 | 24,164 |
| BSS | 625,156 | 612,440 |
| At the start, all three | 1,852,064 (1.77 MB) | 1,703,324 (1.62 MB) |

A Direct connection's first secure page now loads `TLS.OVL` first: 25 ms on the emulated Falcon
(Hatari, the file on its hard disk), then the DSP's self-tests as before.

**Pictures in slices (26 Oct 2026, AERA-Atari 5114bbc).** A PNG is unpacked a row at a time
(common's `png.c` on `inflate_read`), its rows go to the scaler as they come, and the dither takes
a turn every 16 rows: no picture holds the machine for its whole decode. An opaque PNG's rows go to
the solid rows' sums as a JPEG's do, and the filters and the inflate are faster. `img.c` takes a
dither stopped at a turn as pens not made yet (made when the picture is drawn), and the dither a
redraw needs takes no turn. With no buffer the picture's size to unpack it into, a 4 MB ST now
shows the test pages' 800 x 600 PNG it had to refuse (about 100 s in monochrome, in slices, the
page usable meanwhile). Cycle-exact in Hatari (`tests/bench/picbench.py`), decode in ms:

| Picture | Falcon-4:256, before / after | ST:mono, before / after |
|---|---|---|
| the home page's globe, PNG 96 x 96 shown 77 x 77 | 455 / 165 | 3020 / 1145 |
| testorig.png, 227 x 149 RGB | 705 / 710 | 4720 / 4555 |
| a 400 x 300 RGB PNG shown 200 x 150 | 3940 / 3475 | 36670 / 27220 |

## A big site's stylesheet (26 Oct 2026)

Erez's stock Falcon (14 MB, 6.3 MB taken before EWeb started) ran out of memory on ynet.co.il.
Its one stylesheet is 3.36 MB, 974 KB gzip'd: 10,500 rules for every page of the site, of which
about 3,400 have a subject whose id, class or tag the front page has. EWeb 0.43 fetched it whole,
unpacked it up to the 2 MB a sheet could be and then dropped it, the page unstyled: the fetch's
buffers on top of the parsed page were the peak. Had it been read, the copy of its text EWeb kept
(the rules' blocks point into it, parsed when first needed) and the rules would have taken 6.1 MB
(peak 9.5 MB, `drawbench -m`).

- **A sheet read as it comes** (`ec_sheet_begin`, `ec_sheet_feed`, `ec_sheet_end`): a `<link>`'s
  sheet sent gzip'd is kept packed by `fetch` (`fetch_packed`; in the memory and disk caches too,
  its type tagged ` packed=gzip`, unpacked there for any other fetch of it) and unpacked 16 KB at
  a time into the CSS engine (`sheet_stream`, common's `inflate_read`), a turn for GEM between
  pieces; it may unpack to 8 MB. The engine reads the rules that are all there and keeps the rest
  for the next piece: `rules()` stops before a rule or an at-rule whose block hasn't closed
  (`ec_skip_block_closed`). ynet's biggest top-level rule is 68 KB. `ectest -s 1` and `-s 7`
  (make test) give every case's sheet a byte or seven at a time, with the same values.
- **Only a rule's block is kept** (`style_rule`): `ec_sheet` reads the text where it is and copies
  each block, not the sheet.
- **Rules for names the page hasn't** (`ec_prune`, for a page whose scripts don't run): a document's
  names are marked as it interns them (`ec_intern_used`: its tags, classes, ids), and an author
  rule whose subject needs an id, a class or a tag none of them is, is left out with what its
  selectors took. A rule is looked up by those names, matched as atoms (no case folded), so it
  couldn't have matched. An attribute's name isn't enough to tell (`[hidden]`), and a page whose
  scripts run keeps them all.

ynet's sheet on an empty page: kept 6.1 MB to 0.45 MB, peak 9.5 MB to 3.8 MB (the file, read
whole there); with the front page, 8.65 MB to 5.11 MB kept, peak 10.37 MB to 6.96 MB
(`drawbench -m`, which reads the sheets whole and prunes as EWEB.PRG does). EWEB.PRG on Falcon-4
with 14 MB through the real site: the sheet used (the page styled: 135 px high, its scripts not
run), shown in 293 s over the emulated serial line, heap 8.97 MB at the page (doc 1.20 MB, CSS
3.05 MB, kept for Back 2.09 MB: these now on the `shown` line), peak 9.45 MB; 0.43, its sheet
dropped, 6.07 and 8.36 MB. What is left is the page's styles (the CSS context's most, about 650
bytes an element) and the pages kept, given back when a page doesn't fit.

**Nothing styled under `display: none`** (the same pages: `ec_prune`'s, whose scripts don't run):
an element whose display is none gives its style to everything under it (`style_flat`), none of
them made; nothing under it is drawn, and EWeb's selectors never look at a descendant's style.
The speed rig's news page (its body not shown) on Falcon-4:16: its styles 3245 to 150 ms, heap
at the page 1.36 to 0.91 MB, the largest block left free 11 KB to 1.08 MB (0.44 couldn't show it
under NVDI); `drawbench -m`: styled 1.09 to 0.64 MB.

**Fewer rules, each style once** (27 Oct): ynet's front page still took 3.09 MB of CSS, its kept
rules 1.71 MB and its styles 1.14 MB (2,011 made for 3,922 elements, 528 bytes each on the Atari).
- `ec_prune` now leaves out a rule needing an id, a class or a tag the page hasn't anywhere in
  the selector, not only in its subject: `.gone p`, `#nowhere > p`, `section ~ p` with no
  section (`:not()` and `:is()` are left alone). The rules kept 1.71 to 0.72 MB.
- A style computed is looked for among those made before (`uniq_style`: a hash of all of it,
  then compared whole) and taken when it is there, the new one given back to the arena: elements
  whose rules differ often come out alike, and their children then come from the style cache.
  2,011 styles made to 989 (cached 1,647 to 2,635), 1.14 to 0.35 MB. A hash of what made a style
  (its parent and what its rules set) cost less but missed half of them; one of every third
  value collided too often (1.46 MB).

ynet's CSS 3.09 to 1.33 MB, the heap at the styled page 5.05 to 3.28 MB, peak 6.96 to 5.98 MB
(`drawbench -m`, which now prints the CSS context's size and its counts); the same display list.
`measure.py -E ewcss` on the 258 pages, medians: matching bootstrap's sheet 3,308 to 3,375
instructions an element (+2%: the hash), pico's 10,997 to 10,635 (-3%: more from the cache); the
CSS parse 0.3% less. `ectest -p` gives every case the page's names first and prunes (make test),
and a case of its own counts what is left out.

The family's `EWEB.LOG` (`LOG=1`, 0 as it starts): `conn.c`'s log of each connection, a secure
one's handshake step by step, held in memory for a page and its pictures and written once then,
and at the end with the slowest stretches (`aes_slowest`); a crash's report goes after it. No
address's query, cookie or form field (7.3). Erez's demozoo.org failure (the connection lost in
the handshake, Direct, 0.43) didn't happen on the emulated stock Falcon, whose disk costs
nothing: common's `conn.c` now reads TLS.OVL, the roots, the seed and SIGS.DAT before it opens a
connection (TLS.OVL 1.10), and `make hatarigui`'s falcon4 group loads a secure page from a
server with an ECDSA certificate on the stock Falcon.

## ynet.co.il in a narrow window (27 Oct 2026)

**transform** (CSS Transforms 1 and 2): ynet.co.il drew its news drawer over the page; it is
kept off it by `transform: translateX(-103%)`, as many sites hide menus and drawers, and centre
boxes with `translate(-50%, -50%)`. The engine reads `transform`'s translations
(`translate()`, `translateX()`/`Y()`, `translate3d()`, a `matrix()` that only moves) and
`translate` into `-ec-translate-x` and `-y`; rotations, scales, skews and perspective are read
and not drawn. The layout moves the box and all in it as a relative box is moved, a percentage
of its own border box (`tests/layout/transform.html`, the CSS cases).

ynet's front page was 135 px high and its parts over each other because of two more:
- **`all`** (CSS Cascade 4, 3.2): its layout's items are placed absolutely in 1,240 px columns, and
  at a window 940 px wide or less (landscape) its sheet undoes that with `.layoutItem[style] {
  all: unset !important }`, which EWeb didn't know. `all: initial | inherit | unset | revert`
  now sets every property but `direction` and `unicode-bidi` (the CSS cases): the page flows, 13,174
  px high, its header, menu and lead story in order as Chromium draws it at 640 x 480.
- **A box's `height`** is its height, what is in it taller overflowing it (CSS 2.1, 10.5), not
  stretching it: EWeb's first layout made a box at least as tall as its content unless it hid what
  overflowed. ynet's menu bar (`height: 28px`, its hidden pop-up menu in it) was 725 px tall.
  The page is as tall as what is drawn on it, what a box hides aside (`page_overflow`): what
  overflows a box of fixed height, the body's too, is still scrolled to (`tests/layout/overflow.py`).
  Three test pages moved a few pixels (`overflow.html`: the `overflow-x: clip` box keeps its 32
  px, its third line below it, as in Chromium). Where EWeb's fonts are taller than a page's, its
  text now runs out of a box of fixed height as it would in other browsers.

**Stop** is lit while what Esc stops is coming, the page's pictures too (Erez: it went grey
while they were decoded): `ewin.stopper`, set with the throbber, its button alone redrawn when it
changes; pressed while pictures come it stops them as Esc does (`make hatarigui`'s browse group,
`/site/lazy.html`).

## The design

### Every Falcon, fastest on an accelerated one

- **Two builds from one source,** as the family has ST editions: `EWEB.PRG` for the 68030 and
  `EWEBST.PRG` for the 68000. JavaScript uses the FPU, and runs only where there is one (above);
  nothing else in EWeb needs floating point.
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
| HTML5 parser | Hubbub, with libparserutils (replaced: EWeb's own, 19 Oct 2026) | MIT |
| CSS engine | LibCSS, with libwapcaplet (replaced: EWeb's own, 17 Oct 2026) | MIT |
| DOM | libdom | MIT |
| GIF and BMP decoders | libnsgif, libnsbmp | MIT |
| Layout and page handling | NetSurf's own core | GPL-2.0-only |

The MIT parts fit EWeb's GPL-2.0-or-later licence. NetSurf's core is GPL version 2 only: if any of it
is used, EWeb as a whole program becomes GPL-2.0-only, while EWeb's own files keep "or later". NetSurf
also has an Atari front end, under the same terms; EWeb's front end is ours, built on `common/` to the
AERA guidelines.

### 1. Direct: EWeb's own engine on the Falcon

- **Networking:** HTTP/1.1 and TLS 1.2 through `common/` (BearSSL, the connection code, DNS), with
  gzip, cookies (above), redirects and a disk cache.
- **HTML and CSS:** an HTML5 parser and a CSS engine, NetSurf's or fresh by the rule below, with
  selectors, media queries, CSS variables and flexbox.
- **A narrow screen:** EWeb tells sites its screen is about 640 px wide, so responsive sites send their
  simpler mobile layout, which also fits the Falcon's screen.
- **Images:** GIF, PNG and JPEG, and WebP in `EWEB.PRG` (24 Oct 2026: What isn't fetched, above).
  JPEGs decode at n/8 of their size when shown smaller, which is much faster. EWeb asks servers for
  JPEG, PNG and GIF only, so image servers don't send AVIF or WebP (done: above, 19 Oct 2026).
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
- **Layout and drawing** (fresh code): Hebrew words are put right to left within each line after
  it is broken, across links and bold, and right-to-left blocks align right and put list markers on
  the right (above); since 24 Oct 2026 by UAX #9 with the line's words for its characters, numbers
  left to right in Hebrew and brackets mirrored (Hebrew with numbers and Latin, above). Still to
  come: the levels `dir` on an inline element, `<bdi>` and `<bdo>` set.
  `common/bidi.c` (from ClaudeST) handles one line of Atari-charset text, so EWeb needs the fuller
  algorithm, which can move to `common/` for every app.
- **Visual Hebrew:** a page labelled plain ISO-8859-8 (or "visual") is already in display order, so
  layout shows it as it is: its lines aren't reordered and its Hebrew words aren't drawn right to
  left (the HTML Standard's rendering rules for an ISO-8859-8 document, `unicode-bidi:
  isolate-override`, which browsers apply to the whole page; whatever the line's direction, which
  still aligns it). EWeb knows which kind it has from the page's charset (`d->charset`, and View >
  Encoding offers both); a form control's text is still put in logical order. `tests/layout/run.py`
  draws the same lines stored visually and logically (`tests/hebrew/visual`) and compares the
  pictures.
- **Fonts:** EWeb's own fonts have the Hebrew letters (DejaVu's), so pages don't go through the Atari
  character set. Vowel points (niqqud) take no room and aren't drawn yet.
- **Typing:** Hebrew is typed into forms with the Atari's Hebrew keyboard, and fields show it right
  to left (forms, above).

### 2. A little JavaScript on the Falcon

- QuickJS (MIT licence), carried in the repo the way `common/` carries BearSSL. Its code is about
  800 KB for the 68030 at `-O2`. *Done (20 Oct 2026, above).*
- Off by default; switched on per site.
- A lean DOM: selectors, events, classes, `innerHTML`, timers and `fetch`.
- A hard budget of roughly 100–200 KB of script and a few seconds per page. Beyond that the page falls
  back to its `<noscript>` version.
- Enough for menus, tabs, form checks and small widgets. Not enough for frameworks. *Since 26 Oct
  2026 pages of jQuery and its friends too, their scripts kept compiled (above).*

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

**Built for Cloudflare's check (24 Oct 2026),** after Erez met it on Atari-Forum: EWeb knows the
check (`cf-mitigated: challenge`, or its options on a 403 or 503) and shows its own page in its place,
which goes nowhere and runs no script, with the Internet Archive's copy. Through the gateway with its
browser, webgate (AERA-Gateway) opens such a page in the browser, whose extension waits through the
check (its snapshot had been the check itself), and sends EWeb the page; the site's next pages go
straight to the browser for the visit (25 minutes after the last), and nothing of the pass leaves the
browser, so a picture the check refuses stays out. A check that wants a click comes back with
`X-Gateway-Check: click`, and EWeb's page links to live view (`br_live_url`). `make itest` runs it
with a stand-in for the browser when its webgate has it.

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
   *Done so far (19 Oct 2026):* pictures (above); floats, positioning, `rowspan` and vertical
   alignment in cells (your order, above); forms and cookies (above); the HTML parse 40% faster on
   the page set (above), and EWeb's own HTML parser, 44% more and the HTML Standard's
   tree-construction tests nearly all passed (above); the UA stylesheet parsed once a program run (above);
   saving pages and files, bookmarks and the disk cache (above).
4. **Gateway mode in EWeb:** snapshots and live view, over EWeb's own protocol. *Done so far (20 Oct
   2026):* webgate's fetches, the gateway as a proxy, and the ST edition (above). With it the ST
   edition, `EWEBST.PRG` (68000 code, `-fdisable-tree-bswap`): like the family's other ST
   editions it leaves TLS to the Pi gateway, which is how an ST reaches the secure web.
5. **JavaScript:** QuickJS with the lean DOM. *Done (20 Oct 2026):* above, with an FPU.
6. **The web as sites make it** (Erez's go, 22 Oct 2026), in five stages. *Stage 1 done (0.27):*
   refresh, stylesheets by media, encodings, downloads' names, translucency, gradients, legacy
   centring, details and summary (above). Then, after the 1 MB ST target (dropped) and EWeb's own
   256-colour palette (Decisions): **2.** the layout's core: `inline-block` (done, above), boxes that scroll
   (`overflow`, done, above), `@import` (done, above), scrolling sideways (done, above), `border-collapse` (done, above); **3.** reading: find in the
   page (done, above), selecting and copying (done, above), history with visited links (done, above), a page
   drawn while it loads (done, above), Esc during a layout (done, above); **4.** text: more characters in `EWEB.FNT` (done, above), bidirectional text in full
   (numbers, Latin and mirrored brackets in Hebrew, and embeddings: done, above), UAX #14's line
   breaks (done, above); **5.** SVG (done, below), frames and iframes (done, below), more windows (done, below), pictures
   fetched at once (done, below: with the Pi's gateway).

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
