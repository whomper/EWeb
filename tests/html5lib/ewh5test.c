/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ewh5test - EWeb's HTML parser (src/doc/html.c) against the HTML
 * tree-construction tests (html5lib's .dat format, kept by the Web
 * Platform Tests; tests/html5lib/fetch.sh):
 *
 *   ewh5test [-v] FILE.dat...
 *   ewh5test -f PAGE.HTM      the tree of a page, read as EWeb reads it
 *   ewh5test -F PAGE.HTM      the same without the document's own comments
 *                             (html5lib's serialiser leaves them out)
 *
 * Each test's document (or fragment, with its context element) is parsed
 * as EWeb parses a page and written out in the tests' format; it passes
 * when that is the expected tree. -v shows each failure. Prints each
 * file's count, then the total.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "html.h"

typedef struct {
	char *s;
	size_t n, cap;
} BUF;

static void put(BUF *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2;
		b->s = realloc(b->s, b->cap);
		if (b->s == NULL)
			exit(2);
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void puts_(BUF *b, const char *s)
{
	put(b, s, strlen(s));
}

static void indent(BUF *b, int depth)
{
	int i;
	puts_(b, "| ");
	for (i = 0; i < depth; i++)
		puts_(b, "  ");
}

typedef struct {
	char name[4096];
	const char *val;
	size_t len;
} ATTR;

static int by_name(const void *a, const void *b)
{
	return strcmp(((const ATTR *) a)->name, ((const ATTR *) b)->name);
}

static void node(BUF *b, const dnode *n, int depth)
{
	const dnode *c;
	size_t len;
	const char *s;

	indent(b, depth);
	switch (n->type) {
	case DN_DOCTYPE: {
		const char *name = n->text, *pub = name + strlen(name) + 1, *sys = pub + strlen(pub) + 1;
		puts_(b, "<!DOCTYPE ");
		puts_(b, name);
		if (*pub || *sys) {
			puts_(b, " \"");
			puts_(b, pub);
			puts_(b, "\" \"");
			puts_(b, sys);
			puts_(b, "\"");
		}
		puts_(b, ">\n");
		break;
	}
	case DN_OTHER:
		puts_(b, "<!-- ");
		put(b, n->text, n->len);
		puts_(b, " -->\n");
		break;
	case DN_PI:
		puts_(b, "<?");
		puts_(b, n->text);
		puts_(b, " ");
		puts_(b, n->text + strlen(n->text) + 1);
		puts_(b, "?>\n");
		break;
	case DN_TEXT:
		puts_(b, "\"");
		put(b, n->text, n->len);
		puts_(b, "\"\n");
		break;
	case DN_ELEMENT: {
		ATTR *a = n->nattr ? calloc(n->nattr, sizeof *a) : NULL;
		uint16_t i;
		puts_(b, "<");
		if (n->ns == DN_NS_SVG)
			puts_(b, "svg ");
		else if (n->ns == DN_NS_MATHML)
			puts_(b, "math ");
		s = ec_atom_text(n->e.name, &len);
		put(b, s, len);
		puts_(b, ">\n");
		for (i = 0; i < n->nattr; i++) {
			const char *an = ec_atom_text(n->attrs[i].name, &len), *prefix = NULL;
			char lower[4000];
			snprintf(lower, sizeof lower, "%.*s", (int) len, an);
			if (n->ns != DN_NS_HTML)
				an = html_foreign_attr(n->ns, lower, &prefix);
			else
				an = lower;
			snprintf(a[i].name, sizeof a[i].name, "%s%s%s", prefix ? prefix : "", prefix ? " " : "", an);
			a[i].val = n->attrs[i].val;
			a[i].len = n->attrs[i].len;
		}
		if (n->nattr)
			qsort(a, n->nattr, sizeof *a, by_name);
		for (i = 0; i < n->nattr; i++) {
			indent(b, depth + 1);
			puts_(b, a[i].name);
			puts_(b, "=\"");
			put(b, a[i].val, a[i].len);
			puts_(b, "\"\n");
		}
		free(a);
		if (n->ns == DN_NS_HTML && n->tag == T_TEMPLATE) {
			indent(b, depth + 1);
			puts_(b, "content\n");
			for (c = n->first; c != NULL; c = c->next)
				node(b, c, depth + 2);
			return;
		}
		break;
	}
	}
	for (c = n->first; c != NULL; c = c->next)
		node(b, c, depth + 1);
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t) n + 1);
	if (b != NULL && fread(b, 1, (size_t) n, f) != (size_t) n) {
		free(b);
		b = NULL;
	}
	if (b != NULL)
		b[n] = 0;
	fclose(f);
	return b;
}

static int is_section(const char *s)
{
	static const char *const names[] = { "#data\n", "#errors\n", "#new-errors\n",
		"#document\n", "#document-fragment\n", "#script-off\n", "#script-on\n",
		"#description\n" };
	size_t i;
	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		if (strncmp(s, names[i], strlen(names[i])) == 0)
			return 1;
	return 0;
}

