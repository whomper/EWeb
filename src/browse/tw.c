/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* tw.c - Tailwind's utilities made by EWeb (tw.h; DESIGN.md, Tailwind's
   utilities made by EWeb). A class is read as Tailwind 3 reads it:
   variants before it (md:, hover:, dark:, ...), then ! for important,
   then the utility: one of Tailwind's rules (tw_rules), a colour of a
   colour family (text-gray-700, bg-lime-700/30), a token of a family's
   scale (mx-4, w-1/2, -ml-px), or a family's arbitrary value
   (min-h-[250px], bg-[#123456]). Its rule is Tailwind's own, as
   tools/mktw.py kept it of what EWeb reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tw.h"

#ifndef EW_ST
#include "tw_data.h"

#define N(a) ((int) (sizeof(a) / sizeof((a)[0])))

/* the variants EWeb makes: a pseudo-class or element on the class's own
   selector, a selector before it (group-hover, rtl), or a media query;
   w: Tailwind's order among them (a later one wins) */
static const struct tw_var {
	const char *name;
	const char *pseudo;	/* after the class: ":hover" */
	const char *pre;	/* before the class's selector: ".group:hover " */
	const char *media;	/* "(min-width: 768px)" */
	unsigned short w;
} tw_vars[] = {
	{"first", ":first-child", NULL, NULL, 1},
	{"last", ":last-child", NULL, NULL, 2},
	{"odd", ":nth-child(odd)", NULL, NULL, 3},
	{"even", ":nth-child(even)", NULL, NULL, 4},
	{"visited", ":visited", NULL, NULL, 5},
	{"checked", ":checked", NULL, NULL, 6},
	{"focus-within", ":focus-within", NULL, NULL, 7},
	{"hover", ":hover", NULL, NULL, 8},
	{"focus", ":focus", NULL, NULL, 9},
	{"focus-visible", ":focus-visible", NULL, NULL, 10},
	{"active", ":active", NULL, NULL, 11},
	{"disabled", ":disabled", NULL, NULL, 12},
	{"group-hover", NULL, ".group:hover ", NULL, 13},
	{"group-focus", NULL, ".group:focus ", NULL, 14},
	{"placeholder", "::placeholder", NULL, NULL, 15},
	{"before", "::before", NULL, NULL, 16},
	{"after", "::after", NULL, NULL, 17},
	{"ltr", ":where([dir=\"ltr\"], [dir=\"ltr\"] *)", NULL, NULL, 18},
	{"rtl", ":where([dir=\"rtl\"], [dir=\"rtl\"] *)", NULL, NULL, 19},
	{"dark", NULL, NULL, "(prefers-color-scheme: dark)", 30},
	{"print", NULL, NULL, "print", 31},
	{"sm", NULL, NULL, "(min-width: 640px)", 40},
	{"md", NULL, NULL, "(min-width: 768px)", 50},
	{"lg", NULL, NULL, "(min-width: 1024px)", 60},
	{"xl", NULL, NULL, "(min-width: 1280px)", 70},
	{"2xl", NULL, NULL, "(min-width: 1536px)", 80},
};

/* a rule made: its media (an index into the queries met, 0 none), its
   order (variants' weight, then Tailwind's), its text in the buffer */
typedef struct tw_out {
	unsigned short media, w, order;
	long at, len;
} tw_out;

typedef struct tw_buf {
	char *p;
	long len, cap;
	int bad;
} tw_buf;

static void put(tw_buf *b, const char *s, long n)
{
	if (b->bad)
		return;
	if (b->len + n + 1 > b->cap) {
		long c = b->cap ? b->cap * 2 : 4096;
		char *q;
		while (c < b->len + n + 1)
			c *= 2;
		if ((q = realloc(b->p, (size_t) c)) == NULL) {
			b->bad = 1;
			return;
		}
		b->p = q;
		b->cap = c;
	}
	memcpy(b->p + b->len, s, (size_t) n);
	b->len += n;
	b->p[b->len] = 0;
}

static void puts_(tw_buf *b, const char *s)
{
	put(b, s, (long) strlen(s));
}

/* the class as a selector: every character but letters, digits, - and _
   escaped, a digit first as its code point (2xl:flex) */
static void put_class(tw_buf *b, const char *s, size_t n)
{
	size_t i;
	put(b, ".", 1);
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char) s[i];
		if (i == 0 && c >= '0' && c <= '9') {
			/* (a space after it only where the next could be read as
			   more of its hex digits, as Tailwind writes it) */
			char e[8];
			unsigned char d = i + 1 < n ? (unsigned char) s[i + 1] : 0;
			int sp = (d >= '0' && d <= '9') || ((d | 32) >= 'a' && (d | 32) <= 'f') || d == ' ';
			snprintf(e, sizeof e, sp ? "\\%x " : "\\%x", c);
			puts_(b, e);
		} else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
			   c == '_' || c >= 0x80) {
			put(b, (const char *) &s[i], 1);
		} else {
			char e[2] = {'\\', (char) c};
			put(b, e, 2);
		}
	}
}

