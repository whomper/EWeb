/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * layout.c - EWeb's layout. Three passes:
 *   build    the styled document becomes boxes: blocks, inline elements,
 *            text, line breaks, and "atoms" (form controls and images'
 *            alternative text, drawn as one piece);
 *   fix up   a block with both blocks and text in it gets anonymous
 *            blocks round the text; an inline element holding blocks
 *            becomes a block;
 *   lay out  blocks stack down the page with their margins collapsing;
 *            a row (a table row, a flex row) puts its children side by
 *            side; a block of text breaks into lines with EWeb's fonts.
 *            Fills (backgrounds, borders) and text runs go to the page's
 *            display list as they are placed.
 * Floats (left and right) go beside what follows them in their block
 * formatting context: lines are shortened next to them, clear moves below
 * them, and a block that starts a context of its own (a cell, a float,
 * overflow other than visible) holds its floats. Positioning is laid out
 * as ordinary blocks; tables as rows of cells side by side.
 */
#include <stdlib.h>
#include <string.h>

#include "layout.h"

#define AUTO INT32_MIN
#define MAXCOLS 64			/* a table's columns that line up; more go as they come */

/* ---------------------------------------------------------------- memory */

struct achunk { struct achunk *next; size_t used, size; char mem[]; };
struct arena { struct achunk *head; };

/* what a box or a run needs: 4 on the 68030 (2 would do), 8 where
   pointers are 8 bytes (the build machine's checks) */
#define AALIGN (sizeof(void *) > 4 ? 8 : 4)

static void *amalloc(struct arena *a, size_t n)
{
	struct achunk *c = a->head;
	void *p;
	n = (n + AALIGN - 1) & ~(size_t) (AALIGN - 1);
	if (c == NULL || c->used + n > c->size) {
		size_t size = n > 16384 ? n : 16384;
		c = malloc(sizeof *c + size);
		if (c == NULL)
			return NULL;
		c->used = 0;
		c->size = size;
		c->next = a->head;
		a->head = c;
	}
	p = c->mem + c->used;
	c->used += n;
	return p;
}

static void afree(struct arena *a)
{
	struct achunk *c = a->head, *n;
	for (; c != NULL; c = n) {
		n = c->next;
		free(c);
	}
	a->head = NULL;
}

static char *astrdup(struct arena *a, const char *s, size_t n)
{
	char *d = amalloc(a, n + 1);
	if (d != NULL) {
		memcpy(d, s, n);
		d[n] = 0;
	}
	return d;
}

/* ---------------------------------------------------------------- boxes */

enum { BX_BLOCK, BX_INLINE, BX_TEXT, BX_BR, BX_ATOM };
enum { AT_LABEL, AT_FIELD, AT_BUTTON, AT_IMAGE };	/* an atom: alt text; a text field; a button; a picture */

typedef struct box box;
struct box {
	uint8_t kind;
	uint8_t inl;			/* a block whose children are inline: lines */
	uint8_t row;			/* a block whose children go side by side */
	uint8_t wrap;			/* a row that wraps (flex-wrap) */
	uint8_t list;			/* a list item */
	uint8_t anon;			/* an anonymous block: no box of its own */
	uint8_t atom;			/* AT_* */
	uint8_t tablerow;
	uint8_t table;			/* a table: shrinks to fit, its rows share column widths */
	uint8_t repl;			/* a picture as a block (floated, display: block): its one child the atom */
	uint16_t link;			/* 1 + link index, or 0 */
	dnode *n;			/* the element, or NULL */
	const ec_style *st;		/* its style (an anonymous box or text: the parent's) */
	box *parent, *first, *last, *next;
	const char *text;		/* text, or an atom's label */
	uint32_t len;
	int32_t aw, ah;			/* an atom's size, device px (AUTO: from its label) */
	int32_t num;			/* a list item's number */
	void *img;			/* AT_IMAGE: the picture's handle */
	int32_t iw, ih;			/* and its size, CSS px (0: not known yet) */
};

/* ---------------------------------------------------------------- the context */

enum { IT_WORD, IT_SPACE, IT_BREAK, IT_ATOM, IT_GAP };

typedef struct {
	uint8_t kind;
	uint8_t flags;			/* DLF_* */
	uint8_t canbreak;		/* a space a line may end at */
	uint8_t collapse;		/* a space dropped at the start or end of a line */
	uint8_t hidden;			/* visibility: hidden */
	uint8_t dir;			/* bidi: 0 neutral, 1 left to right, 2 right to left */
	uint16_t link;
	ew_font *font;
	uint32_t color, bg;		/* 0xAARRGGBB; bg 0: none */
	const char *text;
	uint32_t len;
	int32_t w;
	int32_t asc, desc;		/* above and below the baseline, leading included */
	int32_t shift;			/* vertical-align: + down */
	box *atom;
	int32_t x;			/* where it went on its line */
} ITEM;

/* a float placed, in device px on the page */
typedef struct {
	int32_t x0, x1, y0, y1;
	uint8_t right;
} FLT;

typedef struct {
	page *p;
	struct arena *a;
	int32_t zoom;
	ITEM *it;
	long nit, itcap;
	int body_bg_taken;
	int fail;
	int32_t cw;			/* the width lines are broken in (AUTO: measuring) */
	int minw;			/* measuring the narrowest a box goes (its longest word), not its widest */
	/* the last block laid out: its background and border items (-1:
	   none), for a row to stretch its cells to the row's height */
	long last_bg, last_bd[4];
	/* the table being laid out: per column, the rows a cell from above
	   still takes, and for the cell's first column where it began */
	struct span {
		int rows;
		int head;		/* the cell's first column */
		int32_t top, h;		/* its row's top and its height */
		long bg, bd[4], k0, k1;	/* its background, borders and items */
		int va;			/* its vertical-align */
	} *spans;
	/* positioned boxes' containing block: the nearest positioned
	   ancestor's padding box (the page at first) */
	int32_t cbx, cby, cbw;
	FLT *fl;			/* the floats placed; those of the context being laid out */
	int nfl, flcap, fl0;		/* from fl0 on */
	int32_t fl_top;			/* no float of this context goes higher than the last */
	int32_t *cols;			/* the table being laid out: its column widths */
	int ncols;
	int32_t sp_h, sp_v;		/* and its border-spacing */
} L;

static int32_t dev(L *l, ec_fx v)
{
	int64_t r = (int64_t) v * l->zoom;
	return (int32_t) ((r + (r >= 0 ? (1 << 19) : -(1 << 19))) >> 20);
}

static int kw(const ec_style *s, int p)
{
	return s->k[p] == EK_KW ? s->v[p].i : 0;
}

/* a length in device px: base is what a percentage is of (AUTO: none,
   so a percentage is auto too); AUTO for auto, none, normal */
static int32_t len(L *l, const ec_style *s, int p, int32_t base)
{
	switch (s->k[p]) {
	case EK_PX:
		return dev(l, s->v[p].i);
	case EK_PCT:
		if (base == AUTO)
			return AUTO;
		return (int32_t) ((int64_t) base * s->v[p].i / (100 * 1024));
	case EK_CALC: {
		const ec_calc *c = s->v[p].p;
		int32_t r = 0, v;
		int i;
		for (i = 0; i < c->n; i++) {
			v = dev(l, c->a[i].px);
			if (c->a[i].pct != 0 && base != AUTO)
				v += (int32_t) ((int64_t) base * c->a[i].pct / (100 * 1024));
			if (c->op == EC_CALC_SUM || i == 0)
				r = c->op == EC_CALC_SUM ? r + v : v;
			else if (c->op == EC_CALC_MIN)
				r = v < r ? v : r;
			else if (c->op == EC_CALC_MAX)
				r = v > r ? v : r;
			else if (i == 1)	/* clamp(min, val, max) */
				r = v > r ? v : r;
			else
				r = v < r ? v : r;
		}
		return r;
	}
	default:
		return AUTO;
	}
}

static int32_t len0(L *l, const ec_style *s, int p, int32_t base)
{
	int32_t v = len(l, s, p, base);
	return v == AUTO ? 0 : v;
}

static uint32_t color(const ec_style *s, int p)
{
	if (s->k[p] == EK_COLOR)
		return (uint32_t) s->v[p].i;
	if (s->k[p] == EK_CURCOLOR)
		return (uint32_t) s->v[EP_COLOR].i;
	return 0;
}

static int contains_ci(const char *s, size_t n, const char *w)
{
	size_t wl = strlen(w), i, j;
	for (i = 0; i + wl <= n; i++) {
		for (j = 0; j < wl; j++) {
			char c = s[i + j];
			if (c >= 'A' && c <= 'Z')
				c += 32;
			if (c != w[j])
				break;
		}
		if (j == wl)
			return 1;
	}
	return 0;
}

static int32_t font_px(L *l, const ec_style *s)
{
	int32_t px = s->k[EP_FONT_SIZE] == EK_PX ? dev(l, s->v[EP_FONT_SIZE].i) : dev(l, 16 << 10);
	return px < 6 ? 6 : px;
}

static ew_font *font_of(L *l, const ec_style *s)
{
	int fam = -1, sty = 0, i;
	const ec_family *f = s->k[EP_FONT_FAMILY] == EK_PTR ? s->v[EP_FONT_FAMILY].p : NULL;

	if (f != NULL) {
		for (i = 0; i < f->n && fam < 0; i++) {
			const char *nm = f->name[i]->s;
			size_t n = f->name[i]->len;
			if (contains_ci(nm, n, "mono") || contains_ci(nm, n, "courier") ||
			    contains_ci(nm, n, "consol") || contains_ci(nm, n, "menlo") ||
			    contains_ci(nm, n, "code") || contains_ci(nm, n, "typewriter"))
				fam = FF_MONO;
			else if (contains_ci(nm, n, "sans") || contains_ci(nm, n, "arial") ||
				 contains_ci(nm, n, "helvetica") || contains_ci(nm, n, "verdana") ||
				 contains_ci(nm, n, "roboto") || contains_ci(nm, n, "segoe") ||
				 contains_ci(nm, n, "system") || contains_ci(nm, n, "tahoma") ||
				 contains_ci(nm, n, "open") || contains_ci(nm, n, "lato") ||
				 contains_ci(nm, n, "inter") || contains_ci(nm, n, "noto"))
				fam = FF_SANS;
			else if (contains_ci(nm, n, "serif") || contains_ci(nm, n, "times") ||
				 contains_ci(nm, n, "georgia") || contains_ci(nm, n, "garamond") ||
				 contains_ci(nm, n, "palatino") || contains_ci(nm, n, "book") ||
				 contains_ci(nm, n, "cambria") || contains_ci(nm, n, "charter"))
				fam = FF_SERIF;
		}
		if (fam < 0) {
			if (f->generic == KW_MONOSPACE || f->generic == KW_UI_MONOSPACE)
				fam = FF_MONO;
			else if (f->generic == KW_SERIF || f->generic == KW_UI_SERIF)
				fam = FF_SERIF;
		}
	}
	if (fam < 0)
		fam = f == NULL ? FF_SERIF : FF_SANS;
	if (s->k[EP_FONT_WEIGHT] == EK_INT && s->v[EP_FONT_WEIGHT].i >= 600)
		sty |= FS_BOLD;
	if (kw(s, EP_FONT_STYLE) == KW_ITALIC || kw(s, EP_FONT_STYLE) == KW_OBLIQUE)
		sty |= FS_ITALIC;
	return font_get(fam, sty, font_px(l, s));
}

/* the line's height for a font in this style */
static int32_t line_height(L *l, const ec_style *s, ew_font *f)
{
	int32_t px = font_px(l, s);
	switch (s->k[EP_LINE_HEIGHT]) {
	case EK_NUM:
		return (int32_t) (((int64_t) px * s->v[EP_LINE_HEIGHT].i + 512) >> 10);
	case EK_PX:
		return dev(l, s->v[EP_LINE_HEIGHT].i);
	case EK_PCT:
		return (int32_t) ((int64_t) px * s->v[EP_LINE_HEIGHT].i / (100 * 1024));
	default: {
		int32_t h = (px * 6 + 4) / 5;		/* normal: 1.2 */
		(void) f;
		return h < px + 2 ? px + 2 : h;
	}
	}
}

/* ---------------------------------------------------------------- the display list */

static dl_item *dl_add(L *l)
{
	page *p = l->p;
	if (p->nitems == p->cap) {
		long cap = p->cap ? p->cap * 2 : 1024;
		dl_item *n = realloc(p->items, cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return NULL;
		}
		p->items = n;
		p->cap = cap;
	}
	memset(&p->items[p->nitems], 0, sizeof p->items[0]);
	return &p->items[p->nitems++];
}

