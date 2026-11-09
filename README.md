# EWeb

<img src="docs/icon.png" width="96" align="right" alt="EWeb's icon: a globe, Europe, Africa and Arabia in green on a blue sea with a grid of lines, drawn pixel by pixel">

A web browser for the Atari Falcon and TT, written for GEM and tuned for a Falcon with a DFB1
accelerator. Its ST edition runs on an ST or STE with 4 MB.

![EWeb on a Falcon in 256 colours, showing yaary.music](docs/screenshot.png)

## Highlights

- **Modern pages**: EWeb's own HTML parser, CSS engine and layout (flexbox, grid, tables, floats,
  frames), much faster on a 68030 than NetSurf's.
- **Pictures**: JPEG, PNG, GIF, WebP and SVG, JPEGs decoded on the Falcon's DSP, dithered to any
  screen; shown after the page, so you can read while they come.
- **Forms and logins**: every kind of field, file uploads, cookies kept between runs.
- **JavaScript** on the sites you choose: menus, tabs, form checks, pop-ups, drag and drop.
- **Translation**: View > Translate translates pages into your language on the Gateway, screen
  first; nothing leaves your home network.
- **Hebrew**: pages right to left in every Hebrew encoding, and typing Hebrew (F10).
- **Secure sites on the Atari itself**: TLS 1.2 with the DSP doing the maths, or through the
  Gateway.
- **The Gateway's browser**: a page made by scripts, or a CAPTCHA, as a snapshot or live view.
- **Search from the address bar** (FrogFind by default), several windows, bookmarks, history,
  find in page, copy and paste, saving and downloads.
- **A disk cache**: pages and pictures come back at once, and are shown even when their site
  can't be reached.
- **Every screen**: monochrome to the Falcon's true colour, with its own fonts on every Atari.
- **One of the AERA family**: the same menus, shortcuts, dialogs and help as the other programs;
  a mail link opens EMail.

## Getting started

Download the latest `eweb-x.y.zip` from **[Releases](../../releases/latest)** and unpack it into a
folder of its own. Keep the files together.

| File | What it is |
|---|---|
| `EWEB.PRG` | the browser, for the Falcon and TT |
| `EWEBST.PRG` | the ST edition, for an ST or STE |
| `EWEB.FNT` | the fonts |
| `EWEBJS.OVL` | JavaScript (needs an FPU) |
| `TLS.OVL`, `CACERT.PEM`, `ROOTS.DAT` | secure connections made on the Atari |
| `EWEB.HYP` | the guide (the Help key) |
| `icons/` | the desktop icon |

1. With STinG or MiNTnet running, double-click `EWEB.PRG` (`EWEBST.PRG` on an ST).
2. Type an address and press Return, or pick a site from the home page.

**Needs:** a Falcon or TT with about 3.5 MB free and a screen 640 pixels wide, under TOS, EmuTOS
or MagiC; scripts need an FPU. The ST edition needs 4 MB and the Gateway for secure sites.

**Tips:** under TOS put Atari's `FOLDR100.PRG` in `AUTO` (renamed `FOLDR400.PRG`), so TOS doesn't
run out of folder memory. With STinG, set the Delay in `STING.CPX` to 10 on a Falcon or TT.

## How it reaches the servers

```
Falcon, TT:  EWEB.PRG ----------------------------------- HTTP or HTTPS --> the site
ST, STE:     EWEBST.PRG -- HTTP, home network --> Gateway -- HTTPS --> the site
```

`EWEB.PRG` connects directly (Options > Direct connection) and checks every certificate against
Mozilla's list. It can also go through the Gateway, AERA-Gateway's web service on a Raspberry Pi,
which makes the secure connection, shrinks pictures, translates pages (`aera-gateway install
translate`) and opens script-made pages in its own browser (`--browser`). To set it up on the
Raspberry Pi, with the Atari's address for `ATARI_IP`:

```
sudo apt install -y git &&
  git clone https://github.com/whomper/AERA-Gateway ~/AERA-Gateway &&
  ~/AERA-Gateway/update-pi.sh --allow ATARI_IP
```

Then in EWeb: Settings > Secure, the Gateway's address and port `8280`, and the Direct connection
off. More in the [gateway's README](gateway/README.md#web-ewebs-gateway).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 perl gperf fonts-dejavu-core fonts-dejavu-extra
pip install -r tools/requirements.txt
make            # binary/: EWEB.PRG, EWEBST.PRG, EWEB.FNT and the rest
make test       # the tests, with AddressSanitizer and UBSan
make dist       # dist/eweb-x.y.zip, as on the Releases page
```

The other targets (`itest`, `hatarigui`, `fuzz` and more) are listed under Tests. A release is
`tools/bump.py`, a commit to `main`, then **Actions > Release > Run workflow**.

## Source

```
ui/              the GEM side: window, toolbar, menus, dialogs
src/browse/      loading, history, bookmarks, the disk cache, pictures
src/doc/         the HTML parser and the document
src/css/         the CSS engine (docs/CSS.md)
src/layout/      the layout
src/gfx/         fonts and drawing
src/svg/         SVG
src/js/          JavaScript (QuickJS)
src/net/         URLs, HTTP and cookies
tests/           the tests and the measuring tools
docs/            DESIGN.md (decisions, measurements, plan), GUIDE.md (the help file)
common/          the AERA family's shared code (from AERA-Atari: don't edit here)
gateway/         a copy of AERA-Gateway's services, for the tests
third_party/     NetSurf's libraries, QuickJS, musl's maths
```

## Tests

- `make test`: the parser, CSS engine, layout, forms, scripts, cookies, cache and network code on
  this computer.
- `make itest`: the browser against local servers, over HTTP and HTTPS, and through the Gateway.
- `make hatarigui`, `make hatarinet`, `make hatarifiles`, `make hatarispeed`: EWeb on emulated
  Falcons, TTs and STs (Hatari).
- `make h5test`, `make urltest`, `make csscmp`, `make fuzz`: against the standards' test suites,
  LibCSS and libFuzzer.

## Licence

EWeb is free software under the GNU General Public License, version 2 or (at your option) any
later version; see [`COPYING`](COPYING). Copyright (C) 2026 AERA | Erez Yaary.

`EWEB.FNT` is drawn from the DejaVu fonts (Bitstream Vera's licence). NetSurf's libraries,
QuickJS, musl's maths and BearSSL are under the MIT licence; libjpeg-turbo under the IJG's and
BSD licences; TJpgDec under ChaN's licence; `CACERT.PEM` is Mozilla's (MPL 2.0).

---

Part of **AERA** for Atari: [ECal](https://github.com/whomper/ECal) · [EContact](https://github.com/whomper/EContact) · [EMail](https://github.com/whomper/EMail) · [EFTP](https://github.com/whomper/EFTP) · [EHypView](https://github.com/whomper/EHypView) · [ClaudeST](https://github.com/whomper/ClaudeST) · [EWeb](https://github.com/whomper/EWeb) · [EUpdate](https://github.com/whomper/EUpdate) · 8-bit: [EDOS](https://github.com/whomper/EDOS) · [EFTP_XL](https://github.com/whomper/EFTP_XL) · Pi: [AERA-Gateway](https://github.com/whomper/AERA-Gateway)
