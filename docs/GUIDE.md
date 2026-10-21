# EWeb user guide

EWeb is a web browser for the Atari Falcon (and the TT). It fetches pages
itself over the internet, secure ones too, and shows them with its own
fonts: text, links, headings, lists, tables, pictures and forms, in
colour or black and white.

## What you need

- A Falcon030 (best with an accelerator such as the DFB1 and TT RAM) or
  a TT, with TOS 4, EmuTOS or MagiC; a screen 640 pixels wide in any
  mode: monochrome, 4, 16 or 256 colours, or the Falcon's true colour.
  On an ST or STE (TOS 1.04 or newer, 4 MB best), use `EWEBST.PRG`, the
  ST edition: it has no secure connections of its own and reaches secure
  sites through AERA-Gateway's web service on a Raspberry Pi (the Pi
  gateway, in [Settings](#settings)); it runs on a 1 MB ST, which has
  room for one page at a time and few pictures
- About 2 MB of free memory, more for big pages; TT RAM is used when
  there is some
- **STinG** for the network (plain TOS), on a network card such as the
  NetUSBee. Without a network EWeb still opens pages on your disks.
  Set the Delay in `STING.CPX` to 10 on a Falcon or TT, 25 to 50 on an
  ST: STinG looks at the card that often, so it sets how fast pages
  come (on a Falcon with a NetUSBee, Delay 10 took downloads from 40 to
  75 KB/s). On a NetUSBee, keep STinG's TCP receive window at 5840
  (MSS 1460): the card holds about four full packets
- Under TOS, Atari's `FOLDR100.PRG` first in `AUTO`, renamed for more
  room (`FOLDR400.PRG`): TOS keeps an entry in a small internal pool for
  every folder a program or accessory has looked into, and halts with
  "OUT OF INTERNAL MEMORY" when it runs out

## Installing

The files are in the latest release on EWeb's GitHub page
(github.com/whomper/EWeb/releases), each on its own and all together in
`EWEB-x.y.zip`. Put them in a folder of their own, on any drive:

```
EWEB.PRG      the program (Falcon, TT)
EWEBST.PRG    the ST edition (ST, STE: secure sites through the gateway)
EWEB.FNT      its fonts (needed: EWeb draws pages with them)
EWEB.HYP      this guide, for EHypView or ST-Guide (the Help key opens it)
CACERT.PEM    secure sites: the root certificates
ROOTS.DAT     the same, read ahead of time
EWEB.INF      settings (EWeb writes it)
SEED.DAT      secure sites: randomness kept for the next start
SIGS.DAT      secure sites: certificate checks remembered
CRASH.LOG     what happened, if EWeb ever crashes
```

EWeb keeps everything in the folder it is started from. Start it from
the desktop with a double click; drop a page (an `.HTM` file) on it to
open that page.

## Opening pages

Click in the address field at the top (or press **^L**), type an
address and press Return:

- a web address: `frogfind.com`, `en.wikipedia.org/wiki/Atari_Falcon`.
  EWeb adds `http://` when there is none; sites that want a secure
  connection send it on to `https://` themselves
- a file on your disks: `C:\PAGES\INDEX.HTM`; **^O** picks one with
  the file selector
- words to search for, such as `atari falcon`: they go to FrogFind,
  a search engine made for vintage computers (another can be set in
  [Settings](#settings))

While a page loads, the status bar at the foot of the window says what
is happening and how much has come, and the thin line along its foot
fills from left to right, in a colour for each step: blue while EWeb
connects, cyan as the page comes, magenta for its stylesheets, yellow
while the page is laid out, green as its pictures come. **Esc**,
**Undo** or the Stop button stop it, and the page you were on stays.

## Pictures

A page is shown as soon as its text is laid out; its pictures come
after, those in the window first, then those below, and each is drawn
as it comes ("Pictures: 3 of 12" on the status bar). You can read and
scroll meanwhile; clicking a link or typing an address goes there at
once. **Esc** (or File > Stop) stops the pictures of the page shown.

Until a picture has come its place is framed, with what it shows written
in it where the page says. Pictures EWeb can't show (WebP, AVIF, SVG)
aren't fetched at all; through the Raspberry Pi gateway they come as
JPEG or PNG, and big ones come made smaller on the Pi, a few dozen
kilobytes instead of a megabyte, or ready for your screen (see
[Secure sites](#secure-sites)). A picture whose size the page doesn't give
shows its description first, and the page is laid out again when its
size is known (at most every two seconds, so that it doesn't jump about
while you read).

EWeb shows JPEG, PNG (with transparency) and GIF (the first picture of
an animated one), decoded at the size the page shows them: a big photo
shown small takes much less time and memory than at its own size. On a
Falcon the DSP decodes a JPEG while the 68030 only hands it the bytes
and takes the pixels (a progressive JPEG's scans the 68030 reads, the
DSP the rest; Settings > General). In 16
or 256 colours and in monochrome they are dithered to the screen's own
colours (on a Falcon in 4, 16 and 256 colours by the DSP, eight times as
quick); in true colour they are as they are. An address of a picture
on its own (a `.jpg` link) shows the picture.

On a slow line, View > Show pictures turns them off: pages then show
what their pictures are instead, and come quicker.

## Forms

Fields, boxes, lists and buttons work as in any browser. Click in a
field to type into it (the pointer turns into a text cursor over one);
**Tab** goes to the next field and **Shift-Tab** back, **Esc** leaves it.
In a field:

- **Left**, **Right**: a letter back or on; **ClrHome** the start,
  **Shift-ClrHome** the end (of the line, in a box of several lines)
- **Up**, **Down**: the line above or below, in a box of several lines
- **Backspace**, **Delete**: the letter before or after
- **^C**, **^X**, **^V**: copy, cut or paste the field's text (not a
  password's)
- **Return** sends the form, as its first button would (in a box of
  several lines it starts a new line)

Hebrew can be typed with the Atari's Hebrew keyboard, and shows right to
left as it is typed.

Click a box (or its words) to tick it, a round button to choose it from
its group, a list to choose from it (a long list shows a screenful at a
time, with Earlier and Later), and a button to send the form. On a box,
a list or a button that has the keyboard (from Tab), **Space** does what
a click does.

A form goes in the page's own character set, as browsers send it; Back
after sending shows it as you filled it in. EWeb can't send files yet
(a file field sends none), and buttons that need JavaScript do nothing.

## Cookies

Sites remember you (a login, a basket, a setting) with cookies. EWeb
keeps them as browsers do: those meant to last are kept in `COOKIES.TXT`
next to `EWEB.PRG` (their values encrypted with the key in `EWEB.KEY`),
written when you quit EWeb, so that you stay logged
in from one day to the next; the others last until you quit. A picture
or stylesheet from another site (an advert's, a tracker's) gets no
cookies and can't set any.

Options > Forget cookies forgets them all at once, the file too: you are
logged out of every site.

## Bookmarks

**Bookmarks > Add bookmark** (**^D**) keeps the page shown; the
Bookmarks menu lists the first twenty, and **Show bookmarks** (**^B**)
shows them all as a page, each with its site and a Remove link.

They are kept in `BOOKMARK.HTM` next to `EWEB.PRG`, at once, in the
bookmark file other browsers import and export: put one exported from
Firefox, Chrome or another browser there (its folders are left out) and
EWeb lists its links, or open EWeb's in another browser.

## Saving pages and files

- **File > Save page as** (**^S**) saves the page shown as it came, in
  the file selector's folder, with a name made from its address
  (`INDEX.HTM` for a site's front page). Its pictures and stylesheets
  stay on the server; open the file again with ^O.
- **A link's file**: Alternate-click the link, or right-click it and
  choose Save the link's file. It goes straight to the disk as it comes,
  so it can be bigger than your memory; the status bar counts it, and
  Esc stops it (nothing is left behind).
- **A file EWeb doesn't show** (a ZIP, a program, a PDF), opened as a
  page: EWeb asks where to save it, and the page you were on stays.

The file selector offers the name; if there is a file of that name
already, EWeb asks before replacing it.

## The cache

Pages, stylesheets and pictures are kept on the disk, in the `CACHE`
folder next to `EWEB.PRG`, for as long as their sites say they stay
the same: a page you open again comes from the disk, often with no
waiting for the network at all, and a site's pictures and stylesheets
come once. When a site says to ask, EWeb asks whether the page changed
and keeps its copy if it didn't. Back and Forward show pages as they
were; Reload (^R) asks the site again.

Pictures are kept as they were shown, ready for the screen: a page of
pictures that took seconds to decode the first time shows them at once
the next time, even after a restart.

When a site can't be reached, a page EWeb kept is shown, and the line
under the page says it may be out of date; a site that says its pages
must always be checked gets an error instead. Sending a form forgets
what was kept for the page it went to.

The cache takes 32 MB (2 MB on a Falcon without TT RAM); Settings >
General > Disk cache changes it, 0 for none. When it is full, what came
first makes room for what comes, so quitting takes no time: EWeb writes
only what came since it last wrote and its list. Options > Empty the
cache forgets it all.

## The window

Along the top, the toolbar: at the left a row of buttons, each a
picture:

- **Back** and **Forward**
- **Reload** the page shown
- **Home**, the start page
- **Search**, the search engine's own page (Settings > General)
- **Stop** a page while it loads

They lie flat until the mouse is over one, which then stands out in a
frame and says its name on the status bar; held down, it sinks, and works
when you let go over it. Beside them is the address of the page shown,
which you can type into.

At the right end is the AERA A. While a page or its pictures come, a star
goes round it over a field of faint stars; when it stands still, EWeb
is waiting for you.

Under that is the page, with the window's slider on the right. The info
line shows the address of the link under the mouse, what a load is
doing, or, for a secure page, how it is secured. The window's title is
the page's.

Click a link to follow it (the pointer turns into a hand over one).
Right-click a link for its commands (open it, save its file, copy its
address), or the page for Back, Forward, Reload and copying its text.

The window can be moved, sized, fulled and iconified as any other; EWeb
lays the page out again for the new width.

## Moving around a page

- the slider and the window's arrows
- **Up** and **Down**: a few lines; with **Shift**, a page
- **Space**: a page down; **Shift-Space**: a page up
- **ClrHome**: the top; **Shift-ClrHome**: the end
- a link to a place on the same page (`#...`) scrolls there

## Back, Forward and Home

**Undo** (or **^Z**) goes back, **Shift-Undo** (or **Shift-^Z**)
forward, to where you were on each page. EWeb keeps the last 50 pages.
The last few stay in memory as they were laid out (in TT RAM when there
is some), so Back and Forward show them at once, without fetching or
reading them again; **^R** fetches the page again.

**Home** opens your home page: EWeb's own start page, or the page you
chose with Options > Use this page as home.

## Text size

**^+** makes the text larger, **^-** smaller, by a tenth each time, and
**^0** puts it back as it was. Only the text changes: the pictures keep
their size and stay as they were decoded, so nothing is fetched or
decoded again (a page's pixel is 0.8 of the screen's, so that pages
made for wider screens fit the Falcon's 640).

## Copying

**^C** copies the page's text to the GEM clipboard (to paste into
another program), or the address when the address field has the
keyboard. In the address field **^X**, **^V** and **^A** cut, paste and
choose all of it.

## Keys

| Key | What it does |
|---|---|
| ^L | type an address (Return goes there) |
| ^O | open a page on your disks |
| ^S | save the page shown |
| ^D | add the page shown to the bookmarks |
| ^B | show the bookmarks |
| Alternate-click | save a link's file |
| Undo, ^Z | back |
| Shift-Undo, Shift-^Z | forward |
| ^R | reload |
| Esc | stop a page loading, or its pictures |
| Up, Down | scroll; with Shift, a page |
| Space, Shift-Space | a page down, up |
| ClrHome, Shift-ClrHome | the top, the end |
| ^+ ^- ^0 | larger, smaller, as it was |
| ^C | copy the page's text, the address, or a field's text |
| Tab, Shift-Tab | the page's next field (or the one before), then the address |
| Return | in a field: send its form |
| ^, | the Settings |
| Help | this guide |
| ^Q, ^W | quit |

## What EWeb shows

EWeb shows pages as their stylesheets lay them out: headings,
paragraphs, lists, tables, links, bold and italic, colours and
backgrounds, borders, pictures, boxes and pictures floated beside the
text, pictures in the background of a page or a box, positioned
boxes, tables with cells over several rows, flexible boxes and grids
(CSS flexbox and grid), forms, Hebrew right to left. It doesn't yet
show:

- file fields (EWeb can't send files yet)
- WebP and AVIF pictures (EWeb asks servers for JPEG, PNG and GIF,
  which most send then; of a picture offered in several forms and
  sizes it takes a JPEG, PNG or GIF one at the size the screen needs),
  SVG drawings, and colour gradients
- what a page's JavaScript makes, unless you let that site's scripts
  run (see [Pages made by scripts](#pages-made-by-scripts)); without
  them EWeb shows the page's `<noscript>` parts, as browsers do with
  scripts off

Sites made for small screens and for text, such as FrogFind, 68k.news,
Wikipedia and many blogs, read well.

## Pages made by scripts

**On the Falcon itself:** `EWEB.PRG` runs a little JavaScript, on a
Falcon or TT with an FPU (a 68881 or 68882, as on the DFB1). It is off
until you choose a site: open one of its pages and pick Options > Run
this site's scripts (ticked while they run); the page comes again with
its scripts. EWeb remembers the sites in `EWEB.INF`. Pick it again to
turn them off.

It is made for the small scripts pages use for menus, tabs, form checks,
a message or a question (`alert` and `confirm` are EWeb's alerts,
`prompt` a small dialog): clicks, typing in a form, timers, the page
changed and laid out again, `fetch`, and the site's cookies (except
those it marks as not for scripts, HttpOnly). Each page may bring 200 KB of script
and run for 5 seconds as it opens (2 seconds for each click or timer
after); a page whose scripts take more is shown as if they were off,
with its `<noscript>` parts, and the status line says so. Big sites
built of scripts (web apps, frameworks) are too much for a 68030: for
those, use the Pi's browser (below). The ST edition and a Falcon without
an FPU run no scripts: there the menu item says "JavaScript needs an
FPU (a 68881 or 68882)", and pages are shown as with scripts off.

With the Pi gateway set up for it (`aera-gateway` with its browser, on
the Pi), two things in the View menu use the Pi's own web browser:

- **Through the Pi's browser:** each page is opened by the browser on
  the Pi, its scripts run there, and EWeb shows the page as it then
  stands: a snapshot. Links and forms work as on any page; the status bar
  says "a snapshot from the Pi's browser". Turn it off for pages that
  don't need it: they come quicker straight from their servers.
- **Live view:** the Pi's browser's screen as a picture, with the page
  shown in EWeb opened there. A click on the picture is a click there;
  type into the field under it and press Type (or Type and Enter), or use
  the buttons for keys, scrolling and Back. For what a snapshot can't
  do: a login made by scripts, a cookie question, a puzzle. "As a page"
  brings what the Pi's browser shows back as a page in EWeb.

## Secure sites

In Falcon mode (Options > Falcon mode, on unless you turn it off) EWeb
opens `https://` pages itself: the Falcon's 68030 and DSP do the TLS
handshake, checked against the root certificates in `CACERT.PEM` (and
`ROOTS.DAT`, the same read ahead of time). The first visit to a site
takes a few seconds; later ones are quicker, as EWeb remembers what it
checked (`SIGS.DAT`).

Without Falcon mode, and always in the ST edition, pages come through
the Pi gateway: AERA-Gateway's web service on a Raspberry Pi in your
home (set up as below), whose address you type in Settings > Secure. The Pi makes the secure connection (TLS 1.3 too) and
hands the page over your home network. It also makes each picture as the
page can show it: one wider than the page comes made smaller, and WebP,
AVIF and the like come as JPEG or PNG (the Pi needs `python3-pil`, which
`aera-gateway install web` adds). A photo of a megabyte comes as a few
dozen kilobytes, and the Falcon decodes far less. When the Pi's gateway
can, it makes each picture ready for your screen too, in its own colours
and at the size the page shows it, exactly as EWeb would have made it:
EWeb takes them so whenever the gateway is on (a photo in a moment
instead of seconds on a Falcon, half a minute on an ST). After you change
the screen's colours, pictures come again from the Pi. `PENS=0` in
`EWEB.INF` turns it off (the Atari then decodes the Pi's JPEGs), `PENS=-1`
takes them only when that is quicker than the Falcon's DSP. With neither, EWeb
says so when you open a secure page, and the page shown stays.

The status bar says how a secure page came.

### Setting up the Pi gateway

On the Raspberry Pi (Raspberry Pi OS), in a terminal, with your Atari's
address in place of `ATARI_IP`:

```
sudo apt install -y git                  (Raspberry Pi OS Lite only)
git clone https://github.com/whomper/AERA-Gateway
cd AERA-Gateway
./update-pi.sh
sudo gateway/aera-gateway install web --allow ATARI_IP
```

- Add `--browser` to the last line for **Through the Pi's browser** and
  **Live view** (see [Pages made by scripts](#pages-made-by-scripts)):
  it installs Chromium and needs a Pi with 2 GB or more.
- `install web` also builds the part that makes pictures ready for your
  screen (AERA-Gateway 1.6 and later), and installs `gcc` and `make` for
  it if they're missing.
- To update the Pi later: `cd ~/AERA-Gateway && ./update-pi.sh`.
- To check it: `gateway/aera-gateway status` on the Pi, or open
  `http://PI_IP:8280/` (the Pi's address) in EWeb.

Then in EWeb: **Settings > Secure**, the Pi gateway `PI_IP` (the Pi's
address), port `8280`.

## Settings

Options > Settings (^,) has two pages:

- **General:** whether pages' pictures are shown, the size of pages
  (per cent), where words typed in the address go, and (on a Falcon)
  whether the DSP helps with pictures. (Which sites' scripts run is
  Options > Run this site's scripts.)
- **Secure:** Falcon mode (secure pages on this Atari), whether the DSP
  checks certificates, and the Pi gateway's address and port (8280).

EWeb keeps its settings in `EWEB.INF`, next to the program, with a note
above each. A setting you change is saved there at once; where the window
was, when you quit:

- `HOME=` the start page (empty: EWeb's own)
- `SEARCH=` where words typed in the address go, `%s` for the words
  (FrogFind: `http://frogfind.com/?q=%s`)
- `ZOOM=` the size of pages, per cent
- `FALCON=` 1 for Falcon mode (secure pages on this Atari), 0 for the
  Pi gateway
- `DSP=` 1 to let the DSP do the secure connections' arithmetic
- `PICTURES=` 1 to show pictures, 0 for what they are instead (View >
  Show pictures)
- `PICDSP=` 1 to let a Falcon's DSP decode JPEGs and dither pictures
  to the screen's colours (each about eight times as quick on a stock
  Falcon), 0 for the 68030 alone
- `GATE=` the Pi gateway's address: without Falcon mode every page comes
  through the Pi, which makes the secure connection itself (TLS 1.3 too)
- `GATEPORT=` its port (8280)
- `SCRIPTS=` the sites whose scripts run, their names with spaces
  between (`file` for pages on your disks)
- `WINDOW=` where the window was

## When something goes wrong

- **"EWeb can't open this page"** with the reason: the address is wrong,
  the site is down, or the network isn't there (is STinG loaded?).
  ^R tries again
- **"EWeb's fonts are missing"**: put `EWEB.FNT` next to `EWEB.PRG`
- **A secure site fails**: check that `CACERT.PEM` and `ROOTS.DAT` are
  next to `EWEB.PRG`, and the Atari's clock is right
- **EWeb crashed**: next time it starts, it offers to show `CRASH.LOG`;
  please send it to the address in About

## About EWeb

EWeb is made by AERA | Erez Yaary, free software under the GNU GPL,
version 2 or later: github.com/whomper/EWeb. Its fonts are drawn from
DejaVu; its character set tables are NetSurf's libparserutils (MIT), its GIF decoder
NetSurf's libnsgif (MIT), and its JPEG decoder libjpeg-turbo (the IJG's
and BSD licences). It belongs to the
AERA family of Atari programs (ECal, EContact, EMail, EFTP, EHypView,
ClaudeST) and looks and works as they do.
