# EWeb's CSS engine

`src/css/` is EWeb's own CSS engine. It takes stylesheets and a document and
gives every element its computed style, the job LibCSS does in NetSurf. It
was written because LibCSS was most of the time a page cost on the Falcon:
19 of the 21 seconds `NEWS.HTM` took on the DFB1 (see `DESIGN.md`). On the
same pages the new engine takes about a tenth of LibCSS's time, and it is
closer to today's specifications.

## How it is fast

Everything is built for a 68030: small caches, slow memory, no FPU in most
Falcons, and a divide that costs 40 to 80 cycles.

- **Declarations are read when first needed.** Parsing a sheet reads the
  selectors and finds the end of each rule's block, but leaves the
  declarations alone until the rule first matches an element. On
  `NEWS.HTM` 240 of 947 blocks are ever read; on a page with Bootstrap,
  about one in twenty.
- **`@media` blocks that don't apply aren't read at all.** Media queries
  are evaluated as the sheet is parsed, for the Falcon's screen; the
  context keeps the widths they would all hold at as they did, and only a
  window sized beyond them has the sheets parsed again.
- **No callbacks while matching.** The document gives the engine its
  elements as `ec_node`s whose names, ids and classes are atoms. Each atom
  points straight at the rules whose subject has it, so finding an element's
  candidate rules is a few pointer reads, with no hashing and no searching.
- **An ancestor filter.** A counting Bloom filter of the ancestors' names,
  ids and classes, kept as the tree is walked, rejects most descendant
  selectors before any walking up the tree: about half of all candidates on
  the news page.
- **Style sharing and a style cache.** An element takes the style of a
  sibling before it without matching at all when the two have the same
  name, classes and the attributes selectors look at (a signature of
  them, made once per element), and neither matches a rule that looks at
  siblings or children (`+`, `~`, `:first-child`, `:empty`; these are kept
  in lists of their own, so checking costs little). Elements with the same
  rules under parents with the same style take a style already computed.
  On the news page 119 of 816 elements share and 432 come from the cache.
- **Each style once.** A style computed is looked for among those made
  before (a hash of all of it, a rotate and an add a word, then compared
  whole) and, when there, taken instead, the new one's memory given back
  (`uniq_style`). Elements whose rules differ often come out the same;
  their children then come from the cache too. ynet.co.il's front page:
  2,011 styles made to 989, its styles' memory 1.14 MB to 0.35 MB.
- **Rules for names the page hasn't** (`ec_prune`, a page whose scripts
  don't run): an author rule needing an id, a class or a tag that isn't
  the document's, in its subject or in an element it looks at above or
  beside it, is left out as the sheet is read.
- **Each property is written once.** Declarations are applied from the
  most important down, so nothing is written and then overwritten.
- **Integer fixed point** (22.10) throughout; absolute units become px as
  the sheet is parsed.
- **Arenas** for names, sheets and styles: no `malloc` per rule or per
  style; a document's styles go in one free, and its sheets in another
  (what a style attribute or a `var()` makes as the styles are computed
  goes with the styles). An arena's pieces are 64 KB; in the ST edition
  4 KB, its first ones smaller, from 512 bytes, each as large as those
  before it together, so a small page's names, sheets and styles take
  about what they need.

## What it supports

- **Syntax:** CSS Syntax 3, with its error recovery (bad URLs, unclosed
  blocks at the end, stray braces, invalid declarations).
- **Selectors 4:** type, universal, id, class and attribute selectors (all
  operators, `i` and `s`); the four combinators; `:not()`, `:is()`,
  `:where()`; `:root`, `:empty`, `:first-child` and the whole child-index
  family, the `of-type` ones; `:link`, `:any-link`; `:lang()`, `:dir()`;
  `:enabled`, `:disabled`, `:checked`, `:required`, `:optional`,
  `:read-only`, `:read-write`, `:placeholder-shown`; `:open` (since 22 Oct
  2026: a details or dialog with its `open` attribute). `:hover`, `:focus`
  and the other user-action pseudo-classes never match in a still page;
  their rules are kept apart for when EWeb is interactive. `::before` and
  `::after` get styles. Unknown pseudo-classes and pseudo-elements make a
  selector invalid, as browsers do; `:has()` is not supported.