/* a body with $V (or $C, $H, $A) put in, and !important on each
   declaration when asked */
static void put_body(tw_buf *b, const char *t, const char *v, const char *c, const char *h, const char *a, int imp)
{
	for (; *t; t++) {
		if (t[0] == '$' && (t[1] == 'V' || t[1] == 'C' || t[1] == 'H' || t[1] == 'A')) {
			const char *r = t[1] == 'V' ? v : t[1] == 'C' ? c : t[1] == 'H' ? h : a;
			puts_(b, r != NULL ? r : "");
			t++;
		} else if (*t == ';' && imp) {
			puts_(b, " !important;");
		} else {
			put(b, t, 1);
		}
	}
	if (imp)
		puts_(b, " !important");
}

static int cmp_rule(const void *k, const void *e)
{
	const tw_name *n = k;
	const char *s = ((const struct tw_rule *) e)->name;
	int r = strncmp(n->s, s, n->len);
	return r ? r : s[n->len] ? -1 : 0;
}

/* the first of a sorted table's entries whose pre is n (len bytes) */
#define FIND_PRE(tab, n, len, out) do { \
	int lo_ = 0, hi_ = N(tab); \
	while (lo_ < hi_) { \
		int mid_ = (lo_ + hi_) / 2, r_ = strncmp((tab)[mid_].pre, (n), (len)); \
		if (r_ == 0 && (tab)[mid_].pre[len]) r_ = 1; \
		if (r_ < 0) lo_ = mid_ + 1; else hi_ = mid_; \
	} \
	(out) = lo_; \
} while (0)

static int pre_is(const char *pre, const char *n, size_t len)
{
	return strncmp(pre, n, len) == 0 && pre[len] == 0;
}