static long fill(L *l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t argb)
{
	dl_item *d;
	if ((argb >> 24) == 0 || w <= 0 || h <= 0)
		return -1;
	d = dl_add(l);
	if (d == NULL)
		return -1;
	d->type = DL_FILL;
	d->x = x;
	d->y = y;
	d->w = w;
	d->h = h;
	d->rgb = argb & 0xffffff;
	return l->p->nitems - 1;
}

static void text(L *l, int32_t x, int32_t base, ITEM *it, const char *s, uint32_t n, int32_t w)
{
	dl_item *d = dl_add(l);
	if (d == NULL)
		return;
	d->type = DL_TEXT;
	d->x = x;
	d->y = base - it->font->ascent;
	d->w = w;
	d->h = it->font->ascent + it->font->descent;
	d->base = it->font->ascent;
	d->rgb = it->color & 0xffffff;
	d->flags = it->flags;
	d->link = it->link;
	d->font = it->font;
	d->text = s;
	d->len = n;
}

static uint16_t add_link(L *l, const char *href)
{
	page *p = l->p;
	if (p->nlinks == p->linkcap) {
		int cap = p->linkcap ? p->linkcap * 2 : 64;
		char **n = realloc(p->links, cap * sizeof *n);
		if (n == NULL)
			return 0;
		p->links = n;
		p->linkcap = cap;
	}
	if (p->nlinks >= 65534)
		return 0;
	p->links[p->nlinks] = astrdup(l->a, href, strlen(href));
	if (p->links[p->nlinks] == NULL)
		return 0;
	return (uint16_t) (++p->nlinks);
}

/* ---------------------------------------------------------------- building */

static box *new_box(L *l, int kind, dnode *n, const ec_style *st, box *parent)
{
	box *b = amalloc(l->a, sizeof *b);
	if (b == NULL) {
		l->fail = 1;
		return NULL;
	}
	memset(b, 0, sizeof *b);
	b->kind = (uint8_t) kind;
	b->n = n;
	b->st = st;
	b->aw = b->ah = AUTO;
	if (parent != NULL) {
		b->parent = parent;
		b->link = parent->link;
		if (parent->last != NULL)
			parent->last->next = b;
		else
			parent->first = b;
		parent->last = b;
	}
	return b;
}

static int block_level(int d)
{
	switch (d) {
	case KW_BLOCK: case KW_LIST_ITEM: case KW_TABLE: case KW_TABLE_ROW_GROUP:
	case KW_TABLE_HEADER_GROUP: case KW_TABLE_FOOTER_GROUP: case KW_TABLE_ROW:
	case KW_TABLE_CELL: case KW_TABLE_CAPTION: case KW_FLEX: case KW_GRID:
	case KW_FLOW_ROOT: case KW_RUN_IN:
		return 1;
	default:
		return 0;
	}
}

/* the text of an element's descendants, white space collapsed */
static char *text_of(L *l, dnode *n, size_t max)
{
	char *buf = amalloc(l->a, max + 1);
	size_t o = 0;
	dnode *c = n->first;
	int space = 0;
	if (buf == NULL)
		return NULL;
	while (c != NULL && c != n) {
		if (c->type == DN_TEXT) {
			uint32_t i;
			for (i = 0; i < c->len && o < max; i++) {
				char ch = c->text[i];
				if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
					space = o > 0;
					continue;
				}
				if (space && o < max)
					buf[o++] = ' ';
				space = 0;
				if (o < max)
					buf[o++] = ch;
			}
		}
		if (c->first != NULL && c->type == DN_ELEMENT && c->tag != T_SCRIPT && c->tag != T_STYLE) {
			c = c->first;
			continue;
		}
		while (c != n && c->next == NULL)
			c = c->parent;
		if (c != n)
			c = c->next;
	}
	buf[o] = 0;
	return buf;
}

static void add_text_box(L *l, box *parent, const ec_style *st, const char *s, uint32_t n)
{
	box *t = new_box(l, BX_TEXT, NULL, st, parent);
	if (t != NULL) {
		t->text = s;
		t->len = n;
	}
}

static void atom(L *l, box *parent, dnode *n, const ec_style *st, int kind, const char *label, int32_t w, int32_t h)
{
	box *b = new_box(l, BX_ATOM, n, st, parent);
	if (b == NULL)
		return;
	b->atom = (uint8_t) kind;
	b->text = label ? label : "";
	b->len = (uint32_t) strlen(b->text);
	b->aw = w;
	b->ah = h;
}

static int32_t char_w(L *l, const ec_style *st)
{
	return font_advance(font_of(l, st), '0');
}

/* form controls, drawn as boxes with their text (they don't work yet) */
static void control(L *l, box *parent, dnode *n, const ec_style *st)
{
	const char *type = dn_attr(n, "type"), *v;
	char buf[80];
	int size;

	if (n->tag == T_INPUT) {
		if (type == NULL)
			type = "text";
		if (strcmp(type, "hidden") == 0)
			return;
		if (strcmp(type, "checkbox") == 0) {
			atom(l, parent, n, st, AT_BUTTON, dn_attr(n, "checked") ? "\xe2\x9c\x93" : " ", AUTO, AUTO);
			return;
		}
		if (strcmp(type, "radio") == 0) {
			atom(l, parent, n, st, AT_LABEL, dn_attr(n, "checked") ? "\xe2\x97\x8f" : "\xe2\x97\x8b",
			     AUTO, AUTO);
			return;
		}
		if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0 ||
		    strcmp(type, "image") == 0) {
			v = dn_attr(n, "value");
			if (v == NULL)
				v = dn_attr(n, "alt");
			if (v == NULL)
				v = strcmp(type, "reset") == 0 ? "Reset" : "Submit";
			atom(l, parent, n, st, AT_BUTTON, v, AUTO, AUTO);
			return;
		}
		v = dn_attr(n, "value");
		if (v != NULL && strcmp(type, "password") == 0) {
			size_t i, k = strlen(v);
			if (k > sizeof buf - 1)
				k = sizeof buf - 1;
			for (i = 0; i < k; i++)
				buf[i] = '*';
			buf[k] = 0;
			v = astrdup(l->a, buf, k);
		}
		if (v == NULL || *v == 0)
			v = dn_attr(n, "placeholder");
		size = dn_attr(n, "size") ? atoi(dn_attr(n, "size")) : 20;
		if (size < 1 || size > 200)
			size = 20;
		atom(l, parent, n, st, AT_FIELD, v, size * char_w(l, st) + 6, AUTO);
		return;
	}
	if (n->tag == T_TEXTAREA) {
		int cols = dn_attr(n, "cols") ? atoi(dn_attr(n, "cols")) : 20;
		int rows = dn_attr(n, "rows") ? atoi(dn_attr(n, "rows")) : 2;
		if (cols < 1 || cols > 200)
			cols = 20;
		if (rows < 1 || rows > 50)
			rows = 2;
		atom(l, parent, n, st, AT_FIELD, text_of(l, n, 120), cols * char_w(l, st) + 6,
		     rows * line_height(l, st, font_of(l, st)) + 6);
		return;
	}
	if (n->tag == T_SELECT) {
		dnode *o, *pick = NULL;
		char *t;
		for (o = n->first; o != NULL; o = o->next) {
			dnode *k = o;
			if (o->type == DN_ELEMENT && o->first != NULL && o->tag != T_OPTION)
				k = o->first;	/* an optgroup's first */
			for (; k != NULL; k = k->next)
				if (k->type == DN_ELEMENT && k->tag == T_OPTION) {
					if (pick == NULL || dn_attr(k, "selected"))
						pick = k;
					if (dn_attr(k, "selected"))
						break;
				}
			if (pick != NULL && dn_attr(pick, "selected"))
				break;
			if (k != o)
				continue;
		}
		t = pick ? text_of(l, pick, 60) : NULL;
		if (t != NULL) {
			size_t k = strlen(t);
			char *u = amalloc(l->a, k + 5);
			if (u != NULL) {
				memcpy(u, t, k);
				memcpy(u + k, " \xe2\x96\xbc", 5);	/* ▼ */
				t = u;
			}
		}
		atom(l, parent, n, st, AT_BUTTON, t ? t : "\xe2\x96\xbc", AUTO, AUTO);
		return;
	}
	if (n->tag == T_BUTTON)
		atom(l, parent, n, st, AT_BUTTON, text_of(l, n, 80), AUTO, AUTO);
}

static void build(L *l, box *parent, dnode *n);

/* the picture an <img> shows: its src, or where a page that loads its
   pictures as they are scrolled to keeps the real one (data-src and the
   like) when src is missing or only a placeholder */
static const char *img_src(dnode *n)
{
	static const char *const lazy[] = { "data-src", "data-lazy-src", "data-original", "data-srcset" };
	const char *s = dn_attr(n, "src");
	size_t i;
	if (s != NULL && *s != 0 && strncmp(s, "data:", 5) != 0)
		return s;
	for (i = 0; i < sizeof lazy / sizeof lazy[0]; i++) {
		const char *v = dn_attr(n, lazy[i]);
		if (v != NULL && *v != 0 && strncmp(v, "data:", 5) != 0) {
			if (i == 3)
				return NULL;	/* a list: not taken apart yet */
			return v;
		}
	}
	return s != NULL && *s != 0 ? s : NULL;
}

static void pseudo(L *l, box *parent, dnode *n, const ec_style *ps)
{
	const ec_content *ct;
	char buf[256];
	size_t o = 0;
	int i;

	if (ps == NULL || ps->k[EP_CONTENT] != EK_PTR || kw(ps, EP_DISPLAY) == KW_NONE)
		return;
	ct = ps->v[EP_CONTENT].p;
	for (i = 0; i < ct->n; i++) {
		const char *s = NULL;
		if (ct->item[i].type == EC_CONTENT_STRING && ct->item[i].s != NULL)
			s = ct->item[i].s->s;
		else if (ct->item[i].type == EC_CONTENT_ATTR && ct->item[i].s != NULL)
			s = dn_attr(n, ct->item[i].s->s);
		else if (ct->item[i].type == EC_CONTENT_QUOTE)
			s = ct->item[i].kw == KW_OPEN_QUOTE ? "\xe2\x80\x9c" :
			    ct->item[i].kw == KW_CLOSE_QUOTE ? "\xe2\x80\x9d" : NULL;
		if (s != NULL) {
			size_t k = strlen(s);
			if (o + k >= sizeof buf)
				k = sizeof buf - 1 - o;
			memcpy(buf + o, s, k);
			o += k;
		}
	}
	if (o == 0)
		return;
	if (block_level(kw(ps, EP_DISPLAY))) {
		box *b = new_box(l, BX_BLOCK, NULL, ps, parent);
		if (b != NULL)
			add_text_box(l, b, ps, astrdup(l->a, buf, o), (uint32_t) o);
	} else {
		box *b = new_box(l, BX_INLINE, NULL, ps, parent);
		if (b != NULL)
			add_text_box(l, b, ps, astrdup(l->a, buf, o), (uint32_t) o);
	}
}

static void build_element(L *l, box *parent, dnode *c)
{
	const ec_style *st = c->e.style;
	int d;
	box *b;

	if (st == NULL)
		return;
	d = kw(st, EP_DISPLAY);
	if (d == KW_NONE || d == KW_TABLE_COLUMN || d == KW_TABLE_COLUMN_GROUP)
		return;
	if (c->ns != DN_NS_HTML && c->tag != T_SVG) {
		/* MathML and the like: its text, inline */
		build(l, parent, c);
		return;
	}
	switch (c->tag) {
	case T_SVG: case T_CANVAS: case T_VIDEO: case T_AUDIO: case T_IFRAME: case T_OBJECT:
	case T_EMBED: case T_TEMPLATE: case T_SCRIPT: case T_STYLE: case T_WBR:
		return;
	case T_BR:
		new_box(l, BX_BR, c, st, parent);
		return;
	case T_IMG: {
		const char *alt = dn_attr(c, "alt"), *src = img_src(c);
		void *h = NULL;
		int32_t iw = 0, ih = 0;
		if (src != NULL && l->p->opt.image != NULL)
			h = l->p->opt.image(l->p->opt.ud, src, &iw, &ih);
		if (h != NULL) {
			box *in = parent;
			if (block_level(d)) {
				/* floated or display: block: a block of its own round it */
				in = new_box(l, BX_BLOCK, c, st, parent);
				if (in == NULL)
					return;
				in->repl = 1;
				if (parent->link)
					in->link = parent->link;
			}
			atom(l, in, c, st, AT_IMAGE, alt, AUTO, AUTO);
			if (in->last != NULL && in->last->n == c) {
				in->last->img = h;
				in->last->iw = iw;
				in->last->ih = ih;
			}
		} else if (alt != NULL && *alt != 0) {
			atom(l, parent, c, st, AT_LABEL, alt, AUTO, AUTO);
		}
		return;
	}
	case T_INPUT: case T_TEXTAREA: case T_SELECT: case T_BUTTON:
		control(l, parent, c, st);
		return;
	default:
		break;
	}
	if (d == KW_CONTENTS) {
		build(l, parent, c);
		return;
	}
	b = new_box(l, block_level(d) ? BX_BLOCK : BX_INLINE, c, st, parent);
	if (b == NULL)
		return;
	if (c->tag == T_A && dn_attr(c, "href") != NULL)
		b->link = add_link(l, dn_attr(c, "href"));
	b->list = d == KW_LIST_ITEM;
	b->tablerow = d == KW_TABLE_ROW;
	b->table = d == KW_TABLE || d == KW_INLINE_TABLE;
	if (d == KW_TABLE_ROW)
		b->row = 1;
	else if ((d == KW_FLEX || d == KW_INLINE_FLEX) &&
		 (kw(st, EP_FLEX_DIRECTION) == KW_ROW || kw(st, EP_FLEX_DIRECTION) == KW_ROW_REVERSE)) {
		b->row = 1;
		b->wrap = kw(st, EP_FLEX_WRAP) == KW_WRAP || kw(st, EP_FLEX_WRAP) == KW_WRAP_REVERSE;
	}
	pseudo(l, b, c, st->before);
	build(l, b, c);
	pseudo(l, b, c, st->after);
}

