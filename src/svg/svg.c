/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * svg.c - SVG pictures (svg.h): libsvgtiny's diagram (each shape a path
 * of lines and cubic curves, its fill and stroke) scaled to the size
 * shown and drawn by raster.c, then made a picture as pic.c makes one:
 * RGB 5-6-5 and an alpha.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svg.h"
#include "util.h"

/* ------------------------------------------------------------ its size */

static int is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* a length as a root's width or height gives it, in CSS px (a number, or
   px, pt, pc, mm, cm, in, em); 0: none, a percentage or nonsense */
static long length_px(const char *s, size_t n)
{
	long long v = 0;
	long frac = 0, div = 1;
	size_t i = 0;
	while (i < n && is_space((unsigned char) s[i]))
		i++;
	if (i == n || s[i] < '0' || s[i] > '9') {
		if (i == n || s[i] != '.')
			return 0;
	}
	for (; i < n && s[i] >= '0' && s[i] <= '9'; i++)
		if (v < 100000)
			v = v * 10 + (s[i] - '0');
	if (i < n && s[i] == '.')
		for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++)
			if (div < 1000) {
				frac = frac * 10 + (s[i] - '0');
				div *= 10;
			}
	/* (whole px, rounded) */
	v = v * 1000 + frac * 1000 / div;
	if (i + 2 <= n && (s[i] == 'p' || s[i] == 'P') && (s[i + 1] == 't' || s[i + 1] == 'T'))
		v = v * 4 / 3;
	else if (i + 2 <= n && (s[i] == 'p' || s[i] == 'P') && (s[i + 1] == 'c' || s[i + 1] == 'C'))
		v = v * 16;
	else if (i + 2 <= n && (s[i] == 'm' || s[i] == 'M') && (s[i + 1] == 'm' || s[i + 1] == 'M'))
		v = v * 960 / 254;
	else if (i + 2 <= n && (s[i] == 'c' || s[i] == 'C') && (s[i + 1] == 'm' || s[i + 1] == 'M'))
		v = v * 9600 / 254;
	else if (i + 2 <= n && (s[i] == 'i' || s[i] == 'I') && (s[i + 1] == 'n' || s[i + 1] == 'N'))
		v = v * 96;
	else if (i + 2 <= n && (s[i] == 'e' || s[i] == 'E') && (s[i + 1] == 'm' || s[i + 1] == 'M'))
		v = v * 16;
	else if (i < n && s[i] == '%')
		return 0;
	return (long) ((v + 500) / 1000);
}

/* a viewBox's width and height, times 1000; 0 if it hasn't four numbers */
static int viewbox(const char *s, size_t n, long *w, long *h)
{
	long v[4];
	int k = 0;
	size_t i = 0;
	while (k < 4) {
		long x = 0, frac = 0, div = 1;
		int neg = 0, any = 0;
		while (i < n && (is_space((unsigned char) s[i]) || s[i] == ','))
			i++;
		if (i < n && (s[i] == '-' || s[i] == '+'))
			neg = s[i++] == '-';
		for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, any = 1)
			if (x < 10000000)
				x = x * 10 + (s[i] - '0');
		if (i < n && s[i] == '.')
			for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++, any = 1)
				if (div < 1000) {
					frac = frac * 10 + (s[i] - '0');
					div *= 10;
				}
		if (!any)
			return 0;
		x = x * 1000 + frac * 1000 / div;
		v[k++] = neg ? -x : x;
	}
	if (v[2] <= 0 || v[3] <= 0)
		return 0;
	*w = v[2];
	*h = v[3];
	return 1;
}

/* the size from the root's width, height and viewBox (CSS's default
   300 x 150 where neither says) */
static void size_of(long w, long h, int vb, long vw, long vh, long *ow, long *oh)
{
	if (w <= 0 && h <= 0) {
		if (vb) {
			w = 300;
			h = (long) ((300LL * vh + vw / 2) / vw);
		} else {
			w = 300;
			h = 150;
		}
	} else if (w <= 0) {
		w = vb ? (long) ((h * (long long) vw + vh / 2) / vh) : 300;
	} else if (h <= 0) {
		h = vb ? (long) ((w * (long long) vh + vw / 2) / vw) : 150;
	}
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	*ow = w;
	*oh = h;
}