static const struct tw_col *colour(const char *s, size_t n)
{
	int lo = 0, hi = N(tw_pal);
	while (lo < hi) {
		int mid = (lo + hi) / 2, r = strncmp(tw_pal[mid].name, s, n);
		if (r == 0 && tw_pal[mid].name[n])
			r = 1;
		if (r == 0)
			return &tw_pal[mid];
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

typedef struct tw_cls {
	const char *full;	/* the class as written, for its selector */
	size_t flen;
	const char *pre[4], *pseudo[4], *media[3];
	int npre, npseudo, nmedia;
	unsigned short w;
	int imp;
} tw_cls;

static int nmq;
static const char *mq[16];	/* the media queries met (joined ones made) */
static char mqbuf[16][96];

static unsigned short media_of(const tw_cls *c)
{
	char q[96];
	int i;
	if (c->nmedia == 0)
		return 0;
	q[0] = 0;
	for (i = 0; i < c->nmedia; i++) {
		if (i)
			strncat(q, " and ", sizeof q - strlen(q) - 1);
		strncat(q, c->media[i], sizeof q - strlen(q) - 1);
	}
	for (i = 0; i < nmq; i++)
		if (!strcmp(mq[i], q))
			return (unsigned short) (i + 1);
	if (nmq == N(mq))
		return 0xffff;
	strcpy(mqbuf[nmq], q);
	mq[nmq] = mqbuf[nmq];
	return (unsigned short) ++nmq;
}

typedef struct tw_list {
	tw_out *o;
	int n, cap;
	tw_buf b;
} tw_list;

/* a rule for class c: its selector (with the rule's own suffix sfx) and
   its body */
static void rule(tw_list *L, const tw_cls *c, const char *sfx, unsigned short order, const char *body,
		 const char *v, const char *col, const char *hex, const char *al)
{
	tw_out *o;
	long at = L->b.len;
	int i, pe = 0;
	unsigned short m = media_of(c);
	if (m == 0xffff)
		return;
	if (L->n == L->cap) {
		int cap = L->cap ? L->cap * 2 : 256;
		tw_out *p = realloc(L->o, sizeof *p * (size_t) cap);
		if (p == NULL) {
			L->b.bad = 1;
			return;
		}
		L->o = p;
		L->cap = cap;
	}
	for (i = 0; i < c->npre; i++)
		puts_(&L->b, c->pre[i]);
	put_class(&L->b, c->full, c->flen);
	for (i = 0; i < c->npseudo; i++)
		if (c->pseudo[i][1] != ':')
			puts_(&L->b, c->pseudo[i]);
		else
			pe = 1;
	if (sfx[0]) {
		if (sfx[0] != ':')
			put(&L->b, " ", 1);
		puts_(&L->b, sfx);
	}
	if (pe)
		for (i = 0; i < c->npseudo; i++)
			if (c->pseudo[i][1] == ':')
				puts_(&L->b, c->pseudo[i]);
	put(&L->b, "{", 1);
	put_body(&L->b, body, v, col, hex, al, c->imp);
	put(&L->b, "}\n", 2);
	o = &L->o[L->n++];
	o->media = m;
	o->w = c->w;
	o->order = order;
	o->at = at;
	o->len = L->b.len - at;
}

/* an arbitrary value: [..] with _ as a space */
static int arbitrary(const char *s, size_t n, char *out, int size)
{
	size_t i;
	int k = 0;
	if (n < 3 || s[0] != '[' || s[n - 1] != ']' || (int) n > size)
		return 0;
	for (i = 1; i + 1 < n; i++)
		out[k++] = s[i] == '_' ? ' ' : s[i];
	out[k] = 0;
	return 1;
}

static int hexd(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
}

/* #rgb, #rrggbb, rgb(r g b) or rgb(r, g, b) (whole numbers): 1 and its
   parts */
static int rgb_of(const char *v, int *rgb)
{
	int i;
	size_t n = strlen(v);
	if (v[0] == '#' && (n == 4 || n == 7)) {
		for (i = 1; i < (int) n; i++)
			if (hexd(v[i]) < 0)
				return 0;
		for (i = 0; i < 3; i++)
			rgb[i] = n == 4 ? hexd(v[1 + i]) * 17 : hexd(v[1 + 2 * i]) * 16 + hexd(v[2 + 2 * i]);
		return 1;
	}
	if (!strncmp(v, "rgb(", 4) && v[n - 1] == ')') {
		const char *p = v + 4;
		for (i = 0; i < 3; i++) {
			int x = 0, k = 0;
			while (*p == ' ' || (*p == ',' && i))
				p++;
			while (*p >= '0' && *p <= '9' && k < 4) {
				x = x * 10 + *p++ - '0';
				k++;
			}
			if (k == 0 || x > 255)
				return 0;
			rgb[i] = x;
		}
		while (*p == ' ')
			p++;
		return *p == ')';
	}
	return 0;
}

static int is_colour_value(const char *v)
{
	return v[0] == '#' || !strncmp(v, "rgb", 3) || !strncmp(v, "hsl", 3);
}

/* the utility u (n bytes) of class c */
static void utility(tw_list *L, tw_cls *c, const char *u, size_t n)
{
	tw_name key;
	const struct tw_rule *r;
	size_t k;
	int i, made = 0;
	char val[80];
	key.s = u;
	key.len = n;
	/* one of Tailwind's rules (some have more than one: placeholder's) */
	r = bsearch(&key, tw_rules, (size_t) N(tw_rules), sizeof tw_rules[0], cmp_rule);
	if (r != NULL) {
		while (r > tw_rules && cmp_rule(&key, r - 1) == 0)
			r--;
		for (; r < tw_rules + N(tw_rules) && cmp_rule(&key, r) == 0; r++)
			rule(L, c, r->suf, r->order, r->body, NULL, NULL, NULL, NULL);
		return;
	}
	/* prefix-token, the token from the last '-' back (a colour's two
	   words: gray-700) */
	for (k = n; k > 1; k--) {
		const char *t;
		size_t pl, tl;
		int at;
		if (u[k - 1] != '-')
			continue;
		pl = k - 1;
		t = u + k;
		tl = n - k;
		/* an arbitrary value */
		if (tl > 2 && t[0] == '[') {
			if (!arbitrary(t, tl, val, sizeof val))
				return;
			if (is_colour_value(val)) {
				/* #rgb, #rrggbb and rgb(r g b) as Tailwind reads them:
				   the family's own form, its opacity variable */
				int rgb[3];
				if (rgb_of(val, rgb)) {
					char tri[16], hex[8];
					snprintf(tri, sizeof tri, "%d %d %d", rgb[0], rgb[1], rgb[2]);
					snprintf(hex, sizeof hex, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
					FIND_PRE(tw_cfams, u, pl, at);
					for (i = at; i < N(tw_cfams) && pre_is(tw_cfams[i].pre, u, pl); i++)
						rule(L, c, tw_cfams[i].suf, tw_cfams[i].order, tw_cfams[i].plain, NULL, tri, hex, NULL);
					return;
				}
				FIND_PRE(tw_cfams, u, pl, at);
				for (i = at; i < N(tw_cfams) && pre_is(tw_cfams[i].pre, u, pl); i++) {
					/* the opacity form: rgb($C / $A) and $H as the value */
					char body[256];
					const char *b = tw_cfams[i].opa, *p;
					int o = 0;
					while (*b && o < (int) sizeof body - 1) {
						if (!strncmp(b, "rgb($C / $A)", 12)) {
							for (p = "$V"; *p && o < (int) sizeof body - 1; p++)
								body[o++] = *p;
							b += 12;
						} else if (!strncmp(b, "$H", 2)) {
							body[o++] = '$';
							if (o < (int) sizeof body - 1)
								body[o++] = 'V';
							b += 2;
						} else {
							body[o++] = *b++;
						}
					}
					body[o] = 0;
					rule(L, c, tw_cfams[i].suf, tw_cfams[i].order, body, val, NULL, NULL, NULL);
				}
				return;
			}
			if (pl == 4 && !strncmp(u, "text", 4)) {
				rule(L, c, "", 0xfff0, "font-size: $V", val, NULL, NULL, NULL);
				return;
			}
			FIND_PRE(tw_fams, u, pl, at);
			if (at < N(tw_fams) && pre_is(tw_fams[at].pre, u, pl))
				rule(L, c, tw_fams[at].suf, tw_fams[at].order, tw_fams[at].body, val, NULL, NULL, NULL);
			return;
		}
		/* a colour: name-shade or black/white, /opacity after it */
		{
			size_t cl = tl, ol = 0;
			const char *slash = memchr(t, '/', tl);
			const struct tw_col *col;
			if (slash != NULL) {
				cl = (size_t) (slash - t);
				ol = tl - cl - 1;
			}
			if ((col = colour(t, cl)) != NULL) {
				char rgb[16], hex[8], al[8];
				int op = -1;
				if (slash != NULL) {
					size_t j;
					op = 0;
					for (j = 0; j < ol; j++) {
						if (slash[1 + j] < '0' || slash[1 + j] > '9' || op > 100)
							return;
						op = op * 10 + slash[1 + j] - '0';
					}
					if (ol == 0 || op > 100)
						return;
				}
				snprintf(rgb, sizeof rgb, "%d %d %d", col->r, col->g, col->b);
				snprintf(hex, sizeof hex, "#%02x%02x%02x", col->r, col->g, col->b);
				if (op >= 0)
					snprintf(al, sizeof al, op == 100 ? "1" : op % 10 ? "0.%02d" : "0.%d", op % 10 ? op : op / 10);
				FIND_PRE(tw_cfams, u, pl, at);
				for (i = at; i < N(tw_cfams) && pre_is(tw_cfams[i].pre, u, pl); i++) {
					rule(L, c, tw_cfams[i].suf, tw_cfams[i].order, op >= 0 ? tw_cfams[i].opa : tw_cfams[i].plain,
					     NULL, rgb, hex, op >= 0 ? al : NULL);
					made = 1;
				}
				if (made)
					return;
			}
		}
		/* a token of a family's scale */
		FIND_PRE(tw_fams, u, pl, at);
		for (i = at; i < N(tw_fams) && pre_is(tw_fams[i].pre, u, pl); i++) {
			int j;
			for (j = tw_fams[i].tok; j < tw_fams[i].tok + tw_fams[i].ntok; j++)
				if (strlen(tw_toks[j].tok) == tl && !strncmp(tw_toks[j].tok, t, tl)) {
					rule(L, c, tw_fams[i].suf, tw_fams[i].order, tw_fams[i].body, tw_toks[j].val, NULL,
					     NULL, NULL);
					made = 1;
					break;
				}
		}
		if (made)
			return;
	}
}

/* class s: its variants, then the utility */
static void one(tw_list *L, const char *s, size_t n)
{
	tw_cls c;
	size_t i, at = 0;
	int depth = 0;
	memset(&c, 0, sizeof c);
	c.full = s;
	c.flen = n;
	for (i = 0; i < n; i++) {
		if (s[i] == '[')
			depth++;
		else if (s[i] == ']')
			depth--;
		else if (s[i] == ':' && depth == 0) {
			size_t vl = i - at;
			int v;
			for (v = 0; v < N(tw_vars); v++)
				if (strlen(tw_vars[v].name) == vl && !strncmp(tw_vars[v].name, s + at, vl))
					break;
			if (v == N(tw_vars))
				return;		/* a variant EWeb doesn't make */
			if (tw_vars[v].pseudo != NULL) {
				if (c.npseudo == N(c.pseudo))
					return;
				c.pseudo[c.npseudo++] = tw_vars[v].pseudo;
			}
			if (tw_vars[v].pre != NULL) {
				if (c.npre == N(c.pre))
					return;
				c.pre[c.npre++] = tw_vars[v].pre;
			}
			if (tw_vars[v].media != NULL) {
				if (c.nmedia == N(c.media))
					return;
				c.media[c.nmedia++] = tw_vars[v].media;
			}
			if (tw_vars[v].w > c.w)
				c.w = (unsigned short) (c.w + tw_vars[v].w * 4);	/* (md:hover after md:) */
			else
				c.w = (unsigned short) (c.w + tw_vars[v].w);
			at = i + 1;
		}
	}
	if (at < n && s[at] == '!') {
		c.imp = 1;
		at++;
	}
	if (at < n)
		utility(L, &c, s + at, n - at);
}

static int cmp_name(const void *a, const void *b)
{
	const tw_name *x = a, *y = b;
	size_t n = x->len < y->len ? x->len : y->len;
	int r = memcmp(x->s, y->s, n);
	return r ? r : (x->len > y->len) - (x->len < y->len);
}

static int cmp_out(const void *a, const void *b)
{
	const tw_out *x = a, *y = b;
	if (x->media != y->media) {
		/* the unconditional first, then each query in the order of its
		   weight (dark before sm before md, as Tailwind puts them) */
		if (!x->media || !y->media)
			return x->media ? 1 : -1;
	}
	if (x->w != y->w)
		return x->w < y->w ? -1 : 1;
	if (x->media != y->media)
		return x->media < y->media ? -1 : 1;
	if (x->order != y->order)
		return x->order < y->order ? -1 : 1;
	return x->at < y->at ? -1 : x->at > y->at;
}

char *tw_sheet(tw_name *names, int n, size_t *len)
{
	tw_list L;
	tw_buf out;
	int i, m;
	memset(&L, 0, sizeof L);
	memset(&out, 0, sizeof out);
	nmq = 0;
	if (n > 0)
		qsort(names, (size_t) n, sizeof *names, cmp_name);
	for (i = 0; i < n; i++)
		if (i == 0 || cmp_name(&names[i - 1], &names[i]) != 0)
			one(&L, names[i].s, names[i].len);
	if (L.n)
		qsort(L.o, (size_t) L.n, sizeof *L.o, cmp_out);
	puts_(&out, tw_base);
	for (i = 0; i < L.n;) {
		m = L.o[i].media;
		if (m) {
			puts_(&out, "@media ");
			puts_(&out, mq[m - 1]);
			puts_(&out, " {\n");
		}
		for (; i < L.n && L.o[i].media == m; i++)
			put(&out, L.b.p + L.o[i].at, L.o[i].len);
		if (m)
			puts_(&out, "}\n");
	}
	free(L.o);
	free(L.b.p);
	if (out.bad || L.b.bad) {
		free(out.p);
		return NULL;
	}
	*len = (size_t) out.len;
	return out.p;
}

int tw_script(const char *url)
{
	if (!strncmp(url, "https:", 6))
		url += 6;
	return !strncmp(url, "//cdn.tailwindcss.com", 21) && (url[21] == 0 || url[21] == '/' || url[21] == '?');
}

#else

char *tw_sheet(tw_name *names, int n, size_t *len)
{
	(void) names;
	(void) n;
	(void) len;
	return NULL;
}

int tw_script(const char *url)
{
	(void) url;
	return 0;
}

#endif