static void build(L *l, box *parent, dnode *n)
{
	dnode *c;
	const ec_style *pst = n->type == DN_ELEMENT && n->e.style ? n->e.style : parent->st;
	for (c = n->first; c != NULL && !l->fail; c = c->next) {
		if (c->type == DN_TEXT) {
			if (c->len > 0)
				add_text_box(l, parent, pst, c->text, c->len);
		} else if (c->type == DN_ELEMENT) {
			build_element(l, parent, c);
		}
	}
}

/* ---------------------------------------------------------------- fixing up */

static int blank(const char *s, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++)
		if (!(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f'))
			return 0;
	return 1;
}

static int keeps_space(const ec_style *s)
{
	int w = kw(s, EP_WHITE_SPACE);
	return w == KW_PRE || w == KW_PRE_WRAP || w == KW_BREAK_SPACES;
}

static void fixup(L *l, box *b)
{
	box *c, *prev, *next;
	int has_block = 0, has_inline = 0;

	for (c = b->first; c != NULL; c = c->next) {
		if (c->first != NULL)
			fixup(l, c);
		if (c->kind == BX_INLINE) {
			box *k;
			for (k = c->first; k != NULL; k = k->next)
				if (k->kind == BX_BLOCK)
					c->kind = BX_BLOCK;
		}
	}
	if (b->kind != BX_BLOCK)
		return;
	for (c = b->first; c != NULL; c = c->next) {
		if (c->kind == BX_BLOCK)
			has_block = 1;
		else if (c->kind != BX_TEXT || !blank(c->text, c->len) || keeps_space(c->st))
			has_inline = 1;
	}
	if (!has_block && !b->row) {
		b->inl = 1;
		return;
	}
	if (!has_inline) {
		/* only blocks: white space between them goes */
		prev = NULL;
		for (c = b->first; c != NULL; c = next) {
			next = c->next;
			if (c->kind == BX_BLOCK) {
				prev = c;
				continue;
			}
			if (prev)
				prev->next = next;
			else
				b->first = next;
			if (b->last == c)
				b->last = prev;
		}
		return;
	}
	/* runs of inline boxes go in anonymous blocks */
	prev = NULL;
	for (c = b->first; c != NULL; c = next) {
		box *a, *k, *end;
		next = c->next;
		if (c->kind == BX_BLOCK) {
			prev = c;
			continue;
		}
		end = c;
		while (end->next != NULL && end->next->kind != BX_BLOCK)
			end = end->next;
		next = end->next;
		/* a run of nothing but collapsible white space: dropped */
		for (k = c; ; k = k->next) {
			if (k->kind != BX_TEXT || !blank(k->text, k->len) || keeps_space(k->st))
				break;
			if (k == end) {
				k = NULL;
				break;
			}
		}
		if (k == NULL) {
			if (prev)
				prev->next = next;
			else
				b->first = next;
			if (next == NULL)
				b->last = prev;
			continue;
		}
		a = amalloc(l->a, sizeof *a);
		if (a == NULL) {
			l->fail = 1;
			return;
		}
		memset(a, 0, sizeof *a);
		a->kind = BX_BLOCK;
		a->anon = 1;
		a->inl = 1;
		a->st = b->st;
		a->parent = b;
		a->link = b->link;
		a->aw = a->ah = AUTO;
		a->first = c;
		a->last = end;
		for (k = c; k != NULL; k = k->next) {
			k->parent = a;
			if (k == end)
				break;
		}
		end->next = NULL;
		a->next = next;
		if (prev)
			prev->next = a;
		else
			b->first = a;
		if (next == NULL)
			b->last = a;
		prev = a;
	}
}

/* ---------------------------------------------------------------- inline items */

static ITEM *item(L *l)
{
	if (l->nit == l->itcap) {
		long cap = l->itcap ? l->itcap * 2 : 256;
		ITEM *n = realloc(l->it, cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return NULL;
		}
		l->it = n;
		l->itcap = cap;
	}
	memset(&l->it[l->nit], 0, sizeof l->it[0]);
	return &l->it[l->nit++];
}

static int is_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* the direction of a word: 2 if it has Hebrew letters, 1 if it has
   other letters or digits, else 0 */
static int word_dir(const char *s, uint32_t n)
{
	const char *e = s + n;
	int ltr = 0;
	while (s < e) {
		uint32_t c = utf8_next(&s, e);
		if (c >= 0x590 && c <= 0x5FF)
			return 2;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    (c >= 0xC0 && c < 0x590))
			ltr = 1;
	}
	return ltr;
}

typedef struct {
	const ec_style *st;
	ew_font *font;
	uint32_t color, bg;
	uint8_t deco, hidden;
	uint16_t link;
	int32_t asc, desc, shift;
} RUNSTATE;

static void runstate(L *l, RUNSTATE *r, const ec_style *st, const RUNSTATE *parent, uint16_t link)
{
	int32_t lh, lead;
	int va;
	uint32_t bg;
	int d;

	r->st = st;
	r->font = font_of(l, st);
	r->color = color(st, EP_COLOR);
	r->link = link;
	r->deco = parent ? parent->deco : 0;
	r->bg = parent ? parent->bg : 0;
	r->shift = parent ? parent->shift : 0;
	r->hidden = kw(st, EP_VISIBILITY) == KW_HIDDEN || kw(st, EP_VISIBILITY) == KW_COLLAPSE;
	if (st->k[EP_TEXT_DECORATION_LINE] == EK_INT) {
		d = st->v[EP_TEXT_DECORATION_LINE].i;
		if (d & EC_DECO_UNDERLINE)
			r->deco |= DLF_UNDER;
		if (d & EC_DECO_OVERLINE)
			r->deco |= DLF_OVER;
		if (d & EC_DECO_LINE_THROUGH)
			r->deco |= DLF_STRIKE;
	}
	bg = color(st, EP_BACKGROUND_COLOR);
	if (parent != NULL && (bg >> 24) != 0)
		r->bg = bg;
	va = kw(st, EP_VERTICAL_ALIGN);
	if (parent != NULL) {
		if (va == KW_SUB)
			r->shift += r->font->px / 4;
		else if (va == KW_SUPER)
			r->shift -= r->font->px / 3;
	}
	lh = line_height(l, st, r->font);
	lead = lh - (r->font->ascent + r->font->descent);
	r->asc = r->font->ascent + lead / 2;
	r->desc = r->font->descent + (lead - lead / 2);
}

static void put_item(L *l, const RUNSTATE *r, int kind, const char *s, uint32_t n, int32_t w)
{
	ITEM *it = item(l);
	if (it == NULL)
		return;
	it->kind = (uint8_t) kind;
	it->font = r->font;
	it->color = r->color;
	it->bg = r->bg;
	it->flags = r->deco;
	it->link = r->link;
	it->hidden = r->hidden;
	it->asc = r->asc - r->shift;
	it->desc = r->desc + r->shift;
	it->shift = r->shift;
	it->text = s;
	it->len = n;
	it->w = w;
	if (kind == IT_WORD)
		it->dir = (uint8_t) word_dir(s, n);
}

/* text into words and spaces, collapsed as its white-space says, copied
   so that a node's words and spaces follow on (one run to draw) */
static void text_items(L *l, const RUNSTATE *r, const char *s, uint32_t n, int *line_start)
{
	int ws = kw(r->st, EP_WHITE_SPACE), tt = kw(r->st, EP_TEXT_TRANSFORM);
	int keep = ws == KW_PRE || ws == KW_PRE_WRAP || ws == KW_BREAK_SPACES;
	int newlines = keep || ws == KW_PRE_LINE;
	int wrap = !(ws == KW_PRE || ws == KW_NOWRAP);
	int32_t sw = font_advance(r->font, ' ');
	uint32_t i = 0, o = 0;
	char *t = amalloc(l->a, n + 1);

	if (t == NULL) {
		l->fail = 1;
		return;
	}
	while (i < n && !l->fail) {
		unsigned char c = (unsigned char) s[i];
		ITEM *it;
		if (c == '\n' && newlines) {
			put_item(l, r, IT_BREAK, NULL, 0, 0);
			*line_start = 1;
			i++;
			continue;
		}
		if (c == '\r' && newlines) {
			i++;
			continue;
		}
		if (is_space(c)) {
			if (keep) {
				t[o] = ' ';
				put_item(l, r, IT_SPACE, t + o, 1, c == '\t' ? 8 * sw : sw);
				o++;
				i++;
				it = &l->it[l->nit - 1];
				it->canbreak = (uint8_t) wrap;
				*line_start = 0;
				continue;
			}
			while (i < n && is_space((unsigned char) s[i]) && !(s[i] == '\n' && newlines))
				i++;
			if (!*line_start && !(l->nit > 0 && l->it[l->nit - 1].kind == IT_SPACE &&
					      l->it[l->nit - 1].collapse)) {
				t[o] = ' ';
				put_item(l, r, IT_SPACE, t + o, 1, sw);
				o++;
				it = &l->it[l->nit - 1];
				it->canbreak = (uint8_t) wrap;
				it->collapse = 1;
			}
			continue;
		}
		{
			uint32_t j = i, start = o;
			while (j < n && !is_space((unsigned char) s[j])) {
				char ch = s[j++];
				if (tt == KW_UPPERCASE && ch >= 'a' && ch <= 'z')
					ch -= 32;
				else if (tt == KW_LOWERCASE && ch >= 'A' && ch <= 'Z')
					ch += 32;
				t[o++] = ch;
			}
			put_item(l, r, IT_WORD, t + start, o - start, (int32_t) font_width(r->font, t + start, o - start));
			*line_start = 0;
			i = j;
		}
	}
	t[o] = 0;
}

/* a whole number from an attribute ("120", "120px"), 0 if none */
static int32_t attr_px(dnode *n, const char *name)
{
	const char *v = dn_attr(n, name);
	int32_t r = 0;
	if (v == NULL)
		return 0;
	while (*v == ' ')
		v++;
	while (*v >= '0' && *v <= '9' && r < 100000)
		r = r * 10 + (*v++ - '0');
	return *v == 0 || *v == 'p' || *v == ' ' ? r : 0;
}

/* a picture's size on the page, device px: CSS width and height (the
   width and height attributes are hints for them), else its own size,
   one from the other by its proportions (its file's, or the attributes'
   before it has come), within max-width and max-height. 0: not known
   until its file comes */