- **The cascade:** origins, `!important`, specificity, order, the style
  attribute, `inherit`, `initial`, `unset`, `revert` (as `unset`), and
  cascade layers (`@layer`).
- **Values:** lengths in every unit (px, em, rem, ex, ch, vw, vh, vmin,
  vmax and the small, large and dynamic viewport ones, in, cm, mm, Q, pt,
  pc); percentages; `calc()`, `min()`, `max()`, `clamp()`; colours in
  every CSS Color 4 syntax for `rgb()` and `hsl()`, hex with alpha, the
  named and system colours, `currentcolor`; `url()`; gradients (since 22
  Oct 2026, CSS Images 3 and 4: `linear-`, `radial-` and `conic-gradient()`
  and their `repeating-` forms, kept whole, `ec_grad`: the direction (an
  angle in any unit, or `calc()`, `min()`, `max()`, `clamp()` of angles,
  worked out; or to a side or corner), the shape, size and centre, each
  colour stop and its positions, hints; lengths px plus a percentage,
  those in em, rem or vw computed with the style, `min()` and the like of
  one unit worked out; one whose stops can't be placed, as `min(10%,
  5px)`, kept as its colours and drawn as their average; an interpolation
  method read and drawn as sRGB; the Compatibility Standard's
  `-webkit-linear-gradient()` and `-webkit-gradient()` kept as the same;
  `DESIGN.md`, CSS gradients).
- **Custom properties and `var()`**, with fallbacks, inheritance and
  cycles.
- **At-rules:** `@media` (Media Queries 4, with ranges and three-valued
  logic), `@supports` (declarations, `not`, `and`, `or`, `selector()`),
  `@layer`, and since 23 Oct 2026 `@import` (CSS Cascade 4, 2: only before
  any rule but `@charset` and `@layer`'s statement; with media queries,
  `layer`, `layer()` and `supports()`) when the context has an importer.
  `@font-face`, `@keyframes`, `@page` and `@container` are skipped.
- **Properties:** the 103 EWeb draws or needs: box model, positioning, floats,
  borders, backgrounds (the first layer, the top one, in the `background`
  shorthand and, since 22 Oct 2026, in a `background-image` of several;
  since 24 Oct 2026 a last layer that covers the box with opaque colours,
  a dark page's black under its glows, is its colours' average as the
  `background-color`, which it hides; `background-size` since 21 Oct 2026,
  kept as two longhands of the engine's own, `-ec-background-size-w` and
  `-h`), fonts, text (including `direction`,
  `unicode-bidi`, `text-align: start | end`, and since 22 Oct 2026 HTML's
  `-webkit-left | -right | -center`, with Gecko's `-moz-` names, and
  `vertical-align: -webkit-baseline-middle`: `DESIGN.md`, Legacy
  centring), lists (since 22 Oct 2026 with CSS Counter Styles 3's
  `disclosure-open` and `disclosure-closed`, a details' summary's
  triangles: `DESIGN.md`, Details and summary), tables, flexbox,
  grid (since 21 Oct 2026: `grid-template-columns` and `-rows` as track
  lists with `fr`, `minmax()`, `fit-content()` and `repeat()`, `auto-fill`
  and `auto-fit` too; `grid-template-areas`; `grid-auto-columns`, `-rows`
  and `-flow`; the line longhands with numbers, `span` and areas' names;
  `justify-items` and `justify-self`; the shorthands `grid-row`,
  `grid-column`, `grid-area` and `grid-template` (rows / columns); line
  names are read past), `gap`, `overflow`, `opacity`, `content`, and their
  shorthands. Since 22 Oct 2026, for translucency (`DESIGN.md`):
  `pointer-events`; `background-clip` (with `-webkit-background-clip` and
  the `background` shorthand's box); and of what EWeb doesn't draw, what
  it needs to show text of a transparent colour or content an animation
  reveals: `text-shadow` (the first shadow's colour), `-webkit-text-stroke`
  (`-width`, `-color` and the shorthand), `animation-name` and the
  `animation` shorthand (whether there is an animation). Unknown properties
  are skipped. Not yet:
  logical properties (`margin-inline-start` and so on, which matter for
  Hebrew), transforms, running animations, `border-radius`, drawing
  shadows.
- **Computed values** as CSS defines them: relative lengths become px
  (percentages stay), `bolder` and `lighter` become weights, display is
  blockified for floats, absolute positioning, the root and flex items,
  borders without a style have no width.

## How it is tested

- `make test` runs `tests/css/ectest` on `tests/css/cases.txt`: cases
  worked out from the specifications (cascade, selectors, shorthands,
  values, `var()`, `@media`, `@supports`, `@layer`, error recovery,
  sharing), with AddressSanitizer and UBSan, and `csscmp` (below) on the
  test pages.
- `make csscmp` compares the engine with LibCSS, value by value, on the
  whole page set (`tests/bench/pages.sh` first): every element, every
  property both engines have (LibCSS has none of the translucency ones
  above, nor `overflow-wrap`, `row-gap`, grid's or `background-size`). A difference counts as explained only when
  it is a known gap in LibCSS. Most are found exactly: the page is styled
  again with one of the engine's `EC_COMPAT_*` switches on (each leaves out
  one thing LibCSS lacks), and the difference is put down to that thing
  only if the values then agree. See `DESIGN.md` for the results.

## Using it

```c
ec_media m = { 640, 480, 16 };          /* viewport, default font size */
ec_ctx *c = ec_create(&m, 0);
ec_sheet(c, ua_css, ua_len, EC_ORIGIN_UA);
ec_sheet(c, page_css, page_len, EC_ORIGIN_AUTHOR);
/* the document: ec_nodes with atoms from ec_intern(c, ...) */
ec_select(c, root);                     /* every node's style is set (-1: out of memory, not all) */
/* node->style->v[EP_COLOR].i, node->style->k[EP_WIDTH], ... */
ec_destroy(c);
```

A stylesheet every document shares (EWeb's UA sheet) can be parsed once
and kept: `ec_share(ua)` finishes parsing a context's sheets and marks its
names shared, and `ec_create_on(&m, 0, ua)` makes a document's context on
it. The base is only read from then on (a document's rules on its names go
into the document's own table), so it may be used by every document of a
program run and is destroyed last.

A sheet that came from an address of its own (a `<link>`'s) is added with
`ec_sheet_at(c, css, len, origin, its_address)` once the context has a
resolver (`ec_set_resolver`): its `url()`s are made absolute against that
address when its declarations are read, as CSS Values 4 says, not against
the page's. An `@import` asks the importer given by `ec_set_importer`
for its sheet (by the address as written and the importing sheet's, NULL
for a `<style>`'s): its rules are added where the `@import` is, against
its own address, for the media it names, in a layer if it says so; at
most `EC_MAX_IMPORTS` between `ec_drop_sheets` and `EC_IMPORT_DEPTH`
deep, so a loop of sheets ends. Without an importer (nsbench, csscmp) an
`@import` is passed over, as before. `ec_media_ok(c, s, n)` tells whether a media query list
holds for c's screen, as an `@media`'s would (a `<link>`'s or a
`<style>`'s `media`, a `<source media>`), and `ec_sizes(c, s, n)` gives the
width an `<img sizes>` names (its first entry whose condition holds, with
`calc()`, `min()`, `max()` and `clamp()` of lengths), or -1.

Media queries are evaluated again when the screen changes (Media Queries
4, 2). `ec_media_span(c, &lo, &hi)` gives the viewport widths at which
every media query c has evaluated (`@media`, `ec_media_ok`) would go the
way it went: each comparison with the width narrows them (`min-width:
600px` on: 600 and up), and an aspect ratio or a `vw` in a query keeps
only the width it was at. `ec_set_media(c, &m)` gives c another screen
for what is evaluated after (`ec_media_ok`, `ec_sizes`, viewport units in
styles computed). At a width outside the span, `ec_drop_sheets(c)` takes
out every sheet c was given (a base's rules stay; the atoms stay, so the
document's nodes keep theirs, and so do a script's selectors) and its
styles, for the sheets to be added again for the new screen. Their memory
is given back with them (their text, rules and declarations are an arena
of their own), so added again they take what they took, however often.
`ec_sheets_short(c)` says whether a sheet added since didn't all fit in
memory, and `ec_mem(c)` how many bytes c takes. EWeb's browser does so as
the window is sized, and when a script changes the page's sheets
(DESIGN.md, Stylesheets by their media, type and set).

`tests/bench/ecstyle.c` builds `ec_node`s from nsbench's document tree;
in EWeb the document's own nodes will be them.
