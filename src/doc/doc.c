/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * doc.c - a page's document: its memory, made by EWeb's HTML parser
 * (html.c); then the element links, indexes, flags and presentational
 * hints the CSS engine reads, the head's title and base, and the
 * stylesheets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "form.h"
#include "html.h"

/* ---------------------------------------------------------------- memory */

/* blocks of memory, the newest first; small at first, then larger */
struct dblk {
	struct dblk *next;
	size_t used, size;
};

#define DALIGN 8
#define DHEAD ((sizeof(struct dblk) + DALIGN - 1) & ~(size_t) (DALIGN - 1))

void *doc_alloc(doc *d, size_t n)
{
	struct dblk *b = d->mem;
	n = (n + DALIGN - 1) & ~(size_t) (DALIGN - 1);
	if (b == NULL || b->used + n > b->size) {
		/* 16 KB first (a small page), then 64 KB (8 KB at a time in
		   the ST edition, for a 1 MB ST); something larger than that
		   gets a block of its own, kept behind the one in use so that
		   keeps its room */
#ifdef EW_ST
		size_t size = 8192;
#else
		size_t size = b == NULL ? 16384 : 65536;
#endif
		int alone = n > size;
		if (alone)
			size = n;
		b = malloc(DHEAD + size);
		if (b == NULL)
			return NULL;
		b->size = size;
		b->used = 0;
		if (alone && d->mem != NULL) {
			b->next = d->mem->next;
			d->mem->next = b;
		} else {
			b->next = d->mem;
			d->mem = b;
		}
	}
	b->used += n;
	return (char *) b + DHEAD + b->used - n;
}

void *doc_grow(doc *d, void *p, size_t old, size_t n)
{
	struct dblk *b = d->mem;
	size_t o = (old + DALIGN - 1) & ~(size_t) (DALIGN - 1);
	void *q;
	if (b != NULL && (char *) p + o == (char *) b + DHEAD + b->used &&
	    b->used - o + ((n + DALIGN - 1) & ~(size_t) (DALIGN - 1)) <= b->size) {
		b->used = b->used - o + ((n + DALIGN - 1) & ~(size_t) (DALIGN - 1));
		return p;
	}
	q = doc_alloc(d, n);
	if (q != NULL && old > 0)
		memcpy(q, p, old < n ? old : n);
	return q;
}

const char *dn_attr(const dnode *n, const char *name)
{
	uint16_t i;
	size_t len = strlen(name);
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		if (al == len && memcmp(an, name, len) == 0)
			return n->attrs[i].val;
	}
	return NULL;
}

/* ---------------------------------------------------------------- after parsing */

static doc *bd_doc;	/* the document being finished (one at a time) */

static void collapse_into(char *out, size_t size, const char *s, size_t n)
{
	size_t o = 0, i;
	int space = 0;
	for (i = 0; i < n && o + 1 < size; i++) {
		unsigned char c = (unsigned char) s[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
			space = o > 0;
			continue;
		}
		if (space && o + 2 < size)
			out[o++] = ' ';
		space = 0;
		out[o++] = (char) c;
	}
	out[o] = 0;
}

/* the element links, indexes and flags the CSS engine reads; the head's
   title and base */
/* ---------------------------------------------------------------- presentational hints */
/* HTML's attributes that stand for CSS (the HTML Standard's rendering
   section): written out as declarations for the CSS engine (ec_node.hint) */

typedef struct {
	char s[400];
	size_t n;
} HINT;

static void put(HINT *h, const char *prop, const char *val)
{
	size_t a = strlen(prop), b = strlen(val);
	if (h->n + a + b + 2 >= sizeof h->s)
		return;
	memcpy(h->s + h->n, prop, a);
	h->s[h->n + a] = ':';
	memcpy(h->s + h->n + a + 1, val, b);
	h->n += a + b + 1;
	h->s[h->n++] = ';';
	h->s[h->n] = 0;
}

