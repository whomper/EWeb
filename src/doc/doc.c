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

int (*doc_poll)(int what);

long doc_mem(const doc *d)
{
	const struct dblk *b;
	long n = 0;
	uint32_t i;
	for (b = d->mem; b != NULL; b = b->next)
		n += (long) (DHEAD + b->size);
	for (i = 0; i < d->nbig; i++)
		n += (long) d->big[i].size;
	return n;
}

char *doc_text_room(doc *d, dnode *n, size_t size)
{
	uint32_t i;
	char *t;
	for (i = 0; i < d->nbig && d->big[i].t != n->text; i++)
		;
	if (i < d->nbig) {
		t = realloc(n->text, size);
		if (t == NULL)
			return NULL;
	} else {
		if (d->nbig == d->capbig) {
			uint32_t cap = d->capbig ? d->capbig * 2 : 8;
			struct dbig *b = realloc(d->big, cap * sizeof *b);
			if (b == NULL)
				return NULL;
			d->big = b;
			d->capbig = cap;
		}
		t = malloc(size);
		if (t == NULL)
			return NULL;
		memcpy(t, n->text, n->len + 1);		/* (its old room stays in the arena) */
		d->big[i = d->nbig++].n = n;
	}
	d->big[i].t = t;
	d->big[i].size = size;
	return t;
}

void doc_text_fit(doc *d)
{
	uint32_t i;
	for (i = 0; i < d->nbig; i++) {
		struct dbig *b = &d->big[i];
		char *t;
		if (b->n->text != b->t || b->n->len + 1 >= b->size)
			continue;
		if ((t = realloc(b->t, b->n->len + 1)) != NULL) {
			b->t = b->n->text = t;
			b->size = b->n->len + 1;
			b->n->cap = b->n->len;
		}
	}
}

void doc_big_free(doc *d)
{
	uint32_t i;
	for (i = 0; i < d->nbig; i++)
		free(d->big[i].t);
	free(d->big);
	d->big = NULL;
	d->nbig = d->capbig = 0;
}

void *doc_alloc(doc *d, size_t n)
{
	struct dblk *b = d->mem;
	n = (n + DALIGN - 1) & ~(size_t) (DALIGN - 1);
	if (b == NULL || b->used + n > b->size) {
		/* 16 KB first (a small page), then 64 KB (8 KB at a time in
		   the ST edition, where a page's memory is what Back and the
		   pictures don't get); something larger than that
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
		while (b == NULL && !alone && size > 4096 && size / 2 >= n) {
			/* short of memory (a small machine's last free pieces):
			   a smaller piece rather than none, as long as it holds n */
			size /= 2;
			b = malloc(DHEAD + size);
		}
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
	const ec_atom *col, *font, *name, *open, *dir, *bdi, *target;
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
	A(dir);
	A(bdi);
	A(target);
#undef A
}

/* a code point's strong direction: 2 right to left (Hebrew, Arabic), 1
   left to right (a letter of another script), 0 none (digits,
   punctuation, symbols, spaces) */
static int strong_dir(uint32_t c)
{
	if ((c >= 0x590 && c < 0x900) || (c >= 0xFB1D && c < 0xFE00) || (c >= 0xFE70 && c < 0xFF00))
		return 2;
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x2B0 && c != 0xD7 && c != 0xF7) ||
	    (c >= 0x370 && c < 0x590 && c != 0x37E && c != 0x387) || (c >= 0x900 && c < 0x2000) ||
	    (c >= 0x3040 && c < 0xFB1D) || (c >= 0xFF21 && c < 0xFFE0))
		return 1;
	return 0;
}

/* auto directionality (the HTML Standard, 3.2.6.4): the first strong
   letter of its text, passing over bdi, script, style and textarea and
   the elements with a dir of their own: 1 right to left, 0 left to right
   (and when there is none) */
static int auto_rtl(const dnode *n, int depth)
{
	const dnode *c;
	if (depth > 64)
		return -1;
	for (c = n->first; c != NULL; c = c->next) {
		if (c->type == DN_TEXT) {
			const unsigned char *p = (const unsigned char *) c->text, *e = p + c->len;
			while (p < e) {
				/* (UTF-8: a lead byte and its continuations; enough to
				   tell a letter's script) */
				uint32_t u = *p++;
				int d, k = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
				u &= k == 3 ? 0x07 : k == 2 ? 0x0F : k == 1 ? 0x1F : 0x7F;
				for (; k > 0 && p < e && (*p & 0xC0) == 0x80; k--)
					u = u << 6 | (*p++ & 0x3F);
				if ((d = strong_dir(u)) != 0)
					return d == 2;
			}
		} else if (c->type == DN_ELEMENT && c->tag != T_SCRIPT && c->tag != T_STYLE && c->tag != T_TEXTAREA &&
			   c->e.name != ha.bdi && attr_atom(c, ha.dir) == NULL) {
			int r = auto_rtl(c, depth + 1);
			if (r >= 0)
				return r;
		}
	}
	return depth == 0 ? 0 : -1;
}

