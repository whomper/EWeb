/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* hebtest: parses a page with Hubbub (nsbench's tree, tests/bench/dom.c)
 * and writes to OUT, in UTF-8, every element's dir attribute and every
 * non-blank text node: the same page in different encodings must give the
 * same. (A file, not stdout: Hubbub's debug build talks on stdout.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dom.h"

static void walk(FILE *out, const node *n)
{
	const node *c;

	if (n->type == N_ELEMENT) {
		uint32_t i;
		for (i = 0; i < n->nattr; i++)
			if (lwc_string_length(n->attr[i].name) == 3 &&
					memcmp(lwc_string_data(n->attr[i].name), "dir", 3) == 0)
				fprintf(out, "<%s dir=%s>\n", lwc_string_data(n->name),
					lwc_string_data(n->attr[i].value));
	} else if (n->type == N_TEXT) {
		uint32_t i;
		for (i = 0; i < n->len; i++)
			if (n->text[i] != ' ' && n->text[i] != '\n' && n->text[i] != '\r') {
				fprintf(out, "text: %.*s\n", (int) n->len, n->text);
				break;
			}
	}
	for (c = n->first; c != NULL; c = c->next)
		walk(out, c);
}

int main(int argc, char **argv)
{
	static char buf[65536];
	FILE *f, *out;
	size_t len;
	node *doc;

	if (argc != 3 || (f = fopen(argv[1], "rb")) == NULL) {
		fprintf(stderr, "usage: hebtest PAGE.HTM OUT\n");
		return 2;
	}
	len = fread(buf, 1, sizeof buf, f);
	fclose(f);
	doc = dom_parse((const uint8_t *) buf, len, 4096);
	if (doc == NULL) {
		fprintf(stderr, "hebtest: Hubbub could not parse %s\n", argv[1]);
		return 1;
	}
	out = fopen(argv[2], "w");
	if (out == NULL) {
		fprintf(stderr, "hebtest: can't write %s\n", argv[2]);
		return 2;
	}
	walk(out, doc);
	fclose(out);
	dom_free(doc);
	return 0;
}
