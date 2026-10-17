/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* ectest: EWeb's CSS engine against cases written from the specifications.
 *
 *   ectest tests/css/cases.txt
 *
 * Each case has a stylesheet, a page and the computed values expected:
 *
 *   === what it tests
 *   --- css
 *   p { margin: 1em 2px }
 *   --- html
 *   <p id=a>x</p>
 *   --- expect
 *   #a margin-top 16px
 *   #a::before content set
 *
 * Values are written as ectest prints them: lengths in px (two decimals
 * at most), percentages, numbers, integers, keywords, colours as #rrggbb
 * (#rrggbbaa if not opaque), currentcolor, calc(Apx + B%), min(...),
 * url(...), gradient, and font families as names|generic. The UA sheet is
 * nsbench's, the viewport 640x480, the default font 16px. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dom.h"
#include "style.h"
#include "ecstyle.h"
#include "ecss.h"
#include "plat.h"

static void fx_str(char *buf, size_t cap, ec_fx v, const char *unit)
{
	double d = v / 1024.0;
	char *p;

	snprintf(buf, cap, "%.2f", d);
	p = buf + strlen(buf) - 1;
	while (*p == '0')
		*p-- = 0;
	if (*p == '.')
		*p = 0;
	if (!strcmp(buf, "-0"))
		strcpy(buf, "0");
	strncat(buf, unit, cap - strlen(buf) - 1);
}

static void value(const ec_style *s, int p, char *buf, size_t cap)
{
	ec_v v = s->v[p];
	char a[64], b[64];
	int i;

	switch (s->k[p]) {
	case EK_KW: snprintf(buf, cap, "%s", ec_kw_name(v.i)); return;
	case EK_PX: fx_str(buf, cap, v.i, "px"); return;
	case EK_PCT: fx_str(buf, cap, v.i, "%"); return;
	case EK_NUM: fx_str(buf, cap, v.i, ""); return;
	case EK_INT: snprintf(buf, cap, "%ld", (long) v.i); return;
	case EK_CURCOLOR: snprintf(buf, cap, "currentcolor"); return;
	case EK_IMAGE: snprintf(buf, cap, "gradient"); return;
	case EK_COLOR: {
		uint32_t c = (uint32_t) v.i;
		if ((c >> 24) == 0xff)
			snprintf(buf, cap, "#%06lx", (unsigned long) (c & 0xffffff));
		else
			snprintf(buf, cap, "#%06lx%02lx", (unsigned long) (c & 0xffffff),
				(unsigned long) (c >> 24));
		return;
	}
	case EK_CALC: {
		const ec_calc *c = v.p;
		static const char *const ops[] = { "calc", "min", "max", "clamp" };
		size_t n = snprintf(buf, cap, "%s(", ops[c->op]);
		for (i = 0; i < c->n && n < cap; i++) {
			fx_str(a, sizeof a, c->a[i].px, "px");
			fx_str(b, sizeof b, c->a[i].pct, "%");
			n += snprintf(buf + n, cap - n, "%s%s + %s", i ? ", " : "", a, b);
		}
		if (n < cap)
			snprintf(buf + n, cap - n, ")");
		return;
	}
	case EK_PTR:
		if (p == EP_FONT_FAMILY) {
			const ec_family *f = v.p;
			size_t n = 0;
			buf[0] = 0;
			for (i = 0; i < f->n && n < cap; i++)
				n += snprintf(buf + n, cap - n, "%s%s", i ? "," : "", f->name[i]->s);
			if (n < cap)
				snprintf(buf + n, cap - n, "|%s", f->generic ? ec_kw_name(f->generic) : "");
		} else if (p == EP_CONTENT) {
			snprintf(buf, cap, "set");
		} else {
			snprintf(buf, cap, "url(%s)", ((const ec_str *) v.p)->s);
		}
		return;
	default:
		snprintf(buf, cap, "(kind %d)", s->k[p]);
	}
}

static int prop_by_name(const char *name)
{
	int p;

	for (p = 0; p < EC_NPROP; p++)
		if (!strcmp(ec_prop_name(p), name))
			return p;
	return -1;
}

static const node *by_id(const node *n, const char *id)
{
	const node *c, *r;

	if (n->type == N_ELEMENT && n->id != NULL && strlen(id) == lwc_string_length(n->id) &&
			!memcmp(lwc_string_data(n->id), id, strlen(id)))
		return n;
	for (c = n->first; c != NULL; c = c->next)
		if ((r = by_id(c, id)) != NULL)
			return r;
	return NULL;
}

static int run_case(const char *name, const char *css, size_t csslen, const char *html,
		size_t htmllen, char *expect, unsigned opts)
{
	node *doc = dom_parse((const uint8_t *) html, htmllen, 4096);
	style_stats ss;
	struct ec_ctx *c;
	int fails = 0;
	char *line, *save = NULL;

	if (doc == NULL) {
		fprintf(stderr, "FAIL %s: the page didn't parse\n", name);
		return 1;
	}
	memset(&ss, 0, sizeof ss);
	ecs_opts = opts;
	c = ecs_sheets(doc, &css, &csslen, 1, &ss);
	ecs_select(c, doc, &ss);
	for (line = strtok_r(expect, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
		char sel[128], prop[64], want[256], got[256], id[128];
		const node *n;
		const ec_style *st;
		const char *pseudo;
		int p;
		if (line[0] == 0 || (line[0] == '#' && line[1] == ' '))
			continue;
		if (sscanf(line, "%127s %63s %255[^\n]", sel, prop, want) != 3) {
			fprintf(stderr, "FAIL %s: can't read \"%s\"\n", name, line);
			fails++;
			continue;
		}
		pseudo = strstr(sel, "::");
		snprintf(id, sizeof id, "%.*s", pseudo ? (int) (pseudo - sel - 1) : (int) strlen(sel) - 1,
			sel + 1);
		n = by_id(doc, id);
		p = prop_by_name(prop);
		if (n == NULL || n->ecn == NULL || (p < 0 && strcmp(prop, "exists"))) {
			fprintf(stderr, "FAIL %s: no element #%s or property %s\n", name, id, prop);
			fails++;
			continue;
		}
		st = ((const ec_node *) n->ecn)->style;
		if (pseudo != NULL)
			st = !strcmp(pseudo, "::before") ? st->before : st->after;
		if (!strcmp(prop, "exists")) {
			snprintf(got, sizeof got, "%s", st != NULL ? "yes" : "no");
		} else if (st == NULL) {
			snprintf(got, sizeof got, "(no style)");
		} else {
			value(st, p, got, sizeof got);
		}
		if (strcmp(got, want) != 0) {
			fprintf(stderr, "FAIL %s: %s %s is %s, not %s\n", name, sel, prop, got, want);
			fails++;
		}
	}
	ecs_free(c, doc);
	dom_free(doc);
	return fails;
}

/* a growing text */
typedef struct buf {
	char *s;
	size_t n, cap;
} buf;

static void add(buf *b, const char *s, size_t n)
{
	if (b->n + n + 2 > b->cap) {
		b->cap = (b->n + n + 2) * 2;
		b->s = realloc(b->s, b->cap);
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n++] = '\n';
	b->s[b->n] = 0;
}

static int cases, failed;

static void flush(const char *name, buf *css, buf *html, buf *expect, unsigned opts)
{
	if (expect->n == 0)
		return;
	cases++;
	if (run_case(name, css->s ? css->s : "", css->n, html->s ? html->s : "", html->n,
			expect->s, opts))
		failed++;
	css->n = html->n = expect->n = 0;
	if (css->s) css->s[0] = 0;
	if (html->s) html->s[0] = 0;
}

/* plat_posix.c's main() calls it */
int nsbench(int argc, char **argv)
{
	size_t len;
	char *text, *p, name[200] = "";
	buf css = { 0 }, html = { 0 }, expect = { 0 }, *cur = NULL;
	unsigned opts = 0;

	if (argc < 2 || (text = load(argv[1], &len)) == NULL) {
		fprintf(stderr, "usage: ectest CASES.TXT\n");
		return 2;
	}
	for (p = text; *p; ) {
		char *e = strchr(p, '\n');
		size_t n = e ? (size_t) (e - p) : strlen(p);
		if (n >= 4 && !strncmp(p, "=== ", 4)) {
			flush(name, &css, &html, &expect, opts);
			snprintf(name, sizeof name, "%.*s", (int) n - 4, p + 4);
			opts = 0;
			cur = NULL;
		} else if (n >= 4 && !strncmp(p, "--- ", 4)) {
			cur = !strncmp(p + 4, "css", 3) ? &css : !strncmp(p + 4, "html", 4) ? &html :
				!strncmp(p + 4, "expect", 6) ? &expect : NULL;
			if (!strncmp(p + 4, "options ", 8))
				opts = strtoul(p + 12, NULL, 0);
		} else if (cur != NULL) {
			add(cur, p, n);
		}
		p += n + (e != NULL);
	}
	flush(name, &css, &html, &expect, opts);
	fprintf(stderr, "%d cases, %d failed\n", cases, failed);
	free(css.s);
	free(html.s);
	free(expect.s);
	free(text);
	return failed != 0;
}
