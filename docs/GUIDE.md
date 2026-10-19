# EWeb user guide

EWeb is a web browser for the Atari Falcon (and the TT). It fetches pages
itself over the internet, secure ones too, and shows them with its own
fonts: text, links, headings, lists, tables, pictures and forms, in
colour or black and white.

## What you need

- A Falcon030 (best with an accelerator such as the DFB1 and TT RAM) or
  a TT, with TOS 4, EmuTOS or MagiC; a screen 640 pixels wide in any
  mode: monochrome, 4, 16 or 256 colours, or the Falcon's true colour
- About 2 MB of free memory, more for big pages; TT RAM is used when
  there is some
- **STinG** for the network (plain TOS), on a network card such as the
  NetUSBee. Without a network EWeb still opens pages on your disks
- Under TOS, Atari's `FOLDR100.PRG` first in `AUTO`, renamed for more
  room (`FOLDR400.PRG`): TOS keeps an entry in a small internal pool for
  every folder a program or accessory has looked into, and halts with
  "OUT OF INTERNAL MEMORY" when it runs out

## Installing

The files are in the latest release on EWeb's GitHub page
(github.com/whomper/EWeb/releases), each on its own and all together in
`EWEB-x.y.zip`. Put them in a folder of their own, on any drive:

```
EWEB.PRG      the program
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

While a page loads, the info line says what is happening and how much
has come; **Esc**, **Undo** or the Stop button stop it, and the page
you were on stays.

## Pictures

A page is shown as soon as its text is laid out; its pictures come
after, those in the window first, then those below, and each is drawn
as it comes ("Pictures: 3 of 12" on the info line). You can read and
scroll meanwhile; clicking a link or typing an address goes there at
once. **Esc** (or File > Stop) stops the pictures of the page shown.

Until a picture has come its place is framed, with what it shows written
in it where the page says. A picture whose size the page doesn't give
shows its description first, and the page is laid out again when its
size is known (at most every two seconds, so that it doesn't jump about
while you read).

EWeb shows JPEG, PNG (with transparency) and GIF (the first picture of
an animated one), decoded at the size the page shows them: a big photo
shown small takes much less time and memory than at its own size. In 16
or 256 colours and in monochrome they are dithered to the screen's own
colours; in true colour they are as they are. An address of a picture
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

## Saving pages and files

- **File > Save page as** (**^S**) saves the page shown as it came, in
  the file selector's folder, with a name made from its address
  (`INDEX.HTM` for a site's front page). Its pictures and stylesheets
  stay on the server; open the file again with ^O.
- **A link's file**: Alternate-click the link, or right-click it and
  choose Save the link's file. It goes straight to the disk as it comes,
  so it can be bigger than your memory; the info line counts it, and
  Esc stops it (nothing is left behind).
- **A file EWeb doesn't show** (a ZIP, a program, a PDF), opened as a
  page: EWeb asks where to save it, and the page you were on stays.

The file selector offers the name; if there is a file of that name
already, EWeb asks before replacing it.

## The window

At the top, from the left:

- **Back** and **Forward** (the arrows)
- **Reload**, which turns into **Stop** while a page loads
- **Home**, the start page
- the address of the page shown, which you can type into

Under them is the page, with the window's slider on the right. The info
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

**^+** makes pages larger, **^-** smaller, by a tenth each time, and
**^0** puts them back as they were. At the usual size a page's pixel is
0.8 of the screen's, so that pages made for wider screens fit the
Falcon's 640.

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
| Help | this guide |
| ^Q, ^W | quit |

## What EWeb shows

EWeb shows pages as their stylesheets lay them out: headings,
paragraphs, lists, tables, links, bold and italic, colours and
backgrounds, borders, pictures, boxes and pictures floated beside the
text, positioned boxes, tables with cells over several rows, forms,
Hebrew right to left. It doesn't yet
show:

- file fields (EWeb can't send files yet)
- pictures in the background of a page or a box (CSS
  `background-image`), WebP and AVIF pictures (EWeb asks servers for
  JPEG, PNG and GIF, which most send then), and SVG drawings
- anything made by JavaScript

Sites made for small screens and for text, such as FrogFind, 68k.news,
Wikipedia and many blogs, read well.

## Secure sites

EWeb opens `https://` pages itself: the Falcon's 68030 and DSP do the
TLS handshake, checked against the root certificates in `CACERT.PEM`
(and `ROOTS.DAT`, the same read ahead of time). The first visit to a
site takes a few seconds; later ones are quicker, as EWeb remembers
what it checked (`SIGS.DAT`). The info line says how a secure page came.

## Settings

EWeb keeps its settings in `EWEB.INF`, next to the program, with a note
above each. A setting you change is saved there at once; where the window
was, when you quit:

- `HOME=` the start page (empty: EWeb's own)
- `SEARCH=` where words typed in the address go, `%s` for the words
  (FrogFind: `http://frogfind.com/?q=%s`)
- `ZOOM=` the size of pages, per cent
- `DSP=` 1 to let the DSP do the secure connections' arithmetic
- `PICTURES=` 1 to show pictures, 0 for what they are instead (View >
  Show pictures)
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
