# NetSurf's libraries in EWeb

The HTML parser and CSS engine EWeb measures first, from the NetSurf project
(https://www.netsurf-browser.org, git.netsurf-browser.org). All four are under
the MIT licence; each folder keeps its `COPYING`.

| Folder | What it is | Commit |
|---|---|---|
| `libparserutils` | input streams and character set codecs | `6b0cbf086ca8eb8fe74b69f0c9ecf274eb2397ca` (3 Jan 2026) |
| `libwapcaplet` | interned strings | `c7c128d3eb3223b216c974471f82e9337fbcf4ba` (19 Feb 2026) |
| `libhubbub` | the HTML5 parser | `6651b8cf87a4aa87bcdb2ff024a02659cd3f9402` (3 Jan 2026) |
| `libcss` | the CSS parser and selection engine | `499f1c4601ad39942fd1b2204053a387bec9b989` (23 Aug 2026, release 0.9.2) |

Only `include/`, `src/`, the generators in `build/` and the licence and README
files were taken: no tests, documentation or upstream Makefiles. EWeb's
`Makefile` builds them, with the flags NetSurf's build system gives
(`-D_ALIGNED=...`, `-D_BSD_SOURCE`, `-D_DEFAULT_SOURCE`) and
`-DWITHOUT_ICONV_FILTER` (no iconv on the Atari). The generated sources
(`aliases.inc`, `entities.inc`, the gperf element table, LibCSS's property
parsers) are made in `build/gen`.

## Local changes

Each is in `patches/` as a patch against the upstream library, ready to send
to NetSurf (git am applies it), and is carried again after an update until
upstream has it.

| Patch | Why |
|---|---|
| `libparserutils-0001-iso-8859-8-i.patch` | Hebrew: ISO-8859-8-I, the logical-order Hebrew charset (and its web label "logical"), had no codec, so such a page was read as Windows-1252 and its Hebrew came out as Latin letters. It is ISO-8859-8's table. Also adds "visual", the web label for ISO-8859-8. `tests/hebrew` checks every Hebrew encoding. |
