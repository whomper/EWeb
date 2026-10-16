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

The counts are exact. The times assume the DFB1 achieves 5–10 million instructions per second at
50 MHz, because the 68030's 256-byte caches hurt an interpreter. Real hardware will confirm the figure.
The last column is Chrome's engine (V8, in Node 22) on one 2.8 GHz Xeon core, from cold.

| Workload | 68030 instructions | DFB1 at 50 MHz | V8 |
|---|---|---|---|
| Parse a 146 KB JSON API response | 16 M | 1.6–3.2 s | 0.65 ms |
| Compile jQuery 3.7.1 (87 KB) | 65 M | 6.5–13 s | 3.7 ms |
| Compile react-dom 18.3.1 (132 KB) | 103 M | 10–21 s | 5.8 ms |
| Load React and render a 100-story front page (1,709 elements, HTML only, no layout) | 171 M | 17–34 s | 22 ms |

What follows from it:
- **JavaScript is the wall.** Compiling minified JavaScript costs about 650–780 instructions per byte.
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

The HTML and CSS cost per page isn't measured yet; that is the first milestone's job (see Order of
work).

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
- **The rest:** forms, bookmarks, history, Hebrew and other right-to-left text through `common/`'s bidi
  code, and a GEM interface that follows the AERA guidelines.
- **Sites that should work well:** Wikipedia, Hacker News, Atari-Forum and other phpBB forums, blogs,
  documentation, text-only news (lite.cnn.com, text.npr.org), DuckDuckGo's HTML version, FrogFind and
  GitHub READMEs.

What to expect: pages load in seconds to tens of seconds, and scrolling moves in steps. The screen sits
in ST-RAM on the original 16 MHz bus, so redrawing it is slow even with the accelerator.

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
   NetSurf-or-fresh calls.
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
- [Cloudflare: detect a challenge page response](https://developers.cloudflare.com/cloudflare-challenges/challenge-types/challenge-pages/detect-response)
- [Google reCAPTCHA: verifying the user's response (token valid for two minutes)](https://developers.google.com/recaptcha/docs/verify/)