static void hints(dnode *n)
{
	HINT h;
	char buf[32];
	const char *v;
	int cell = n->tag == T_TD || n->tag == T_TH, pic;
	const char *t;

	if (n->nattr == 0 && !cell && n->e.name != ha.bdi)
		return;
	h.n = 0;
	h.s[0] = 0;
	/* dir=auto, and a bdi without a dir: its direction from its text */
	v = HA(n, dir);
	if (v != NULL ? is(v, "auto") : n->e.name == ha.bdi)
		put(&h, "direction", auto_rtl(n, 0) == 1 ? "rtl" : "ltr");

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
	/* an image button is sized as a picture is (the HTML Standard,
	   15.4.3: its width and height map to the dimension properties, and
	   hspace and vspace to its margins), so that its size is known before
	   its file comes: webgate's live view, asked for once at that size */
	pic = n->tag == T_IMG || (n->tag == T_INPUT && (t = HA(n, type)) != NULL && is(t, "image"));
	if (pic) {
		if ((v = dimension(HA(n, hspace), buf)) != NULL)
			put(&h, "margin-left", v), put(&h, "margin-right", v);
		if ((v = dimension(HA(n, vspace), buf)) != NULL)
			put(&h, "margin-top", v), put(&h, "margin-bottom", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_HR || pic || n->e.name == ha.col) {
		if ((v = dimension(HA(n, width), buf)) != NULL)
			put(&h, "width", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_TR || pic) {
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

/* ---------------------------------------------------------------- refresh */

#define RWS(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\f' || (c) == '\r')

/* the shared declarative refresh steps (the HTML Standard, 4.2.5.3), up
   to the address's parse, step by step */
int doc_refresh_parse(const char *in, unsigned long *secs, const char **url, size_t *ulen)
{
	const char *p = in, *u;
	unsigned long t = 0;
	size_t n;
	char quote;
	while (RWS(*p))
		p++;
	/* the time: digits, or none before a '.' (".5" is 0) */
	if (*p >= '0' && *p <= '9') {
		for (; *p >= '0' && *p <= '9'; p++)
			if (t < DOC_REFRESH_MAX)
				t = t * 10 + (unsigned long) (*p - '0');
		if (t > DOC_REFRESH_MAX)
			t = DOC_REFRESH_MAX;
	} else if (*p != '.') {
		return 0;
	}
	while ((*p >= '0' && *p <= '9') || *p == '.')
		p++;		/* the fraction, ignored */
	*secs = t;
	*url = NULL;
	*ulen = 0;
	if (*p) {
		if (*p != ';' && *p != ',' && !RWS(*p))
			return 0;
		while (RWS(*p))
			p++;
		if (*p == ';' || *p == ',')
			p++;
		while (RWS(*p))
			p++;
	}
	if (*p == 0)
		return 1;	/* no address: the page itself */
	u = p;			/* urlString, if "url=" doesn't follow */
	n = strlen(u);
	if ((*p | 0x20) == 'u') {
		p++;
		if ((*p | 0x20) != 'r')
			goto parse;
		p++;
		if ((*p | 0x20) != 'l')
			goto parse;
		p++;
		while (RWS(*p))
			p++;
		if (*p != '=')
			goto parse;
		p++;
		while (RWS(*p))
			p++;
	}
	/* skip quotes: the address ends at the same quote, if there is one */
	quote = *p == '\'' || *p == '"' ? *p++ : 0;
	u = p;
	n = strlen(u);
	if (quote) {
		const char *q = strchr(u, quote);
		if (q != NULL)
			n = (size_t) (q - u);
	}
parse:
	*url = u;
	*ulen = n;
	return 1;
}

/* (a template's contents aren't in the document: the Refresh state's
   steps don't run for a <meta> there) */
int doc_refresh_meta(const dnode *n)
{
	const char *v = dn_attr(n, "http-equiv"), *c, *u;
	const dnode *a;
	unsigned long s;
	size_t ul;
	if (n->ns != DN_NS_HTML || v == NULL || !is(v, "refresh") || (c = dn_attr(n, "content")) == NULL || !*c ||
	    !doc_refresh_parse(c, &s, &u, &ul))
		return 0;
	for (a = n->parent; a != NULL; a = a->parent)
		if (a->tag == T_TEMPLATE && a->ns == DN_NS_HTML)
			return 0;
	return 1;
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
	} else if (n->tag == T_BASE) {
		const char *h = HA(n, href), *t = HA(n, target);
		if (h != NULL && d->base[0] == 0 && strlen(h) < sizeof d->base)
			strcpy(d->base, h);
		if (t != NULL && d->target[0] == 0 && strlen(t) < sizeof d->target)
			strcpy(d->target, t);
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
	d->scripting = o != NULL && o->scripting;
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
int doc_has_scripts(const doc *d)
{
	const dnode *n = d->root;
	while (n != NULL) {
		if (n->type == DN_ELEMENT) {
			uint16_t i;
			if (n->ns == DN_NS_HTML && n->tag == T_SCRIPT)
				return 1;
			for (i = 0; i < n->nattr; i++) {
				size_t al;
				const char *a = ec_atom_text(n->attrs[i].name, &al);
				if (al > 2 && a[0] == 'o' && a[1] == 'n')
					return 1;
			}
		}
		/* the next in tree order */
		if (n->first != NULL)
			n = n->first;
		else {
			while (n != NULL && n->next == NULL)
				n = n->parent;
			if (n != NULL)
				n = n->next;
		}
	}
	return 0;
}

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
		if (s > w && (cls[n->ncls++] = ec_intern_used(d->css, w, (size_t) (s - w))) == NULL)
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
		n->e.id = len ? ec_intern_used(d->css, v, len) : NULL;
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
	if (d->frames_free != NULL)
		d->frames_free(d->frames);
	form_free(d);
	doc_big_free(d);
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

/* is the text from s to e, in any case, word (in lower case) */
static int word_is(const char *s, const char *e, const char *word)
{
	for (; s < e && *word; s++, word++)
		if ((*s >= 'A' && *s <= 'Z' ? *s | 0x20 : *s) != *word)
			return 0;
	return s == e && *word == 0;
}

/* the HTML Standard's "matches the environment" for a <link>'s or a
   <style>'s media (4.2.4, 4.2.6): none is all; else the media query list
   evaluated as an @media's is (Media Queries 4, the same screen). The
   commonest three, nine in ten on the page set, are told without the
   CSS engine's tokens */
static int media_ok(doc *d, const char *m)
{
	const char *e;
	if (m == NULL)
		return 1;
	e = m + strlen(m);
	if (word_is(m, e, "screen") || word_is(m, e, "all"))
		return 1;
	if (word_is(m, e, "print"))
		return 0;
	return ec_media_ok(d->css, m, (size_t) (e - m));
}

/* a type EWeb reads a sheet of. A <style>'s (4.2.6, "update a style
   block"): none, empty or text/css. A <link>'s (4.2.4, "processing the
   type attribute"): none or a MIME type whose essence is text/css; an
   empty one too, as browsers take it */
static int css_type(const char *t, int link)
{
	const char *e;
	if (t == NULL || *t == 0)
		return 1;
	if (!link)
		return word_is(t, t + strlen(t), "text/css");
	while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r' || *t == '\f')
		t++;
	for (e = t; *e && *e != ';'; e++)
		;
	while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r' || e[-1] == '\f'))
		e--;
	return e == t || word_is(t, e, "text/css");
}

/* the attributes a sheet's element is read by, found in one pass (a
   <link> has many: a look for each cost more than the rest) */
enum { SA_REL, SA_HREF, SA_TYPE, SA_MEDIA, SA_TITLE, SA_DISABLED, SA_AS, SA_N };

static void sheet_attrs(const dnode *n, const char **v)
{
	uint16_t i;
	int k;
	for (k = 0; k < SA_N; k++)
		v[k] = NULL;
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		switch (al) {
		case 2: k = memcmp(an, "as", 2) ? -1 : SA_AS; break;
		case 3: k = memcmp(an, "rel", 3) ? -1 : SA_REL; break;
		case 4: k = !memcmp(an, "href", 4) ? SA_HREF : !memcmp(an, "type", 4) ? SA_TYPE : -1; break;
		case 5: k = !memcmp(an, "media", 5) ? SA_MEDIA : !memcmp(an, "title", 5) ? SA_TITLE : -1; break;
		case 8: k = memcmp(an, "disabled", 8) ? -1 : SA_DISABLED; break;
		default: k = -1; break;
		}
		if (k >= 0 && v[k] == NULL)
			v[k] = n->attrs[i].val;
	}
}

/* what a style or link element is as a sheet, by its attributes a: 0
   none (a type not CSS; a link not to a stylesheet, disabled, or with an
   empty href, which would fetch the page itself: 4.2.4), 1 a sheet, 2 an
   alternative sheet (rel="alternate stylesheet" with a title: applied
   only when its title is the preferred set's; with none, never, as in
   Gecko) */
static int sheet_kind(const dnode *n, const char **a)
{
	if (n->tag == T_STYLE)
		return css_type(a[SA_TYPE], 0);
	if (a[SA_REL] == NULL || a[SA_HREF] == NULL || *a[SA_HREF] == 0 || !word_in(a[SA_REL], "stylesheet") ||
	    a[SA_DISABLED] != NULL || !css_type(a[SA_TYPE], 1))
		return 0;
	if (!word_in(a[SA_REL], "alternate"))
		return 1;
	return a[SA_TITLE] != NULL && *a[SA_TITLE] ? 2 : 0;
}

typedef struct {
	doc *d;
	void (*cb)(void *, int, const char *, size_t);
	void *ud;
	dnode *top;
	const char *set;	/* the preferred style sheet set's name, or NULL */
	uint8_t looked;		/* the rest of the page looked through for it */
	uint32_t key;		/* doc_sheets_key's, as the walk goes */
} SHEETS_WALK;

/* the node after n as doc_sheets walks the tree: into n's children when
   down */
static dnode *walk_next(const dnode *top, dnode *n, int down)
{
	if (down && n->first != NULL)
		return n->first;
	while (n != top && n->next == NULL)
		n = n->parent;
	return n == top ? NULL : n->next;
}

/* nothing under n is looked at: a style or link element, a template,
   SVG (as sheet_of says) */
static int sheet_leaf(const dnode *n)
{
	if (n->type != DN_ELEMENT)
		return 0;
	if (n->ns != DN_NS_HTML)
		return n->tag == T_SVG;
	return n->tag == T_STYLE || n->tag == T_LINK || n->tag == T_TEMPLATE;
}

/* the title the first sheet after n names a set by (a titled sheet, not
   an alternative), or NULL */
static const char *set_after(const dnode *top, dnode *n)
{
	const char *a[SA_N];
	for (n = walk_next(top, n, 0); n != NULL; n = walk_next(top, n, !sheet_leaf(n)))
		if (n->type == DN_ELEMENT && n->ns == DN_NS_HTML && (n->tag == T_STYLE || n->tag == T_LINK)) {
			sheet_attrs(n, a);
			if (a[SA_TITLE] != NULL && *a[SA_TITLE] && sheet_kind(n, a) == 1)
				return a[SA_TITLE];
		}
	return NULL;
}

/* CSSOM's "add a CSS style sheet" for n, a sheet (kind 1) or an
   alternative (2) titled t: an untitled sheet is always enabled; the
   first titled sheet that isn't an alternative names the preferred set,
   and as EWeb has no menu to choose another, a sheet of another set (its
   title another) is disabled. An alternative is enabled when its title
   is the preferred set's, though that be named after it ("enable a CSS
   style sheet set" as the name is given; Gecko and Blink do so): the
   rest of the page is looked through for the name then, which a page
   with no alternatives never costs. 1 when enabled */
static int in_set(SHEETS_WALK *w, dnode *n, int kind, const char *t)
{
	if (t == NULL || *t == 0)
		return 1;
	if (w->set == NULL && kind == 1) {
		w->set = t;
	} else if (w->set == NULL && !w->looked) {
		w->set = set_after(w->top, n);
		w->looked = 1;
	}
	return w->set != NULL && strcmp(t, w->set) == 0;
}

/* a sheet's element into doc_sheets_key: which element, its attributes,
   and a <style>'s text (a script's change makes it anew, at another
   address). Only an element that is a sheet (sheet_kind): one that a
   script makes one, or makes no longer one, comes into it or goes, and
   the other <link>s a script adds or changes (a preconnect, a preload of
   a script, an icon) leave it as it was */
static uint32_t key_of(uint32_t h, const dnode *n, const char **a)
{
	const dnode *c;
	int k;
	h = h * 33 + (uint32_t) (uintptr_t) n;
	for (k = 0; k < SA_N; k++) {
		const char *s = a[k];
		if (s == NULL) {
			h = h * 33 + 1;
			continue;
		}
		for (; *s; s++)
			h = h * 33 + (uint8_t) *s;
		h *= 33;
	}
	if (n->tag == T_STYLE)
		for (c = n->first; c != NULL; c = c->next)
			h = (h * 33 + (uint32_t) (uintptr_t) c->text) * 33 + c->len;
	return h;
}

/* n's sheet, if it is a style or link element: 1 when nothing under it
   is looked at (those, a template, SVG). A sheet whose media doesn't
   match isn't fetched (the HTML Standard lets a browser fetch only the
   sheets it applies, 4.2.4): when the window comes to a width it holds
   at, the sheets are taken again (src/browse, as the @media in them) */
static int sheet_of(SHEETS_WALK *w, dnode *n)
{
	const char *a[SA_N];
	dnode *c;
	int kind;
	if (n->type != DN_ELEMENT || n->ns != DN_NS_HTML)
		return n->type == DN_ELEMENT && n->tag == T_SVG;
	if (n->tag != T_STYLE && n->tag != T_LINK)
		return n->tag == T_TEMPLATE;
	sheet_attrs(n, a);
	kind = sheet_kind(n, a);
	if (kind == 0)
		return 1;
	if (w->d->scripting)
		w->key = key_of(w->key, n, a);
	if (!in_set(w, n, kind, a[SA_TITLE]) || !media_ok(w->d, a[SA_MEDIA]))
		return 1;
	if (n->tag == T_LINK) {
		w->cb(w->ud, 1, a[SA_HREF], strlen(a[SA_HREF]));
	} else {
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
			w->cb(w->ud, 0, buf, len);
			free(buf);
		}
	}
	return 1;
}

/* in document order, without recursion (a page can be nested deeper than
   the Atari's stack would go) */
void doc_sheets(doc *d, void (*cb)(void *ud, int is_link, const char *s, size_t len), void *ud)
{
	dnode *top = d->root, *n = top;
	SHEETS_WALK w;
	w.d = d;
	w.cb = cb;
	w.ud = ud;
	w.top = top;
	w.set = NULL;
	w.looked = 0;
	w.key = 5381;
	for (n = top; n != NULL;) {
		if (!sheet_of(&w, n) && n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != top && n->next == NULL)
			n = n->parent;
		if (n == top)
			break;
		n = n->next;
	}
	if (d->scripting)
		d->sheets_key = w.key;
}

uint32_t doc_sheets_key(const doc *d)
{
	const char *a[SA_N];
	dnode *n;
	uint32_t h = 5381;
	for (n = d->root; n != NULL; n = walk_next(d->root, n, !sheet_leaf(n)))
		if (n->type == DN_ELEMENT && n->ns == DN_NS_HTML && (n->tag == T_STYLE || n->tag == T_LINK)) {
			sheet_attrs(n, a);
			if (sheet_kind(n, a) != 0)
				h = key_of(h, n, a);
		}
	return h;
}

int doc_link_loads(const dnode *n)
{
	const char *a[SA_N];
	if (n->type != DN_ELEMENT || n->ns != DN_NS_HTML || n->tag != T_LINK)
		return 0;
	sheet_attrs(n, a);
	if (sheet_kind(n, a) != 0)
		return 1;
	/* rel=preload as=style: what the async-CSS pattern's handler makes
	   a stylesheet as it loads */
	return a[SA_REL] != NULL && word_in(a[SA_REL], "preload") && a[SA_HREF] != NULL && *a[SA_HREF] &&
	       a[SA_AS] != NULL && word_is(a[SA_AS], a[SA_AS] + strlen(a[SA_AS]), "style");
}

void doc_add_ua(doc *d)
{
	/* a document on the base has the UA sheet already */
	if (d->ua == NULL)
		ec_sheet(d->css, doc_ua_css, strlen(doc_ua_css), EC_ORIGIN_UA);
	if (d->quirks == 1)
		ec_sheet(d->css, doc_quirks_css, strlen(doc_quirks_css), EC_ORIGIN_UA);
}

long doc_mark_links(doc *d, int (*visited)(void *ud, const char *href), void *ud, int dry)
{
	dnode *n = d->root;
	long changed = 0;
	/* the tree in document order, without recursion (it may be deep) */
	while (n != NULL) {
		if (n->type == DN_ELEMENT && (n->e.flags & EC_NODE_LINK)) {
			const char *h = HA(n, href);
			uint16_t v = visited != NULL && h != NULL && visited(ud, h) ? EC_NODE_VISITED : 0;
			if ((n->e.flags & EC_NODE_VISITED) != v) {
				changed++;
				if (!dry)
					n->e.flags = (uint16_t) ((n->e.flags & ~EC_NODE_VISITED) | v);
			}
		}
		if (n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != NULL && n->next == NULL)
			n = n->parent;
		if (n != NULL)
			n = n->next;
	}
	return changed;
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