/* the section at *p (a line "#name"): its name, its text (the newline
   before the next section cut), and *p moves on */
static char *section(char **p, char *name, size_t size)
{
	char *s = *p, *e, *text;

	if (*s != '#')
		return NULL;
	e = strchr(s, '\n');
	if (e == NULL)
		return NULL;
	snprintf(name, size, "%.*s", (int) (e - s - 1), s + 1);
	text = e + 1;
	if (is_section(text)) {
		*e = 0;
		*p = text;
		return e;
	}
	for (e = text;; e++) {
		e = strstr(e, "\n#");
		if (e == NULL) {
			e = text + strlen(text);
			break;
		}
		if (is_section(e + 1))
			break;
	}
	*p = *e ? e + 1 : e;
	*e = 0;
	return text;
}

int main(int argc, char **argv)
{
	int verbose = 0, a;
	unsigned long all_pass = 0, all_fail = 0, frag_pass = 0, frag_fail = 0;
	ec_media m = { 640, 400, 16 };

	if (argc == 3 && (strcmp(argv[1], "-f") == 0 || strcmp(argv[1], "-F") == 0)) {
		FILE *f = fopen(argv[2], "rb");
		static char page[1 << 23];
		size_t n = f ? fread(page, 1, sizeof page, f) : 0;
		doc *d = doc_parse(page, n, NULL, &m);
		BUF got = { NULL, 0, 0 };
		const dnode *c;
		if (d == NULL)
			return 1;
		for (c = d->root->first; c != NULL; c = c->next)
			if (argv[1][1] == 'f' || c->type == DN_ELEMENT)
				node(&got, c, 0);
		fputs(got.s ? got.s : "", stdout);
		free(got.s);
		doc_free(d);
		fclose(f);
		return 0;
	}
	for (a = 1; a < argc; a++) {
		char *file, *p;
		unsigned long pass = 0, fail = 0;

		if (strcmp(argv[a], "-v") == 0) {
			verbose = 1;
			continue;
		}
		file = slurp(argv[a]);
		if (file == NULL) {
			printf("%s: can't read\n", argv[a]);
			return 1;
		}
		p = strstr(file, "#data\n");
		while (p != NULL && *p) {
			char name[40], *data = NULL, *expected = NULL, *text, *context = NULL;
			int scripting = 1;
			html_opts o;
			doc *d;
			BUF got = { NULL, 0, 0 };

			do {
				text = section(&p, name, sizeof name);
				if (text == NULL)
					break;
				if (strcmp(name, "data") == 0)
					data = text;
				else if (strcmp(name, "document-fragment") == 0)
					context = text;
				else if (strcmp(name, "script-off") == 0)
					scripting = 0;
				else if (strcmp(name, "document") == 0)
					expected = text;
				while (*p == '\n')
					p++;
			} while (*p == '#' && strncmp(p, "#data\n", 6) != 0);
			if (data == NULL)
				break;
			if (expected == NULL)
				continue;
			{
				size_t n = strlen(expected);
				while (n > 0 && expected[n - 1] == '\n')
					expected[--n] = 0;
			}
			memset(&o, 0, sizeof o);
			o.scripting = scripting;
			o.charset = "utf-8";
			if (context != NULL) {
				char *nl = strchr(context, '\n');
				if (nl)
					*nl = 0;
				o.context_ns = DN_NS_HTML;
				if (strncmp(context, "svg ", 4) == 0) {
					o.context_ns = DN_NS_SVG;
					context += 4;
				} else if (strncmp(context, "math ", 5) == 0) {
					o.context_ns = DN_NS_MATHML;
					context += 5;
				}
				o.context = context;
			}
			d = doc_parse_opts(data, strlen(data), &o, &m);
			if (d != NULL) {
				const dnode *c;
				for (c = d->root->first; c != NULL; c = c->next)
					node(&got, c, 0);
				if (got.n > 0 && got.s[got.n - 1] == '\n')
					got.s[--got.n] = 0;
			}
			if (d != NULL && got.s != NULL && strcmp(got.s, expected) == 0) {
				pass++;
				frag_pass += context != NULL;
			} else {
				fail++;
				frag_fail += context != NULL;
				if (verbose)
					printf("FAIL\n#data\n%s\n%s%s%s#expected\n%s\n#got\n%s\n\n", data,
					       context ? "#context\n" : "", context ? context : "",
					       context ? "\n" : "", expected, got.s ? got.s : "(nothing)");
			}
			doc_free(d);
			free(got.s);
		}
		printf("%-48s %5lu passed %5lu failed\n", argv[a], pass, fail);
		all_pass += pass;
		all_fail += fail;
		free(file);
	}
	printf("all: %lu passed, %lu failed; %.1f%% (documents %lu of %lu, fragments %lu of %lu)\n",
	       all_pass, all_fail, all_pass + all_fail ? 100.0 * all_pass / (all_pass + all_fail) : 0.0,
	       all_pass - frag_pass, all_pass + all_fail - frag_pass - frag_fail, frag_pass, frag_pass + frag_fail);
	return 0;
}
