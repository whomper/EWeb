# EWeb user guide

EWeb is a web browser for the Atari Falcon and TT, and, in its ST
edition (`EWEBST.PRG`), for the ST and STE. It fetches pages itself over
the internet, secure ones too, and shows them with its own fonts: text,
links, headings, lists, tables, pictures and forms, in colour or black
and white.

## What you need

- A Falcon030 (best with an accelerator such as the DFB1 and TT RAM) or
  a TT, with its own TOS (4 on a Falcon, 3 on a TT), EmuTOS or MagiC; a
  screen 640 pixels wide in any mode: monochrome, 4, 16 or 256 colours, or
  the Falcon's true colour.
  On an ST or STE (TOS 1.04 or newer) with 4 MB, use `EWEBST.PRG`, the
  ST edition: it has no secure connections of its own and reaches secure
  sites through AERA-Gateway's web service on a Raspberry Pi (the Pi
  gateway, in [Settings](#settings))
- For `EWEB.PRG` about 3.5 MB of free memory (the program itself takes
  1.5 MB, a page up to about 11 times its size): a Falcon's own 4 MB is
  enough when the family's accessories aren't loaded; TT RAM is used when
  there is some. A page's scripts need an FPU and `EWEBJS.OVL` next to
  `EWEB.PRG` (0.9 MB more, taken the first time they run)
- **STinG** for the network, on a network card such as the NetUSBee
  (under FreeMiNT or MagiC-Net, their own MiNTnet works too). Without a
  network EWeb still opens pages on your disks.
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
COOKIES.TXT   cookies kept from one day to the next (EWeb writes it)
EWEB.KEY      the key their values are encrypted with (EWeb writes it)
BOOKMARK.HTM  your bookmarks (EWeb writes it)
CACHE\        the disk cache: EWEB.PAK and EWEB.IDX (EWeb writes them)
CRASH.LOG     what happened, if EWeb ever crashes
```

EWeb keeps everything in the folder it is started from. Start it from
the desktop with a double click; drop a page (an `.HTM` file) on it, or
on its window, to open that page. Other programs can hand it an address
or a file to show (VA_START), and a mail address (a `mailto:` link) goes
to EMail, when it is running.

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
fills from left to right, in a colour for each step (in 16 colours or
more; black in monochrome and 4 colours): blue while EWeb connects, cyan
as the page comes, magenta for its stylesheets, yellow while the page is
laid out, green as its pictures come. **Esc**,
**Undo** or the Stop button stop it, and the page you were on stays:
while it comes, and while a long page is read and laid out too (the
window keeps redrawing meanwhile).

A page that takes a while to come shows its start while the rest comes,
when EWeb already has its stylesheets (a site you have been to), and a
long one while the whole is laid out. You can scroll it; links and fields
work once the whole page is there, and it stays where you scrolled to.
On an Atari without TT-RAM only a page small enough to fit in memory
beside its start does this, and EWEBST.PRG doesn't.

## Pages that go on by themselves

Some pages send you on to another after a while, or load themselves
again (a "refresh"): a page that has moved, a search engine's link
to a result, a news page that keeps itself up to date. EWeb does as
they ask once the page is shown (a page that loads itself again,
once its pictures are in too, and then shows its pictures as they
are now: a camera's, a weather map), and the status bar says what is
coming: "Going on to example.com in 5 s: Esc stays here." **Esc** or
the Stop button keep the page you are on, and while you type in the
address field the page waits for you. The page that sent you on isn't
kept in Back's list, so **Undo** goes to the page before it; Back and
Forward show a page without sending you on again. A page that sends
you on at once, again and again, is stopped after five times, and the
status bar says so until you go elsewhere.

## Pictures

A page is shown as soon as its text is laid out; its pictures come
after, those in the window first, then those below, and each is drawn
as it comes ("Pictures: 3 of 12" on the status bar). You can read and
scroll meanwhile; clicking a link or typing an address goes there at
once. **Esc** (or File > Stop) stops the pictures of the page shown.

Until a picture has come its place is framed, with what it shows written
in it where the page says. EWeb shows a WebP picture of up to about a
megapixel itself (one that big takes about 40 seconds on a fast Falcon;
the ST edition shows none). SVG drawings, as logos and icons often are,
EWeb draws itself at the size the page shows them, in both editions,
whether they come as a file or are written into the page; their
writing, clipping, masks and filters aren't drawn. Pictures EWeb can't
show (a bigger WebP, AVIF) aren't fetched at all; through the Raspberry Pi gateway they
come as JPEG or PNG, and big ones come made smaller on the Pi, a few dozen
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
DSP the rest; Settings > General). In monochrome and in 4, 16 or 256
colours they are dithered to the screen's own colours (on a Falcon in 4,
16 and 256 colours by the DSP, eight times as quick); in true colour they
are as they are. An address of a picture
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
(a file field sends none), and buttons that need JavaScript do nothing
unless you let that site's scripts run (see
[Pages made by scripts](#pages-made-by-scripts)).

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

## History

**View > History** (**^Y**) shows the pages you have opened, the
newest first, under the day you last opened each, a hundred to a page.
A link to a page you have opened is drawn in its visited colour (purple,
unless the page chooses another; in 16 colours, dark blue beside the
bright blue of the others), so you can see where you have been; a link
to a place on a page counts once you have followed it.

The newest 1000 (fewer when their addresses are long) are kept in
`HISTORY.TXT` next to `EWEB.PRG`, written when you quit EWeb; an
address with a password in it is never kept. **Options > Forget history** forgets them, the file
too. A page whose scripts run never shows which of its links you have
visited: a script could read it, so they stay in the colour of a link
not yet followed.

## Saving pages and files

- **File > Save page as** (**^S**) saves the page shown as it came, in
  the file selector's folder, with a name made from its address
  (`INDEX.HTM` for a site's front page). Its pictures and stylesheets
  stay on the server; open the file again with ^O.
- **A link's file**: Alternate-click the link, or right-click it and
  choose Save the link's file. EWeb asks where to save it as soon as the
  site answers, then it goes straight to the disk as it comes, so it can
  be bigger than your memory; the status bar counts it, and Esc stops it
  (nothing is left behind). Sites wait about a minute for you to choose;
  if one gives up, EWeb asks it for the file once more. A link the page
  marks as a download (its site's own) is saved this way when you click
  it.
- **A file EWeb doesn't show** (a ZIP, a program, a PDF), opened as a
  page, and one its site says to save rather than show (a text file it
  sends as an attachment): EWeb asks where to save it, and the page you
  were on stays.

The file selector offers a name: the one the site or the link gives the
file when there is one (so `download.php?id=12` comes as `GAME.ZIP`),
else one made from its address, made to fit the Atari's disks (eight
letters and three, in capitals, only the characters TOS takes, `.tar.gz`
as `.TGZ`). If there is a file of that name already, EWeb asks before
replacing it. A packed archive (`.gz`, `.tgz`) is saved as it came, not
unpacked.

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

The cache takes 32 MB (2 MB without TT RAM, as on a stock Falcon or an
ST); Settings > General > Disk cache changes it, up to 64 MB, 0 for
none. When it is full, what came first makes room for what comes, so
quitting takes no time: EWeb writes only what came since it last wrote
and its list. Options > Empty the cache forgets it all.

## The window

Along the top, the toolbar: at the left a row of buttons, each a
picture:

- **Back** and **Forward**
- **Reload** the page shown
- **Home**, the home page
- **Search**, the search engine's own page (Settings > General)
- **Stop** a page while it loads

They lie flat until the mouse is over one, which then wakes: it lights
up, stands out in a frame and says its name on the status bar (one that
can't be used is greyed); held down, it sinks, and works
when you let go over it. Beside them is the address of the page shown,
which you can type into.

At the right end is the AERA A. While a page or its pictures come, a star
goes round it over a field of faint stars; when it stands still, EWeb
is waiting for you.

Under that is the page, with the window's slider on the right. The
status bar at the foot of the window shows the address of the link under
the mouse, a button's name, what a load is doing, or, for a secure page,
how it is secured. The window's title is the page's.

Click a link to follow it (the pointer turns into a hand over one).
Right-click a link for its commands (open it, open it in a new window,
save its file, copy its address), the page for Back, Forward, Reload,
Copy and Select all, or the address for Copy, Cut, Paste and Select all.

The window can be moved, sized, fulled and iconified as any other; EWeb
lays the page out again for the new width.

## More windows

**^N** (File > New window) opens another window with the home page, a
little below and to the right of the one in front; **Control-click** a
link, or choose Open it in a new window from its menu, to read it in one.
A link the page means for a new window (its target) opens one too, and
a link naming a window EWeb opened for it earlier loads in that window.
Each window has its own page, Back and Forward, and address; the
settings, bookmarks, history, cookies and cache are the same in all of
them.

The keys and the menus work on the window in front. One page loads at a
time: while it does, the other windows can be moved, topped and
redrawn but wait for it. Pictures come for the window in front first,
then for the others; a page's timers and its refresh wait while its
window is behind. **^W** (File > Close window) closes the window in
front, and closing the last one quits EWeb.

How many windows there may be depends on the memory: eight beside TT
RAM, four with 6 MB of ST RAM free, else two (a 4 MB Falcon, a 1040 ST).

## Moving around a page

- the sliders and the window's arrows (the one along the foot moves a
  page wider than the window sideways)
- **Up** and **Down**: a few lines; with **Shift**, a page
- **Left** and **Right**: a page wider than the window a little
  sideways; with **Shift**, a window's width
- **Space**: a page down; **Shift-Space**: a page up
- **ClrHome**: the top; **Shift-ClrHome**: the end
- a box on the page with a grey bar of its own down its right side or
  along its foot (a code block, a list in a frame) holds more than it
  shows: click the bar above or below its darker part to move the box
  on a boxful, or drag that part
- a link to a place on the same page (`#...`) scrolls there, opening
  the folded part it is in (below)
- a heading with a small triangle pointing right is a part the page
  has folded away (FAQ pages and documentation have them): click it to
  open the part (the triangle points down), and again to fold it; the
  window stays where it is

## Back, Forward and Home

**Undo** (or **^Z**) goes back, **Shift-Undo** (or **Shift-^Z**)
forward, to where you were on each page. EWeb keeps the last 50 pages.
The last few stay in memory as they were laid out (in TT RAM when there
is some), so Back and Forward show them at once, without fetching or
reading them again; **^R** fetches the page again.

**Home** opens your home page: EWeb's own, or the page you chose with
Options > Use this page as home.

## Frames

Some pages are made of frames: the window split into parts, each a page
of its own (a menu down the left and the text beside it), or a smaller
page in a box of its own on a page (an iframe: a map, a player, a part
of another site). EWeb shows each frame's page in its place, fetched
just after the page itself: the frames of a split window first, the
boxes after the page's pictures. A frame that holds more than it shows
has a grey bar of its own, to click or drag; on a split window, point
at a frame and **Up**, **Down**, **Space** and **ClrHome** move it.

A link in a frame opens where the page says: in its own frame, in the
frame it names (a menu's links in the text beside it), or in the whole
window. **Undo** goes back a step in a frame as well as from page to
page, and the address shown stays the page's. Scripts in frames don't
run, so a player or an advert in a box may stay empty.

## The home page

EWeb's own home page greets you with the date, and has a search box (its
words go to your search engine, Settings > General), the pages you opened
last and your first bookmarks, a row of sites for each kind (search,
news, reading, Atari, the early web, AERA) that read well on an Atari,
and a tip. Bookmarks and History at its foot list all of them.

## Text size

**^+** makes the text larger, **^-** smaller, by a tenth each time, and
**^0** puts it back as it was. Only the text changes: the pictures keep
their size and stay as they were decoded, so nothing is fetched or
decoded again (a page's pixel is 0.8 of the screen's, so that pages
made for wider screens fit the Falcon's 640).

## Encodings

Most pages say which encoding their text is in (UTF-8, Windows-1255
for Hebrew, Windows-1252 for Western European languages, and so on),
and EWeb reads them so. A page that doesn't say is read as UTF-8 when
its text is UTF-8; otherwise by its address, as browsers do: Windows-1255
when it ends in .il (an Israeli site), Windows-1251 for a Russian,
Ukrainian, Belarusian, Bulgarian, Serbian or Macedonian site (.ru, .ua,
.by, .bg, .rs, .mk and the like), ISO-8859-7 for a Greek or Cypriot one
(.gr, .cy), else Windows-1252. Text files
(.TXT on your disks, or text from a server) are read the same way, and
shown as their lines.

When a page's letters still come out wrong (Latin letters where there
should be Hebrew, or two odd signs in place of each accented letter),
**View > Encoding...** lists the
encodings EWeb has; choose the right one and the page is read again in
it, from what came (nothing is fetched again). The choice stays with
that page, when you reload it and when you come back to it, until you
open another; **Auto** goes back to what the page says. The list:

- Unicode (UTF-8)
- Western (Windows-1252, ISO-8859-1) and Western (ISO-8859-15)
- Hebrew (Windows-1255), Hebrew, logical (ISO-8859-8-I) and Hebrew,
  visual (ISO-8859-8)
- Central European (Windows-1250, ISO-8859-2), Turkish (Windows-1254,
  ISO-8859-9)
- Cyrillic (Windows-1251, ISO-8859-5) and Greek (Windows-1253,
  ISO-8859-7)
- Baltic (Windows-1257, ISO-8859-13, ISO-8859-4), Nordic (ISO-8859-10)
  and South European (ISO-8859-3)

EWeb's fonts have the Latin letters (with Vietnamese, Romanian and the
accents written over a letter), Hebrew, Greek, Cyrillic and the common
symbols (arrows, mathematics, currencies, check marks, card suits); a
character they haven't got, such as Arabic, Armenian, Georgian or an
emoji, is shown as ?. A page in
visual Hebrew (ISO-8859-8, its letters stored in the order they are
shown) is shown as it is stored, left to right, as browsers show it;
if its Hebrew comes out reversed, the page is logical Hebrew: choose
Hebrew, logical (ISO-8859-8-I). Hebrew mixed with English and numbers
is ordered as browsers order it (the Unicode bidirectional algorithm),
with what a page says of each part's direction: `dir` on any element
(`dir=auto` from its text), `<bdi>`, `<bdo>`, CSS `direction` and
`unicode-bidi`, and the direction marks in the text.

