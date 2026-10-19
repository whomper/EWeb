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
  are evaluated as the sheet is parsed, for the Falcon's screen.
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
- **Each property is written once.** Declarations are applied from the
  most important down, so nothing is written and then overwritten.
- **Integer fixed point** (22.10) throughout; absolute units become px as
  the sheet is parsed.
- **Arenas** for sheets and styles: no `malloc` per rule or per style, and
  a document's styles go in one free.

## What it supports

- **Syntax:** CSS Syntax 3, with its error recovery (bad URLs, unclosed
  blocks at the end, stray braces, invalid declarations).
- **Selectors 4:** type, universal, id, class and attribute selectors (all
  operators, `i` and `s`); the four combinators; `:not()`, `:is()`,
  `:where()`; `:root`, `:empty`, `:first-child` and the whole child-index
  family, the `of-type` ones; `:link`, `:any-link`; `:lang()`, `:dir()`;
  `:enabled`, `:disabled`, `:checked`, `:required`, `:optional`,
  `:read-only`, `:read-write`, `:placeholder-shown`. `:hover`, `:focus`
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
  named and system colours, `currentcolor`; `url()`; gradients (kept as
  "an image without a URL").
- **Custom properties and `var()`**, with fallbacks, inheritance and
  cycles.
- **At-rules:** `@media` (Media Queries 4, with ranges and three-valued
  logic), `@supports` (declarations, `not`, `and`, `or`, `selector()`),
  `@layer`. `@import`, `@font-face`, `@keyframes`, `@page` and
  `@container` are skipped.
- **Properties:** the 83 EWeb draws: box model, positioning, floats,
  borders, backgrounds (first layer), fonts, text (including `direction`,
  `unicode-bidi`, `text-align: start | end`), lists, tables, flexbox,
  `gap`, `overflow`, `opacity`, `content`, and their shorthands. Unknown
  properties are skipped. Not yet: grid's own properties, logical
  properties (`margin-inline-start` and so on, which matter for Hebrew),
  transforms, animations, `border-radius`, shadows.
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
  property both engines have. A difference counts as explained only when
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
ec_select(c, root);                     /* every node's style is set */
/* node->style->v[EP_COLOR].i, node->style->k[EP_WIDTH], ... */
ec_destroy(c);
```

A stylesheet every document shares (EWeb's UA sheet) can be parsed once
and kept: `ec_share(ua)` finishes parsing a context's sheets and marks its
names shared, and `ec_create_on(&m, 0, ua)` makes a document's context on
it. The base is only read from then on (a document's rules on its names go
into the document's own table), so it may be used by every document of a
program run and is destroyed last.

`tests/bench/ecstyle.c` builds `ec_node`s from nsbench's document tree;
in EWeb the document's own nodes will be them.
