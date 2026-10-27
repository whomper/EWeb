/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * html.h - EWeb's HTML parser (html.c): the HTML Standard's encoding
 * sniffing, tokenizer and tree construction (13.2), writing EWeb's
 * document nodes (doc.h) straight from the page, out of the document's
 * memory (doc_alloc).
 */
#ifndef EW_HTML_H
#define EW_HTML_H

#include <stddef.h>
#include <stdint.h>

#include "doc.h"

typedef struct html_opts {
	/* the encoding (certain, after a BOM): the HTTP answer's, or the one
	   the user chose (View > Encoding); NULL: sniff */
	const char *charset;
	/* when nothing names one and the bytes aren't UTF-8: the locale's
	   default (the sniffing's last step); NULL: windows-1252 */
	const char *fallback;
	/* a text file (text/plain): its bytes are text, in a <pre> (no
	   prescan for a <meta>) */
	int plaintext;
	int scripting;		/* 0: <noscript> is parsed as markup (EWeb) */
	/* the text of scripts left out, the elements kept (a browser whose
	   scripts won't run on the page: cnn.com's 2.6 MB of it); not the
	   HTML Standard's tree, which html5lib's tests check */
	int drop_scripts;
	/* the fragment case (13.4): the context element's name and
	   namespace (DN_NS_*), or NULL for a whole document */
	const char *context;
	int context_ns;
} html_opts;

/* Parses a page into d (d->root, d->charset, d->quirks, d->elements).
   In the fragment case d->root is a document node holding the
   fragment's nodes. Returns 0 if out of memory. */
int html_parse(doc *d, const char *data, size_t len, const html_opts *o);

/* the Encoding Standard's "get an encoding" for a label, among those
   EWeb decodes: the encoding's name ("windows-1255", "UTF-8", as the
   standard names it), or NULL for none */
const char *html_encoding(const char *label);
/* For a page's links and forms (their text goes in the page's
   encoding): 1 when name is an encoding of a byte a character, ASCII
   below 0x80 (windows-1255, ISO-8859-8...), with hi[b] the character
   the byte 0x80 + b stands for (0xFFFD: none); 0 for UTF-8, UTF-16 and
   any other, whose text goes as UTF-8 */
int html_enc_table(const char *name, uint16_t hi[128]);

/* an attribute of an SVG or MathML element (ns) as the HTML Standard
   names it: the document keeps attribute names in lower case, the
   Standard adjusts some to camel case (viewBox) and puts some in a
   namespace (xlink:href). Returns the local name; *prefix is the
   namespace's ("xlink", "xml", "xmlns") or NULL. For the test runner. */
const char *html_foreign_attr(int ns, const char *name, const char **prefix);

/* For scripts (src/js): a fragment parsed into d's memory and atoms, as
   innerHTML parses it (UTF-8, in the context of an element named context
   in the namespace context_ns): a document node holding its nodes, not
   in d's tree; NULL if out of memory. d->root and the rest stay. */
dnode *html_fragment(doc *d, const char *context, int context_ns, const char *data, size_t len, int scripting);
/* a new HTML element named name (no attributes) in d's memory, as
   document.createElement makes it; NULL if out of memory */
dnode *html_element(doc *d, const char *name, size_t len);

#endif
