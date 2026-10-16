# EWeb design

EWeb is a web browser for the Atari Falcon with a DFB1 accelerator, part of the AERA family. This
page holds the decisions made so far, what the Falcon was measured to do, the design that follows from
it, and how EWeb gets past CAPTCHAs. There is no code yet.

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
below), and the 68030's 256-byte caches hurt an interpreter. Real hardware will confirm the figure. The
last column is Chrome's engine (V8, in Node 22) on one 2.8 GHz Xeon core, from cold.

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
- **The DSP is the more useful helper.** `common/` already uses it for RSA in TLS. EWeb's candidates
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

**Standards:** Hubbub passes 1,139 of the 1,679 current tree-construction tests (the Web Platform
Tests' `html/syntax/parsing`, 15 Oct 2026; `make h5test`): 67.8%, and the same as 68030 code. 198
fragment tests don't apply (Hubbub has no fragment parsing). Without the two newest features,
`<template>` (121 failures) and processing instructions (84, a 2026 change), it passes 76.6%. The
rest are edge cases: script text cut short at the end of a file, `<ruby>`, newer elements such as
`<search>`.

**Where the time goes** (callgrind, the BBC page with Pico): selection is about half, close to a third
of that composing each element's style with its parent's; parsing the CSS is a quarter; parsing the
HTML a fifth. Two fifths of the HTML parse, and part of the CSS parse, is libparserutils turning UTF-8
into UTF-8, decoding and re-encoding every character. `nsbench`'s own tree code is under 2%.

**The first NetSurf-or-fresh calls:**
- **HTML parser: Hubbub stays.** It costs 2–3 seconds on a median page, and a fresh parser would have
  to match its 1,139 passing tests before it could count as better. The first gain is a UTF-8 path
  that skips the decode and re-encode (HTML and CSS both read through it), measured on the page set.
  `<template>` needs adding.
- **CSS: where fresh code can win most.** Parsing and selection cost 3–9 times what the HTML does (5
  times on the median page with Pico). LibCSS computes every property it knows, the spoken-media ones
  too, for every element. A fresh engine that computes only what EWeb draws, and keeps parsed sheets
  per site, is the first candidate; LibCSS stays as the yardstick until one beats it on the page set.
- **Layout: fresh code, decided by Hebrew.** NetSurf's layout uses `direction` only for default
  alignment and margins, and nothing in NetSurf reorders text, so a line mixing Hebrew and English
  would come out backwards. Fresh layout with the bidirectional algorithm built in is both more
  compliant and needed (see Hebrew, below).

## The design

### Every Falcon, fastest on an accelerated one

- **Two builds from one source,** as the family has ST editions: one uses the FPU, for Falcons that
  have one; the other doesn't, and runs JavaScript's maths in software, several times slower. Nothing
  else in EWeb needs floating point.
- **At start,** EWeb reads the system cookies (`_CPU`, `_FPU`, `_MCH`), asks TOS for TT-RAM, and times
  a short loop to see how fast the CPU is.
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
  Hebrew turned into Latin letters; one line fixes it (`third_party/netsurf/patches`, ready to send
  upstream), with the web's labels "logical" and "visual". `tests/hebrew` checks the same page in all
  six labels, on this computer and as 68030 code (`make test`, `make hebtest68k`).
- **CSS** (LibCSS): `direction` and `unicode-bidi` are there. EWeb's UA sheet adds the HTML rules for
  `dir="rtl"`, `dir="ltr"`, `<bdi>` and `<bdo>`; `dir="auto"` is layout's, from the first strong
  letter.
- **Layout and drawing** (fresh code): the Unicode bidirectional algorithm (UAX #9) on each line after
  it is broken, across links and bold, with numbers kept left to right, brackets mirrored and the
  levels `dir`, `<bdi>` and `<bdo>` set; right-to-left blocks align right and put list markers on the
  right. `common/bidi.c` (from ClaudeST) is the start: it handles one line of Atari-charset text, so
  EWeb needs the fuller algorithm, which can move to `common/` for every app.
- **Visual Hebrew:** a page labelled plain ISO-8859-8 (or "visual") is already in display order, so
  layout shows it as it is, without reordering. EWeb knows which kind it has from the page's charset.
- **Fonts:** Hebrew is drawn through the Atari character set (letters at 0xC2-0xDC), as the other apps
  draw it (`common/`'s charset code). Vowel points (niqqud) have no glyphs there and are dropped unless
  EWeb gets fonts of its own.
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

## Order of work

1. **The gateway's `web` service, in AERA-Gateway,** with live view for CAB. This gets you past
   CAPTCHAs first, before EWeb exists.
2. **EWeb's first milestone:** port Hubbub and LibCSS, fetch a page directly, parse its HTML and lay
   out the text on the real Falcon, and measure what HTML and CSS cost there. That makes the first
   NetSurf-or-fresh calls. *Done so far (16 Oct 2026):* the port, the measurements on emulated
   68030s and the first calls (above), and `NSBENCH.TTP` in release v0.1 for your Falcon (it runs on
   an emulated stock Falcon too: 16 MHz, no FPU, no TT-RAM). *Still to do:* the timed run on your
   Falcon, fetching and laying out text.
3. **The document web:** CSS, images, forms, bookmarks and history.
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
- [Web Platform Tests: html/syntax/parsing](https://github.com/web-platform-tests/wpt/tree/master/html/syntax/parsing)
- [Cloudflare: detect a challenge page response](https://developers.cloudflare.com/cloudflare-challenges/challenge-types/challenge-pages/detect-response)
- [Google reCAPTCHA: verifying the user's response (token valid for two minutes)](https://developers.google.com/recaptcha/docs/verify/)