What you type into a page's forms, and the words in its links, go to
the site in the page's encoding (or the one its form asks for), as
sites made for those pages expect.

## Finding words

**Edit > Find...** (**^F**) asks for words and marks the first of them
in yellow from the top of the window down, bringing it into view (into
a box that scrolls, too); the status bar says which it is, "2 of 5".
**^G** finds the next, **Shift-^G** the one before, going round at the
page's end. Capitals and small letters are the same (é and É too), a
space finds any space or the end of a line, and Hebrew is found as it
is read. Text the page hides isn't found.

## Selecting and copying

Drag over the page's text to select it, from one place to another (on a
link too: a drag selects, a click follows it); the window scrolls when
the mouse goes above or below it. A double click selects a word,
**Shift**-click takes the selection on to there, and **Edit > Select
all** (**^A**) selects all of the page's text. A click elsewhere on the
page lets it go. The selection stays the same text when the page is laid
out again (a picture comes, the window is sized), and Hebrew is
selected as it is read.

**^C** copies the selection to the GEM clipboard (to paste into another
program), or, with nothing selected, all of the page's text; the address
when the address field has the keyboard. Words found by **^F** are
selected too: **^C** copies them, and **^G** goes on from the selection.
In the address field **^X**, **^V** and **^A** cut, paste and choose all
of it.