static int image_size(L *l, box *b, int32_t *w, int32_t *h)
{
	int32_t iw = b->iw, ih = b->ih, mw, mh;
	int guess;
	*w = len(l, b->st, EP_WIDTH, l->cw);
	*h = len(l, b->st, EP_HEIGHT, AUTO);
	/* a guess when its file's size is needed and not known yet */
	guess = (iw <= 0 || ih <= 0) && (*w == AUTO || *h == AUTO);
	if (iw <= 0 || ih <= 0) {
		/* the proportions the page gives, until the file says */
		iw = attr_px(b->n, "width");
		ih = attr_px(b->n, "height");
		if (iw <= 0 || ih <= 0)
			iw = ih = 0;
		else if (*w == AUTO && *h == AUTO)
			*w = dev(l, iw << 10), *h = dev(l, ih << 10);
	} else if (*w == AUTO && *h == AUTO) {
		*w = dev(l, iw << 10);
		*h = dev(l, ih << 10);
	}
	if (*w == AUTO && *h != AUTO)
		*w = iw > 0 ? (int32_t) ((int64_t) *h * iw / ih) : 0;
	else if (*h == AUTO && *w != AUTO)
		*h = iw > 0 ? (int32_t) ((int64_t) *w * ih / iw) : 0;
	if (*w == AUTO || *h == AUTO) {
		*w = *h = 0;
		return 1;
	}
	mw = len(l, b->st, EP_MAX_WIDTH, l->cw);
	if (mw != AUTO && *w > mw && mw >= 0) {
		if (iw > 0 && len(l, b->st, EP_HEIGHT, AUTO) == AUTO)
			*h = (int32_t) ((int64_t) *h * mw / *w);
		*w = mw;
	}
	mh = len(l, b->st, EP_MAX_HEIGHT, AUTO);
	if (mh != AUTO && *h > mh && mh >= 0) {
		if (iw > 0 && len(l, b->st, EP_WIDTH, l->cw) == AUTO)
			*w = (int32_t) ((int64_t) *w * mh / *h);
		*h = mh;
	}
	if (*w < 0)
		*w = 0;
	if (*h < 0)
		*h = 0;
	return guess;
}

static int32_t atom_w(L *l, box *b, ew_font *f)
{
	int32_t w = b->aw;
	if (b->atom == AT_IMAGE) {
		int32_t h;
		image_size(l, b, &w, &h);
		if (w > 0 && h > 0)
			return w;
		w = AUTO;		/* its alt text, until it comes */
	}
	if (w == AUTO)
		w = (int32_t) font_width(f, b->text, b->len) + (b->atom == AT_LABEL || b->atom == AT_IMAGE ? 0 : 8);
	return w;
}

