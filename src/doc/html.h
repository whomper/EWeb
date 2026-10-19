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

#include "doc.h"

typedef struct html_opts {
	const char *charset;	/* the HTTP answer's (certain), or NULL: sniff */
	int scripting;		/* 0: <noscript> is parsed as markup (EWeb) */
	/* the fragment case (13.4): the context element's name and
	   namespace (DN_NS_*), or NULL for a whole document */
	const char *context;
	int context_ns;
} html_opts;

/* Parses a page into d (d->root, d->charset, d->quirks, d->elements).
   In the fragment case d->root is a document node holding the
   fragment's nodes. Returns 0 if out of memory. */
int html_parse(doc *d, const char *data, size_t len, const html_opts *o);

/* an attribute of an SVG or MathML element (ns) as the HTML Standard
   names it: the document keeps attribute names in lower case, the
   Standard adjusts some to camel case (viewBox) and puts some in a
   namespace (xlink:href). Returns the local name; *prefix is the
   namespace's ("xlink", "xml", "xmlns") or NULL. For the test runner. */
const char *html_foreign_attr(int ns, const char *name, const char **prefix);

#endif