/* the root's start tag's attribute name (case aside) in the file */
static const char *attr_in(const char *t, const char *end, const char *name, size_t *len)
{
	size_t nl = strlen(name);
	while (t < end && *t != '>') {
		const char *a;
		size_t al;
		while (t < end && (is_space((unsigned char) *t) || *t == '/'))
			t++;
		a = t;
		while (t < end && !is_space((unsigned char) *t) && *t != '=' && *t != '>')
			t++;
		al = (size_t) (t - a);
		while (t < end && is_space((unsigned char) *t))
			t++;
		if (t < end && *t == '=') {
			const char *v;
			char q = 0;
			for (t++; t < end && is_space((unsigned char) *t); t++)
				;
			if (t < end && (*t == '"' || *t == '\''))
				q = *t++;
			v = t;
			while (t < end && (q ? *t != q : !is_space((unsigned char) *t) && *t != '>'))
				t++;
			if (al == nl) {
				size_t i;
				for (i = 0; i < nl && (a[i] | 32) == (name[i] | 32); i++)
					;
				if (i == nl) {
					*len = (size_t) (t - v);
					return v;
				}
			}
			if (q && t < end)
				t++;
		} else if (al == 0 && t < end && *t != '>') {
			t++;
		}
	}
	return NULL;
}

int svg_size(const char *data, long len, long *w, long *h)
{
	const char *end = data + len, *t, *v;
	long aw = 0, ah = 0, vw = 0, vh = 0;
	size_t vl;
	int vb = 0;
	/* the first <svg (past a <?xml?>, comments, a DOCTYPE) */
	for (t = data; t + 4 < end; t++)
		if (t[0] == '<' && (t[1] | 32) == 's' && (t[2] | 32) == 'v' && (t[3] | 32) == 'g' &&
		    (is_space((unsigned char) t[4]) || t[4] == '>' || t[4] == '/'))
			break;
	if (t + 4 >= end)
		return 0;
	t += 4;
	if ((v = attr_in(t, end, "width", &vl)) != NULL)
		aw = length_px(v, vl);
	if ((v = attr_in(t, end, "height", &vl)) != NULL)
		ah = length_px(v, vl);
	if ((v = attr_in(t, end, "viewbox", &vl)) != NULL)
		vb = viewbox(v, vl, &vw, &vh);
	size_of(aw, ah, vb, vw, vh, w, h);
	return 1;
}

int svg_late(const char *data, long len)
{
	long i = 0, w, h;
	if (len >= 3 && !memcmp(data, "\xef\xbb\xbf", 3))
		i = 3;
	while (i < len && is_space((unsigned char) data[i]))
		i++;
	if (len - i < 5 || (memcmp(data + i, "<?xml", 5) && memcmp(data + i, "<!--", 4) &&
			    (len - i < 13 || memcmp(data + i, "<!DOCTYPE svg", 13))))
		return 0;
	return svg_size(data, len, &w, &h);
}

void svg_node_size(const dnode *n, long *w, long *h)
{
	const char *v;
	long aw = 0, ah = 0, vw = 0, vh = 0;
	int vb = 0;
	if ((v = dn_attr(n, "width")) != NULL)
		aw = length_px(v, strlen(v));
	if ((v = dn_attr(n, "height")) != NULL)
		ah = length_px(v, strlen(v));
	if ((v = dn_attr(n, "viewbox")) != NULL)
		vb = viewbox(v, strlen(v), &vw, &vh);
	size_of(aw, ah, vb, vw, vh, w, h);
}

/* ------------------------------------------------------------ drawing */

PIC *svg_decode(const char *data, long len, int w, int h, int *why)
{
	/* (the media only for the parser's CSS context, which keeps its
	   names: an SVG file's sheets are svgread.c's) */
	static const ec_media media = { 1024, 768, 16 };
	doc *d = doc_parse(data, (size_t) len, "UTF-8", &media);	/* (XML's, unless it says) */
	const dnode *k;
	PIC *p = NULL;
	if (d == NULL) {
		*why = PIC_MEMORY;
		return NULL;
	}
	/* its first <svg>: the HTML parser puts it in a body */
	for (k = d->root; k != NULL; ) {
		if (k->type == DN_ELEMENT && k->ns == DN_NS_SVG && k->tag == T_SVG)
			break;
		if (k->first != NULL) {
			k = k->first;
			continue;
		}
		while (k != NULL && k->next == NULL)
			k = k->parent;
		if (k != NULL)
			k = k->next;
	}
	*why = PIC_DAMAGED;
	if (k != NULL)
		p = svg_render(k, d->root, w, h, 0, why);
	doc_free(d);
	return p;
}

/* ------------------------------------------------------------ inline */

typedef struct {
	SBUF b;
	int fail;			/* out of memory */
	const dnode *top, *root;
	const dnode *extra[32];		/* elements outside it that it names */
	int nextra;
} MK;