static void collect(L *l, box *b, const RUNSTATE *pr, int *line_start)
{
	box *c;
	for (c = b->first; c != NULL && !l->fail; c = c->next) {
		RUNSTATE r;
		switch (c->kind) {
		case BX_TEXT:
			text_items(l, pr, c->text, c->len, line_start);
			break;
		case BX_BR:
			put_item(l, pr, IT_BREAK, NULL, 0, 0);
			*line_start = 1;
			break;
		case BX_ATOM: {
			ITEM *it;
			runstate(l, &r, c->st, pr, c->link ? c->link : pr->link);
			put_item(l, &r, IT_ATOM, c->text, c->len, atom_w(l, c, r.font));
			it = &l->it[l->nit - 1];
			it->atom = c;
			it->dir = (uint8_t) word_dir(c->text, c->len);
			if (c->atom == AT_IMAGE) {
				int32_t iw, ih;
				image_size(l, c, &iw, &ih);
				if (iw > 0 && ih > 0) {
					/* on the baseline, or its middle on the text's */
					int va = kw(c->st, EP_VERTICAL_ALIGN);
					it->asc = ih;
					it->desc = 0;
					if (va == KW_MIDDLE) {
						it->asc = ih / 2 + (r.font->ascent - r.font->descent) / 2;
						it->desc = ih - it->asc;
					} else if (va == KW_TEXT_BOTTOM || va == KW_BOTTOM) {
						it->asc = ih - r.font->descent;
						it->desc = r.font->descent;
					}
					if (it->desc < 0) {
						it->asc += it->desc;
						it->desc = 0;
					}
					it->dir = 0;
				}
			} else if (c->ah != AUTO || c->atom != AT_LABEL) {
				int32_t h = c->ah != AUTO ? c->ah : r.font->ascent + r.font->descent + 4;
				it->asc = h - 2 - r.font->descent;
				it->desc = r.font->descent + 2;
				if (it->asc < r.asc)
					it->asc = r.asc;
			}
			*line_start = 0;
			break;
		}
		case BX_INLINE: {
			int32_t gl, gr;
			runstate(l, &r, c->st, pr, c->link ? c->link : pr->link);
			gl = len0(l, c->st, EP_MARGIN_LEFT, AUTO) + len0(l, c->st, EP_PADDING_LEFT, AUTO) +
			     (kw(c->st, EP_BORDER_LEFT_STYLE) > KW_HIDDEN ? len0(l, c->st, EP_BORDER_LEFT_WIDTH, AUTO) : 0);
			gr = len0(l, c->st, EP_MARGIN_RIGHT, AUTO) + len0(l, c->st, EP_PADDING_RIGHT, AUTO) +
			     (kw(c->st, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN ? len0(l, c->st, EP_BORDER_RIGHT_WIDTH, AUTO) : 0);
			if (gl > 0) {
				put_item(l, &r, IT_GAP, NULL, 0, gl);
				*line_start = 0;
			}
			collect(l, c, &r, line_start);
			if (gr > 0)
				put_item(l, &r, IT_GAP, NULL, 0, gr);
			break;
		}
		default:
			break;
		}
	}
}

/* ---------------------------------------------------------------- floats */

/* what floats of the context leave of x0..x1 between y0 and y1; 1 if
   any float is there */
static int band(L *l, int32_t y0, int32_t y1, int32_t x0, int32_t x1, int32_t *lx, int32_t *rx)
{
	int i, any = 0;
	*lx = x0;
	*rx = x1;
	for (i = l->fl0; i < l->nfl; i++) {
		const FLT *f = &l->fl[i];
		if (f->y1 <= y0 || f->y0 >= y1 || f->x1 <= x0 || f->x0 >= x1)
			continue;
		any = 1;
		if (f->right) {
			if (f->x0 < *rx)
				*rx = f->x0;
		} else if (f->x1 > *lx) {
			*lx = f->x1;
		}
	}
	return any;
}

/* the first float bottom below y that is beside x0..x1 (AUTO: none) */
static int32_t next_bottom(L *l, int32_t y, int32_t x0, int32_t x1)
{
	int i;
	int32_t b = AUTO;
	for (i = l->fl0; i < l->nfl; i++) {
		const FLT *f = &l->fl[i];
		if (f->y1 > y && f->y0 <= y && f->x1 > x0 && f->x0 < x1 && (b == AUTO || f->y1 < b))
			b = f->y1;
	}
	return b;
}

/* the lowest bottom of the context's floats on a side (KW_LEFT, KW_RIGHT,
   KW_BOTH), or AUTO */
static int32_t floats_bottom(L *l, int side)
{
	int i;
	int32_t b = AUTO;
	for (i = l->fl0; i < l->nfl; i++) {
		const FLT *f = &l->fl[i];
		if ((side == KW_BOTH || (side == KW_RIGHT) == (f->right != 0)) && (b == AUTO || f->y1 > b))
			b = f->y1;
	}
	return b;
}

/* the display list from item k on moved by (dx, dy) */
static void shift_items(L *l, long k, int32_t dx, int32_t dy)
{
	for (; k < l->p->nitems; k++) {
		l->p->items[k].x += dx;
		l->p->items[k].y += dy;
	}
}

/* a box that starts a block formatting context of its own: its floats
   are its own, and it holds them */
static int own_context(const box *b)
{
	int d;
	if (b->parent == NULL || b->anon)
		return b->parent == NULL;
	if (b->parent->row || b->table || kw(b->st, EP_FLOAT) != KW_NONE || kw(b->st, EP_POSITION) == KW_ABSOLUTE ||
	    kw(b->st, EP_POSITION) == KW_FIXED)
		return 1;
	d = kw(b->st, EP_DISPLAY);
	if (d == KW_INLINE_BLOCK || d == KW_FLOW_ROOT || d == KW_TABLE_CELL || d == KW_FLEX || d == KW_GRID)
		return 1;
	return kw(b->st, EP_OVERFLOW_X) != KW_VISIBLE || kw(b->st, EP_OVERFLOW_Y) != KW_VISIBLE;
}

/* ---------------------------------------------------------------- lines */

static void reverse(long *o, uint8_t *d, long a, long b)
{
	while (a < b) {
		long t = o[a];
		uint8_t u = d[a];
		o[a] = o[b];
		d[a] = d[b];
		o[b] = t;
		d[b] = u;
		a++;
		b--;
	}
}

/* the items i..j-1 of a line in the order they are drawn (Hebrew words
   right to left): word-level bidi, enough for Hebrew mixed with Latin */
static void visual_order(L *l, long i, long j, int rtl, long *ord)
{
	long k, n = j - i, s, e;
	uint8_t *d = malloc(n ? 2 * n : 1), *strong;
	if (d == NULL) {
		for (k = 0; k < n; k++)
			ord[k] = i + k;
		return;
	}
	strong = d + n;
	for (k = 0; k < n; k++) {
		ITEM *it = &l->it[i + k];
		strong[k] = it->kind == IT_WORD || it->kind == IT_ATOM ? it->dir : 0;
		ord[k] = i + k;
	}
	/* a neutral between two of the same direction takes it; others the line's */
	for (k = 0; k < n; k++) {
		if (strong[k] != 0) {
			d[k] = strong[k];
			continue;
		}
		for (s = k - 1; s >= 0 && strong[s] == 0; s--)
			;
		for (e = k + 1; e < n && strong[e] == 0; e++)
			;
		d[k] = (uint8_t) (s >= 0 && e < n && strong[s] == strong[e] ? strong[s] : rtl ? 2 : 1);
	}
	if (rtl)
		reverse(ord, d, 0, n - 1);
	for (s = 0; s < n; s = e) {
		for (e = s; e < n && d[e] == d[s]; e++)
			;
		if (d[s] == (rtl ? 1 : 2))
			reverse(ord, d, s, e - 1);
	}
	free(d);
}

/* a picture's item in the display list */
static void image_item(L *l, box *b, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t link, ew_font *f,
		       int flags)
{
	dl_item *d = dl_add(l);
	if (d == NULL)
		return;
	d->type = DL_IMAGE;
	d->x = x;
	d->y = y;
	d->w = w;
	d->h = h;
	d->link = link;
	d->img = b->img;
	d->flags = (uint8_t) flags;
	d->text = b->text;	/* its alt text, while it comes */
	d->len = b->len;
	d->font = f;
	d->rgb = 0x606060;
}

static void draw_atom(L *l, ITEM *it, int32_t x, int32_t base)
{
	box *b = it->atom;
	int32_t h = it->asc + it->desc, y = base - it->asc + it->shift;
	if (b->atom == AT_IMAGE) {
		int32_t iw, ih;
		int guess = image_size(l, b, &iw, &ih), sized = iw > 0 && ih > 0;
		/* with no size yet: its alt text, drawn below, and its place for
		   the picture to be fetched (DLF_ALT: nothing drawn) */
		image_item(l, b, x, y, sized ? iw : it->w, sized ? ih : h, it->link, it->font,
			   (guess ? DLF_NOSIZE : 0) | (sized ? 0 : DLF_ALT));
		if (sized)
			return;
	}
	if (b->atom != AT_LABEL && b->atom != AT_IMAGE) {
		uint32_t edge = 0xff808080;
		fill(l, x, y, it->w, h, b->atom == AT_BUTTON ? 0xffe0e0e0 : 0xffffffff);
		fill(l, x, y, it->w, 1, edge);
		fill(l, x, y + h - 1, it->w, 1, edge);
		fill(l, x, y, 1, h, edge);
		fill(l, x + it->w - 1, y, 1, h, edge);
		if (b->len > 0) {
			ITEM t = *it;
			int32_t tw = (int32_t) font_width(it->font, b->text, b->len);
			int32_t tx = b->atom == AT_BUTTON ? x + (it->w - tw) / 2 : x + 3;
			if (b->atom == AT_FIELD && b->n && !dn_attr(b->n, "value") && b->n->tag == T_INPUT)
				t.color = 0xff808080;		/* a placeholder */
			if (tw > it->w - 6)
				tw = it->w - 6;
			text(l, tx, base, &t, b->text, b->len, tw);
		}
	} else {
		ITEM t = *it;
		t.color = 0xff606060;
		t.flags &= (uint8_t) ~(DLF_UNDER);
		text(l, x, base, &t, b->text, b->len, it->w);
	}
}

/* splits the word at items[k] so that its first part is at most w wide;
   returns 0 if not even one character fits (one goes anyway) */
static void split_word(L *l, long k, int32_t w)
{
	ITEM *it = &l->it[k], copy;
	const char *s = it->text, *e = s + it->len, *p = s, *last = s;
	int32_t x = 0;
	while (p < e) {
		const char *q = p;
		int32_t a = font_advance(it->font, utf8_next(&q, e));
		if (x + a > w && p > s)
			break;
		x += a;
		last = q;
		p = q;
	}
	if (last >= e)
		return;
	copy = *it;
	if (item(l) == NULL)
		return;
	it = &l->it[k];
	memmove(&l->it[k + 2], &l->it[k + 1], (l->nit - k - 2) * sizeof *it);
	it->len = (uint32_t) (last - s);
	it->w = x;
	l->it[k + 1] = copy;
	l->it[k + 1].text = last;
	l->it[k + 1].len = (uint32_t) (e - last);
	l->it[k + 1].w = (int32_t) font_width(copy.font, last, e - last);
}

/* lays the items out in lines from (x, y), w wide; returns the height,
   and the first baseline in *first */
static int32_t lines(L *l, const box *b, int32_t x, int32_t y, int32_t w, int32_t *first)
{
	const ec_style *st = b->st;
	ew_font *sf = font_of(l, st);
	int32_t lh = line_height(l, st, sf), lead = lh - (sf->ascent + sf->descent);
	int32_t strut_a = sf->ascent + lead / 2, strut_d = sf->descent + (lead - lead / 2);
	int ta = kw(st, EP_TEXT_ALIGN), rtl = kw(st, EP_DIRECTION) == KW_RTL;
	int32_t indent = len0(l, st, EP_TEXT_INDENT, w), cy = y;
	long i = 0, n = l->nit, *ord;
	int firstline = 1;

	ord = malloc((n ? n : 1) * sizeof *ord);
	if (ord == NULL) {
		l->fail = 1;
		return 0;
	}
	while (i < l->nit) {
		long j, k, last_break = -1, end;
		int32_t lw = 0, avail, asc = strut_a, desc = strut_d, off, base, lx = x, lwid = w;
		int any = 0, broke = 0, justify;

		/* the line's start: collapsible spaces go */
		while (i < l->nit && l->it[i].kind == IT_SPACE && l->it[i].collapse)
			i++;
		if (i >= l->nit)
			break;
		if (l->nfl > l->fl0) {
			/* beside the floats there: below them when not even the
			   first word fits */
			int32_t rx, need = l->it[i].w < w ? l->it[i].w : w, hgt, nb;
			int tries;
			for (tries = 0; tries < 64; tries++) {
				hgt = l->it[i].asc + l->it[i].desc > lh ? l->it[i].asc + l->it[i].desc : lh;
				if (!band(l, cy, cy + hgt, x, x + w, &lx, &rx) || rx - lx >= need)
					break;
				nb = next_bottom(l, cy, x, x + w);
				if (nb == AUTO || nb <= cy)
					break;
				cy = nb;
			}
			lwid = rx - lx;
		}
		avail = lwid - (firstline ? indent : 0);
		for (j = i; j < l->nit; j++) {
			ITEM *it = &l->it[j];
			if (it->kind == IT_BREAK) {
				broke = 1;
				break;
			}
			if (it->kind == IT_SPACE) {
				lw += it->w;
				if (it->canbreak)
					last_break = j;
				continue;
			}
			if (lw + it->w > avail && any) {
				/* at the last space; with none, before this word */
				if (last_break >= i)
					j = last_break + 1;
				break;
			}
			if (it->kind == IT_WORD && it->w > avail - lw && lw == 0) {
				split_word(l, j, avail);
				if (l->nit > n) {
					long *o2 = realloc(ord, l->nit * sizeof *ord);
					if (o2 == NULL) {
						l->fail = 1;
						free(ord);
						return 0;
					}
					ord = o2;
					n = l->nit;
				}
				it = &l->it[j];
			}
			lw += it->w;
			if (it->kind != IT_GAP)
				any = 1;
		}
		end = j;
		/* trailing spaces don't count */
		while (end > i && l->it[end - 1].kind == IT_SPACE && (l->it[end - 1].collapse || l->it[end - 1].canbreak))
			end--;
		lw = 0;
		for (k = i; k < end; k++) {
			ITEM *it = &l->it[k];
			lw += it->w;
			if (it->kind == IT_SPACE)
				continue;
			if (it->asc > asc)
				asc = it->asc;
			if (it->desc > desc)
				desc = it->desc;
		}
		avail = lwid - (firstline ? indent : 0);
		off = firstline ? indent : 0;
		if (ta == KW_RIGHT || (ta == KW_END && !rtl) || ((ta == KW_START || ta == KW_JUSTIFY || ta == 0) && rtl))
			off += avail - lw;
		else if (ta == KW_CENTER)
			off += (avail - lw) / 2;
		if (rtl && off < 0)
			off = 0;
		/* justified: the room left goes to the spaces, but on a line
		   that ends a paragraph or at a <br> */
		justify = ta == KW_JUSTIFY && !broke && j < l->nit && avail > lw;
		if (justify) {
			long ns = 0, si = 0;
			for (k = i; k < end; k++)
				ns += l->it[k].kind == IT_SPACE;
			if (ns == 0) {
				justify = 0;
			} else {
				int32_t room = avail - lw;
				for (k = i; k < end; k++)
					if (l->it[k].kind == IT_SPACE) {
						l->it[k].w += room / ns + (si < room % ns ? 1 : 0);
						si++;
					}
			}
		}
		base = cy + asc;
		if (*first == AUTO)
			*first = base;
		visual_order(l, i, end, rtl, ord);
		/* positions, backgrounds, then the text */
		{
			int32_t px = lx + off;
			for (k = 0; k < end - i; k++) {
				ITEM *it = &l->it[ord[k]];
				it->x = px;
				px += it->w;
			}
			for (k = 0; k < end - i; k++) {
				ITEM *it = &l->it[ord[k]];
				if (it->bg && !it->hidden && it->kind != IT_BREAK) {
					/* a run of one background: one fill */
					int32_t x0 = it->x, x1 = it->x + it->w;
					while (k + 1 < end - i && l->it[ord[k + 1]].bg == it->bg) {
						k++;
						x1 = l->it[ord[k]].x + l->it[ord[k]].w;
					}
					fill(l, x0, base - asc, x1 - x0, asc + desc, it->bg);
				}
			}
			for (k = 0; k < end - i; k++) {
				ITEM *it = &l->it[ord[k]];
				if (it->hidden)
					continue;
				if (it->kind == IT_ATOM) {
					draw_atom(l, it, it->x, base);
				} else if (it->kind == IT_WORD && it->dir == 2) {
					ITEM t = *it;
					t.flags |= DLF_RTL;
					text(l, it->x, base + it->shift, &t, it->text, it->len, it->w);
				} else if (it->kind == IT_WORD || (it->kind == IT_SPACE && it->flags)) {
					/* one run for words and spaces that follow on in the text */
					const char *s = it->text;
					uint32_t len = it->len;
					int32_t ww = it->w;
					while (k + 1 < end - i) {
						ITEM *nx = &l->it[ord[k + 1]];
						if ((nx->kind != IT_WORD && nx->kind != IT_SPACE) || nx->dir == 2 || justify ||
						    nx->text != s + len || nx->font != it->font || nx->color != it->color ||
						    nx->flags != it->flags || nx->link != it->link || nx->hidden ||
						    nx->shift != it->shift || ord[k + 1] != ord[k] + 1)
							break;
						len += nx->len;
						ww += nx->w;
						k++;
					}
					text(l, it->x, base + it->shift, it, s, len, ww);
				}
			}
		}
		cy += asc + desc;
		firstline = 0;
		i = broke ? j + 1 : j;
	}
	free(ord);
	return cy - y;
}

static int32_t inline_layout(L *l, box *b, int32_t x, int32_t y, int32_t w, int32_t *first)
{
	RUNSTATE r;
	int line_start = 1;
	int32_t h;
	l->nit = 0;
	l->cw = w;
	runstate(l, &r, b->st, NULL, b->link);
	collect(l, b, &r, &line_start);
	if (l->fail)
		return 0;
	h = lines(l, b, x, y, w, first);
	return h;
}

/* a picture as a block of its own (floated, display: block): no line
   round it; with no size yet, its alt text on a line */
static int32_t repl_layout(L *l, box *b, int32_t x, int32_t y, int32_t w)
{
	box *a = b->first;
	int32_t iw, ih, fb = AUTO;
	int guess;
	l->cw = w;
	guess = image_size(l, a, &iw, &ih);
	if (iw <= 0 || ih <= 0)
		return inline_layout(l, b, x, y, w, &fb);
	image_item(l, a, x, y, iw, ih, b->link, font_of(l, b->st), guess ? DLF_NOSIZE : 0);
	return ih;
}

/* ---------------------------------------------------------------- measuring */

static int32_t measure(L *l, box *b);
static int columns(L *l, box *t, int32_t w, int32_t *cols);

/* the widest line of a block of text, unbroken */
static int32_t inline_measure(L *l, box *b)
{
	RUNSTATE r;
	int line_start = 1;
	long i;
	int32_t lw = 0, best = 0;
	l->nit = 0;
	l->cw = AUTO;
	runstate(l, &r, b->st, NULL, b->link);
	collect(l, b, &r, &line_start);
	if (l->minw && !keeps_space(b->st) && kw(b->st, EP_WHITE_SPACE) != KW_NOWRAP) {
		/* the narrowest: its widest word or picture */
		for (i = 0; i < l->nit; i++)
			if (l->it[i].kind != IT_SPACE && l->it[i].kind != IT_BREAK && l->it[i].w > best)
				best = l->it[i].w;
		return best;
	}
	for (i = 0; i < l->nit; i++) {
		if (l->it[i].kind == IT_BREAK) {
			if (lw > best)
				best = lw;
			lw = 0;
		} else {
			lw += l->it[i].w;
		}
	}
	return lw > best ? lw : best;
}

static int32_t extras(L *l, const box *b)
{
	const ec_style *s = b->st;
	if (b->anon)
		return 0;
	return len0(l, s, EP_MARGIN_LEFT, AUTO) + len0(l, s, EP_MARGIN_RIGHT, AUTO) +
	       len0(l, s, EP_PADDING_LEFT, AUTO) + len0(l, s, EP_PADDING_RIGHT, AUTO) +
	       (kw(s, EP_BORDER_LEFT_STYLE) > KW_HIDDEN ? len0(l, s, EP_BORDER_LEFT_WIDTH, AUTO) : 0) +
	       (kw(s, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN ? len0(l, s, EP_BORDER_RIGHT_WIDTH, AUTO) : 0);
}

/* a block's widest: its border box with margins, nothing broken */
static int32_t measure(L *l, box *b)
{
	int32_t w = AUTO, inner = 0, mx;
	box *c;
	if (!b->anon)
		w = len(l, b->st, EP_WIDTH, AUTO);
	if (w != AUTO) {
		if (kw(b->st, EP_BOX_SIZING) == KW_BORDER_BOX)
			return w + len0(l, b->st, EP_MARGIN_LEFT, AUTO) + len0(l, b->st, EP_MARGIN_RIGHT, AUTO);
		return w + extras(l, b);
	}
	if (b->repl) {
		int32_t ih;
		l->cw = AUTO;
		image_size(l, b->first, &inner, &ih);
		if (inner <= 0)
			inner = inline_measure(l, b);
	} else if (b->inl) {
		inner = inline_measure(l, b);
	} else if (b->table) {
		/* its columns at their widest, and the spacing between them */
		int32_t cols[MAXCOLS];
		const ec_style *s = b->st;
		int n = columns(l, b, AUTO, cols), i;
		int32_t sp = s->k[EP_BORDER_SPACING_H] == EK_PX ? dev(l, s->v[EP_BORDER_SPACING_H].i) : 0;
		inner = sp * (n + 1);
		for (i = 0; i < n; i++)
			inner += cols[i];
	} else {
		for (c = b->first; c != NULL; c = c->next) {
			int32_t m = measure(l, c);
			if (b->row)
				inner += m;
			else if (m > inner)
				inner = m;
		}
	}
	if (!b->anon) {
		mx = len(l, b->st, EP_MAX_WIDTH, AUTO);
		if (mx != AUTO && inner > mx)
			inner = mx;
	}
	return inner + extras(l, b);
}

/* ---------------------------------------------------------------- blocks */

static int32_t collapse(int32_t a, int32_t b)
{
	if (a >= 0 && b >= 0)
		return a > b ? a : b;
	if (a < 0 && b < 0)
		return a < b ? a : b;
	return a + b;
}

static int32_t layout_block(L *l, box *b, int32_t x, int32_t y, int32_t cbw, int fixed_w,
			    int32_t *mb_out, int32_t *first);

static void marker(L *l, box *b, int32_t x, int32_t cw, int32_t base)
{
	const ec_style *st = b->st;
	int t = kw(st, EP_LIST_STYLE_TYPE);
	char buf[24];
	const char *s = buf;
	ITEM it;
	RUNSTATE r;
	int32_t w;
	int n = b->num;

	switch (t) {
	case KW_NONE: return;
	case KW_CIRCLE: s = "\xe2\x97\xa6"; break;	/* ◦ */
	case KW_SQUARE: s = "\xe2\x96\xaa"; break;	/* ▪ */
	case KW_DECIMAL: case KW_DECIMAL_LEADING_ZERO: case KW_HEBREW: case KW_ARMENIAN:
	case KW_GEORGIAN: case KW_LOWER_GREEK: {
		char d[12];
		int k = 0, o = 0;
		unsigned v = n < 0 ? (unsigned) -n : (unsigned) n;
		if (n < 0)
			buf[o++] = '-';
		do {
			d[k++] = (char) ('0' + v % 10);
			v /= 10;
		} while (v);
		if (t == KW_DECIMAL_LEADING_ZERO && k == 1 && n >= 0)
			buf[o++] = '0';
		while (k)
			buf[o++] = d[--k];
		buf[o++] = '.';
		buf[o] = 0;
		break;
	}
	case KW_LOWER_ALPHA: case KW_LOWER_LATIN: case KW_UPPER_ALPHA: case KW_UPPER_LATIN: {
		char d[8];
		int k = 0, o = 0, base = t == KW_UPPER_ALPHA || t == KW_UPPER_LATIN ? 'A' : 'a';
		unsigned v = n > 0 ? (unsigned) n : 1;
		while (v > 0 && k < 7) {
			v--;
			d[k++] = (char) (base + v % 26);
			v /= 26;
		}
		while (k)
			buf[o++] = d[--k];
		buf[o++] = '.';
		buf[o] = 0;
		break;
	}
	case KW_LOWER_ROMAN: case KW_UPPER_ROMAN: {
		static const char *const r1[] = { "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" };
		static const char *const r10[] = { "", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc" };
		static const char *const r100[] = { "", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm" };
		unsigned v = n > 0 && n < 4000 ? (unsigned) n : 1, i;
		buf[0] = 0;
		for (i = 0; i < v / 1000; i++)
			strcat(buf, "m");
		strcat(buf, r100[v / 100 % 10]);
		strcat(buf, r10[v / 10 % 10]);
		strcat(buf, r1[v % 10]);
		if (t == KW_UPPER_ROMAN)
			for (i = 0; buf[i]; i++)
				buf[i] = (char) (buf[i] - 32);
		strcat(buf, ".");
		break;
	}
	default: s = "\xe2\x80\xa2"; break;		/* • */
	}
	runstate(l, &r, st, NULL, 0);
	memset(&it, 0, sizeof it);
	it.font = r.font;
	it.color = r.color;
	s = astrdup(l->a, s, strlen(s));
	if (s == NULL)
		return;
	w = (int32_t) font_width(r.font, s, strlen(s));
	if (kw(st, EP_DIRECTION) == KW_RTL)
		text(l, x + cw + r.font->px / 2, base, &it, s, (uint32_t) strlen(s), w);
	else
		text(l, x - w - r.font->px / 2, base, &it, s, (uint32_t) strlen(s), w);
}

static int colspan(const box *c)
{
	const char *v = c->n != NULL ? dn_attr(c->n, "colspan") : NULL;
	int n = v != NULL ? atoi(v) : 1;
	return n < 1 ? 1 : n > MAXCOLS ? MAXCOLS : n;
}

/* a cell's widest, or the width it asks for */
static int32_t cell_pref(L *l, box *c, int32_t w)
{
	int32_t cw = c->anon ? AUTO : len(l, c->st, EP_WIDTH, w);
	if (cw == AUTO)
		return measure(l, c);
	if (kw(c->st, EP_BOX_SIZING) == KW_BORDER_BOX)
		return cw + len0(l, c->st, EP_MARGIN_LEFT, w) + len0(l, c->st, EP_MARGIN_RIGHT, w);
	return cw + extras(l, c);
}

static int rowspan(const box *c);

/* a row's cells' widths into their columns; occ: per column, the rows a
   cell from a row above still takes (as row_layout skips them) */
static void col_row(L *l, box *r, int32_t w, int32_t *cols, int *n, int *occ)
{
	box *c;
	int i = 0, k, taken[MAXCOLS];
	for (k = 0; k < MAXCOLS; k++)
		taken[k] = occ[k] > 0;
	for (c = r->first; c != NULL && i < MAXCOLS; c = c->next) {
		int span = colspan(c), rs = rowspan(c);
		while (i < MAXCOLS && taken[i])
			i++;
		if (i >= MAXCOLS)
			break;
		if (span == 1) {
			int32_t p = cell_pref(l, c, w);
			if (p > cols[i])
				cols[i] = p;
		}
		for (k = i; k < i + span && k < MAXCOLS; k++)
			if (rs > 1)
				occ[k] = rs;
		i += span;
	}
	for (k = 0; k < MAXCOLS; k++)
		if (occ[k] > 0)
			occ[k]--;
	if (i > MAXCOLS)
		i = MAXCOLS;
	if (i > *n)
		*n = i;
}

/* a table's columns at their widest; returns their number */
static int columns(L *l, box *t, int32_t w, int32_t *cols)
{
	box *c, *r;
	int n = 0, occ[MAXCOLS];
	memset(cols, 0, MAXCOLS * sizeof *cols);
	memset(occ, 0, sizeof occ);
	for (c = t->first; c != NULL; c = c->next) {
		if (c->tablerow)
			col_row(l, c, w, cols, &n, occ);
		else if (!c->row)
			for (r = c->first; r != NULL; r = r->next)
				if (r->tablerow)
					col_row(l, r, w, cols, &n, occ);
	}
	return n;
}

/* fits the columns into w: shares out what is over or under. Too wide,
   each keeps the narrowest it goes (mins: its longest word) and the rest
   is shared by how much more each would take */
static void fit_columns(int32_t *cols, const int32_t *mins, int n, int32_t w, int grow)
{
	int32_t total = 0, given = 0, tmin = 0;
	int i;
	for (i = 0; i < n; i++) {
		if (cols[i] < 1)
			cols[i] = 1;
		total += cols[i];
		tmin += mins[i] < cols[i] ? mins[i] : cols[i];
	}
	if (total == w || n == 0 || (total < w && !grow))
		return;
	if (total > w && tmin < w) {
		int32_t room = w - tmin, extra = total - tmin;
		for (i = 0; i < n; i++) {
			int32_t m = mins[i] < cols[i] ? mins[i] : cols[i];
			int32_t k = m + (extra > 0 ? (int32_t) ((int64_t) (cols[i] - m) * room / extra) : 0);
			cols[i] = k < 1 ? 1 : k;
			given += cols[i];
		}
		if (w - given > 0)
			cols[n - 1] += w - given;
		return;
	}
	for (i = 0; i < n; i++) {
		int32_t k = (int32_t) ((int64_t) cols[i] * w / total);
		if (k < 1)
			k = 1;
		cols[i] = k;
		given += k;
	}
	if (w - given > 0)
		cols[n - 1] += w - given;
}

/* a block's background and borders (as layout_block left them in
   last_bg, last_bd) made dh taller */
static void stretch(L *l, long bg, const long *bd, int32_t dh)
{
	dl_item *it = l->p->items;
	if (dh <= 0)
		return;
	if (bg >= 0)
		it[bg].h += dh;
	if (bd[1] >= 0)
		it[bd[1]].y += dh;
	if (bd[2] >= 0)
		it[bd[2]].h += dh;
	if (bd[3] >= 0)
		it[bd[3]].h += dh;
}

/* a cell's content (its items from k0 to k1, but its background and
   borders) moved down dy: vertical-align middle or bottom */
static void cell_shift(L *l, long k0, long k1, long bg, const long *bd, int32_t dy)
{
	long k;
	int j;
	if (dy <= 0)
		return;
	for (k = k0; k < k1; k++) {
		if (k == bg)
			continue;
		for (j = 0; j < 4 && bd[j] != k; j++)
			;
		if (j == 4)
			l->p->items[k].y += dy;
	}
}

static int rowspan(const box *c)
{
	const char *v = c->n != NULL ? dn_attr(c->n, "rowspan") : NULL;
	int n = v != NULL ? atoi(v) : 1;
	return n < 1 ? 1 : n > 1000 ? 1000 : n;
}

/* children side by side: a table row's cells, a flex row's items */
static int32_t row_layout(L *l, box *b, int32_t x, int32_t y, int32_t w, int32_t *first)
{
	box *c;
	int n = 0, i, col = 0;
	int32_t *pref, total = 0, h = 0, cx, lineh = 0, cy = y, *cols = b->tablerow ? l->cols : NULL;
	int32_t colx[MAXCOLS + 1];
	box **kids;
	int *start = NULL, above[MAXCOLS];
	long *k0 = NULL, *k1 = NULL, *kbg = NULL, (*kbd)[4] = NULL;
	int32_t *hh = NULL;

	for (c = b->first; c != NULL; c = c->next)
		n++;
	if (n == 0)
		return 0;
	pref = malloc(n * sizeof *pref);
	kids = malloc(n * sizeof *kids);
	start = malloc(n * sizeof *start);
	k0 = malloc(n * sizeof *k0);
	k1 = malloc(n * sizeof *k1);
	kbg = malloc(n * sizeof *kbg);
	kbd = malloc(n * sizeof *kbd);
	hh = malloc(n * sizeof *hh);
	if (pref == NULL || kids == NULL || start == NULL || k0 == NULL || k1 == NULL || kbg == NULL || kbd == NULL ||
	    hh == NULL) {
		free(pref);
		free(kids);
		free(start);
		free(k0);
		free(k1);
		free(kbg);
		free(kbd);
		free(hh);
		l->fail = 1;
		return 0;
	}
	if (cols != NULL) {
		/* where each column starts; which are taken by a cell from a row above */
		colx[0] = x + l->sp_h;
		for (i = 0; i < l->ncols; i++)
			colx[i + 1] = colx[i] + cols[i] + l->sp_h;
		for (i = 0; i < MAXCOLS; i++)
			above[i] = l->spans != NULL && l->spans[i].rows > 0;
	}
	for (c = b->first, i = 0; c != NULL; c = c->next, i++) {
		kids[i] = c;
		start[i] = 0;
		if (cols != NULL) {
			/* a table's row: its cells take the table's columns, past
			   those a cell from above still takes */
			int span = colspan(c), k;
			while (col < l->ncols && above[col])
				col++;
			start[i] = col;
			pref[i] = 0;
			for (k = 0; k < span && col < l->ncols; k++)
				pref[i] += cols[col++] + (k > 0 ? l->sp_h : 0);
		} else {
			pref[i] = cell_pref(l, c, w);
		}
		if (pref[i] < 1)
			pref[i] = 1;
		total += pref[i];
	}
	if (cols != NULL) {
		/* the columns already fit the table */
	} else if (!b->wrap) {
		if (total > w) {
			/* too wide: each gives up a share, none below a little */
			int32_t minw = w / (n * 2) > 24 ? 24 : w / (n * 2);
			for (i = 0; i < n; i++) {
				pref[i] = (int32_t) ((int64_t) pref[i] * w / total);
				if (pref[i] < minw)
					pref[i] = minw;
			}
		} else if (b->tablerow && total < w && n > 0) {
			/* a table row fills its width: the room goes to its cells by their size */
			int32_t room = w - total, given = 0;
			for (i = 0; i < n; i++) {
				int32_t add = (int32_t) ((int64_t) room * pref[i] / total);
				pref[i] += add;
				given += add;
			}
			pref[n - 1] += room - given;
		} else if (!b->tablerow) {
			/* flex-grow shares the room */
			int64_t grow = 0;
			for (i = 0; i < n; i++)
				if (!kids[i]->anon && kids[i]->st->k[EP_FLEX_GROW] == EK_NUM)
					grow += kids[i]->st->v[EP_FLEX_GROW].i;
			if (grow > 0) {
				int32_t room = w - total;
				for (i = 0; i < n; i++)
					if (!kids[i]->anon && kids[i]->st->k[EP_FLEX_GROW] == EK_NUM)
						pref[i] += (int32_t) (room * kids[i]->st->v[EP_FLEX_GROW].i / grow);
			}
		}
	}
	cx = x;
	if (cols != NULL) {
		cx += l->sp_h;
		cy += l->sp_v;
	}
	for (i = 0; i < n; i++) {
		int32_t mb, ch, f = AUTO, pw = pref[i] > w ? w : pref[i];
		if (cols != NULL && start[i] < l->ncols)
			cx = colx[start[i]];
		if (b->wrap && cx > x && cx + pw > x + w) {
			cy += lineh;
			lineh = 0;
			cx = x;
		}
		k0[i] = l->p->nitems;
		ch = layout_block(l, kids[i], cx, cy, pw, 1, &mb, &f);
		k1[i] = l->p->nitems;
		kbg[i] = l->last_bg;
		memcpy(kbd[i], l->last_bd, sizeof kbd[i]);
		hh[i] = ch;
		if (*first == AUTO && f != AUTO)
			*first = f;
		/* a cell taking rows below doesn't set this row's height */
		if (ch > lineh && !(cols != NULL && rowspan(kids[i]) > 1 && l->spans != NULL))
			lineh = ch;
		cx += pw + (cols != NULL ? l->sp_h : 0);
	}
	if (cols != NULL && l->spans != NULL) {
		int k;
		/* the cells from above that end in this row: the row as tall as
		   they need, and they as tall as their rows */
		for (k = 0; k < l->ncols; k++) {
			struct span *sp = &l->spans[k];
			if (!above[k] || --sp->rows > 0 || sp->head != k)
				continue;
			if (sp->top + sp->h > cy + lineh)
				lineh = sp->top + sp->h - cy;
		}
		for (k = 0; k < l->ncols; k++) {
			struct span *sp = &l->spans[k];
			int32_t dh = cy + lineh - sp->top - sp->h;
			if (!above[k] || sp->rows != 0 || sp->head != k)
				continue;
			stretch(l, sp->bg, sp->bd, dh);
			if (sp->va == KW_MIDDLE || sp->va == KW_BOTTOM)
				cell_shift(l, sp->k0, sp->k1, sp->bg, sp->bd, sp->va == KW_MIDDLE ? dh / 2 : dh);
		}
		/* this row's cells that take rows below */
		for (i = 0; i < n; i++) {
			int rs = rowspan(kids[i]), span = colspan(kids[i]), j;
			if (rs < 2)
				continue;
			for (j = start[i]; j < start[i] + span && j < l->ncols; j++) {
				struct span *sp = &l->spans[j];
				sp->rows = rs - 1;
				sp->head = start[i];
				sp->top = cy;
				sp->h = hh[i];
				sp->bg = kbg[i];
				memcpy(sp->bd, kbd[i], sizeof sp->bd);
				sp->k0 = k0[i];
				sp->k1 = k1[i];
				sp->va = kids[i]->anon ? 0 : kw(kids[i]->st, EP_VERTICAL_ALIGN);
			}
		}
	}
	/* each cell as tall as its row (a flex row's items too: they
	   stretch), its content where vertical-align puts it */
	if (!b->wrap) {
		for (i = 0; i < n; i++) {
			int32_t dh = lineh - hh[i];
			int va;
			if (cols != NULL && rowspan(kids[i]) > 1 && l->spans != NULL)
				continue;
			stretch(l, kbg[i], kbd[i], dh);
			va = kids[i]->anon ? 0 : kw(kids[i]->st, EP_VERTICAL_ALIGN);
			if (b->tablerow && dh > 0 && (va == KW_MIDDLE || va == KW_BOTTOM))
				cell_shift(l, k0[i], k1[i], kbg[i], kbd[i], va == KW_MIDDLE ? dh / 2 : dh);
		}
	}
	h = cy + lineh - y;
	free(pref);
	free(kids);
	free(start);
	free(k0);
	free(k1);
	free(kbg);
	free(kbd);
	free(hh);
	return h;
}

/* a float of the block being laid out (its content x..x+w), its top no
   higher than y: laid out at its width (asked for, or its widest within
   w), then put as far left or right as it goes where it fits beside the
   floats already there, else below them */
static void place_float(L *l, box *c, int32_t x, int32_t w, int32_t y)
{
	int right = kw(c->st, EP_FLOAT) == KW_RIGHT, clr = kw(c->st, EP_CLEAR), tries;
	int32_t wf = cell_pref(l, c, w), hf, mb, f = AUTO, fy, lx = x, rx = x + w, nb;
	long k0 = l->p->nitems;
	FLT *fl;
	if (wf > w)
		wf = w;
	if (wf < 1)
		wf = 1;
	hf = layout_block(l, c, 0, 0, wf, 1, &mb, &f);
	if (l->fail)
		return;
	fy = y > l->fl_top ? y : l->fl_top;
	if (clr == KW_LEFT || clr == KW_RIGHT || clr == KW_BOTH) {
		int32_t cb = floats_bottom(l, clr);
		if (cb != AUTO && cb > fy)
			fy = cb;
	}
	for (tries = 0; tries < 64; tries++) {
		if (!band(l, fy, fy + (hf > 0 ? hf : 1), x, x + w, &lx, &rx) || rx - lx >= wf)
			break;
		nb = next_bottom(l, fy, x, x + w);
		if (nb == AUTO || nb <= fy)
			break;
		fy = nb;
	}
	if (rx - lx < wf) {
		lx = x;
		rx = x + w;
	}
	shift_items(l, k0, right ? rx - wf : lx, fy);
	if (c->n != NULL)
		c->n->y += fy;
	if (l->nfl == l->flcap) {
		int cap = l->flcap ? l->flcap * 2 : 32;
		FLT *n2 = realloc(l->fl, cap * sizeof *n2);
		if (n2 == NULL) {
			l->fail = 1;
			return;
		}
		l->fl = n2;
		l->flcap = cap;
	}
	fl = &l->fl[l->nfl++];
	fl->x0 = right ? rx - wf : lx;
	fl->x1 = fl->x0 + wf;
	fl->y0 = fy;
	fl->y1 = fy + hf;
	fl->right = (uint8_t) right;
	l->fl_top = fy;
}

/* an absolutely positioned box (or fixed: the page is its containing
   block, as EWeb has no viewport of its own): out of the flow, where
   left, right and top put it in its containing block, else where it
   would have been (sx, sy) */
static void place_abs(L *l, box *c, int32_t sx, int32_t sy)
{
	int fixed = kw(c->st, EP_POSITION) == KW_FIXED;
	int32_t cbx = fixed ? 0 : l->cbx, cby = fixed ? 0 : l->cby, cbw = fixed ? l->p->width : l->cbw;
	int32_t left = len(l, c->st, EP_LEFT, cbw), right = len(l, c->st, EP_RIGHT, cbw);
	int32_t top = len(l, c->st, EP_TOP, AUTO), wf, mb, f = AUTO, ax, ay;
	long k0 = l->p->nitems;
	if (len(l, c->st, EP_WIDTH, cbw) == AUTO && left != AUTO && right != AUTO)
		wf = cbw - left - right;	/* stretched between its sides */
	else
		wf = cell_pref(l, c, cbw);
	if (wf > cbw)
		wf = cbw;
	if (wf < 1)
		wf = 1;
	layout_block(l, c, 0, 0, wf, 1, &mb, &f);
	if (l->fail)
		return;
	ax = left != AUTO ? cbx + left : right != AUTO ? cbx + cbw - right - wf : sx;
	ay = top != AUTO ? cby + top : sy;
	{
		/* text for screen readers only: a box of a pixel that hides
		   what overflows it, or put far off the page. EWeb doesn't clip,
		   so it would be drawn over the page: it goes */
		int32_t cw = len(l, c->st, EP_WIDTH, cbw), chh = len(l, c->st, EP_HEIGHT, AUTO);
		int hides = kw(c->st, EP_OVERFLOW_X) != KW_VISIBLE || kw(c->st, EP_OVERFLOW_Y) != KW_VISIBLE;
		if ((hides && ((cw != AUTO && cw <= 2) || (chh != AUTO && chh <= 2))) || ax + wf <= 0 || ay < -1000) {
			l->p->nitems = k0;
			return;
		}
	}
	shift_items(l, k0, ax, ay);
	if (c->n != NULL)
		c->n->y += ay;
}

/* lays a block out with its margin box's top left at (x, y) in a
   containing block cbw wide; fixed_w: cbw is its own width (a row's
   share). Returns the margin box's height, with the bottom margin in
   *mb_out (for collapsing) and the first baseline in *first. */
static int32_t layout_block(L *l, box *b, int32_t x, int32_t y, int32_t cbw, int fixed_w,
			    int32_t *mb_out, int32_t *first)
{
	const ec_style *s = b->st;
	int32_t ml = 0, mr = 0, mt = 0, mb = 0, bl = 0, br = 0, bt = 0, bb = 0, pl = 0, pr = 0, pt = 0, pb = 0;
	int32_t w, ch = 0, cx, cy, hp, mn, mx, fb = AUTO;
	long bgi = -1;
	uint32_t bg = 0;
	int auto_l = 0, auto_r = 0, wauto = 1, saved_n = l->ncols, table_n = 0;
	int32_t *saved_cols = l->cols, *table_cols = NULL, saved_sh = l->sp_h, saved_sv = l->sp_v;
	int32_t table_sh = 0, table_sv = 0, saved_top = l->fl_top;
	int32_t saved_cbx = l->cbx, saved_cby = l->cby, saved_cbw = l->cbw;
	int saved_fl0 = l->fl0, own = own_context(b), pos = b->anon ? KW_STATIC : kw(s, EP_POSITION);
	long bd[4] = { -1, -1, -1, -1 }, kstart = l->p->nitems;
	struct span *saved_spans = l->spans;

	if (l->fail)
		return 0;
	if (own) {
		/* a context of its own: the floats from here on are its */
		l->fl0 = l->nfl;
		l->fl_top = AUTO;
	}
	if (!b->anon) {
		ml = len(l, s, EP_MARGIN_LEFT, cbw);
		mr = len(l, s, EP_MARGIN_RIGHT, cbw);
		auto_l = ml == AUTO;
		auto_r = mr == AUTO;
		if (auto_l)
			ml = 0;
		if (auto_r)
			mr = 0;
		mt = len0(l, s, EP_MARGIN_TOP, cbw);
		mb = len0(l, s, EP_MARGIN_BOTTOM, cbw);
		pl = len0(l, s, EP_PADDING_LEFT, cbw);
		pr = len0(l, s, EP_PADDING_RIGHT, cbw);
		pt = len0(l, s, EP_PADDING_TOP, cbw);
		pb = len0(l, s, EP_PADDING_BOTTOM, cbw);
		if (kw(s, EP_BORDER_LEFT_STYLE) > KW_HIDDEN)
			bl = len0(l, s, EP_BORDER_LEFT_WIDTH, AUTO);
		if (kw(s, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN)
			br = len0(l, s, EP_BORDER_RIGHT_WIDTH, AUTO);
		if (kw(s, EP_BORDER_TOP_STYLE) > KW_HIDDEN)
			bt = len0(l, s, EP_BORDER_TOP_WIDTH, AUTO);
		if (kw(s, EP_BORDER_BOTTOM_STYLE) > KW_HIDDEN)
			bb = len0(l, s, EP_BORDER_BOTTOM_WIDTH, AUTO);
		/* a width of a thin border rounds to a pixel, not to nothing */
		if (kw(s, EP_BORDER_LEFT_STYLE) > KW_HIDDEN && bl == 0 && s->v[EP_BORDER_LEFT_WIDTH].i > 0) bl = 1;
		if (kw(s, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN && br == 0 && s->v[EP_BORDER_RIGHT_WIDTH].i > 0) br = 1;
		if (kw(s, EP_BORDER_TOP_STYLE) > KW_HIDDEN && bt == 0 && s->v[EP_BORDER_TOP_WIDTH].i > 0) bt = 1;
		if (kw(s, EP_BORDER_BOTTOM_STYLE) > KW_HIDDEN && bb == 0 && s->v[EP_BORDER_BOTTOM_WIDTH].i > 0) bb = 1;
	}
	if (fixed_w) {
		w = cbw - ml - mr - bl - br - pl - pr;
	} else {
		w = b->anon ? AUTO : len(l, s, EP_WIDTH, cbw);
		wauto = w == AUTO;
		if (!wauto && kw(s, EP_BOX_SIZING) == KW_BORDER_BOX)
			w -= bl + br + pl + pr;
		if (wauto)
			w = cbw - ml - mr - bl - br - pl - pr;
		if (!b->anon) {
			mx = len(l, s, EP_MAX_WIDTH, cbw);
			mn = len(l, s, EP_MIN_WIDTH, cbw);
			if (mx != AUTO) {
				if (kw(s, EP_BOX_SIZING) == KW_BORDER_BOX)
					mx -= bl + br + pl + pr;
				if (w > mx) {
					w = mx;
					wauto = 0;
				}
			}
			if (mn != AUTO && w < mn)
				w = mn;
		}
		/* margin: auto centres a block narrower than its container */
		if (!wauto && (auto_l || auto_r)) {
			int32_t room = cbw - w - bl - br - pl - pr;
			if (room > 0) {
				if (auto_l && auto_r) {
					ml = room / 2;
					mr = room - ml;
				} else if (auto_l) {
					ml = room - mr;
				}
			}
		}
	}
	if (b->repl && wauto && !fixed_w) {
		/* a picture: its own width, not its container's */
		int32_t iw, ih;
		l->cw = cbw;
		image_size(l, b->first, &iw, &ih);
		if (iw > 0 && iw < w) {
			w = iw;
			if (auto_l || auto_r) {
				int32_t room = cbw - w - bl - br - pl - pr;
				if (auto_l && auto_r) {
					ml = room / 2;
					mr = room - ml;
				} else if (auto_l) {
					ml = room - mr;
				}
			}
		}
	}
	if (b->table) {
		/* its columns at their widest; a table without a width shrinks to them */
		int32_t *cols = amalloc(l->a, MAXCOLS * sizeof *cols), mins[MAXCOLS], total, sp;
		int n, i, was = l->minw;
		if (cols == NULL) {
			l->fail = 1;
			return 0;
		}
		table_sh = s->k[EP_BORDER_SPACING_H] == EK_PX ? dev(l, s->v[EP_BORDER_SPACING_H].i) : 0;
		table_sv = s->k[EP_BORDER_SPACING_V] == EK_PX ? dev(l, s->v[EP_BORDER_SPACING_V].i) : 0;
		l->minw = 1;
		columns(l, b, w, mins);
		l->minw = was;
		n = columns(l, b, w, cols);
		sp = table_sh * (n + 1);
		total = sp;
		for (i = 0; i < n; i++)
			total += cols[i];
		if (wauto && !fixed_w && total < w) {
			if (auto_l && auto_r)
				ml += (w - total) / 2;
			w = total;
		}
		fit_columns(cols, mins, n, w - sp > n ? w - sp : n, !wauto || fixed_w);
		table_cols = cols;
		table_n = n;
	}
	if (w < 1)
		w = 1;
	if (b->n != NULL)
		b->n->y = y + mt;
	if (pos != KW_STATIC && pos != KW_STICKY) {
		/* positioned: the containing block of what is absolute in it */
		l->cbx = x + ml + bl;
		l->cby = y + mt + bt;
		l->cbw = pl + w + pr;
	}
	if (b->table) {
		l->spans = calloc(MAXCOLS, sizeof *l->spans);
		if (l->spans == NULL) {
			l->spans = saved_spans;
			l->fail = 1;
			return 0;
		}
	}
	if (!b->anon) {
		bg = color(s, EP_BACKGROUND_COLOR);
		if ((bg >> 24) != 0 && !(b->n != NULL && b->n->tag == T_BODY && l->body_bg_taken))
			bgi = fill(l, x + ml, y + mt, bl + pl + w + pr + br, 1, bg);
	}
	cx = x + ml + bl + pl;
	cy = y + mt + bt + pt;
	if (b->table) {
		l->cols = table_cols;
		l->ncols = table_n;
		l->sp_h = table_sh;
		l->sp_v = table_sv;
	} else if (!b->tablerow && b->n != NULL && kw(s, EP_DISPLAY) != KW_TABLE_ROW_GROUP &&
		   kw(s, EP_DISPLAY) != KW_TABLE_HEADER_GROUP && kw(s, EP_DISPLAY) != KW_TABLE_FOOTER_GROUP) {
		/* inside a cell: rows of their own table only */
		l->cols = NULL;
		l->ncols = 0;
	}
	if (b->repl) {
		ch = repl_layout(l, b, cx, cy, w);
	} else if (b->inl) {
		ch = inline_layout(l, b, cx, cy, w, &fb);
	} else if (b->row) {
		ch = row_layout(l, b, cx, cy, w, &fb);
	} else {
		box *c;
		int32_t edge = cy, prev_mb = 0;
		int num = 0, reversed = 0;
		if (b->n != NULL && b->n->tag == T_OL) {
			const char *st = dn_attr(b->n, "start");
			num = st ? atoi(st) - 1 : 0;
			reversed = dn_attr(b->n, "reversed") != NULL;
			if (reversed) {
				box *k;
				int cnt = 0;
				for (k = b->first; k != NULL; k = k->next)
					if (k->list)
						cnt++;
				num = st ? atoi(st) + 1 : cnt + 1;
			}
		}
		for (c = b->first; c != NULL && !l->fail; c = c->next) {
			int32_t cmt = c->anon ? 0 : len0(l, c->st, EP_MARGIN_TOP, w), cmb = 0, cf = AUTO, h, top;
			if (c->list) {
				const char *v = c->n ? dn_attr(c->n, "value") : NULL;
				num = v ? atoi(v) : (reversed ? num - 1 : num + 1);
				c->num = num;
			}
			if (!c->anon && kw(c->st, EP_FLOAT) != KW_NONE) {
				/* out of the flow: beside what follows */
				place_float(l, c, cx, w, edge + (prev_mb > 0 ? prev_mb : 0));
				continue;
			}
			if (!c->anon && (kw(c->st, EP_POSITION) == KW_ABSOLUTE || kw(c->st, EP_POSITION) == KW_FIXED)) {
				/* out of the flow altogether */
				place_abs(l, c, cx, edge + (prev_mb > 0 ? prev_mb : 0));
				continue;
			}
			top = edge + collapse(prev_mb, cmt) - cmt;
			if (!c->anon && l->nfl > l->fl0) {
				int clr = kw(c->st, EP_CLEAR);
				if (clr == KW_LEFT || clr == KW_RIGHT || clr == KW_BOTH) {
					/* below the floats on that side */
					int32_t cb = floats_bottom(l, clr);
					if (cb != AUTO && cb > top + cmt) {
						top = cb - cmt;
						prev_mb = 0;
					}
				}
			}
			if (l->nfl > l->fl0 && own_context(c)) {
				/* a table or a box of its own beside the floats, not under them */
				int32_t lx, rx;
				if (band(l, top + cmt, top + cmt + 1, cx, cx + w, &lx, &rx) && rx - lx > 0) {
					h = layout_block(l, c, lx, top, rx - lx, 0, &cmb, &cf);
					if (fb == AUTO && cf != AUTO)
						fb = cf;
					edge = top + h - cmb;
					prev_mb = cmb;
					continue;
				}
			}
			h = layout_block(l, c, cx, top, w, 0, &cmb, &cf);
			if (fb == AUTO && cf != AUTO)
				fb = cf;
			edge = top + h - cmb;
			prev_mb = cmb;
		}
		ch = edge - cy + (prev_mb > 0 ? prev_mb : 0);
		if (b->first == NULL)
			ch = 0;
	}
	if (b->table && ch > 0)
		ch += table_sv;		/* the spacing under the last row */
	if (own) {
		/* it holds its floats */
		int32_t fbot = floats_bottom(l, KW_BOTH);
		if (fbot != AUTO && fbot - cy > ch)
			ch = fbot - cy;
		l->nfl = l->fl0;
		l->fl0 = saved_fl0;
		l->fl_top = saved_top;
	}
	if (b->list)
		marker(l, b, cx, w, fb != AUTO ? fb : cy + font_of(l, s)->ascent);
	if (!b->anon) {
		hp = len(l, s, EP_HEIGHT, AUTO);
		if (hp != AUTO) {
			if (kw(s, EP_BOX_SIZING) == KW_BORDER_BOX)
				hp -= bt + bb + pt + pb;
			if (hp > ch || (kw(s, EP_OVERFLOW_Y) != KW_VISIBLE && hp >= 0))
				ch = hp < 0 ? 0 : hp;
		}
		mn = len(l, s, EP_MIN_HEIGHT, AUTO);
		if (mn != AUTO && ch < mn)
			ch = mn;
		mx = len(l, s, EP_MAX_HEIGHT, AUTO);
		if (mx != AUTO && ch > mx && kw(s, EP_OVERFLOW_Y) != KW_VISIBLE)
			ch = mx;
		if (bgi >= 0)
			l->p->items[bgi].h = bt + pt + ch + pb + bb;
		{
			int32_t bx = x + ml, by = y + mt, bw = bl + pl + w + pr + br, bh = bt + pt + ch + pb + bb;
			bd[0] = fill(l, bx, by, bw, bt, color(s, EP_BORDER_TOP_COLOR));
			bd[1] = fill(l, bx, by + bh - bb, bw, bb, color(s, EP_BORDER_BOTTOM_COLOR));
			bd[2] = fill(l, bx, by + bt, bl, bh - bt - bb, color(s, EP_BORDER_LEFT_COLOR));
			bd[3] = fill(l, bx + bw - br, by + bt, br, bh - bt - bb, color(s, EP_BORDER_RIGHT_COLOR));
		}
	}
	l->cols = saved_cols;
	l->ncols = saved_n;
	l->sp_h = saved_sh;
	l->sp_v = saved_sv;
	if (b->table) {
		free(l->spans);
		l->spans = saved_spans;
	}
	if (pos == KW_RELATIVE) {
		/* moved where top, left, bottom and right say, its place kept */
		int32_t dx = len(l, s, EP_LEFT, cbw), dy = len(l, s, EP_TOP, AUTO);
		if (dx == AUTO) {
			dx = len(l, s, EP_RIGHT, cbw);
			dx = dx == AUTO ? 0 : -dx;
		}
		if (dy == AUTO) {
			dy = len(l, s, EP_BOTTOM, AUTO);
			dy = dy == AUTO ? 0 : -dy;
		}
		if (dx != 0 || dy != 0)
			shift_items(l, kstart, dx, dy);
	}
	if (pos != KW_STATIC && pos != KW_STICKY) {
		l->cbx = saved_cbx;
		l->cby = saved_cby;
		l->cbw = saved_cbw;
	}
	l->last_bg = bgi;
	memcpy(l->last_bd, bd, sizeof bd);
	*mb_out = mb;
	*first = fb;
	return mt + bt + pt + ch + pb + bb + mb;
}

/* ---------------------------------------------------------------- the page */

page *layout_page(doc *d, const LAYOUT_OPT *o)
{
	L l;
	page *p = calloc(1, sizeof *p);
	struct arena *a = calloc(1, sizeof *a);
	box *root;
	dnode *html = d->html;
	int32_t mb, fb = AUTO;

	if (p == NULL || a == NULL) {
		free(p);
		free(a);
		return NULL;
	}
	memset(&l, 0, sizeof l);
	l.p = p;
	l.a = a;
	p->d = d;
	p->mem = a;
	p->opt = *o;
	p->width = o->width;
	p->bg = 0xffffff;
	l.zoom = o->zoom > 0 ? o->zoom : 1024;
	l.cbw = o->width;		/* the page: the containing block of what is absolute in nothing positioned */
	l.last_bg = -1;
	if (html == NULL || html->e.style == NULL) {
		p->height = 0;
		return p;
	}
	/* the canvas: the root's background, else the body's */
	{
		uint32_t hb = color(html->e.style, EP_BACKGROUND_COLOR);
		if ((hb >> 24) != 0) {
			p->bg = hb & 0xffffff;
		} else if (d->body != NULL && d->body->e.style != NULL) {
			uint32_t bb = color(d->body->e.style, EP_BACKGROUND_COLOR);
			if ((bb >> 24) != 0) {
				p->bg = bb & 0xffffff;
				l.body_bg_taken = 1;
			}
		}
	}
	root = new_box(&l, BX_BLOCK, html, html->e.style, NULL);
	if (root != NULL) {
		if (kw(html->e.style, EP_DISPLAY) != KW_NONE) {
			pseudo(&l, root, html, html->e.style->before);
			build(&l, root, html);
		}
		fixup(&l, root);
		p->height = layout_block(&l, root, 0, 0, o->width, 0, &mb, &fb);
	}
	free(l.it);
	free(l.fl);
	if (l.fail) {
		page_free(p);
		return NULL;
	}
	return p;
}

void page_free(page *p)
{
	if (p == NULL)
		return;
	free(p->items);
	free(p->links);
	if (p->mem != NULL) {
		afree(p->mem);
		free(p->mem);
	}
	free(p);
}

int page_link_at(const page *p, int32_t x, int32_t y)
{
	long i;
	for (i = p->nitems - 1; i >= 0; i--) {
		const dl_item *d = &p->items[i];
		if (d->link && x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h)
			return d->link - 1;
	}
	return -1;
}

static int32_t anchor_in(const dnode *n, const char *id)
{
	const dnode *c;
	if (n->type == DN_ELEMENT && n->y >= 0) {
		const char *v = dn_attr(n, "id");
		if ((v != NULL && strcmp(v, id) == 0) ||
		    (n->tag == T_A && (v = dn_attr(n, "name")) != NULL && strcmp(v, id) == 0))
			return n->y;
	}
	for (c = n->first; c != NULL; c = c->next) {
		int32_t y = anchor_in(c, id);
		if (y >= 0)
			return y;
	}
	return -1;
}

int32_t page_anchor(const page *p, const char *id)
{
	if (p->d == NULL || p->d->root == NULL || id == NULL || *id == 0)
		return -1;
	return anchor_in(p->d->root, id);
}