/* a dimension: "120" is 120px, "50%" half; NULL unless a number above zero */
static const char *dimension(const char *v, char *out)
{
	unsigned long k = 0;
	int digits = 0;
	if (v == NULL)
		return NULL;
	while (*v == ' ' || *v == '\t' || *v == '\n')
		v++;
	while (*v >= '0' && *v <= '9' && k < 100000) {
		k = k * 10 + (unsigned long) (*v++ - '0');
		digits++;
	}
	while (*v >= '0' && *v <= '9')
		v++;
	if (*v == '.')
		for (v++; *v >= '0' && *v <= '9'; v++)
			;
	if (!digits || k == 0)
		return NULL;
	sprintf(out, "%lu%s", k, *v == '%' ? "%" : "px");
	return out;
}

/* a colour: #rgb or #rrggbb, with or without the #, or a name; not
   "transparent", which the HTML Standard's rules for parsing a legacy
   colour value turn down (the colour is then the inherited one) */
static const char *colour(const char *v, char *out)
{
	size_t i, n;
	int hex = 1, alpha = 1;
	if (v == NULL)
		return NULL;
	while (*v == ' ')
		v++;
	if (*v == '#')
		v++;
	n = strlen(v);
	while (n > 0 && v[n - 1] == ' ')
		n--;
	if (n == 0 || n > 20)
		return NULL;
	for (i = 0; i < n; i++) {
		char c = v[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			hex = 0;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			alpha = 0;
	}
	if (hex && (n == 3 || n == 6)) {
		out[0] = '#';
		memcpy(out + 1, v, n);
		out[n + 1] = 0;
		return out;
	}
	if (alpha) {
		static const char tr[] = "transparent";
		for (i = 0; i < n && i < sizeof tr - 1 && (v[i] | 0x20) == tr[i]; i++)
			;
		if (i == n && n == sizeof tr - 1)
			return NULL;
		memcpy(out, v, n);
		out[n] = 0;
		return out;
	}
	return NULL;
}

static int is(const char *v, const char *w)
{
	for (; *v && *w; v++, w++)
		if ((*v | 0x20) != *w)
			return 0;
	return *v == 0 && *w == 0;
}

static const char *list_type(const char *v, int ol)
{
	if (v == NULL)
		return NULL;
	if (ol || (v[0] && !v[1])) {
		switch (v[0]) {
		case '1': return "decimal";
		case 'a': return "lower-alpha";
		case 'A': return "upper-alpha";
		case 'i': return "lower-roman";
		case 'I': return "upper-roman";
		}
	}
	if (is(v, "disc") || is(v, "circle") || is(v, "square"))
		return is(v, "disc") ? "disc" : is(v, "circle") ? "circle" : "square";
	return NULL;
}

/* the attributes the hints and links look at, as atoms of the document
   being finished: compared as pointers, not as text */
static struct {
	const ec_atom *align, *bgcolor, *border, *cellpadding, *cellspacing, *color, *face, *height, *href, *hspace, *leftmargin, *marginheight, *marginwidth, *nowrap, *size, *text, *topmargin, *type, *valign, *vspace, *width;
	const ec_atom *col, *font, *name, *open;
} ha;

static const char *attr_atom(const dnode *n, const ec_atom *a)
{
	uint16_t i;
	for (i = 0; i < n->nattr; i++)
		if (n->attrs[i].name == a)
			return n->attrs[i].val;
	return NULL;
}

#define HA(n, name) attr_atom((n), ha.name)

static void hint_atoms(doc *d)
{
#define A(x) ha.x = ec_intern(d->css, #x, sizeof #x - 1)
	A(align);
	A(bgcolor);
	A(border);
	A(cellpadding);
	A(cellspacing);
	A(color);
	A(face);
	A(height);
	A(href);
	A(hspace);
	A(leftmargin);
	A(marginheight);
	A(marginwidth);
	A(nowrap);
	A(size);
	A(text);
	A(topmargin);
	A(type);
	A(valign);
	A(vspace);
	A(width);
	A(col);
	A(font);
	A(name);
	A(open);
#undef A
}

static void hints(dnode *n)
{
	HINT h;
	char buf[32];
	const char *v;
	int cell = n->tag == T_TD || n->tag == T_TH;

	if (n->nattr == 0 && !cell)
		return;
	h.n = 0;
	h.s[0] = 0;

	if ((v = HA(n, align)) != NULL) {
		const char *t = HA(n, type);
		int part = cell || n->tag == T_TR || n->tag == T_THEAD || n->tag == T_TBODY || n->tag == T_TFOOT;
		if (n->tag == T_IMG || n->tag == T_IFRAME || n->tag == T_OBJECT || n->tag == T_EMBED ||
		    (n->tag == T_INPUT && t != NULL && is(t, "image"))) {
			/* the HTML Standard's rendering, 15.4.3: a float, or where
			   it sits on its line; bottom on the baseline, as HTML 3.2
			   had it and both engines do */
			t = is(v, "left") || is(v, "right") ? NULL : is(v, "top") ? "top" :
			    is(v, "middle") || is(v, "center") ? "-webkit-baseline-middle" :
			    is(v, "bottom") || is(v, "baseline") ? "baseline" : is(v, "texttop") ? "text-top" :
			    is(v, "absmiddle") || is(v, "abscenter") ? "middle" : is(v, "absbottom") ? "bottom" : NULL;
			if (t != NULL)
				put(&h, "vertical-align", t);
			else if (is(v, "left") || is(v, "right"))
				put(&h, "float", is(v, "left") ? "left" : "right");
		} else if (n->tag == T_TABLE) {
			/* 15.3.8: a float, or centred by its margins */
			if (is(v, "left") || is(v, "right"))
				put(&h, "float", is(v, "left") ? "left" : "right");
			else if (is(v, "center"))
				put(&h, "margin-left", "auto"), put(&h, "margin-right", "auto");
		} else if (n->tag == T_HR) {
			/* 15.3.11: placed by its margins */
			if (is(v, "left") || is(v, "right") || is(v, "center")) {
				put(&h, "margin-left", is(v, "left") ? "0" : "auto");
				put(&h, "margin-right", is(v, "right") ? "0" : "auto");
			}
		} else if (n->tag != T_INPUT) {
			/* text-align. On div and a table's parts (and p, as both
			   engines do) the legacy values, which align the blocks
			   inside as well as the lines (15.3.3, 15.3.8: "align
			   descendants"); justify aligns them as usual */
			int legacy = part || n->tag == T_DIV || n->tag == T_P || n->tag == T_CAPTION;
			t = is(v, "left") ? (legacy ? "-webkit-left" : "left") :
			    is(v, "right") ? (legacy ? "-webkit-right" : "right") :
			    is(v, "center") || is(v, "middle") ? (legacy ? "-webkit-center" : "center") :
			    is(v, "justify") ? "justify" : part && is(v, "absmiddle") ? "center" : NULL;
			if (t != NULL)
				put(&h, "text-align", t);
		}
	}
	if ((v = HA(n, valign)) != NULL &&
	    (is(v, "top") || is(v, "middle") || is(v, "bottom") || is(v, "baseline")))
		put(&h, "vertical-align", is(v, "top") ? "top" : is(v, "middle") ? "middle" :
		    is(v, "bottom") ? "bottom" : "baseline");
	if ((v = colour(HA(n, bgcolor), buf)) != NULL)
		put(&h, "background-color", v);
	if (n->tag == T_BODY) {
		if ((v = colour(HA(n, text), buf)) != NULL)
			put(&h, "color", v);
		if ((v = dimension(HA(n, marginwidth), buf)) != NULL ||
		    (v = dimension(HA(n, leftmargin), buf)) != NULL)
			put(&h, "margin-left", v), put(&h, "margin-right", v);
		if ((v = dimension(HA(n, marginheight), buf)) != NULL ||
		    (v = dimension(HA(n, topmargin), buf)) != NULL)
			put(&h, "margin-top", v), put(&h, "margin-bottom", v);
	}
	if (n->tag == T_IMG) {
		if ((v = dimension(HA(n, hspace), buf)) != NULL)
			put(&h, "margin-left", v), put(&h, "margin-right", v);
		if ((v = dimension(HA(n, vspace), buf)) != NULL)
			put(&h, "margin-top", v), put(&h, "margin-bottom", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_HR || n->tag == T_IMG || n->e.name == ha.col) {
		if ((v = dimension(HA(n, width), buf)) != NULL)
			put(&h, "width", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_TR || n->tag == T_IMG) {
		if ((v = dimension(HA(n, height), buf)) != NULL)
			put(&h, "height", v);
	}
	if (cell) {
		const dnode *tb;
		if (HA(n, nowrap) != NULL)
			put(&h, "white-space", "nowrap");
		for (tb = n->parent; tb != NULL && tb->tag != T_TABLE; tb = tb->parent)
			;
		if (tb != NULL) {
			const char *b = HA(tb, border);
			if (b != NULL && (*b == 0 || dimension(b, buf) != NULL))
				put(&h, "border", "1px inset gray");
			if ((v = dimension(HA(tb, cellpadding), buf)) != NULL)
				put(&h, "padding", v);
			else if ((v = HA(tb, cellpadding)) != NULL && *v == '0')
				put(&h, "padding", "0");
		}
	}
	if (n->tag == T_TABLE) {
		const char *b = HA(n, border);
		if (b != NULL) {
			char bw[48];
			v = dimension(b, buf);
			if (*b == 0)
				v = "1px";
			if (v != NULL) {
				sprintf(bw, "%s outset gray", v);
				put(&h, "border", bw);
			}
		}
		if ((v = dimension(HA(n, cellspacing), buf)) != NULL)
			put(&h, "border-spacing", v);
		else if ((v = HA(n, cellspacing)) != NULL && *v == '0')
			put(&h, "border-spacing", "0");
	}
	if (n->tag == T_HR && (v = colour(HA(n, color), buf)) != NULL) {
		put(&h, "border-color", v);
		put(&h, "background-color", v);
	}
	if (n->e.name == ha.font) {
		if ((v = colour(HA(n, color), buf)) != NULL)
			put(&h, "color", v);
		if ((v = HA(n, size)) != NULL) {
			static const char *const sizes[] = { "x-small", "small", "medium", "large", "x-large",
							     "xx-large", "48px" };
			int k, rel = 0;
			while (*v == ' ')
				v++;
			if (*v == '+' || *v == '-')
				rel = *v++ == '+' ? 1 : -1;
			if (*v >= '0' && *v <= '9') {
				k = atoi(v);
				k = rel > 0 ? 3 + k : rel < 0 ? 3 - k : k;
				if (k < 1)
					k = 1;
				if (k > 7)
					k = 7;
				put(&h, "font-size", sizes[k - 1]);
			}
		}
		if ((v = HA(n, face)) != NULL) {
			char f[96];
			size_t i, k = 0;
			for (i = 0; v[i] && k < sizeof f - 1; i++) {
				char c = v[i];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				    c == ' ' || c == ',' || c == '-')
					f[k++] = c;
			}
			f[k] = 0;
			if (k > 0)
				put(&h, "font-family", f);
		}
	}
	if (n->tag == T_OL || n->tag == T_UL || n->tag == T_LI) {
		if ((v = list_type(HA(n, type), n->tag == T_OL ||
				   (n->tag == T_LI && n->parent && n->parent->tag == T_OL))) != NULL)
			put(&h, "list-style-type", v);
	}
	if (h.n > 0) {
		char *s = doc_alloc(bd_doc, h.n + 1);
		if (s != NULL) {
			memcpy(s, h.s, h.n + 1);
			n->e.hint = s;
			n->e.nhint = (uint32_t) h.n;
		}
	}
}

/* the groups of details (their name attribute) with one open, in the
   document being finished: a later one of a group starts closed (the
   HTML Standard, 4.11.1: a details' insertion steps, ensure details
   exclusivity by closing the given element if needed). Eight groups are
   kept track of; pages have one or two. */
static struct {
	const char *name[8];
	int n;
} groups;

static void details_inserted(doc *d, dnode *n)
{
	const char *name = HA(n, name);
	const dnode *k;
	int i;
	if (name == NULL || *name == 0 || HA(n, open) == NULL)
		return;
	for (k = n->parent; k != NULL; k = k->parent)
		if (DN_IS(k, T_TEMPLATE))
			return;		/* a template's contents: a tree of their own */
	for (i = 0; i < groups.n; i++)
		if (strcmp(groups.name[i], name) == 0) {
			dn_set_open(d, n, 0);
			return;
		}
	if (groups.n < 8)
		groups.name[groups.n++] = name;
}

/* one element: its links, flags and hints, and its children's links */
static void finish_one(doc *d, dnode *n, dnode *parent)
{
	dnode *c, *prev = NULL;
	uint16_t idx = 0;
	int empty = 1;

	n->e.parent = parent ? &parent->e : NULL;
	n->e.first = NULL;
	n->e.flags = 0;
	n->e.cls = n->cls;
	n->e.ncls = n->ncls;
	n->e.attr = n->attrs;
	n->e.nattr = n->nattr;
	if (n->tag == T_A && HA(n, href) != NULL)
		n->e.flags |= EC_NODE_LINK;
	if (n->type == DN_ELEMENT && n->ns == DN_NS_HTML && n->e.hint == NULL)
		hints(n);
	if (n->tag == T_DETAILS && n->ns == DN_NS_HTML && n->nattr >= 2)
		details_inserted(d, n);
	if (n->tag == T_HTML && d->html == NULL)
		d->html = n;
	else if (n->tag == T_BODY && d->body == NULL)
		d->body = n;
	else if (n->tag == T_TITLE && d->title[0] == 0) {
		size_t len = 0;
		char *buf;
		for (c = n->first; c != NULL; c = c->next)
			if (c->type == DN_TEXT)
				len += c->len;
		buf = malloc(len + 1);
		if (buf != NULL) {
			len = 0;
			for (c = n->first; c != NULL; c = c->next)
				if (c->type == DN_TEXT) {
					memcpy(buf + len, c->text, c->len);
					len += c->len;
				}
			collapse_into(d->title, sizeof d->title, buf, len);
			free(buf);
		}
	} else if (n->tag == T_BASE && d->base[0] == 0) {
		const char *h = HA(n, href);
		if (h != NULL && strlen(h) < sizeof d->base)
			strcpy(d->base, h);
	}
	for (c = n->first; c != NULL; c = c->next) {
		if (c->type == DN_TEXT && c->len > 0)
			empty = 0;
		if (c->type != DN_ELEMENT)
			continue;
		empty = 0;
		c->e.index = ++idx;
		c->e.prev = prev ? &prev->e : NULL;
		c->e.next = NULL;
		if (prev != NULL)
			prev->e.next = &c->e;
		else
			n->e.first = &c->e;
		prev = c;
	}
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT)
			c->e.nsib = idx;
	if (empty)
		n->e.flags |= EC_NODE_EMPTY;
}

static dnode *element_from(dnode *c)
{
	while (c != NULL && c->type != DN_ELEMENT)
		c = c->next;
	return c;
}

/* top and every element under it, in document order, without recursion
   (a page can be nested deeper than the Atari's stack holds) */
static void finish(doc *d, dnode *top, dnode *parent)
{
	dnode *n = top, *c;
	groups.n = 0;
	finish_one(d, top, parent);
	for (;;) {
		if ((c = element_from(n->first)) != NULL) {
			n = c;
		} else {
			while (n != top && element_from(n->next) == NULL)
				n = n->parent;
			if (n == top)
				return;
			n = element_from(n->next);
		}
		finish_one(d, n, n->parent);
	}
}

/* The UA sheet, parsed once a program run into a base context that
   every document's is made on (ec_share): parsing it for each page cost
   about 320 K 68030 instructions, most of a small page's time. */
static ec_ctx *ua;

static int ua_base(const ec_media *media)
{
	if (ua == NULL) {
		ec_ctx *c = ec_create(media, 0);
		if (c == NULL)
			return 0;
		if (ec_sheet(c, doc_ua_css, strlen(doc_ua_css), EC_ORIGIN_UA) != 0) {
			ec_destroy(c);
			return 0;
		}
		ec_share(c);
		ua = c;
	}
	return 1;
}

doc *doc_parse_opts(const char *data, size_t len, const html_opts *o, const ec_media *media)
{
	doc *d = calloc(1, sizeof *d);
	dnode *c;

	if (d == NULL)
		return NULL;
	d->css = ua_base(media) ? ec_create_on(media, 0, ua) : ec_create(media, 0);
	if (d->css != NULL && ua != NULL)
		d->ua = ua;
	if (d->css == NULL || !html_parse(d, data, len, o)) {
		doc_free(d);
		return NULL;
	}
	bd_doc = d;
	hint_atoms(d);
	for (c = d->root->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT) {
			finish(d, c, NULL);
			c->e.index = c->e.nsib = 1;
			break;
		}
	bd_doc = NULL;
	form_init(d);		/* out of memory: the controls it made */
	return d;
}

doc *doc_parse(const char *data, size_t len, const char *charset, const ec_media *media)
{
	html_opts o;
	memset(&o, 0, sizeof o);
	o.charset = charset;
	return doc_parse_opts(data, len, &o, media);
}

#ifndef EW_ST	/* the ST edition runs no scripts */
/* the element links, flags and hints again, and the form controls, after
   a script changed the tree */
void doc_refresh(doc *d)
{
	dnode *c;
	if (d->root == NULL)
		return;
	bd_doc = d;
	hint_atoms(d);
	d->html = d->body = NULL;
	for (c = d->root->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT) {
			finish(d, c, NULL);
			c->e.index = c->e.nsib = 1;
			break;
		}
	bd_doc = NULL;
	form_init(d);
}

/* the class attribute, split at white space, into n->cls */
static int split_classes(doc *d, dnode *n, const char *s, size_t len)
{
	const char *e = s + len, *w;
	size_t k = 0;
	const ec_atom **cls;
	for (w = s; w < e;) {
		while (w < e && (*w == ' ' || *w == '\t' || *w == '\n' || *w == '\r' || *w == '\f'))
			w++;
		if (w < e)
			k++;
		while (w < e && !(*w == ' ' || *w == '\t' || *w == '\n' || *w == '\r' || *w == '\f'))
			w++;
	}
	n->ncls = 0;
	n->cls = NULL;
	if (k == 0)
		return 1;
	if ((cls = doc_alloc(d, k * sizeof *cls)) == NULL)
		return 0;
	while (s < e) {
		while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		w = s;
		while (s < e && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		if (s > w && (cls[n->ncls++] = ec_intern(d->css, w, (size_t) (s - w))) == NULL)
			return 0;
	}
	n->cls = cls;
	return 1;
}

int dn_set_attr(doc *d, dnode *n, const char *name, const char *val, size_t len)
{
	size_t nl = strlen(name);
	const ec_atom *a = ec_intern(d->css, name, nl);
	char *v = doc_alloc(d, len + 1);
	uint16_t i;
	if (a == NULL || v == NULL)
		return 0;
	memcpy(v, val, len);
	v[len] = 0;
	for (i = 0; i < n->nattr; i++)
		if (n->attrs[i].name == a)
			break;
	if (i == n->nattr) {
		ec_attr *at = doc_alloc(d, (n->nattr + 1u) * sizeof *at);
		if (at == NULL)
			return 0;
		if (n->nattr)
			memcpy(at, n->attrs, n->nattr * sizeof *at);
		n->attrs = at;
		n->nattr++;
		n->attrs[i].name = a;
	}
	n->attrs[i].val = v;
	n->attrs[i].len = (uint32_t) len;
	if (nl == 5 && memcmp(name, "class", 5) == 0 && !split_classes(d, n, v, len))
		return 0;
	if (nl == 2 && memcmp(name, "id", 2) == 0)
		n->e.id = len ? ec_intern(d->css, v, len) : NULL;
	n->e.hint = NULL;	/* made again from the attributes */
	n->e.nhint = 0;
	return 1;
}

void dn_remove_attr(doc *d, dnode *n, const char *name)
{
	size_t nl = strlen(name);
	uint16_t i;
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		if (al == nl && memcmp(an, name, nl) == 0)
			break;
	}
	if (i == n->nattr)
		return;
	memmove(&n->attrs[i], &n->attrs[i + 1], (size_t) (n->nattr - i - 1) * sizeof *n->attrs);
	n->nattr--;
	if (nl == 5 && memcmp(name, "class", 5) == 0) {
		n->cls = NULL;
		n->ncls = 0;
	}
	if (nl == 2 && memcmp(name, "id", 2) == 0)
		n->e.id = NULL;
	n->e.hint = NULL;
	n->e.nhint = 0;
	(void) d;
}

#endif

/* ---------------------------------------------------------------- details */

dnode *dn_summary(const dnode *details)
{
	dnode *c;
	for (c = details->first; c != NULL; c = c->next)
		if (DN_IS(c, T_SUMMARY))
			return c;
	return NULL;
}

int dn_set_open(doc *d, dnode *n, int open)
{
	const ec_atom *a = ec_intern(d->css, "open", 4);
	uint16_t i;
	if (a == NULL)
		return 0;
	for (i = 0; i < n->nattr && n->attrs[i].name != a; i++)
		;
	if (!open && i < n->nattr) {
		/* in place: the parser shares attributes only between the
		   formatting elements it clones, and details isn't one */
		memmove(&n->attrs[i], &n->attrs[i + 1], (size_t) (n->nattr - i - 1) * sizeof *n->attrs);
		n->nattr--;
	} else if (open && i == n->nattr) {
		ec_attr *at = doc_alloc(d, (n->nattr + 1u) * sizeof *at);
		if (at == NULL)
			return 0;
		if (n->nattr)
			memcpy(at, n->attrs, n->nattr * sizeof *at);
		at[n->nattr].name = a;
		at[n->nattr].val = "";
		at[n->nattr].len = 0;
		n->attrs = at;
		n->nattr++;
	}
	n->e.attr = n->attrs;
	n->e.nattr = n->nattr;
	return 1;
}

/* the tree n is in (the HTML Standard's root): its topmost ancestor, or
   the template whose contents it is in (a tree of their own: the
   standard's template contents, a DocumentFragment) */
static const dnode *tree_of(const dnode *n)
{
	while (n->parent != NULL && !DN_IS(n->parent, T_TEMPLATE))
		n = n->parent;
	return n->parent != NULL ? n->parent : n;
}

/* (in tree order, without recursion: a page can be nested deeper than the
   Atari's stack holds; n's own tree only, not into a template's
   contents) */
dnode *dn_details_rival(const doc *d, const dnode *n)
{
	const char *name = dn_attr(n, "name");
	const dnode *top;
	dnode *k;
	(void) d;
	if (name == NULL || *name == 0)
		return NULL;
	top = tree_of(n);
	k = (dnode *) top;
	for (;;) {
		const char *v;
		if (k != n && DN_IS(k, T_DETAILS) && dn_attr(k, "open") != NULL && (v = dn_attr(k, "name")) != NULL &&
		    strcmp(v, name) == 0)
			return k;
		if (k->first != NULL && (k == top || !DN_IS(k, T_TEMPLATE))) {
			k = k->first;
			continue;
		}
		while (k != top && k->next == NULL)
			k = k->parent;
		if (k == top)
			return NULL;
		k = k->next;
	}
}

int dn_details_toggle(doc *d, dnode *n, void (*changed)(void *ud, dnode *k), void *ud)
{
	int open = dn_attr(n, "open") == NULL;
	dnode *k;
	if (!dn_set_open(d, n, open))
		return 0;
	if (changed != NULL)
		changed(ud, n);
	if (open && (k = dn_details_rival(d, n)) != NULL && dn_set_open(d, k, 0) && changed != NULL)
		changed(ud, k);
	return 1;
}

int dn_folded(const dnode *n)
{
	const dnode *c, *k;
	for (c = n, k = n->parent; k != NULL; c = k, k = k->parent)
		if (DN_IS(k, T_DETAILS) && dn_attr(k, "open") == NULL && c != dn_summary(k))
			return 1;
	return 0;
}

int dn_reveal(doc *d, dnode *n, void (*changed)(void *ud, dnode *k), void *ud)
{
	dnode *c, *k;
	int opened = 0;
	for (c = n, k = n->parent; k != NULL; c = k, k = k->parent)
		if (DN_IS(k, T_DETAILS) && dn_attr(k, "open") == NULL && c != dn_summary(k)) {
			if (!dn_details_toggle(d, k, changed, ud))
				break;
			opened++;
		}
	return opened;
}

void doc_free(doc *d)
{
	struct dblk *b, *next;
	if (d == NULL)
		return;
	if (d->js_free != NULL)
		d->js_free(d->js);
	form_free(d);
	for (b = d->mem; b != NULL; b = next) {
		next = b->next;
		free(b);
	}
	if (d->css != NULL)
		ec_destroy(d->css);
	free(d);
}

/* ---------------------------------------------------------------- styles */

static int word_in(const char *list, const char *word)
{
	size_t wl = strlen(word);
	const char *s = list;
	while (*s) {
		const char *w;
		size_t i;
		while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f')
			s++;
		w = s;
		while (*s && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		if ((size_t) (s - w) == wl) {
			for (i = 0; i < wl; i++)
				if ((w[i] | 0x20) != word[i])
					break;
			if (i == wl)
				return 1;
		}
	}
	return 0;
}

/* a media attribute EWeb takes: none, all or screen (or a list with one) */
static int media_ok(const char *m)
{
	const char *s;
	if (m == NULL || *m == 0)
		return 1;
	for (s = m; *s; s++) {
		if ((s[0] | 0x20) == 's' && strncmp(s, "screen", 6) == 0)
			return 1;
		if ((s[0] | 0x20) == 'a' && strncmp(s, "all", 3) == 0)
			return 1;
	}
	return 0;
}

/* n's sheet, if it is a style or link element: 1 when nothing under it
   is looked at (those, a template, SVG) */
static int sheet_of(dnode *n, void (*cb)(void *, int, const char *, size_t), void *ud)
{
	dnode *c;
	if (n->type == DN_ELEMENT) {
		if (n->tag == T_STYLE && n->ns == DN_NS_HTML && media_ok(dn_attr(n, "media"))) {
			size_t len = 0;
			char *buf;
			for (c = n->first; c != NULL; c = c->next)
				if (c->type == DN_TEXT)
					len += c->len;
			buf = malloc(len + 1);
			if (buf != NULL) {
				len = 0;
				for (c = n->first; c != NULL; c = c->next)
					if (c->type == DN_TEXT) {
						memcpy(buf + len, c->text, c->len);
						len += c->len;
					}
				buf[len] = 0;
				cb(ud, 0, buf, len);
				free(buf);
			}
			return 1;
		}
		if (n->tag == T_LINK) {
			const char *rel = dn_attr(n, "rel"), *href = dn_attr(n, "href");
			if (rel != NULL && href != NULL && word_in(rel, "stylesheet") && !word_in(rel, "alternate") &&
			    media_ok(dn_attr(n, "media")))
				cb(ud, 1, href, strlen(href));
			return 1;
		}
		if (n->tag == T_TEMPLATE || n->tag == T_SVG)
			return 1;
	}
	return 0;
}

/* in document order, without recursion (a page can be nested deeper than
   the Atari's stack holds) */
void doc_sheets(doc *d, void (*cb)(void *ud, int is_link, const char *s, size_t len), void *ud)
{
	dnode *top = d->root, *n = top;
	if (top == NULL)
		return;
	for (;;) {
		if (!sheet_of(n, cb, ud) && n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != top && n->next == NULL)
			n = n->parent;
		if (n == top)
			return;
		n = n->next;
	}
}

void doc_add_ua(doc *d)
{
	/* a document on the base has the UA sheet already */
	if (d->ua == NULL)
		ec_sheet(d->css, doc_ua_css, strlen(doc_ua_css), EC_ORIGIN_UA);
	if (d->quirks == 1)
		ec_sheet(d->css, doc_quirks_css, strlen(doc_quirks_css), EC_ORIGIN_UA);
}

int doc_select(doc *d)
{
	dnode *c;
	if (d->root == NULL)
		return 0;
	for (c = d->root->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT)
			return ec_select(d->css, &c->e);
	return 0;
}
