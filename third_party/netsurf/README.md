# NetSurf's libraries in EWeb

The HTML parser and CSS engine EWeb measured first, from the NetSurf project
(https://www.netsurf-browser.org, git.netsurf-browser.org). All four are under
the MIT licence; each folder keeps its `COPYING`. EWeb's own parser and CSS
engine have replaced Hubbub and LibCSS in `EWEB.PRG` (docs/DESIGN.md), which
keeps only libparserutils' character set codecs; Hubbub and LibCSS stay as the
yardsticks of nsbench and `make h5test`.

| Folder | What it is | Commit |
|---|---|---|
| `libparserutils` | input streams and character set codecs | `6b0cbf086ca8eb8fe74b69f0c9ecf274eb2397ca` (3 Jan 2026) |
| `libwapcaplet` | interned strings | `c7c128d3eb3223b216c974471f82e9337fbcf4ba` (19 Feb 2026) |
| `libhubbub` | the HTML5 parser | `6651b8cf87a4aa87bcdb2ff024a02659cd3f9402` (3 Jan 2026) |
| `libcss` | the CSS parser and selection engine | `499f1c4601ad39942fd1b2204053a387bec9b989` (23 Aug 2026, release 0.9.2) |
| `libsvgtiny` | SVG to paths (a yardstick only) | `aea93365ea0586f598974697fe8555a003bd2e41` (23 Oct 2026; from the GitHub mirror, github.com/netsurf-browser/libsvgtiny) |

Only `include/`, `src/`, the generators in `build/` and the licence and README
files were taken: no tests, documentation or upstream Makefiles.
`libsvgtiny` isn't in `EWEB.PRG`: EWeb's own SVG reader (`src/svg`) replaced it
on speed and standards (docs/DESIGN.md, SVG), and it stays as the yardstick,
`svgrender -T` (`tests/svg/tiny.c`, on EWeb's document as libdom:
`tests/svg/dom.c`); its colour table is made by gperf in `build/gen/libsvgtiny`. EWeb's
`Makefile` builds them, with the flags NetSurf's build system gives
(`-D_ALIGNED=...`, `-D_BSD_SOURCE`, `-D_DEFAULT_SOURCE`) and
`-DWITHOUT_ICONV_FILTER` (no iconv on the Atari). The generated sources
(`aliases.inc`, `entities.inc`, the gperf element table, LibCSS's property
parsers) are made in `build/gen`.

## Local changes

Each is in `patches/` as a patch against the upstream library (git am applies
it), so that an update of a library can carry it again. They are EWeb's own:
EWeb takes no part in NetSurf's development and doesn't send them upstream.

| Patch | Why |
|---|---|
| `libparserutils-0001-iso-8859-8-i.patch` | Hebrew: ISO-8859-8-I, the logical-order Hebrew charset (and its web label "logical"), had no codec, so such a page was read as Windows-1252 and its Hebrew came out as Latin letters. It is ISO-8859-8's table. Also adds "visual", the web label for ISO-8859-8. `tests/hebrew` checks every Hebrew encoding. |
| `libparserutils-0002-input-end-of-document.patch` | A document whose last chunk was 64n+1 characters long lost its last character (the read codec kept it, and nothing asked for it at the end of the input), and the input stream never flushed its filter at the end, so other output kept back could be lost too. `tests/input` checks every length around those limits. |
| `libparserutils-0003-utf8-copy.patch` | Speed and standards: UTF-8 input was decoded to UCS-4 and encoded again, 30% of Hubbub's parse on a 68030; now it is copied, and what isn't UTF-8 is replaced as the WHATWG Encoding Standard's decoder does (one U+FFFD for each maximal subpart), the same whatever the chunks. `tests/input` checks it against a plain version of that decoder. |
| `libparserutils-0004-input-runs.patch` | Speed: `parserutils_inputstream_avail()`, the decoded input from an offset on (whole characters), so the tokeniser can take a run of characters at once (`libhubbub-0002`); the UTF-8 copy looks at four bytes at a time; and a page in a charset of a byte a character (Windows-1252, ISO-8859-8, Windows-1255, ...) goes through a table of its bytes 0x80-0xFF in UTF-8, ASCII copied, instead of two codecs a character at a time (a third of such a page's parse). The output is the same; `tests/input` checks the single-byte charsets against their codecs in random chunks. |
| `libparserutils-0005-encoding-standard-indexes.patch` | Standards: the single-byte codecs decode as the WHATWG Encoding Standard's indexes, which browsers use: the ISO-8859 encodings' bytes 0x80-0x9F are the C1 controls, the Windows code pages' unassigned bytes in 0x80-0x9F theirs too, and Windows-1255's 0xCA is U+05BA (a Hebrew point). They were U+FFFD, in a page's text and in its links' and forms' queries, which sent `&#65533;` where browsers send the byte. `tests/browse/enctest.c` checks every single-byte encoding EWeb decodes against its index. |
| `libhubbub-0001-prescan-1024-bytes.patch` | Standards: the HTML standard's prescan for a `<meta charset>` reads the first 1024 bytes; Hubbub read 512, so pages with the meta between the two were first read as Windows-1252 and had to be parsed again. |
| `libhubbub-0002-runs.patch` | Speed: in the data state, tag and attribute names and attribute values, the tokeniser took one character a turn (a call, a peek, a few tests and a buffer append each), most of the parse on a 68030. It now takes the run up to the next character the state treats specially in one go. The tokens are the same (`make h5test`, the page set's display lists). |
