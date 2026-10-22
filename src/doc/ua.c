/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* EWeb's UA stylesheet: the HTML Standard's rendering section, cut to
 * what EWeb draws. <center> and caption centre their blocks as well as
 * their lines (15.3.3, 15.3.8: -webkit-center), and th is centred only
 * where its row's text-align is still the initial (-ec-center-or-inherit).
 * A details' summary is a list item with a disclosure triangle (15.5.4;
 * its counter-increment left out: EWeb's layout doesn't number it); what
 * a closed details hides, and the summary one without has, are the
 * layout's. */
#include "doc.h"

const char doc_ua_css[] =
	"html, address, blockquote, body, center, dialog, div, figure, figcaption,"
	" footer, form, header, hr, legend, listing, main, p, plaintext, pre,"
	" search, xmp, article, aside, h1, h2, h3, h4, h5, h6, hgroup, nav,"
	" section, dir, dd, dl, dt, menu, ol, ul, details, summary,"
	" fieldset, optgroup, frameset, frame { display: block }\n"
	"area, base, basefont, datalist, head, link, meta, noembed, noframes,"
	" param, rp, script, style, template, title, [hidden], dialog:not([open]) { display: none }\n"
	"li { display: list-item }\n"
	"details > summary:first-of-type { display: list-item; list-style: disclosure-closed inside }\n"
	"details[open] > summary:first-of-type { list-style-type: disclosure-open }\n"
	"table { display: table; border-spacing: 2px; border-collapse: separate }\n"
	"caption { display: table-caption; text-align: -webkit-center }\n"
	"colgroup { display: table-column-group } col { display: table-column }\n"
	"thead { display: table-header-group; vertical-align: middle }\n"
	"tbody { display: table-row-group; vertical-align: middle }\n"
	"tfoot { display: table-footer-group; vertical-align: middle }\n"
	"tr { display: table-row; vertical-align: inherit }\n"
	"td, th { display: table-cell; vertical-align: inherit; padding: 1px }\n"
	"th { font-weight: bold; text-align: -ec-center-or-inherit }\n"
	"input, select, button, textarea { display: inline-block }\n"
	"input, select, textarea, button { font-size: 13.33px }\n"
	"body { margin: 8px }\n"
	"p, blockquote, figure, listing, plaintext, pre, xmp, dl { margin-top: 1em; margin-bottom: 1em }\n"
	"blockquote, figure { margin-left: 40px; margin-right: 40px }\n"
	"address, cite, dfn, em, i, var { font-style: italic }\n"
	"b, strong { font-weight: bold }\n"
	"code, kbd, samp, tt, pre, listing, plaintext, xmp { font-family: monospace }\n"
	"pre, listing, plaintext, xmp { white-space: pre }\n"
	"textarea { white-space: pre-wrap }\n"
	"h1 { font-size: 2em; margin: .67em 0 } h2 { font-size: 1.5em; margin: .83em 0 }\n"
	"h3 { font-size: 1.17em; margin: 1em 0 } h4 { margin: 1.33em 0 }\n"
	"h5 { font-size: .83em; margin: 1.67em 0 } h6 { font-size: .67em; margin: 2.33em 0 }\n"
	"h1, h2, h3, h4, h5, h6 { font-weight: bold }\n"
	"ul, ol, menu, dir { padding-left: 40px; margin: 1em 0 }\n"
	"ul, menu, dir { list-style-type: disc } ol { list-style-type: decimal }\n"
	"ul ul, ol ul, menu ul, ul menu { list-style-type: circle }\n"
	"ul ul ul, ul ol ul, ol ul ul, ol ol ul { list-style-type: square }\n"
	"li ul, li ol, li menu, ul ul, ol ol, ul ol, ol ul { margin-top: 0; margin-bottom: 0 }\n"
	"dd { margin-left: 40px }\n"
	"dt { font-weight: bold }\n"
	"hr { border: 1px inset; margin: .5em auto; color: gray; height: 0 }\n"
	"fieldset { margin: 0 2px; padding: .35em .75em .625em; border: 2px groove ThreeDFace }\n"
	"a:link, a:any-link { color: #0000ee; text-decoration: underline }\n"
	"a:visited { color: #551a8b }\n"
	"u, ins { text-decoration: underline } s, strike, del { text-decoration: line-through }\n"
	"abbr[title], acronym[title] { text-decoration: dotted underline }\n"
	"sub { vertical-align: sub; font-size: smaller } sup { vertical-align: super; font-size: smaller }\n"
	"small { font-size: smaller } big { font-size: larger }\n"
	"mark { background-color: yellow; color: black }\n"
	"center { text-align: -webkit-center }\n"
	"img, svg, video, canvas, iframe, object, embed { display: inline-block }\n"
	"[dir=rtl] { direction: rtl } [dir=ltr] { direction: ltr }\n"
	"bdi, bdo { unicode-bidi: isolate } bdo[dir] { unicode-bidi: isolate-override }\n"
	"q:before { content: open-quote } q:after { content: close-quote }\n";

/* and in quirks mode (15.3.8): a table starts its text-align afresh, so
   the text in a table that <center> centres starts at the left. The
   rule's font, line-height and white-space parts are left out: EWeb
   keeps them inherited (DESIGN.md, Legacy centring). */
const char doc_quirks_css[] = "table { text-align: start }";
