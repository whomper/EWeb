/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Pages from the fuzz input (src/doc/html.c, EWeb's HTML parser): the
 * first byte picks the charset the answer gave (none, so the page is
 * sniffed, or one of a few), scripting on or off and a fragment's context
 * element; the rest is the page. The tree that comes out must hold
 * together: each node's parent, siblings and first and last children
 * agree.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "html.h"

static long check(const dnode *n, int depth)
{
	const dnode *c, *prev = NULL;
	long k = 1;
	if (depth > 100000)
		abort();
	for (c = n->first; c != NULL; c = c->next) {
		if (c->parent != n || c->prev != prev)
			abort();
		if (c->type == DN_TEXT && (c->text == NULL || c->text[c->len] != 0))
			abort();
		k += check(c, depth + 1);
		prev = c;
	}
	if (n->last != prev)
		abort();
	return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char *const charsets[8] = { NULL, NULL, NULL, "utf-8", "windows-1252", "utf-16le",
						 "iso-8859-8", "windows-1255" };
	static const struct { const char *name; int ns; } contexts[8] = {
		{ NULL, 0 }, { NULL, 0 }, { NULL, 0 }, { "body", DN_NS_HTML }, { "td", DN_NS_HTML },
		{ "template", DN_NS_HTML }, { "svg", DN_NS_SVG }, { "textarea", DN_NS_HTML } };
	ec_media m = { 640, 400, 16 };
	html_opts o;
	doc *doc;

	if (n < 1)
		return 0;
	memset(&o, 0, sizeof o);
	o.charset = charsets[d[0] & 7];
	o.scripting = (d[0] >> 3) & 1;
	o.context = contexts[(d[0] >> 4) & 7].name;
	o.context_ns = contexts[(d[0] >> 4) & 7].ns;
	doc = doc_parse_opts((const char *) d + 1, n - 1, &o, &m);
	if (doc != NULL)
		check(doc->root, 0);
	doc_free(doc);
	return 0;
}