static void esc(MK *m, const char *s, size_t n, int attr)
{
	size_t i, k = 0;
	for (i = 0; i < n; i++) {
		const char *r = s[i] == '&' ? "&amp;" : s[i] == '<' ? "&lt;" : s[i] == '>' ? "&gt;" :
				attr && s[i] == '"' ? "&quot;" : NULL;
		if (r == NULL)
			continue;
		m->fail |= !sb_add(&m->b, s + k, (long) (i - k));
		m->fail |= !sb_adds(&m->b, r);
		k = i + 1;
	}
	m->fail |= !sb_add(&m->b, s + k, (long) (n - k));
}

/* is n under (or) top? */
static int under(const dnode *n, const dnode *top)
{
	for (; n != NULL; n = n->parent)
		if (n == top)
			return 1;
	return 0;
}

/* an id a value names (#id, url(#id)): the element, when it is outside
   what is being written and not taken yet */
static void named(MK *m, const char *v)
{
	const char *h;
	for (h = strchr(v, '#'); h != NULL; h = strchr(h + 1, '#')) {
		size_t l = 0;
		const dnode *k;
		while (h[1 + l] && h[1 + l] != ')' && h[1 + l] != '"' && h[1 + l] != '\'' && !is_space((unsigned char) h[1 + l]))
			l++;
		if (l == 0 || (h > v && h[-1] != '(' && h[-1] != '"' && h[-1] != '\'' && h != v &&
			       !is_space((unsigned char) h[-1])))
			continue;
		for (k = m->top; k != NULL; ) {
			const char *id;
			if (k->type == DN_ELEMENT && (id = dn_attr(k, "id")) != NULL && strlen(id) == l &&
			    !strncmp(id, h + 1, l))
				break;
			if (k->first != NULL) {
				k = k->first;
				continue;
			}
			while (k != NULL && k->next == NULL)
				k = k->parent;
			if (k != NULL)
				k = k->next;
		}
		if (k != NULL && !under(k, m->root) && m->nextra < 32) {
			int i;
			for (i = 0; i < m->nextra && !under(k, m->extra[i]); i++)
				;
			if (i == m->nextra)
				m->extra[m->nextra++] = k;
		}
	}
}

static void write_node(MK *m, const dnode *n, int depth, uint32_t color)
{
	const dnode *c;
	size_t l;
	const char *nm;
	uint16_t i;
	if (n->type == DN_TEXT) {
		esc(m, n->text, n->len, 0);
		return;
	}
	if (n->type != DN_ELEMENT || depth > 64)
		return;
	nm = ec_atom_text(n->e.name, &l);
	m->fail |= !sb_addc(&m->b, '<');
	m->fail |= !sb_add(&m->b, nm, (long) l);
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		if (depth == 0 && al == 5 && !strncmp(an, "color", 5))
			continue;	/* (the element's own colour, below) */
		m->fail |= !sb_addc(&m->b, ' ');
		m->fail |= !sb_add(&m->b, an, (long) al);
		m->fail |= !sb_adds(&m->b, "=\"");
		esc(m, n->attrs[i].val, n->attrs[i].len, 1);
		m->fail |= !sb_addc(&m->b, '"');
		if ((al == 4 && !strncmp(an, "href", 4)) || (al == 10 && !strncmp(an, "xlink:href", 10)) ||
		    memchr(n->attrs[i].val, '#', n->attrs[i].len) != NULL)
			named(m, n->attrs[i].val);
	}
	if (depth == 0) {
		char cb[24];
		snprintf(cb, sizeof cb, " color=\"#%06lx\"", (unsigned long) (color & 0xffffff));
		m->fail |= !sb_adds(&m->b, cb);
	}
	m->fail |= !sb_addc(&m->b, '>');
	for (c = n->first; c != NULL; c = c->next)
		write_node(m, c, depth + 1, color);
	if (depth == 0 && m->nextra > 0) {
		/* what it names elsewhere in the page (a sprite sheet's symbols,
		   a gradient), and what they name in turn */
		int k;
		m->fail |= !sb_adds(&m->b, "<defs>");
		for (k = 0; k < m->nextra; k++)
			write_node(m, m->extra[k], 1, color);
		m->fail |= !sb_adds(&m->b, "</defs>");
	}
	m->fail |= !sb_adds(&m->b, "</");
	m->fail |= !sb_add(&m->b, nm, (long) l);
	m->fail |= !sb_addc(&m->b, '>');
}

char *svg_markup(const dnode *n, uint32_t color, size_t *len)
{
	MK m;
	const dnode *top = n;
	while (top->parent != NULL)
		top = top->parent;
	memset(&m, 0, sizeof m);
	sb_init(&m.b);
	m.top = top;
	m.root = n;
	write_node(&m, n, 0, color);
	if (m.fail) {
		sb_free(&m.b);
		return NULL;
	}
	*len = (size_t) m.b.len;
	return sb_steal(&m.b);
}