## Keys

| Key | What it does |
|---|---|
| ^L | type an address (Return goes there) |
| ^O | open a page on your disks |
| ^S | save the page shown |
| ^D | add the page shown to the bookmarks |
| ^B | show the bookmarks |
| ^Y | show the history |
| ^F | find words on the page |
| ^G, Shift-^G | find the next, the one before |
| Alternate-click | save a link's file |
| Control-click | open a link in a new window |
| ^N | a new window, with the home page |
| Undo, ^Z | back |
| Shift-Undo, Shift-^Z | forward |
| ^R | reload |
| Esc | stop a page loading, or its pictures; stay on a page that would go on |
| Up, Down | scroll; with Shift, a page |
| Left, Right | scroll a wide page sideways; with Shift, a window's width |
| Space, Shift-Space | a page down, up |
| ClrHome, Shift-ClrHome | the top, the end |
| ^+ ^- ^0 | larger, smaller, as it was |
| ^C | copy the selection (else the page's text), the address, or a field's text |
| ^X, ^V | cut, paste (in the address or a field) |
| ^A | all the page's text; in the address, the whole of it, to type over |
| Tab, Shift-Tab | the page's next field (or the one before), then the address |
| Return | in a field: send its form |
| ^, | the Settings |
| Help | this guide |
| ^W | close the window in front (the last one: quit) |
| ^Q | quit |

Help > Keys shows the main keys in a box of their own.

## What EWeb shows

EWeb shows pages as their stylesheets lay them out: headings,
paragraphs, lists, tables, links, bold and italic, colours and
backgrounds, borders, pictures, boxes and pictures floated beside the
text, pictures in the background of a page or a box, positioned
boxes (one over another as the page asks), tables (cells over several rows and columns, borders that
collapse into single lines), boxes in a line of text
(buttons, menus and cards: CSS's inline-block), flexible boxes and
grids (CSS flexbox and grid), forms, frames (see [Frames](#frames)),
Hebrew right to left (with its
numbers, English words and brackets the right way round). A line ends
at a space, or inside a word where it may: after a hyphen or a slash, at
a dash, between Chinese or Japanese characters, and at a soft hyphen,
which then shows as a hyphen at the line's end. Translucent
colours and see-through boxes (CSS's opacity) are mixed with what is
under them, as near as your screen's colours allow: with 16 colours or
fewer a faint tint may not show, and but for true colour a see-through
picture is drawn as a fine pattern. What a page hides with no opacity
at all isn't drawn, though its links still work (unless the page lets
clicks through it to what is under it). What a page would fade in with
an animation, or as you scroll to it (AOS, Webflow, pictures loaded as
you scroll), is shown, but other text a page's script would show may
stay hidden while scripts are off. Colour gradients (CSS's linear,
radial and conic ones) are drawn, smooth in true colour and in a fine
pattern of the screen's colours otherwise. The ST edition, short of
memory, draws those that run straight down or across in the screen's
own colours (in greys on a monochrome screen, in a few bands with 4 or
16 colours) and any other in its colours' average. A gradient painted
through text, a shadow or an outline, which EWeb doesn't draw, shows
the text in their colour. Of a background in several layers EWeb draws
the top one. Older pages that centre their tables and boxes with
`<center>` or `align` show them centred (or at the right) as they were
meant, with pictures beside or within the text where `align` puts them.
It doesn't yet show:

- a file to send: a file field shows "No file" (EWeb can't send files
  yet)
- AVIF pictures, and WebP ones of more than about a megapixel (EWeb
  asks servers for JPEG, PNG and GIF, which most send then; of a
  picture offered in several forms and sizes it takes a JPEG, PNG or
  GIF one at the size the screen needs), animated WebP, and the
  writing in an SVG drawing
- what a page's JavaScript makes, unless you let that site's scripts
  run (see [Pages made by scripts](#pages-made-by-scripts)); without
  them EWeb shows the page's `<noscript>` parts, as browsers do with
  scripts off

Sites made for small screens and for text, such as FrogFind, 68k.news,
Wikipedia and many blogs, read well.

A page's styles for printing, and those a site keeps for screens wider
or narrower than the window, are left out (EWeb doesn't fetch them).
When you make the window wider or narrower, a page made for several
sizes takes the styles it has for the new one, fetching them then if
they hadn't come yet (the mouse is busy meanwhile); what you typed into
its fields stays.

## Pages made by scripts

**On the Falcon itself:** `EWEB.PRG` runs a little JavaScript, on a
Falcon or TT with an FPU (a 68881 or 68882, as on the DFB1). Settings >
Scripts says where:

- **On the sites you choose** (as it starts): open one of a site's
  pages and pick Options > Run this site's scripts (ticked while they
  run); the page comes again with its scripts. Pick it again to turn
  them off.
- **On every site** (tick "On every site, not only those chosen"):
  Options > Run this site's scripts, unticked, leaves a site out. A site
  whose scripts are too much for EWeb (below) is left out until you quit
  EWeb, so its pages aren't made twice; picking the menu item tries it
  again.
- **On no site** (untick "Run pages' scripts").

Scripts cost a Falcon time: a page with scripts shows once they have
come and run, and EWeb starts its JavaScript for each such page (about
a second on a 50 MHz 68030). On every site, pages that need no scripts
but bring them anyway are slower than with scripts off. EWeb remembers
the setting and the sites in `EWEB.INF`.

It is made for the small scripts pages use for menus, tabs, form checks,
a message or a question (`alert` and `confirm` are EWeb's alerts,
`prompt` a small dialog): clicks, typing in a form, timers, the page
changed and laid out again, `fetch`, and the site's cookies (except
those it marks as not for scripts, HttpOnly). Each page may bring 200 KB of script
and run for 5 seconds as it opens (2 seconds for each click or timer
after); a page whose scripts take more is shown as if they were off,
with its `<noscript>` parts, and the status line says so. Big sites
built of scripts (web apps, frameworks) are too much for a 68030: for
those, use the Pi's browser (below). The ST edition runs no scripts and
has no such menu item; on a Falcon without an FPU, picking it says
"JavaScript needs an FPU (a 68881 or 68882)", and pages are shown as
with scripts off.

With the Pi gateway set up for it (`aera-gateway` with its browser, on
the Pi) and in use (its address in Settings > Secure, and on a Falcon or
TT the Direct connection off), two things in the View menu use the Pi's own web
browser:

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

With a Direct connection (Options > Direct connection, on unless you
turn it off) EWeb opens `https://` pages itself, on a Falcon, a TT or any Atari with a 68020 or better: the
68030 (and a Falcon's DSP) do the TLS handshake, checked against the root certificates in `CACERT.PEM` (and
`ROOTS.DAT`, the same read ahead of time). The first visit to a site
takes a few seconds; later ones are quicker, as EWeb remembers what it
checked (`SIGS.DAT`).

Without a Direct connection, and always in the ST edition, pages come through
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

Or, from AERA-Gateway 1.8, every gateway for your Atari in one line:

```
sudo apt install -y git &&
  git clone https://github.com/whomper/AERA-Gateway ~/AERA-Gateway &&
  ~/AERA-Gateway/update-pi.sh --allow ATARI_IP
```

- Add `--browser` to the last line for **Through the Pi's browser** and
  **Live view** (see [Pages made by scripts](#pages-made-by-scripts)):
  it installs Chromium and needs a Pi with 2 GB or more.
- `install web` also builds the part that makes pictures ready for your
  screen (AERA-Gateway 1.6 and later), and installs `gcc` and `make` for
  it if they're missing.
- To update the Pi later: `cd ~/AERA-Gateway && ./update-pi.sh`.
- To check it: `sudo aera-gateway status` on the Pi; or, before EWeb
  goes through it (or with a Direct connection), open `http://PI_IP:8280/` (the
  Pi's address) in EWeb: it says webgate is running.

Then in EWeb: **Settings > Secure**, the Pi gateway `PI_IP` (the Pi's
address), port `8280`; on a Falcon or TT, untick Direct connection there too,
or pages still come straight from their servers.

## Settings

Options > Settings (^,) has three pages (two in the ST edition):

- **General:** whether pages' pictures are shown, the text size (per
  cent, 50 to 200), where words typed in the address go, the disk
  cache's size (MB, 0 for none), in `EWEB.PRG` whether a Falcon's DSP
  does the pictures, whether EWeb uses its own colours in 256
  colours, and **3D dialogs**: EWeb's dialogs and alerts in the 3D look
  where the system has one (TOS 4, MagiC, XaAES, EmuTOS 1.4 on a Falcon;
  greyed on TOS 1 to 3, which have none).
- **Secure:** Direct connection (TLS on this Atari), whether the DSP
  checks certificates, and the Pi gateway's address and port (8280).
- **Scripts** (`EWEB.PRG`): whether pages' scripts run, and on every site
  or only on those you choose (see
  [Pages made by scripts](#pages-made-by-scripts)).

The search address, the gateway's address and the answer to a page's
question (a script's `prompt`) can be longer than their fields (255, 63
and 255 characters): the field moves along with the cursor (Shift+left
and right: to the start and the end; ^C and ^X copy or cut the whole
text, ^V puts the clipboard's text in at the cursor).

EWeb keeps its settings in `EWEB.INF`, next to the program, with a note
above each and an empty line after it. A setting you change is saved
there at once; where the window was, when you quit:

- `HOME=` the start page (empty: EWeb's own)
- `SEARCH=` where words typed in the address go, `%s` for the words
  (FrogFind: `http://frogfind.com/?q=%s`)
- `ZOOM=` the size of the text, per cent (50 to 200)
- `FALCON=` 1 for a Direct connection (secure pages on this Atari), 0 for the
  Pi gateway
- `DSP=` 1 to let the DSP do the secure connections' arithmetic
- `PICTURES=` 1 to show pictures, 0 for what they are instead (View >
  Show pictures)
- `PICDSP=` 1 to let a Falcon's DSP decode JPEGs and dither pictures
  to the screen's colours (each about eight times as quick on a stock
  Falcon), 0 for the 68030 alone
- `PALETTE=` in 256 colours, 1 for EWeb's own colours while it runs
  (a colour cube and greys, with the pale tints pages' backgrounds and
  pictures need; the system's first 16 stay, and its own come back when
  EWeb quits), 0 for the system's as they are (another program's
  pictures beside EWeb's keep their colours)
- `DIALOGS3D=` 1 for dialogs and alerts in the 3D look where the
  system has one, 0 for them flat everywhere
- `PENS=` through the Pi gateway, pictures made ready for your screen on
  the Pi: 1 always, -1 only when that is quicker than the DSP, 0 never
  (the Atari decodes them)
- `GATE=` the Pi gateway's address: without a Direct connection every page comes
  through the Pi, which makes the secure connection itself (TLS 1.3 too)
- `GATEPORT=` its port (8280)
- `SNAPSHOT=` 1 for pages through the Pi's browser (View > Through the
  Pi's browser), 0 for pages as their servers send them
- `RUNSCRIPTS=` pages' scripts: 0 on no site, 1 on the sites in
  `SCRIPTS=`, 2 on every site but those in `NOSCRIPTS=` (Settings >
  Scripts)
- `SCRIPTS=` the sites whose scripts run, their names with spaces
  between (`file` for pages on your disks)
- `NOSCRIPTS=` on every site, the sites whose scripts don't run
- `CACHE=` the disk cache's size, MB, 0 for none (Settings > General);
  not in the file until you change it, and till then 32 with TT RAM,
  else 2
- `WINDOW=` where the window was

## When something goes wrong

- **"EWeb can't open this page"** with the reason: the address is wrong,
  the site is down, or the network isn't there (is STinG loaded?).
  ^R tries again
- **"Google wants JavaScript that EWeb doesn't run"**: Google's search
  shows its results only to browsers that first run a check of its own,
  which EWeb's little JavaScript doesn't pass. The page has a link that
  searches for the same words with the search engine in Settings
  (FrogFind unless you chose another), which needs no JavaScript; words
  typed in the address go there too
- **"This site lets in only browsers that pass Cloudflare's check"**:
  the site has Cloudflare's check ("Just a moment...") in front of it, a
  script for the big browsers that EWeb can't run. The page links to the
  Internet Archive's copy of the page, if it has one. Through the Pi
  gateway with its browser (AERA-Gateway with `--browser`), the Pi passes
  the check itself and EWeb shows the page; when the check wants a click,
  the page's **Live view** link shows the Pi's screen to tick it, and
  then ^R loads the page
- **"EWeb's fonts are missing"**: put `EWEB.FNT` next to `EWEB.PRG`
- **A secure site fails**: check that `CACERT.PEM` and `ROOTS.DAT` are
  next to `EWEB.PRG`, and the Atari's clock is right
- **EWeb crashed**: next time it starts, it offers to show `CRASH.LOG`;
  please send it with a bug report on EWeb's GitHub page
  (github.com/whomper/EWeb/issues)

## About EWeb

EWeb is made by AERA | Erez Yaary, free software under the GNU GPL,
version 2 or later: github.com/whomper/EWeb. Its fonts are drawn from
DejaVu; its character set tables are NetSurf's libparserutils (MIT), its
GIF decoder NetSurf's libnsgif (MIT), its JPEG decoder libjpeg-turbo (the
IJG's and BSD licences; ChaN's TJpgDec in the ST edition), its secure
connections BearSSL (MIT), and its scripts QuickJS with musl's maths
(MIT). It belongs to the AERA family of Atari programs (ECal, EContact,
EMail, EFTP, EHypView, ClaudeST, EUpdate) and looks and works as they do.
