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
#include "form.h"

#define AUTO INT32_MIN
#define MAXCOLS 64			/* a table's columns that line up; more go as they come */

/* ---------------------------------------------------------------- memory */

struct achunk { struct achunk *next; size_t used, size; char mem[]; };
struct arena { struct achunk *head; };

/* what a box or a run needs: 4 on the 68030 (2 would do), 8 where
   pointers are 8 bytes (the build machine's checks) */
#define AALIGN (sizeof(void *) > 4 ? 8 : 4)
#ifdef EW_ST
#define ACHUNK 8192	/* the ST edition: smaller pieces, for a 1 MB ST */
#else
#define ACHUNK 16384
#endif

static void *amalloc(struct arena *a, size_t n)
{
	struct achunk *c = a->head;
	void *p;
	n = (n + AALIGN - 1) & ~(size_t) (AALIGN - 1);
	if (c == NULL || c->used + n > c->size) {
		size_t size = n > ACHUNK ? n : ACHUNK;
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
/* an atom: alt text; a picture; a form control (drawn from its state, form.h) */
enum { AT_LABEL, AT_IMAGE, AT_CONTROL };

typedef struct box box;
struct box {
	uint8_t kind;
	uint8_t inl;			/* a block whose children are inline: lines */
	uint8_t row;			/* a block whose children go side by side */
	uint8_t wrap;			/* a flex row that wraps (flex-wrap) */
	uint8_t flex;			/* a flex container: its children are flex items */
	uint8_t grid;			/* a grid container: its children are grid items */
	uint8_t list;			/* a list item */
	uint8_t anon;			/* an anonymous block: no box of its own */
	uint8_t atom;			/* AT_* */
	uint8_t tablerow;
	uint8_t table;			/* a table: shrinks to fit, its rows share column widths */
	uint8_t repl;			/* a picture as a block (floated, display: block): its one child the atom */
	uint16_t link;			/* 1 + link index, or 0 */
	uint16_t ctl;			/* 1 + index into page.ctls, or 0 */
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
	uint16_t ctl;			/* a label's text: its control */
	ew_font *font;
	uint32_t color, bg;		/* 0xAARRGGBB, as solid as drawn (its alpha, then the
					   opacity it is in); bg 0: none */
	uint8_t alpha;			/* the opacity it is in, 0..255 (CSS Color 4) */
	const char *text;
	uint32_t len;
	int32_t w;
	int32_t asc, desc;		/* above and below the baseline, leading included */
	int32_t shift;			/* vertical-align: + down */
	box *atom;
	int32_t x;			/* where it went on its line */
#ifndef EW_ST
	dnode *node;			/* the element it is in (for scripts' events) */
#endif
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
	int32_t text;			/* LAYOUT_OPT.text */
	ITEM *it;
	long nit, itcap;
	dnode *bg_taken;		/* the element whose background colour the canvas took */
	dnode *canvas_bg;		/* the element whose background picture the canvas has */
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
	unsigned alpha;			/* the opacity of the box being laid out, times its
					   ancestors', 0..255 */
	uint32_t clip;			/* a background it is in painted through its text
					   (background-clip: text): the colour that text is
					   drawn in when its own is transparent; 0: none */
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

/* opacity (CSS Color 4, Transparency: the opacity property), 0..255:
   not inherited, but it applies to the element and all that is in it as
   a group. EWeb takes it into each item the group draws (their alphas
   multiplied), which is the group's look wherever its items don't lie
   over one another; opacity 0 draws nothing, and the element still takes
   its room and its clicks */
static unsigned opacity(const ec_style *s)
{
	int32_t v = s->k[EP_OPACITY] == EK_NUM ? s->v[EP_OPACITY].i : 1024;
	return v >= 1024 ? 255 : v <= 0 ? 0 : (unsigned) ((v * 255 + 512) >> 10);
}

static int is_data(const char *u)
{
	return strncmp(u, "data:", 5) == 0;
}

/* the opacity EWeb draws an element at: its own, but whole where the
   page has something lift it that EWeb doesn't run or draw:
   - an animation (EWeb runs none): the element as its animation leaves
     it, which EWeb takes to be in full (a fade-in, what an element of
     opacity 0 with an animation nearly always has; CSS Animations 1's
     fill-mode keeps its last keyframe);
   - an <img> whose picture EWeb took from data-src and the like (as the
     page's lazy loader would, which also lifts its opacity: lazysizes'
     .lazyload);
   - AOS's (data-aos) and Webflow's (data-w-id) elements, which their
     scripts fade in as the page is scrolled to them;
   - a checkbox or radio button of next to no opacity: a page's own
     control drawn in its place from generated content, which EWeb
     doesn't draw (Bootstrap 4's custom controls), would leave none */
static unsigned own_opacity(const ec_style *s, const dnode *n)
{
	unsigned a = opacity(s);
	if (a == 255 || s->k[EP_ANIMATION_NAME] != EK_KW)
		return 255;
	if (n == NULL || n->type != DN_ELEMENT || n->ns != DN_NS_HTML)
		return a;
	if (dn_attr(n, "data-aos") != NULL || dn_attr(n, "data-w-id") != NULL)
		return 255;
	if (n->tag == T_IMG) {
		static const char *const lazy[] = { "data-src", "data-srcset", "data-lazy-src", "data-original" };
		const char *src = dn_attr(n, "src"), *v;
		size_t i;
		if (src == NULL || *src == 0 || is_data(src))
			for (i = 0; i < sizeof lazy / sizeof lazy[0]; i++)
				if ((v = dn_attr(n, lazy[i])) != NULL && *v != 0 && !is_data(v))
					return 255;
	}
	if (n->tag == T_INPUT && n->ctl != NULL && (n->ctl->kind == FC_CHECKBOX || n->ctl->kind == FC_RADIO) &&
	    a < 16)
		return 255;
	return a;
}

/* text whose own colour is transparent that the page shows another way,
   which EWeb doesn't draw: a background painted through it
   (background-clip: text, clip), its outline (-webkit-text-stroke) or
   its shadow (text-shadow); the colour of the first of these it has (of
   alpha 0: not drawn). The glyphs show the background painted through them,
   else their outline; a shadow is only under them, offset, blurred and
   often faint (and inherited: a page's soft shadow on all its text would
   otherwise be what its gradient headings are drawn in) */
static uint32_t text_paint(const ec_style *s, uint32_t clip)
{
	uint32_t c;
	if (clip != 0)
		return clip;
	if (s->k[EP_TEXT_STROKE_WIDTH] == EK_PX && s->v[EP_TEXT_STROKE_WIDTH].i > 0 &&
	    ((c = color(s, EP_TEXT_STROKE_COLOR)) >> 24) != 0)
		return c;
	return color(s, EP_TEXT_SHADOW);
}

/* a gradient's first colour that shows (its alpha not 0), 0: none */
static uint32_t grad_first(const ec_grad *g, uint32_t cur)
{
	int i;
	for (i = 0; i < g->n; i++) {
		uint32_t c = g->s[i].flags & EC_GS_CUR ? cur : g->s[i].argb;
		if (!(g->s[i].flags & EC_GS_HINT) && (c >> 24) != 0)
			return c;
	}
	return 0;
}

uint32_t grad_average(const ec_grad *g, uint32_t cur)
{
	uint32_t a = 0, r = 0, gr = 0, b = 0, n = 0;
	int i;
	for (i = 0; i < g->n; i++) {
		uint32_t c = g->s[i].flags & EC_GS_CUR ? cur : g->s[i].argb, ca = c >> 24;
		if (g->s[i].flags & EC_GS_HINT)
			continue;
		a += ca;
		r += (c >> 16 & 255) * ca;
		gr += (c >> 8 & 255) * ca;
		b += (c & 255) * ca;
		n++;
	}
	if (a == 0)
		return 0;
	return (a + n / 2) / n << 24 | (r + a / 2) / a << 16 | (gr + a / 2) / a << 8 | (b + a / 2) / a;
}

/* background-clip: text: the colour its text is drawn in, in place of
   the background: the gradient's first colour that shows (the glyphs
   aren't drawn through the gradient, and its first colour reads where
   an average with a transparent stop would be faint), else the
   background colour, else (a picture, a gradient of no colour, or none:
   most often a gradient EWeb couldn't compute, as Tailwind 4's, made
   of custom properties only @property starts) the text's own colour,
   solid; pages don't hide text this way. 0: not background-clip: text */
static uint32_t clip_paint(const ec_style *s)
{
	uint32_t c;
	if (kw(s, EP_BACKGROUND_CLIP) != KW_TEXT)
		return 0;
	if (s->k[EP_BACKGROUND_IMAGE] == EK_IMAGE &&
	    (c = grad_first(s->v[EP_BACKGROUND_IMAGE].p, color(s, EP_COLOR))) != 0)
		return c;
	if (((c = color(s, EP_BACKGROUND_COLOR)) >> 24) != 0)
		return c;
	return 0xff000000 | color(s, EP_COLOR);
}

/* pointer-events: none (CSS UI 4): its items aren't what a click is on */
static int no_hit(const ec_style *s)
{
	return kw(s, EP_POINTER_EVENTS) == KW_NONE;
}

/* two alphas together (0..255 each): a times b */
static unsigned alpha_mul(unsigned a, unsigned b)
{
	return b == 255 ? a : a == 255 ? b : (a * b + 127) / 255;
}

/* a colour (0xAARRGGBB) in a group of opacity a */
static uint32_t fade(uint32_t argb, unsigned a)
{
	return a == 255 ? argb : (uint32_t) alpha_mul(argb >> 24, a) << 24 | (argb & 0xffffff);
}

/* colour s laid over colour b, both 0xAARRGGBB: the one colour they
   make (source-over, Compositing and Blending 1), for a translucent
   background on one beneath it */
static uint32_t over(uint32_t s, uint32_t b)
{
	unsigned as = s >> 24, ab = b >> 24, ao, k;
	uint32_t r = 0;
	if (as == 255 || ab == 0)
		return s;
	ab = (ab * (255 - as) + 127) / 255;
	ao = as + ab;
	if (ao == 0)
		return 0;
	for (k = 0; k < 24; k += 8)
		r |= (((s >> k & 255) * as + (b >> k & 255) * ab + ao / 2) / ao) << k;
	return (uint32_t) ao << 24 | r;
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

/* text's size in device px: the page's, times the text size the reader
   chose (only text grows or shrinks with it) */
static int32_t text_px(L *l, int32_t px)
{
	return l->text == 1024 ? px : (int32_t) (((int64_t) px * l->text + 512) >> 10);
}

static int32_t font_px(L *l, const ec_style *s)
{
	int32_t px = text_px(l, s->k[EP_FONT_SIZE] == EK_PX ? dev(l, s->v[EP_FONT_SIZE].i) : dev(l, 16 << 10));
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
		return text_px(l, dev(l, s->v[EP_LINE_HEIGHT].i));
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

#ifndef EW_ST
/* a block's border box, for scripts (a click's element, the element's
   place) */
static void block_rect(L *l, dnode *n, int32_t x, int32_t y, int32_t w, int32_t h)
{
	page *p = l->p;
	if (p->nblocks == p->blockcap) {
		long cap = p->blockcap ? p->blockcap * 2 : 256;
		struct pg_block *nb = realloc(p->blocks, cap * sizeof *nb);
		if (nb == NULL)
			return;
		p->blocks = nb;
		p->blockcap = cap;
	}
	p->blocks[p->nblocks].n = n;
	p->blocks[p->nblocks].x = x;
	p->blocks[p->nblocks].y = y;
	p->blocks[p->nblocks].w = w;
	p->blocks[p->nblocks].h = h;
	p->nblocks++;
}
#endif

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
	d->rgb = argb;
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
	d->rgb = it->color;		/* (alpha 0: kept for its link and clicks, not drawn) */
	d->flags = it->flags;
	d->link = it->link;
	d->ctl = it->ctl;
	d->font = it->font;
	d->text = s;
	d->len = n;
#ifndef EW_ST
	d->node = it->node;
#endif
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

static uint16_t add_ctl(L *l, fctl *c)
{
	page *p = l->p;
	if (p->nctls == p->ctlcap) {
		int cap = p->ctlcap ? p->ctlcap * 2 : 16;
		fctl **n = realloc(p->ctls, cap * sizeof *n);
		if (n == NULL)
			return 0;
		p->ctls = n;
		p->ctlcap = cap;
	}
	if (p->nctls >= 65534)
		return 0;
	p->ctls[p->nctls] = c;
	return (uint16_t) (++p->nctls);
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

/* a form control: an atom of its size, drawn from its state (form.h),
   so that typing in it draws it again without laying the page out */
static void control(L *l, box *parent, dnode *n, const ec_style *st)
{
	fctl *c = n->ctl;
	ew_font *f = font_of(l, st);
	char lab[256];
	int32_t w, h = AUTO, line = f->ascent + f->descent + 4;
	box *b;

	if (c == NULL || c->kind == FC_HIDDEN)
		return;
	switch (c->kind) {
	case FC_TEXT:
	case FC_PASSWORD: {
		int size = dn_attr(n, "size") ? atoi(dn_attr(n, "size")) : 20;
		if (size < 1 || size > 200)
			size = 20;
		w = size * char_w(l, st) + 6;
		break;
	}
	case FC_TEXTAREA: {
		int cols = dn_attr(n, "cols") ? atoi(dn_attr(n, "cols")) : 20;
		int rows = dn_attr(n, "rows") ? atoi(dn_attr(n, "rows")) : 2;
		if (cols < 1 || cols > 200)
			cols = 20;
		if (rows < 1 || rows > 50)
			rows = 2;
		w = cols * char_w(l, st) + 6;
		h = rows * line_height(l, st, f) + 6;
		break;
	}
	case FC_CHECKBOX:
	case FC_RADIO:
		w = line - 2;
		break;
	case FC_SELECT: {
		/* as wide as its widest option, so that choosing one moves nothing */
		int i;
		w = 0;
		for (i = 0; i < c->nopt; i++) {
			int32_t ow;
			form_option_text(c->opt[i], lab, sizeof lab);
			ow = (int32_t) font_width(f, lab, (uint32_t) strlen(lab));
			if (ow > w)
				w = ow;
		}
		w += (int32_t) font_width(f, " \xe2\x96\xbc", 4) + 8;
		break;
	}
	case FC_IMAGE: {
		const char *src = dn_attr(n, "src");
		int32_t iw = 0, ih = 0;
		void *im = NULL;
		if (src != NULL && *src && l->p->opt.image != NULL)
			im = l->p->opt.image(l->p->opt.ud, src, &iw, &ih);
		if (im != NULL) {
			form_label(c, lab, sizeof lab);
			atom(l, parent, n, st, AT_IMAGE, astrdup(l->a, lab, strlen(lab)), AUTO, AUTO);
			if (parent->last != NULL && parent->last->n == n) {
				parent->last->img = im;
				parent->last->iw = iw;
				parent->last->ih = ih;
				parent->last->ctl = add_ctl(l, c);
			}
			return;
		}
		/* no picture: a button with its alt text */
		form_label(c, lab, sizeof lab);
		w = (int32_t) font_width(f, lab, (uint32_t) strlen(lab)) + 12;
		break;
	}
	default:
		form_label(c, lab, sizeof lab);
		w = (int32_t) font_width(f, lab, (uint32_t) strlen(lab)) + 12;
		if (w < 16)
			w = 16;
	}
	atom(l, parent, n, st, AT_CONTROL, "", w, h);
	b = parent->last;
	if (b != NULL && b->n == n)
		b->ctl = add_ctl(l, c);
}

static void build(L *l, box *parent, dnode *n);

/* ---------------------------------------------------------------- which picture */

/* a srcset's candidate: its address, and its density (x 1024) or width */
typedef struct {
	const char *u;
	uint32_t n;
	int32_t x, w;
} CAND;

#define MAXCAND 16

static int is_ws(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* a srcset's candidates, after the HTML Standard's parsing of one */
static int srcset(const char *s, CAND *c, int max)
{
	int n = 0;
	while (*s && n < max) {
		const char *u, *ue, *d, *de;
		int bad = 0, depth = 0, hasx = 0, hasw = 0;
		int32_t x = 0, w = 0;
		while (is_ws(*s) || *s == ',')
			s++;
		if (*s == 0)
			break;
		u = s;
		while (*s && !is_ws(*s))
			s++;
		ue = s;
		if (ue[-1] == ',') {
			/* no descriptors */
			while (ue > u && ue[-1] == ',')
				ue--;
			d = de = s;
		} else {
			d = s;
			while (*s && !(*s == ',' && depth == 0)) {
				if (*s == '(')
					depth++;
				else if (*s == ')' && depth > 0)
					depth--;
				s++;
			}
			de = s;
		}
		/* the descriptors: Nx, Nw (Nh: not used) */
		while (d < de) {
			const char *t;
			while (d < de && is_ws(*d))
				d++;
			t = d;
			while (d < de && !is_ws(*d))
				d++;
			if (d == t)
				break;
			if (d[-1] == 'w' && !hasw && !hasx) {
				const char *q = t;
				w = 0;
				while (q < d - 1 && *q >= '0' && *q <= '9')
					w = w * 10 + (*q++ - '0');
				bad |= q != d - 1 || w <= 0 || w > 100000;
				hasw = 1;
			} else if (d[-1] == 'x' && !hasw && !hasx) {
				const char *q = t;
				int32_t ip = 0, fr = 0, sc = 1;
				while (q < d - 1 && *q >= '0' && *q <= '9')
					ip = ip * 10 + (*q++ - '0');
				if (q < d - 1 && *q == '.')
					for (q++; q < d - 1 && *q >= '0' && *q <= '9'; q++)
						if (sc < 10000) {
							fr = fr * 10 + (*q - '0');
							sc *= 10;
						}
				x = ip * 1024 + fr * 1024 / sc;
				bad |= q != d - 1 || x <= 0 || ip > 100;
				hasx = 1;
			} else if (d[-1] != 'h') {
				bad = 1;
			}
		}
		if (!bad && ue > u) {
			c[n].u = u;
			c[n].n = (uint32_t) (ue - u);
			c[n].x = hasx ? x : 0;
			c[n].w = hasw ? w : 0;
			n++;
		}
		if (*s == ',')
			s++;
	}
	return n;
}

/* from a srcset (and an img's src as 1x when it may be one), the
   candidate nearest the screen's density at or above it (the least to
   fetch that still looks sharp), else the densest; its address and its
   density (x 1024), 0 when there is none */
static const char *choose(L *l, const char *set, const char *sizes, const char *src, int32_t *dens)
{
	CAND c[MAXCAND + 1];
	int n = srcset(set, c, MAXCAND), i, best = -1, anyw = 0, one = 0, real = 0;
	int32_t slot = -1, want = l->zoom, bd = 0;
	for (i = 0; i < n; i++) {
		anyw |= c[i].w > 0;
		one |= c[i].w == 0 && (c[i].x == 0 || c[i].x == 1024);
		real |= !is_data(c[i].u);
	}
	if (src != NULL && *src != 0 && !anyw && !one) {
		c[n].u = src;
		c[n].n = (uint32_t) strlen(src);
		c[n].x = 1024;
		c[n].w = 0;
		real |= !is_data(src);
		n++;
	}
	if (anyw) {
		if (sizes != NULL && *sizes != 0)
			slot = ec_sizes(l->p->d->css, sizes, strlen(sizes));
		if (slot <= 0)
			slot = (int32_t) ((int64_t) l->p->width * 1024 / l->zoom);	/* 100vw */
		if (slot <= 0)
			slot = 1;
	}
	for (i = 0; i < n; i++) {
		int32_t d = c[i].w > 0 ? (int32_t) ((int64_t) c[i].w * 1024 / slot) : c[i].x ? c[i].x : 1024;
		if (real && is_data(c[i].u))
			continue;		/* a placeholder */
		if (best < 0 || (bd < want ? d > bd : d >= want && d < bd)) {
			best = i;
			bd = d;
		}
	}
	if (best < 0)
		return NULL;
	*dens = bd > 0 ? bd : 1024;
	return astrdup(l->a, c[best].u, c[best].n);
}

/* a <source>'s type: one EWeb reads (JPEG, PNG, GIF), or none given */
static int img_type_ok(const char *t)
{
	static const char *const ok[] = { "image/jpeg", "image/jpg", "image/pjpeg", "image/png", "image/gif" };
	size_t i, n;
	if (t == NULL)
		return 1;
	while (is_ws(*t))
		t++;
	for (n = 0; t[n] && !is_ws(t[n]) && t[n] != ';'; n++)
		;
	if (n == 0)
		return 1;
	for (i = 0; i < sizeof ok / sizeof ok[0]; i++)
		if (strlen(ok[i]) == n && contains_ci(t, n, ok[i]))
			return 1;
	return 0;
}

/* the picture an <img> shows: a <picture>'s first <source> whose type
   EWeb reads and whose media holds, else its own srcset with its src,
   else where a page that loads its pictures as they are scrolled to
   keeps the real one (data-srcset, data-src and the like) when src is
   missing or only a placeholder; and its density (x 1024: a picture
   chosen for 2x is shown at half its size) */
static const char *img_src(L *l, dnode *n, int32_t *dens)
{
	static const char *const lazy[] = { "data-src", "data-lazy-src", "data-original" };
	const char *s = dn_attr(n, "src"), *set, *r;
	size_t i;
	*dens = 1024;
	if (n->parent != NULL && n->parent->tag == T_PICTURE && n->parent->ns == DN_NS_HTML) {
		dnode *k;
		for (k = n->parent->first; k != NULL && k != n; k = k->next) {
			const char *m;
			if (k->type != DN_ELEMENT || k->tag != T_SOURCE)
				continue;
			m = dn_attr(k, "media");
			if (!img_type_ok(dn_attr(k, "type")) || (m != NULL && *m != 0 &&
								 !ec_media_ok(l->p->d->css, m, strlen(m))))
				continue;
			set = dn_attr(k, "srcset");
			if (set == NULL || *set == 0 || (is_data(set) && dn_attr(k, "data-srcset") != NULL))
				set = dn_attr(k, "data-srcset");
			if (set != NULL && (r = choose(l, set, dn_attr(k, "sizes"), NULL, dens)) != NULL)
				return r;
		}
	}
	set = dn_attr(n, "srcset");
	if (set != NULL && *set != 0 && (r = choose(l, set, dn_attr(n, "sizes"),
						 s != NULL && !is_data(s) ? s : NULL, dens)) != NULL && !is_data(r))
		return r;
	*dens = 1024;
	if (s != NULL && *s != 0 && !is_data(s))
		return s;
	set = dn_attr(n, "data-srcset");
	if (set != NULL && *set != 0 && (r = choose(l, set, dn_attr(n, "data-sizes") ? dn_attr(n, "data-sizes") :
						 dn_attr(n, "sizes"), NULL, dens)) != NULL && !is_data(r))
		return r;
	*dens = 1024;
	for (i = 0; i < sizeof lazy / sizeof lazy[0]; i++) {
		const char *v = dn_attr(n, lazy[i]);
		if (v != NULL && *v != 0 && !is_data(v))
			return v;
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
		int32_t iw = 0, ih = 0, dens;
		const char *alt = dn_attr(c, "alt"), *src = img_src(l, c, &dens);
		void *h = NULL;
		if (src != NULL && l->p->opt.image != NULL)
			h = l->p->opt.image(l->p->opt.ud, src, &iw, &ih);
		if (h != NULL && dens != 1024 && iw > 0 && ih > 0) {
			/* chosen for another density: its size in CSS px */
			iw = (int32_t) ((int64_t) iw * 1024 / dens);
			ih = (int32_t) ((int64_t) ih * 1024 / dens);
			if (iw < 1)
				iw = 1;
			if (ih < 1)
				ih = 1;
		}
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
	b->grid = d == KW_GRID || d == KW_INLINE_GRID;
	if (d == KW_FLEX || d == KW_INLINE_FLEX) {
		b->flex = 1;
		/* a row's items measure side by side */
		b->row = kw(st, EP_FLEX_DIRECTION) == KW_ROW || kw(st, EP_FLEX_DIRECTION) == KW_ROW_REVERSE;
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
	if (EC_DEEP()) {
		l->p->deep = 1;		/* the stack's floor: no deeper */
		return;
	}
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
	if (EC_DEEP()) {
		l->p->deep = 1;		/* the stack's floor: what is in it left out */
		b->first = b->last = NULL;
		return;
	}

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
	if (b->flex || b->grid) {
		/* a flex or grid container's element children are its items, blocks
		   (its text and pictures go in anonymous items) */
		for (c = b->first; c != NULL; c = c->next)
			if (c->kind == BX_INLINE && c->n != NULL) {
				c->kind = BX_BLOCK;
				fixup(l, c);
			}
	}
	for (c = b->first; c != NULL; c = c->next) {
		if (c->kind == BX_BLOCK)
			has_block = 1;
		else if (c->kind != BX_TEXT || !blank(c->text, c->len) || keeps_space(c->st))
			has_inline = 1;
	}
	if (!has_block && !b->row && !b->flex && !b->grid) {
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
	uint32_t color, bg;		/* as ITEM's */
	uint32_t clip;			/* as L's: a background painted through its text */
	uint8_t deco, hidden;		/* deco: DLF_UNDER..., and DLF_NOHIT */
	uint8_t alpha;			/* the opacity it is in, 0..255 */
	uint16_t link;
	uint16_t ctl;			/* in a <label>: its control (1 + index into page.ctls) */
	int32_t asc, desc, shift;
#ifndef EW_ST
	dnode *node;			/* the element whose run it is */
#endif
} RUNSTATE;

/* a run's state from its element (n, st; NULL and the block's style for
   the block's own text) */
static void runstate(L *l, RUNSTATE *r, const ec_style *st, const dnode *n, const RUNSTATE *parent,
		     uint16_t link)
{
	int32_t lh, lead;
	int va;
	uint32_t bg, col;
	int d;

	r->st = st;
	r->font = font_of(l, st);
	/* the block's own opacity is in l->alpha already (and an atom that
	   is a block of its own has its block's style) */
	r->alpha = (uint8_t) (parent == NULL ? l->alpha : st == parent->st ? parent->alpha :
			      alpha_mul(parent->alpha, own_opacity(st, n)));
	r->clip = parent == NULL ? l->clip : st == parent->st ? parent->clip : clip_paint(st);
	if (r->clip == 0 && parent != NULL)
		r->clip = parent->clip;
	col = color(st, EP_COLOR);
	if ((col >> 24) == 0)
		col = text_paint(st, r->clip);
	r->color = fade(col, r->alpha);
	r->link = link;
#ifndef EW_ST
	r->node = parent ? parent->node : NULL;
#endif
	r->ctl = parent ? parent->ctl : 0;
	r->deco = (uint8_t) ((parent ? parent->deco & ~DLF_NOHIT : 0) | (no_hit(st) ? DLF_NOHIT : 0));
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
	if (parent != NULL && st->k[EP_BACKGROUND_IMAGE] == EK_IMAGE)
		/* an inline's gradient: its colours' average over its colour
		   (a run is one fill) */
		bg = over(grad_average(st->v[EP_BACKGROUND_IMAGE].p, color(st, EP_COLOR)), bg);
	if (parent != NULL && (bg >> 24) != 0 && kw(st, EP_BACKGROUND_CLIP) != KW_TEXT)
		r->bg = over(fade(bg, r->alpha), r->bg);	/* one fill for both, as solid as they are together */
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
	it->ctl = r->ctl;
	it->hidden = r->hidden;
	it->alpha = r->alpha;
#ifndef EW_ST
	it->node = r->node;
#endif
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
		w = (int32_t) font_width(f, b->text, b->len);
	return w;
}

/* the control a <label> is for (its for attribute's, else the first in
   it): 1 + its index into page.ctls, 0 for none */
static uint16_t label_for(L *l, dnode *n)
{
	const char *id = dn_attr(n, "for");
	fctl *c = NULL;
	int k;
	if (id != NULL) {
		for (k = 0; k < l->p->nctls; k++) {
			const char *v = dn_attr(l->p->ctls[k]->n, "id");
			if (v != NULL && strcmp(v, id) == 0)
				return (uint16_t) (k + 1);
		}
		return 0;
	} else {
		dnode *d;
		for (d = n->first; d != NULL && c == NULL;) {
			if (d->type == DN_ELEMENT && d->ctl != NULL)
				c = d->ctl;
			else if (d->first != NULL)
				d = d->first;
			else {
				while (d != n && d->next == NULL)
					d = d->parent;
				d = d != n ? d->next : NULL;
			}
		}
	}
	for (k = 0; c != NULL && k < l->p->nctls; k++)
		if (l->p->ctls[k] == c)
			return (uint16_t) (k + 1);
	return 0;
}

static void collect(L *l, box *b, const RUNSTATE *pr, int *line_start)
{
	box *c;
	if (EC_DEEP()) {
		l->p->deep = 1;
		return;
	}
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
			runstate(l, &r, c->st, c->n, pr, c->link ? c->link : pr->link);
#ifndef EW_ST
			if (c->n != NULL)
				r.node = c->n;
#endif
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
			runstate(l, &r, c->st, c->n, pr, c->link ? c->link : pr->link);
#ifndef EW_ST
			if (c->n != NULL)
				r.node = c->n;
#endif
			if (c->n != NULL && c->n->tag == T_LABEL && c->n->ns == DN_NS_HTML)
				r.ctl = label_for(l, c->n);
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
	if (b->parent->row || b->parent->flex || b->parent->grid || b->table || kw(b->st, EP_FLOAT) != KW_NONE || kw(b->st, EP_POSITION) == KW_ABSOLUTE ||
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
		       int flags, unsigned alpha)
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
	d->ctl = b->ctl;
	d->flags = (uint8_t) flags;
	d->text = b->text;	/* its alt text, while it comes */
	d->len = b->len;
	d->font = f;
	d->rgb = (uint32_t) alpha << 24 | 0x606060;	/* its opacity; its alt text's colour */
#ifndef EW_ST
	d->node = b->n;
#endif
}

/* a CSS length of a gradient (22.10 px) in device px, 22.10 */
static ec_fx dev_fx(L *l, ec_fx v)
{
	return (ec_fx) ((int64_t) v * l->zoom >> 10);
}

/* a gradient as the canvas draws it: its lengths in device px,
   currentcolor the element's colour; NULL when out of memory */
static const ec_grad *grad_dev(L *l, const ec_style *s, const ec_grad *g)
{
	ec_grad *r = amalloc(l->a, sizeof *r + g->n * sizeof r->s[0]);
	uint32_t cur = color(s, EP_COLOR);
	int i;
	if (r == NULL) {
		l->fail = 1;
		return NULL;
	}
	memcpy(r, g, sizeof *r + g->n * sizeof r->s[0]);
	r->x.px = dev_fx(l, g->x.px);
	r->y.px = dev_fx(l, g->y.px);
	r->rx.px = dev_fx(l, g->rx.px);
	r->ry.px = dev_fx(l, g->ry.px);
	for (i = 0; i < g->n; i++) {
		if (g->s[i].flags & EC_GS_CUR)
			r->s[i].argb = cur;
		if (g->type != EC_GRAD_CONIC)
			r->s[i].at.px = dev_fx(l, g->s[i].at.px);
	}
	return r;
}

/* a background picture (the first layer's url()) or gradient: its item,
   here in the painting order, with the picture's size in CSS px in x and
   y (a gradient's 0 x 0: none of its own) until bg_place puts it where
   the box is; -1: none */
static long bg_item(L *l, const ec_style *s, dnode *n)
{
	const ec_str *u;
	void *h;
	int32_t iw = 0, ih = 0;
	dl_item *d;
	if (s != NULL && s->k[EP_BACKGROUND_IMAGE] == EK_IMAGE) {
		const ec_grad *g = grad_dev(l, s, s->v[EP_BACKGROUND_IMAGE].p);
		if (g == NULL || (d = dl_add(l)) == NULL)
			return -1;
		d->type = DL_GRAD;
		d->img = (void *) g;
		d->rgb = (uint32_t) l->alpha << 24;	/* its box's opacity */
		d->flags = DLF_TILE;
#ifndef EW_ST
		d->node = n;
#endif
		return l->p->nitems - 1;
	}
	if (s == NULL || s->k[EP_BACKGROUND_IMAGE] != EK_PTR || l->p->opt.image == NULL)
		return -1;
	u = s->v[EP_BACKGROUND_IMAGE].p;
	if (u == NULL || u->len == 0)
		return -1;
	h = l->p->opt.image(l->p->opt.ud, u->s, &iw, &ih);
	if (h == NULL)
		return -1;
	d = dl_add(l);
	if (d == NULL)
		return -1;
	d->type = DL_IMAGE;
	d->img = h;
	d->rgb = (uint32_t) l->alpha << 24;	/* its box's opacity */
	d->flags = DLF_TILE | DLF_NOSIZE | DLF_ALT;
	d->x = iw;
	d->y = ih;
#ifndef EW_ST
	d->node = n;
#endif
	return l->p->nitems - 1;
}

/* the background picture or gradient at item i over a box's border box
   (x, y, w, h), by its background-size, -position and -repeat, drawn
   short of it by the insets in (left, top, right, bottom: the box
   background-clip names) or NULL; a picture whose file hasn't come yet:
   over the box, nothing drawn, and the page laid out again when it has */
static void bg_place(L *l, long i, const ec_style *s, int32_t x, int32_t y, int32_t w, int32_t h,
		     int32_t bl, int32_t bt, int32_t br, int32_t bb, const int32_t *in)
{
	dl_item *d;
	dl_tile *t;
	int32_t iw, ih, pw = w - bl - br, ph = h - bt - bb, ph0 = ph, tw, th, ox, oy, x1, y1;
	int rep, sw, grad, grow = 0;
	if (i < 0 || l->fail)
		return;
	d = &l->p->items[i];
	grad = d->type == DL_GRAD;
	iw = d->x;
	ih = d->y;
	d->x = x;
	d->y = y;
	d->w = w > 0 ? w : 0;
	d->h = h > 0 ? h : 0;
	if (!grad && (iw <= 0 || ih <= 0))
		return;
	if (!grad)
		d->flags = DLF_TILE | DLF_ALT;	/* (nothing to draw) */
	if (w <= 0 || (h <= 0 && !grad))
		return;		/* (a gradient of no height yet may grow with its box) */
	if (pw <= 0)
		pw = 1;
	if (ph <= 0)
		ph = 1;
	iw = dev(l, iw << 10);
	ih = dev(l, ih << 10);
	if (iw <= 0)
		iw = 1;
	if (ih <= 0)
		ih = 1;
	sw = s->k[EP_BACKGROUND_SIZE_W] == EK_KW ? s->v[EP_BACKGROUND_SIZE_W].i : 0;
	if (grad) {
		/* no size of its own: the area's, in each way the size doesn't
		   give (CSS Images 3, 5.3, the default sizing algorithm; cover
		   and contain alike) */
		tw = sw == KW_COVER || sw == KW_CONTAIN ? AUTO : len(l, s, EP_BACKGROUND_SIZE_W, pw);
		th = sw == KW_COVER || sw == KW_CONTAIN ? AUTO : len(l, s, EP_BACKGROUND_SIZE_H, ph);
		if (tw == AUTO)
			tw = pw;
		if (th == AUTO) {
			th = ph0 > 0 ? ph0 : 0;
			grow = 1;
		}
	} else if (sw == KW_COVER || sw == KW_CONTAIN) {
		/* as large as covers the area, or as fits in it */
		int wide = (int64_t) pw * ih > (int64_t) ph * iw;
		if (wide == (sw == KW_COVER)) {
			tw = pw;
			th = (int32_t) ((int64_t) ih * pw / iw);
		} else {
			th = ph;
			tw = (int32_t) ((int64_t) iw * ph / ih);
		}
	} else {
		tw = len(l, s, EP_BACKGROUND_SIZE_W, pw);
		th = len(l, s, EP_BACKGROUND_SIZE_H, ph);
		if (tw == AUTO && th == AUTO) {
			tw = iw;
			th = ih;
		} else if (tw == AUTO) {
			tw = (int32_t) ((int64_t) iw * th / ih);
		} else if (th == AUTO) {
			th = (int32_t) ((int64_t) ih * tw / iw);
		}
	}
	if (tw < 1)
		tw = 1;
	if (th < 1 && !grow)
		th = 1;
	/* in the padding box: a percentage is of the room the tile leaves */
	ox = x + bl + len0(l, s, EP_BACKGROUND_POSITION_X, pw - tw);
	oy = y + bt + len0(l, s, EP_BACKGROUND_POSITION_Y, ph - th);
	t = amalloc(l->a, sizeof *t);
	if (t == NULL) {
		l->fail = 1;
		return;
	}
	rep = kw(s, EP_BACKGROUND_REPEAT);
	t->rx = (uint8_t) (rep != KW_NO_REPEAT && rep != KW_REPEAT_Y);
	t->ry = (uint8_t) (rep != KW_NO_REPEAT && rep != KW_REPEAT_X);
	t->w = tw;
	t->h = th;
	t->grow = (uint8_t) grow;
	/* drawn in the border box (or the box it is clipped to), and only
	   where the tiles are */
	x1 = x + w;
	y1 = y + h;
	if (in != NULL) {
		x += in[0];
		y += in[1];
		x1 -= in[2];
		y1 -= in[3];
	}
	if (!t->rx) {
		if (ox > x)
			x = ox;
		if (ox + tw < x1)
			x1 = ox + tw;
	}
	if (!t->ry) {
		if (oy > y)
			y = oy;
		if (oy + th < y1)
			y1 = oy + th;
	}
	if (x1 <= x || (y1 <= y && !grow))
		return;
	if (y1 < y)
		y1 = y;
	d->x = x;
	d->y = y;
	d->w = x1 - x;
	d->h = y1 - y;
	t->dx = ox - x;
	t->dy = oy - y;
	d->tile = t;
	d->flags = DLF_TILE;
}

/* a form control: its frame, then its content as an item drawn from
   its state */
static void control_item(L *l, ITEM *it, int32_t x, int32_t y, int32_t h, int32_t base)
{
	box *b = it->atom;
	fctl *c = l->p->ctls[b->ctl - 1];
	dl_item *d;
	int framed = c->kind != FC_RADIO, button = !form_typed(c) && c->kind != FC_CHECKBOX;
	int32_t in = framed ? 1 : 0;
	if (framed) {
		uint32_t edge = fade(0xff808080, it->alpha);
		fill(l, x, y, it->w, h, fade(c->disabled ? 0xfff0f0f0 : button ? 0xffe0e0e0 : 0xffffffff, it->alpha));
		fill(l, x, y, it->w, 1, edge);
		fill(l, x, y + h - 1, it->w, 1, edge);
		fill(l, x, y, 1, h, edge);
		fill(l, x + it->w - 1, y, 1, h, edge);
	}
	d = dl_add(l);
	if (d == NULL)
		return;
	d->type = DL_CTL;
#ifndef EW_ST
	d->node = c->n;
#endif
	d->x = x + in;
	d->y = y + in;
	d->w = it->w - 2 * in;
	d->h = h - 2 * in;
	d->base = (int16_t) (base - d->y);
	/* its text in its colour; the rest of it as solid as its opacity
	   (browsers draw a box's tick and the like in colours of their own) */
	d->ink = c->disabled ? fade(0xff808080, it->alpha) : it->color;
	d->rgb = (uint32_t) it->alpha << 24 | (d->ink & 0xffffff);
	d->flags = it->flags & DLF_NOHIT;
	d->font = it->font;
	d->ctl = b->ctl;
	d->link = it->link;
	if (c->kind == FC_TEXTAREA)
		d->len = (uint32_t) line_height(l, b->st, it->font);
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
			   (guess ? DLF_NOSIZE : 0) | (sized ? 0 : DLF_ALT) | (it->flags & DLF_NOHIT), it->alpha);
		if (sized)
			return;
	}
	if (b->atom == AT_CONTROL && b->ctl) {
		control_item(l, it, x, y, h, base + it->shift);
	} else {
		ITEM t = *it;
		t.color = fade(0xff606060, it->alpha);
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
				if ((it->bg >> 24) != 0 && !it->hidden && it->kind != IT_BREAK) {
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
	runstate(l, &r, b->st, NULL, NULL, b->link);
#ifndef EW_ST
	{
		box *e;
		for (e = b; e != NULL && e->n == NULL; e = e->parent)
			;
		r.node = e != NULL ? e->n : NULL;
	}
#endif
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
	image_item(l, a, x, y, iw, ih, b->link, font_of(l, b->st),
		   (guess ? DLF_NOSIZE : 0) | (no_hit(b->st) ? DLF_NOHIT : 0), l->alpha);
	return ih;
}

/* ---------------------------------------------------------------- measuring */

static int32_t measure(L *l, box *b);
static int32_t gap(L *l, const ec_style *s, int p, int32_t base);
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
	runstate(l, &r, b->st, NULL, NULL, b->link);
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
	if (EC_DEEP()) {
		l->p->deep = 1;
		return 0;
	}
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
	} else if (b->grid && b->st->k[EP_GRID_TEMPLATE_COLUMNS] == EK_PTR) {
		/* its columns' lengths, the others as wide as its widest item */
		const ec_tracks *t = b->st->v[EP_GRID_TEMPLATE_COLUMNS].p;
		int32_t widest = 0, g = gap(l, b->st, EP_COLUMN_GAP, AUTO);
		int k;
		for (c = b->first; c != NULL; c = c->next) {
			int32_t m = measure(l, c);
			if (m > widest)
				widest = m;
		}
		for (k = 0; k < t->n; k++)
			inner += (t->t[k].mx.k == EC_TR_LEN ? dev(l, t->t[k].mx.v) :
				  t->t[k].mn.k == EC_TR_LEN ? dev(l, t->t[k].mn.v) : widest) + (k ? g : 0);
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
		int k = 0;
		for (c = b->first; c != NULL; c = c->next, k++) {
			int32_t m = measure(l, c);
			if (b->row && !(b->flex && b->wrap && l->minw))
				inner += m;
			else if (m > inner)
				inner = m;
		}
		if (b->flex && b->row && k > 1 && !(b->wrap && l->minw))
			inner += gap(l, b->st, EP_COLUMN_GAP, AUTO) * (k - 1);
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
	runstate(l, &r, st, NULL, NULL, 0);
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

/* a background item made dh taller: a gradient as tall as its box with
   it */
static void taller(dl_item *d, int32_t dh)
{
	d->h += dh;
	if (d->type == DL_GRAD && d->tile != NULL && d->tile->grow)
		((dl_tile *) d->tile)->h += dh;
}

/* a block's background and borders (as layout_block left them in
   last_bg, last_bd) made dh taller */
static void stretch(L *l, long bg, const long *bd, int32_t dh)
{
	dl_item *it = l->p->items;
	if (dh <= 0)
		return;
	if (bg >= 0) {
		taller(&it[bg], dh);
		if (it[bg].type == DL_FILL && (it[bg].flags & DLF_TILE))
			taller(&it[bg + 1], dh);	/* its picture or gradient */
	}
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
		if (k == bg || (bg >= 0 && k == bg + 1 && l->p->items[bg].type == DL_FILL &&
				(l->p->items[bg].flags & DLF_TILE)))
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

/* children side by side: a table row's cells */
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
/* ---------------------------------------------------------------- flex */

/* a flex container's items, laid out after CSS Flexbox's algorithm (9.2
   to 9.9): each item's base size (flex-basis, else its width, else its
   widest), shared out by flex-grow or taken back by flex-shrink (each
   kept within its min and max: an automatic min is its narrowest), lines
   when it wraps, justify-content along a line, align-items, align-self
   and align-content across. Sizes are outer (margins, borders and
   padding in) along the main axis; items are laid out at their place
   along it, then moved across (their display items) where alignment puts
   them, or stretched */
typedef struct {
	box *b;
	int32_t base, hyp, mn, mx, size;	/* along the main axis, outer */
	int32_t inner;				/* the base less margins, borders, padding (shrinking's weight) */
	int32_t grow, shrink;			/* ec_fx */
	int32_t pos, cross, f;			/* its place on its line; laid out: across, its first baseline */
	int32_t top;				/* where it was laid out across */
	long k0, k1, bg, bd[4];
	int line, frozen, align, autos, order;
} FITEM;

/* gap: row-gap or column-gap, normal 0 */
static int32_t gap(L *l, const ec_style *s, int p, int32_t base)
{
	int32_t g = len(l, s, p, base);
	return g == AUTO || g < 0 ? 0 : g;
}

/* an item's align-self, auto being its container's align-items; normal
   is stretch, start-ish are flex-start */
static int fl_align(const FITEM *it, int ai)
{
	int a = it->b->anon ? KW_AUTO : kw(it->b->st, EP_ALIGN_SELF);
	if (a == KW_AUTO || a == 0)
		a = ai;
	if (a == KW_NORMAL || a == 0)
		a = KW_STRETCH;
	if (a == KW_START || a == KW_SELF_START)
		a = KW_FLEX_START;
	if (a == KW_END || a == KW_SELF_END)
		a = KW_FLEX_END;
	return a;
}

/* the display items from k0 to k1 moved */
static void shift_range(L *l, long k0, long k1, int32_t dx, int32_t dy)
{
	long k;
	if (dx == 0 && dy == 0)
		return;
	for (k = k0; k < k1; k++) {
		l->p->items[k].x += dx;
		l->p->items[k].y += dy;
	}
}

/* where free room puts a line's first item and what goes between them
   (justify-content or align-content's distribution) */
static void distribute(int how, int32_t free, int n, int32_t *start, int32_t *between)
{
	*start = 0;
	*between = 0;
	if (free < 0 && (how == KW_SPACE_BETWEEN))
		how = KW_FLEX_START;
	if (free < 0 && (how == KW_SPACE_AROUND || how == KW_SPACE_EVENLY))
		how = KW_CENTER;
	switch (how) {
	case KW_FLEX_END: case KW_END: case KW_RIGHT:
		*start = free;
		break;
	case KW_CENTER:
		*start = free / 2;
		break;
	case KW_SPACE_BETWEEN:
		if (n > 1)
			*between = free / (n - 1);
		break;
	case KW_SPACE_AROUND:
		*between = free / n;
		*start = *between / 2;
		break;
	case KW_SPACE_EVENLY:
		*between = free / (n + 1);
		*start = *between;
		break;
	default:
		break;
	}
}

/* a line's items' sizes along the main axis: room shared out by
   flex-grow, or what is over taken back by flex-shrink (by flex-shrink
   times its base), each within its min and max (9.7) */
static void flex_resolve(FITEM *it, int n, int32_t room)
{
	int32_t used = 0, init_free;
	int i, growing, round;
	for (i = 0; i < n; i++)
		used += it[i].hyp;
	growing = used < room;
	for (i = 0; i < n; i++) {
		it[i].size = it[i].hyp;
		it[i].frozen = (growing ? it[i].grow == 0 : it[i].shrink == 0) ||
			       (growing && it[i].base > it[i].hyp) || (!growing && it[i].base < it[i].hyp);
	}
	init_free = room;
	for (i = 0; i < n; i++)
		init_free -= it[i].frozen ? it[i].size : it[i].base;
	for (round = 0; round <= n; round++) {
		int64_t sum = 0, sumw = 0;
		int32_t free = room, viol = 0;
		for (i = 0; i < n; i++) {
			if (it[i].frozen) {
				free -= it[i].size;
			} else {
				free -= it[i].base;
				sum += growing ? it[i].grow : it[i].shrink;
				sumw += (int64_t) it[i].shrink * it[i].inner;
			}
		}
		if (sum == 0)
			break;
		/* factors adding to less than 1 take only that share of the room */
		if (sum < 1024) {
			int32_t part = (int32_t) ((int64_t) init_free * sum / 1024);
			if (growing ? part < free : part > free)
				free = part;
		}
		for (i = 0; i < n; i++) {
			int32_t want, got;
			if (it[i].frozen)
				continue;
			if (growing)
				want = it[i].base + (int32_t) ((int64_t) free * it[i].grow / sum);
			else
				want = it[i].base + (sumw > 0 ? (int32_t) ((int64_t) free * it[i].shrink * it[i].inner / sumw) : 0);
			got = want;
			if (got > it[i].mx)
				got = it[i].mx;
			if (got < it[i].mn)
				got = it[i].mn;
			it[i].size = got;
			viol += got - want;
		}
		for (i = 0; i < n; i++) {
			if (it[i].frozen)
				continue;
			if (viol == 0 || (viol > 0 && it[i].size == it[i].mn) || (viol < 0 && it[i].size == it[i].mx))
				it[i].frozen = 1;
		}
		if (viol == 0)
			break;
	}
}

static int32_t flex_layout(L *l, box *b, int32_t x, int32_t y, int32_t w, int32_t hdef, int32_t *first)
{
	const ec_style *s = b->st;
	int dir = kw(s, EP_FLEX_DIRECTION), column = dir == KW_COLUMN || dir == KW_COLUMN_REVERSE;
	int rev = dir == KW_ROW_REVERSE || dir == KW_COLUMN_REVERSE;
	int wrap = !column && b->wrap, wrev = kw(s, EP_FLEX_WRAP) == KW_WRAP_REVERSE;
	int jc = kw(s, EP_JUSTIFY_CONTENT), ai = kw(s, EP_ALIGN_ITEMS), ac = kw(s, EP_ALIGN_CONTENT);
	int32_t cgap = gap(l, s, EP_COLUMN_GAP, w), rgap = gap(l, s, EP_ROW_GAP, hdef);
	int n = 0, i, j, nl = 0;
	int32_t total = 0, *ltop = NULL, *lh = NULL, *lstart = NULL;
	FITEM *it;
	box *c;

	for (c = b->first; c != NULL; c = c->next)
		n++;
	if (n == 0)
		return 0;
	it = calloc((size_t) n, sizeof *it);
	lh = malloc((size_t) (n + 1) * sizeof *lh);
	ltop = malloc((size_t) (n + 1) * sizeof *ltop);
	lstart = malloc((size_t) (n + 1) * sizeof *lstart);
	if (it == NULL || lh == NULL || ltop == NULL || lstart == NULL) {
		free(it);
		free(lh);
		free(ltop);
		free(lstart);
		l->fail = 1;
		return 0;
	}
	/* the items, by order (as they come when it is the same); those
	   positioned absolutely are out of the flow */
	n = 0;
	for (c = b->first; c != NULL; c = c->next) {
		FITEM t;
		if (!c->anon && (kw(c->st, EP_POSITION) == KW_ABSOLUTE || kw(c->st, EP_POSITION) == KW_FIXED)) {
			place_abs(l, c, x, y);
			continue;
		}
		memset(&t, 0, sizeof t);
		t.b = c;
		t.order = c->anon || c->st->k[EP_ORDER] != EK_INT ? 0 : c->st->v[EP_ORDER].i;
		for (j = n; j > 0 && it[j - 1].order > t.order; j--)
			it[j] = it[j - 1];
		it[j] = t;
		n++;
	}
	if (n == 0)
		goto done;

	if (!column) {
		/* each item's base size, its min and max, along the row */
		for (i = 0; i < n; i++) {
			const ec_style *cs = it[i].b->st;
			int anon = it[i].b->anon;
			int32_t ex = extras(l, it[i].b), bw = AUTO, v;
			int bbox = !anon && kw(cs, EP_BOX_SIZING) == KW_BORDER_BOX;
			int32_t mex = anon ? 0 : len0(l, cs, EP_MARGIN_LEFT, w) + len0(l, cs, EP_MARGIN_RIGHT, w);
			if (!anon && cs->k[EP_FLEX_BASIS] != EK_KW)
				bw = len(l, cs, EP_FLEX_BASIS, w);
			if (bw != AUTO)
				it[i].base = bw + (bbox ? mex : ex);
			else
				it[i].base = cell_pref(l, it[i].b, w);
			it[i].inner = it[i].base - ex > 0 ? it[i].base - ex : 0;
			it[i].mx = INT32_MAX;
			v = anon ? AUTO : len(l, cs, EP_MAX_WIDTH, w);
			if (v != AUTO)
				it[i].mx = v + (bbox ? mex : ex);
			v = anon ? AUTO : len(l, cs, EP_MIN_WIDTH, w);
			if (v != AUTO) {
				it[i].mn = v + (bbox ? mex : ex);
			} else if (anon || kw(cs, EP_OVERFLOW_X) == KW_VISIBLE) {
				/* automatic: its narrowest (a word), no wider than its width */
				int was = l->minw;
				int32_t sw = anon ? AUTO : len(l, cs, EP_WIDTH, w);
				l->minw = 1;
				it[i].mn = measure(l, it[i].b);
				l->minw = was;
				if (sw != AUTO && sw + (bbox ? mex : ex) < it[i].mn)
					it[i].mn = sw + (bbox ? mex : ex);
				if (it[i].mn > it[i].mx)
					it[i].mn = it[i].mx;
			}
			it[i].hyp = it[i].base < it[i].mn ? it[i].mn : it[i].base > it[i].mx ? it[i].mx : it[i].base;
			it[i].grow = anon || cs->k[EP_FLEX_GROW] != EK_NUM ? 0 : cs->v[EP_FLEX_GROW].i;
			it[i].shrink = anon ? 1024 : cs->k[EP_FLEX_SHRINK] != EK_NUM ? 1024 : cs->v[EP_FLEX_SHRINK].i;
			it[i].align = fl_align(&it[i], ai);
			it[i].autos = !anon && ((cs->k[EP_MARGIN_LEFT] == EK_KW && kw(cs, EP_MARGIN_LEFT) == KW_AUTO) +
						(cs->k[EP_MARGIN_RIGHT] == EK_KW && kw(cs, EP_MARGIN_RIGHT) == KW_AUTO));
		}
		/* lines: as many items as go in the width (one, when it doesn't wrap) */
		for (i = 0; i < n; ) {
			int32_t sum = 0;
			lstart[nl] = i;
			for (j = i; j < n; j++) {
				int32_t add = it[j].hyp + (j > i ? cgap : 0);
				if (wrap && j > i && sum + add > w)
					break;
				sum += add;
				it[j].line = nl;
			}
			i = j;
			nl++;
		}
		lstart[nl] = n;
		/* each line: sizes, places along it, laid out */
		for (j = 0; j < nl; j++) {
			int a = lstart[j], e = lstart[j + 1], k, nauto = 0;
			int32_t free, start, between, pos, maxasc = AUTO, h = 0;
			flex_resolve(it + a, e - a, w - cgap * (e - a - 1));
			free = w - cgap * (e - a - 1);
			for (k = a; k < e; k++) {
				free -= it[k].size;
				nauto += it[k].autos;
			}
			if (nauto > 0 && free > 0) {
				/* auto margins take the room, justify-content none */
				int32_t each = free / nauto;
				start = 0;
				between = 0;
				pos = 0;
				for (k = a; k < e; k++) {
					const ec_style *cs = it[k].b->st;
					if (!it[k].b->anon && cs->k[EP_MARGIN_LEFT] == EK_KW && kw(cs, EP_MARGIN_LEFT) == KW_AUTO)
						pos += each;
					it[k].pos = pos;
					pos += it[k].size + cgap;
					if (!it[k].b->anon && cs->k[EP_MARGIN_RIGHT] == EK_KW && kw(cs, EP_MARGIN_RIGHT) == KW_AUTO)
						pos += each;
				}
			} else {
				distribute(jc, free, e - a, &start, &between);
				pos = start;
				for (k = a; k < e; k++) {
					it[k].pos = pos;
					pos += it[k].size + cgap + between;
				}
			}
			ltop[j] = y + total;
			for (k = a; k < e; k++) {
				int32_t mb, f = AUTO, ix = rev ? x + w - it[k].pos - it[k].size : x + it[k].pos;
				it[k].k0 = l->p->nitems;
				it[k].cross = layout_block(l, it[k].b, ix, ltop[j], it[k].size, 1, &mb, &f);
				it[k].k1 = l->p->nitems;
				it[k].bg = l->last_bg;
				memcpy(it[k].bd, l->last_bd, sizeof it[k].bd);
				it[k].f = f;
				it[k].top = ltop[j];
				if (it[k].align == KW_BASELINE && f != AUTO && f - ltop[j] > maxasc)
					maxasc = f - ltop[j];
			}
			/* baseline items: their first lines' baselines lined up */
			for (k = a; k < e; k++) {
				int32_t d = 0;
				if (it[k].align == KW_BASELINE && it[k].f != AUTO && maxasc != AUTO)
					d = maxasc - (it[k].f - ltop[j]);
				if (d > 0) {
					shift_range(l, it[k].k0, it[k].k1, 0, d);
					it[k].top += d;
					if (it[k].f != AUTO)
						it[k].f += d;
				}
				if (it[k].top - ltop[j] + it[k].cross > h)
					h = it[k].top - ltop[j] + it[k].cross;
			}
			/* a line that doesn't wrap fills a container of a set height */
			if (!wrap && hdef != AUTO && hdef > h)
				h = hdef;
			lh[j] = h;
			total += h + (j < nl - 1 ? rgap : 0);
		}
		/* the lines across: align-content in a container of a set
		   height (stretch: each line taller), wrap-reverse from the
		   bottom up */
		{
			int32_t room = hdef != AUTO && wrap ? hdef - total : 0, start = 0, between = 0, extra = 0, at;
			int how = ac == KW_NORMAL || ac == 0 ? KW_STRETCH : ac;
			if (room > 0) {
				if (how == KW_STRETCH)
					extra = room / nl;
				else
					distribute(how, room, nl, &start, &between);
			}
			at = start;
			for (j = 0; j < nl; j++) {
				int32_t top;
				lh[j] += extra;
				top = wrev ? y + (hdef != AUTO && wrap ? hdef : total + nl * extra) - at - lh[j] : y + at;
				for (i = lstart[j]; i < lstart[j + 1]; i++) {
					int32_t d = top - ltop[j], room2 = lh[j] - (it[i].top - ltop[j]) - it[i].cross;
					/* within its line: align-self */
					if (it[i].align == KW_CENTER)
						d += room2 / 2;
					else if (it[i].align == KW_FLEX_END)
						d += room2;
					shift_range(l, it[i].k0, it[i].k1, 0, d);
					if (it[i].f != AUTO)
						it[i].f += d;
					if (it[i].align == KW_STRETCH && (it[i].b->anon || it[i].b->st->k[EP_HEIGHT] == EK_KW))
						stretch(l, it[i].bg, it[i].bd, room2);
				}
				at += lh[j] + rgap + between;
			}
			total += nl * extra;
			if (hdef != AUTO && wrap && hdef > total)
				total = hdef;
		}
		for (i = 0; i < n && *first == AUTO; i++)
			if (it[i].line == 0 && it[i].f != AUTO)
				*first = it[i].f;
	} else {
		/* a column: each item as wide as align-self makes it, laid out
		   down in turn; in a container of a set height, flex-grow
		   shares the room left and justify-content places them */
		int32_t cy = 0, free, start, between, sumgrow = 0, given = 0;
		for (i = 0; i < n; i++) {
			const ec_style *cs = it[i].b->st;
			int anon = it[i].b->anon;
			int32_t iw = w, mb, f = AUTO, ox = 0, bh;
			it[i].align = fl_align(&it[i], ai);
			it[i].grow = anon || cs->k[EP_FLEX_GROW] != EK_NUM ? 0 : cs->v[EP_FLEX_GROW].i;
			if (!(it[i].align == KW_STRETCH && (anon || cs->k[EP_WIDTH] == EK_KW))) {
				iw = cell_pref(l, it[i].b, w);
				if (iw > w)
					iw = w;
				if (it[i].align == KW_CENTER)
					ox = (w - iw) / 2;
				else if (it[i].align == KW_FLEX_END)
					ox = w - iw;
			}
			it[i].k0 = l->p->nitems;
			bh = layout_block(l, it[i].b, x + ox, y + cy, iw, 1, &mb, &f);
			it[i].k1 = l->p->nitems;
			it[i].bg = l->last_bg;
			memcpy(it[i].bd, l->last_bd, sizeof it[i].bd);
			/* a flex-basis of a length makes it at least that tall */
			if (!anon && cs->k[EP_FLEX_BASIS] == EK_PX && cs->k[EP_HEIGHT] == EK_KW) {
				int32_t want = dev(l, cs->v[EP_FLEX_BASIS].i) + len0(l, cs, EP_MARGIN_TOP, w) +
					       len0(l, cs, EP_MARGIN_BOTTOM, w) + len0(l, cs, EP_PADDING_TOP, w) +
					       len0(l, cs, EP_PADDING_BOTTOM, w) +
					       (kw(cs, EP_BORDER_TOP_STYLE) > KW_HIDDEN ? len0(l, cs, EP_BORDER_TOP_WIDTH, AUTO) : 0) +
					       (kw(cs, EP_BORDER_BOTTOM_STYLE) > KW_HIDDEN ? len0(l, cs, EP_BORDER_BOTTOM_WIDTH, AUTO) : 0);
				if (want > bh) {
					stretch(l, it[i].bg, it[i].bd, want - bh);
					bh = want;
				}
			}
			it[i].pos = cy;
			it[i].size = bh;
			it[i].f = f;
			sumgrow += it[i].grow;
			cy += bh + (i < n - 1 ? rgap : 0);
		}
		total = cy;
		free = hdef != AUTO ? hdef - total : 0;
		if (free > 0 && sumgrow > 0) {
			/* taller by flex-grow, those below moved down */
			for (i = 0; i < n; i++) {
				int32_t add = (int32_t) ((int64_t) free * it[i].grow / sumgrow);
				shift_range(l, it[i].k0, it[i].k1, 0, given);
				it[i].pos += given;
				if (add > 0)
					stretch(l, it[i].bg, it[i].bd, add);
				it[i].size += add;
				given += add;
			}
			total += given;
			free -= given;
		}
		distribute(jc, free, n, &start, &between);
		for (i = 0; i < n; i++) {
			int32_t place = start + it[i].pos + i * between, d;
			if (rev) {
				int32_t span = hdef != AUTO ? hdef : total;
				place = span - place - it[i].size;
			}
			d = place - it[i].pos;
			shift_range(l, it[i].k0, it[i].k1, 0, d);
			if (it[i].f != AUTO)
				it[i].f += d;
		}
		for (i = 0; i < n && *first == AUTO; i++)
			if (it[i].f != AUTO)
				*first = it[i].f;
	}
done:
	free(it);
	free(lh);
	free(ltop);
	free(lstart);
	return total;
}

/* ---------------------------------------------------------------- grid */

/* a grid container's items, after CSS Grid 1, in a first form: the
   explicit grid from grid-template-columns, -rows and -areas (repeat(),
   auto-fill and auto-fit), each item placed by its lines (numbers, spans,
   areas' names) or auto-placed (row by row or column by column, sparse),
   the grid growing by grid-auto-columns and -rows. Columns are sized
   from the width (lengths, percentages, min-content, max-content, auto,
   minmax(), fit-content() and fr, the items of one column giving its
   content sizes, those spanning more made room for), rows by what their
   items need (lengths, and fr in a grid of a set height); items are
   stretched or aligned in their areas by justify-self and align-self.
   Line names other than areas', subgrid and masonry are not taken */
typedef struct {
	box *b;
	int r0, r1, c0, c1;			/* its area: rows r0..r1-1, columns c0..c1-1 */
	int32_t h, f, top;			/* laid out: outer height, baseline, top */
	long k0, k1, bg, bd[4];
	int ja, aa;				/* justify-self, align-self */
} GITEM;

typedef struct {
	int32_t base, lim;
	ec_fx fr;				/* flexible: its fr, else 0 */
	int stretchy;				/* auto at its max: takes room left */
} GTRACK;

/* the track list's size at track i of an axis: the explicit list's (an
   auto repeat expanded nrep times), else the auto tracks' in turn */
static void gtrack_def(const ec_style *s, int tp, int ap, int i, int nrep, ec_tsize *mn, ec_tsize *mx)
{
	const ec_tracks *t = s->k[tp] == EK_PTR ? s->v[tp].p : NULL, *a = s->k[ap] == EK_PTR ? s->v[ap].p : NULL;
	int n = 0, k;
	if (t != NULL) {
		n = t->rep ? t->n - t->rep_n + t->rep_n * nrep : t->n;
		if (i < n) {
			if (t->rep && i >= t->rep_at)
				k = i < t->rep_at + t->rep_n * nrep ? t->rep_at + (i - t->rep_at) % t->rep_n :
					i - t->rep_n * (nrep - 1);
			else
				k = i;
			*mn = t->t[k].mn;
			*mx = t->t[k].mx;
			return;
		}
	}
	if (a != NULL) {
		k = (i - n) % a->n;
		*mn = a->t[k].mn;
		*mx = a->t[k].mx;
		return;
	}
	mn->k = mx->k = EC_TR_AUTO;
	mn->v = mx->v = 0;
}

/* a track size in px against base (a percentage's; AUTO: none), or AUTO
   when it isn't a length */
static int32_t gsize_px(L *l, const ec_tsize *z, int32_t base)
{
	if (z->k == EC_TR_LEN || z->k == EC_TR_FIT)
		return dev(l, z->v);
	if (z->k == EC_TR_PCT)
		return base == AUTO ? AUTO : (int32_t) ((int64_t) base * z->v / (100 * 1024));
	return AUTO;
}

/* how many times an auto repeat goes in avail (at least once) */
static int grid_repeats(L *l, const ec_tracks *t, int32_t avail, int32_t g)
{
	int32_t fixed = 0, rep = 0, k, n;
	if (t == NULL || !t->rep || avail == AUTO)
		return 1;
	for (k = 0; k < t->n; k++) {
		int32_t v = gsize_px(l, &t->t[k].mx, avail);
		if (v == AUTO)
			v = gsize_px(l, &t->t[k].mn, avail);
		if (v == AUTO)
			v = 0;
		if (k >= t->rep_at && k < t->rep_at + t->rep_n)
			rep += v + g;
		else
			fixed += v + g;
	}
	if (rep <= 0)
		return 1;
	n = (avail - fixed + g) / rep;
	return n < 1 ? 1 : n > 200 ? 200 : n;
}

/* the explicit tracks of an axis */
static int grid_explicit(const ec_tracks *t, int nrep)
{
	if (t == NULL)
		return 0;
	return t->rep ? t->n - t->rep_n + t->rep_n * nrep : t->n;
}

/* an item's lines on an axis from its start and end: *a..*b (b past),
   -1 for auto; span set when it only says how many */
static void grid_lines_of(const ec_style *s, int ps, int pe, const ec_areas *ar, int rows, int nexp,
			  int *a, int *b, int *span)
{
	int k, i;
	*a = *b = -1;
	*span = 1;
	for (k = 0; k < 2; k++) {
		int p = k ? pe : ps, v, at = -1;
		if (s->k[p] == EK_INT) {
			v = s->v[p].i;
			if (v >= EC_GRID_SPAN) {
				*span = v - EC_GRID_SPAN;
				continue;
			}
			at = v > 0 ? v - 1 : nexp + 1 + v;
			if (at < 0)
				at = 0;
		} else if (s->k[p] == EK_PTR && ar != NULL) {
			/* an area's name: its first line, or for the end its last */
			const ec_str *nm = s->v[p].p;
			int lo = -1, hi = -1;
			for (i = 0; i < ar->rows * ar->cols; i++)
				if (ar->name[i] != NULL && ar->name[i]->len == nm->len &&
				    memcmp(ar->name[i]->s, nm->s, nm->len) == 0) {
					int pos = rows ? i / ar->cols : i % ar->cols;
					if (lo < 0 || pos < lo)
						lo = pos;
					if (pos > hi)
						hi = pos;
				}
			if (lo >= 0)
				at = k ? hi + 1 : lo;
		}
		if (k)
			*b = at;
		else
			*a = at;
	}
	if (*a >= 0 && *b >= 0) {
		if (*b < *a) {
			int t = *a;
			*a = *b;
			*b = t;
		}
		if (*b == *a)
			*b = *a + 1;
		*span = *b - *a;
	} else if (*a < 0 && *b >= 0) {
		*a = *b - *span;
		if (*a < 0)
			*a = 0;
		*b = *a + *span;
	} else if (*a >= 0) {
		*b = *a + *span;
	}
}

/* the cells taken: rows x cols, growing as rows come */
typedef struct {
	unsigned char *m;
	int rows, cols;
} GOCC;

static int gocc_fits(GOCC *o, int r, int c, int nr, int nc)
{
	int i, j;
	if (c + nc > o->cols)
		return 0;
	for (i = r; i < r + nr; i++)
		for (j = c; j < c + nc; j++)
			if (i < o->rows && o->m[i * o->cols + j])
				return 0;
	return 1;
}

static int gocc_take(GOCC *o, int r, int c, int nr, int nc)
{
	int i, j;
	if (r + nr > o->rows) {
		int rows = r + nr + 8;
		unsigned char *m = realloc(o->m, (size_t) rows * o->cols);
		if (m == NULL)
			return 0;
		memset(m + (size_t) o->rows * o->cols, 0, (size_t) (rows - o->rows) * o->cols);
		o->m = m;
		o->rows = rows;
	}
	for (i = r; i < r + nr; i++)
		for (j = c; j < c + nc && j < o->cols; j++)
			o->m[i * o->cols + j] = 1;
	return 1;
}

/* justify-self or align-self, auto being the container's -items, normal
   stretch, start-ish start */
static int g_align(int a, int items)
{
	if (a == KW_AUTO || a == 0)
		a = items;
	if (a == KW_NORMAL || a == 0)
		a = KW_STRETCH;
	if (a == KW_START || a == KW_SELF_START || a == KW_FLEX_START || a == KW_LEFT)
		a = KW_FLEX_START;
	if (a == KW_END || a == KW_SELF_END || a == KW_FLEX_END || a == KW_RIGHT)
		a = KW_FLEX_END;
	return a;
}

/* the tracks' sizes in room (each base grown to its limit as room
   allows, the rest to fr tracks, or the auto ones when there are none) */
static void grid_fit(GTRACK *t, int n, int32_t room, int fill_auto)
{
	int32_t used = 0, left;
	int64_t frsum = 0;
	int i, k, nflex = 0, ngrow;
	for (i = 0; i < n; i++) {
		used += t[i].base;
		if (t[i].fr > 0) {
			frsum += t[i].fr;
			nflex++;
		}
	}
	left = room - used;
	/* grow the others to their limits, evenly */
	for (k = 0; k < n && left > 0; k++) {
		int32_t each;
		ngrow = 0;
		for (i = 0; i < n; i++)
			if (t[i].fr == 0 && t[i].lim > t[i].base)
				ngrow++;
		if (ngrow == 0)
			break;
		each = left / ngrow > 0 ? left / ngrow : 1;
		for (i = 0; i < n && left > 0; i++)
			if (t[i].fr == 0 && t[i].lim > t[i].base) {
				int32_t add = t[i].lim - t[i].base < each ? t[i].lim - t[i].base : each;
				if (add > left)
					add = left;
				t[i].base += add;
				left -= add;
			}
	}
	if (nflex > 0 && left + 0 > 0) {
		/* fr: the room's share each, none below its base (those
		   bigger keep it, and the share is worked out again) */
		int32_t pool = left;
		unsigned char fixed[256];
		memset(fixed, 0, sizeof fixed);
		for (i = 0; i < n; i++)
			if (t[i].fr > 0)
				pool += t[i].base;
		for (k = 0; k < 4; k++) {
			int64_t fs = 0;
			int32_t p = pool, again = 0;
			for (i = 0; i < n && i < 256; i++)
				if (t[i].fr > 0) {
					if (fixed[i])
						p -= t[i].base;
					else
						fs += t[i].fr;
				}
			if (fs == 0)
				break;
			if (fs < 1024)
				fs = 1024;
			for (i = 0; i < n && i < 256; i++)
				if (t[i].fr > 0 && !fixed[i] && (int32_t) ((int64_t) p * t[i].fr / fs) < t[i].base) {
					fixed[i] = 1;
					again = 1;
				}
			if (again)
				continue;
			for (i = 0; i < n && i < 256; i++)
				if (t[i].fr > 0 && !fixed[i])
					t[i].base = (int32_t) ((int64_t) p * t[i].fr / fs);
			break;
		}
		(void) frsum;
	} else if (left > 0 && fill_auto) {
		/* no fr: the auto tracks share what is left (justify-content normal) */
		int na = 0;
		for (i = 0; i < n; i++)
			na += t[i].stretchy;
		for (i = 0; i < n && na > 0; i++)
			if (t[i].stretchy) {
				int32_t add = left / na;
				t[i].base += add;
				left -= add;
				na--;
			}
	}
}

/* not inlined: layout_block is on the stack once a level of the page, and
   would carry grid's locals in each */
static __attribute__((noinline)) int32_t grid_layout(L *l, box *b, int32_t x, int32_t y, int32_t w, int32_t hdef,
						     int32_t *first)
{
	const ec_style *s = b->st;
	const ec_tracks *tc = s->k[EP_GRID_TEMPLATE_COLUMNS] == EK_PTR ? s->v[EP_GRID_TEMPLATE_COLUMNS].p : NULL;
	const ec_tracks *tr = s->k[EP_GRID_TEMPLATE_ROWS] == EK_PTR ? s->v[EP_GRID_TEMPLATE_ROWS].p : NULL;
	const ec_areas *ar = s->k[EP_GRID_TEMPLATE_AREAS] == EK_PTR ? s->v[EP_GRID_TEMPLATE_AREAS].p : NULL;
	int flow = s->k[EP_GRID_AUTO_FLOW] == EK_INT ? s->v[EP_GRID_AUTO_FLOW].i : 0;
	int32_t cg = gap(l, s, EP_COLUMN_GAP, w), rg = gap(l, s, EP_ROW_GAP, hdef), total = 0;
	int crep = grid_repeats(l, tc, w, cg), rrep = grid_repeats(l, tr, hdef, rg);
	int ncx = grid_explicit(tc, crep), nrx = grid_explicit(tr, rrep), ncols, nrows, n = 0, i, j, k;
	int ji = kw(s, EP_JUSTIFY_ITEMS), ai = kw(s, EP_ALIGN_ITEMS), jc = kw(s, EP_JUSTIFY_CONTENT);
	GITEM *it;
	GTRACK *col = NULL, *row = NULL;
	GOCC occ = { NULL, 0, 0 };
	int32_t *cx = NULL, *ry = NULL;
	box *c;

	if (ar != NULL) {
		if (ar->cols > ncx)
			ncx = ar->cols;
		if (ar->rows > nrx)
			nrx = ar->rows;
	}
	for (c = b->first; c != NULL; c = c->next)
		n++;
	if (n == 0)
		return hdef != AUTO ? 0 : 0;
	it = calloc((size_t) n, sizeof *it);
	if (it == NULL) {
		l->fail = 1;
		return 0;
	}
	/* each item's lines where it says */
	n = 0;
	ncols = ncx > 0 ? ncx : 1;
	nrows = nrx;
	for (c = b->first; c != NULL; c = c->next) {
		GITEM *g = &it[n];
		int sr, sc;
		if (!c->anon && (kw(c->st, EP_POSITION) == KW_ABSOLUTE || kw(c->st, EP_POSITION) == KW_FIXED)) {
			place_abs(l, c, x, y);
			continue;
		}
		g->b = c;
		if (c->anon) {
			g->r0 = g->c0 = -1;
			g->r1 = g->c1 = 1;
		} else {
			grid_lines_of(c->st, EP_GRID_ROW_START, EP_GRID_ROW_END, ar, 1, nrx, &g->r0, &g->r1, &sr);
			grid_lines_of(c->st, EP_GRID_COLUMN_START, EP_GRID_COLUMN_END, ar, 0, ncx, &g->c0, &g->c1, &sc);
			if (g->r0 < 0)
				g->r1 = sr;
			if (g->c0 < 0)
				g->c1 = sc;
		}
		if (g->c0 >= 0 && g->c1 > ncols)
			ncols = g->c1;
		if (g->c0 < 0 && g->c1 > ncols)
			ncols = g->c1;
		if (g->r0 >= 0 && g->r1 > nrows)
			nrows = g->r1;
		if (ncols > 1000)
			ncols = 1000;
		g->ja = g_align(c->anon ? KW_AUTO : kw(c->st, EP_JUSTIFY_SELF), ji);
		g->aa = g_align(c->anon ? KW_AUTO : kw(c->st, EP_ALIGN_SELF), ai);
		n++;
	}
	/* placing: those with both lines; then auto-placement in order (row
	   by row, or column by column), each from where the last went */
	occ.cols = ncols;
	{
		int cr = 0, cc = 0;
		for (i = 0; i < n; i++)
			if (it[i].r0 >= 0 && it[i].c0 >= 0)
				gocc_take(&occ, it[i].r0, it[i].c0, it[i].r1 - it[i].r0, it[i].c1 - it[i].c0);
		for (i = 0; i < n; i++) {
			GITEM *g = &it[i];
			int sr = g->r0 >= 0 ? g->r1 - g->r0 : g->r1, sc = g->c0 >= 0 ? g->c1 - g->c0 : g->c1, r, cc2;
			if (g->r0 >= 0 && g->c0 >= 0)
				continue;
			if (sc > ncols)
				sc = ncols;
			if (g->r0 >= 0) {
				/* its row set: the first columns free there */
				for (cc2 = 0; cc2 + sc <= ncols && !gocc_fits(&occ, g->r0, cc2, sr, sc); cc2++)
					;
				if (cc2 + sc > ncols)
					cc2 = 0;
				g->c0 = cc2;
				g->c1 = cc2 + sc;
			} else if (g->c0 >= 0) {
				/* its column set: down from the cursor's row */
				for (r = (flow & EC_FLOW_COLUMN) ? 0 : cr; !gocc_fits(&occ, r, g->c0, sr, g->c1 - g->c0); r++)
					;
				g->r0 = r;
				g->r1 = r + sr;
				if (!(flow & EC_FLOW_COLUMN)) {
					cr = r;
					cc = g->c1;
				}
			} else if (!(flow & EC_FLOW_COLUMN)) {
				for (;;) {
					if (cc + sc > ncols) {
						cc = 0;
						cr++;
						continue;
					}
					if (gocc_fits(&occ, cr, cc, sr, sc))
						break;
					cc++;
				}
				g->r0 = cr;
				g->r1 = cr + sr;
				g->c0 = cc;
				g->c1 = cc + sc;
				cc += sc;
			} else {
				/* column by column, down the explicit rows */
				int rows = nrx > 0 ? nrx : 1;
				for (;;) {
					if (cr + sr > rows && cr > 0) {
						cr = 0;
						cc++;
						continue;
					}
					if (cc + sc > ncols) {
						/* past the columns there are: more on the right */
						int nc = cc + sc;
						unsigned char *m = calloc((size_t) (occ.rows ? occ.rows : 1) * nc, 1);
						if (m == NULL)
							break;
						for (r = 0; r < occ.rows; r++)
							memcpy(m + r * nc, occ.m + r * occ.cols, (size_t) occ.cols);
						free(occ.m);
						occ.m = m;
						occ.cols = ncols = nc;
					}
					if (gocc_fits(&occ, cr, cc, sr, sc))
						break;
					cr++;
				}
				g->r0 = cr;
				g->r1 = cr + sr;
				g->c0 = cc;
				g->c1 = cc + sc;
				cr += sr;
			}
			gocc_take(&occ, g->r0, g->c0, g->r1 - g->r0, g->c1 - g->c0);
			if (g->r1 > nrows)
				nrows = g->r1;
		}
	}
	free(occ.m);
	if (nrows < 1)
		nrows = 1;
	col = calloc((size_t) ncols, sizeof *col);
	row = calloc((size_t) nrows, sizeof *row);
	cx = malloc((size_t) (ncols + 1) * sizeof *cx);
	ry = malloc((size_t) (nrows + 1) * sizeof *ry);
	if (col == NULL || row == NULL || cx == NULL || ry == NULL) {
		l->fail = 1;
		goto out;
	}

	/* the columns: each one's base and limit, from its sizes and its
	   items' content (auto-fit's empty ones none) */
	for (i = 0; i < ncols; i++) {
		ec_tsize mn, mx;
		int32_t v, cmin = 0, cmax = 0;
		int content = 0;
		if (tc != NULL && tc->rep == 2 && i >= tc->rep_at && i < tc->rep_at + tc->rep_n * crep) {
			for (k = 0; k < n && !(it[k].c0 <= i && it[k].c1 > i); k++)
				;
			if (k == n)
				continue;
		}
		gtrack_def(s, EP_GRID_TEMPLATE_COLUMNS, EP_GRID_AUTO_COLUMNS, i, crep, &mn, &mx);
		content = gsize_px(l, &mn, w) == AUTO || (mx.k != EC_TR_FR && gsize_px(l, &mx, w) == AUTO) || mn.k == EC_TR_FIT;
		if (content)
			for (k = 0; k < n; k++)
				if (it[k].c0 == i && it[k].c1 == i + 1) {
					int was = l->minw;
					int32_t a, m;
					l->minw = 1;
					a = measure(l, it[k].b);
					l->minw = was;
					m = cell_pref(l, it[k].b, w);
					if (a > cmin)
						cmin = a;
					if (m > cmax)
						cmax = m;
				}
		if (cmax < cmin)
			cmax = cmin;
		v = gsize_px(l, &mn, w);
		col[i].base = v != AUTO ? v : mn.k == EC_TR_MAX ? cmax : cmin;
		if (mn.k == EC_TR_FIT)
			col[i].base = cmin;
		if (mx.k == EC_TR_FR) {
			col[i].fr = mx.v;
			col[i].lim = col[i].base;
		} else {
			v = gsize_px(l, &mx, w);
			col[i].lim = mx.k == EC_TR_FIT ? (cmax < v ? cmax : v) : v != AUTO ? v : mx.k == EC_TR_MIN ? cmin : cmax;
			col[i].stretchy = mx.k == EC_TR_AUTO;
		}
		if (col[i].lim < col[i].base)
			col[i].lim = col[i].base;
	}
	/* an item over several columns: room for its narrowest */
	for (k = 0; k < n; k++) {
		int span = it[k].c1 - it[k].c0, was = l->minw;
		int32_t have = cg * (span - 1), need;
		if (span < 2 || it[k].c1 > ncols)
			continue;
		for (i = it[k].c0; i < it[k].c1; i++)
			have += col[i].base;
		l->minw = 1;
		need = measure(l, it[k].b);
		l->minw = was;
		if (need > have) {
			int32_t add = (need - have + span - 1) / span;
			for (i = it[k].c0; i < it[k].c1; i++) {
				col[i].base += add;
				if (col[i].lim < col[i].base)
					col[i].lim = col[i].base;
			}
		}
	}
	grid_fit(col, ncols, w - cg * (ncols - 1), jc == KW_NORMAL || jc == KW_STRETCH || jc == 0);
	{
		/* justify-content: the columns where the room left puts them */
		int32_t used = cg * (ncols - 1), start, between;
		for (i = 0; i < ncols; i++)
			used += col[i].base;
		distribute(jc == KW_NORMAL || jc == KW_STRETCH ? KW_FLEX_START : jc, w - used, ncols, &start, &between);
		cx[0] = x + start;
		for (i = 0; i < ncols; i++)
			cx[i + 1] = cx[i] + col[i].base + cg + between;
	}

	/* the rows, top down: each item laid out where its area starts,
	   each row as tall as its items need (those over several rows in
	   their last), or its length */
	ry[0] = y;
	for (j = 0; j < nrows; j++) {
		ec_tsize mn, mx;
		int32_t h = 0, v;
		gtrack_def(s, EP_GRID_TEMPLATE_ROWS, EP_GRID_AUTO_ROWS, j, rrep, &mn, &mx);
		v = gsize_px(l, &mn, hdef);
		if (v != AUTO)
			h = v;
		row[j].fr = mx.k == EC_TR_FR && hdef != AUTO ? mx.v : 0;
		for (k = 0; k < n; k++) {
			GITEM *g = &it[k];
			int32_t aw, iw, ix, mb, f = AUTO;
			if (g->r0 != j || g->c0 >= ncols)
				continue;
			if (g->c1 > ncols)
				g->c1 = ncols;
			aw = cx[g->c1] - cx[g->c0] - cg;
			if (g->c1 == ncols)
				aw = cx[g->c1] - cx[g->c0] - cg;
			iw = aw;
			ix = cx[g->c0];
			if (g->ja != KW_STRETCH || (!g->b->anon && g->b->st->k[EP_WIDTH] != EK_KW)) {
				iw = cell_pref(l, g->b, aw);
				if (iw > aw)
					iw = aw;
				if (g->ja == KW_CENTER)
					ix += (aw - iw) / 2;
				else if (g->ja == KW_FLEX_END)
					ix += aw - iw;
			}
			g->k0 = l->p->nitems;
			g->h = layout_block(l, g->b, ix, ry[j], iw, 1, &mb, &f);
			g->k1 = l->p->nitems;
			g->bg = l->last_bg;
			memcpy(g->bd, l->last_bd, sizeof g->bd);
			g->f = f;
			g->top = ry[j];
		}
		v = gsize_px(l, &mx, hdef);
		for (k = 0; k < n; k++) {
			GITEM *g = &it[k];
			int32_t need;
			if (g->r1 - 1 != j || g->c0 >= ncols)
				continue;
			need = g->top + g->h - ry[j];
			if (v != AUTO && mn.k != EC_TR_AUTO && mn.k != EC_TR_MIN && mn.k != EC_TR_MAX)
				continue;	/* a row of a set size */
			if (need > h)
				h = need;
		}
		if (v != AUTO && h > v && gsize_px(l, &mn, hdef) != AUTO)
			h = v;
		row[j].base = h;
		ry[j + 1] = ry[j] + h + rg;
	}
	/* fr rows in a grid of a set height: the room left, and those below
	   moved down */
	{
		int32_t used = rg * (nrows - 1), left;
		int64_t frs = 0;
		for (j = 0; j < nrows; j++) {
			used += row[j].base;
			frs += row[j].fr;
		}
		left = hdef != AUTO ? hdef - used : 0;
		if (left > 0 && frs > 0) {
			int32_t shift = 0;
			if (frs < 1024)
				frs = 1024;
			for (j = 0; j < nrows; j++) {
				int32_t add = row[j].fr > 0 ? (int32_t) ((int64_t) left * row[j].fr / frs) : 0;
				for (k = 0; k < n; k++)
					if (it[k].r0 == j && shift) {
						shift_range(l, it[k].k0, it[k].k1, 0, shift);
						it[k].top += shift;
						if (it[k].f != AUTO)
							it[k].f += shift;
					}
				row[j].base += add;
				ry[j] += shift;
				shift += add;
			}
			ry[nrows] += shift;
		}
	}
	/* each item in its area's height: stretched, or moved by align-self */
	for (k = 0; k < n; k++) {
		GITEM *g = &it[k];
		int32_t ah, room;
		if (g->c0 >= ncols)
			continue;
		ah = ry[g->r1] - ry[g->r0] - rg;
		room = ah - (g->top - ry[g->r0]) - g->h;
		if (room <= 0)
			continue;
		if (g->aa == KW_STRETCH && (g->b->anon || g->b->st->k[EP_HEIGHT] == EK_KW))
			stretch(l, g->bg, g->bd, room);
		else if (g->aa == KW_CENTER || g->aa == KW_FLEX_END) {
			int32_t d = g->aa == KW_CENTER ? room / 2 : room;
			shift_range(l, g->k0, g->k1, 0, d);
			if (g->f != AUTO)
				g->f += d;
		}
	}
	total = ry[nrows] - rg - y;
	if (total < 0)
		total = 0;
	for (k = 0; k < n && *first == AUTO; k++)
		if (it[k].r0 == 0 && it[k].f != AUTO)
			*first = it[k].f;
out:
	free(it);
	free(col);
	free(row);
	free(cx);
	free(ry);
	return total;
}

static int32_t layout_block(L *l, box *b, int32_t x, int32_t y, int32_t cbw, int fixed_w,
			    int32_t *mb_out, int32_t *first)
{
	const ec_style *s = b->st;
	int32_t ml = 0, mr = 0, mt = 0, mb = 0, bl = 0, br = 0, bt = 0, bb = 0, pl = 0, pr = 0, pt = 0, pb = 0;
	int32_t w, ch = 0, cx, cy, hp, mn, mx, fb = AUTO;
	long bgi = -1, bgp = -1;
	uint32_t bg = 0;
	int auto_l = 0, auto_r = 0, wauto = 1, saved_n = l->ncols, table_n = 0;
	int32_t *saved_cols = l->cols, *table_cols = NULL, saved_sh = l->sp_h, saved_sv = l->sp_v;
	int32_t table_sh = 0, table_sv = 0, saved_top = l->fl_top;
	int32_t saved_cbx = l->cbx, saved_cby = l->cby, saved_cbw = l->cbw;
	int saved_fl0 = l->fl0, own = own_context(b), pos = b->anon ? KW_STATIC : kw(s, EP_POSITION);
	long bd[4] = { -1, -1, -1, -1 }, kstart = l->p->nitems;
	struct span *saved_spans = l->spans;
	unsigned saved_alpha = l->alpha;
	uint32_t saved_clip = l->clip;
	int32_t bg_top = 0, bg_bottom = 0;	/* the background colour's fill short of the border box */
	int32_t in_l = 0, in_r = 0;

	if (l->fail)
		return 0;
	if (EC_DEEP()) {
		l->p->deep = 1;		/* the stack's floor: this box and what is in it left out */
		*mb_out = 0;
		*first = AUTO;
		return 0;
	}
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
		int clip = kw(s, EP_BACKGROUND_CLIP);
		/* what it draws, and all in it, as solid as its opacity says */
		l->alpha = alpha_mul(l->alpha, own_opacity(s, b->n));
		if (clip == KW_TEXT) {
			/* its background painted through its text, not under it:
			   text of a transparent colour drawn in it (text_paint) */
			uint32_t cp = clip_paint(s);
			if (cp != 0)
				l->clip = cp;
		} else {
			/* its colour, picture or gradient to the edge of the box
			   background-clip says (CSS Backgrounds 3, 3.7); the colour
			   not where the canvas has it */
			if (clip == KW_PADDING_BOX || clip == KW_CONTENT_BOX) {
				in_l = bl;
				in_r = br;
				bg_top = bt;
				bg_bottom = bb;
			}
			if (clip == KW_CONTENT_BOX) {
				in_l += pl;
				in_r += pr;
				bg_top += pt;
				bg_bottom += pb;
			}
			bg = fade(color(s, EP_BACKGROUND_COLOR), l->alpha);
			if ((bg >> 24) != 0 && !(b->n != NULL && b->n == l->bg_taken))
				bgi = fill(l, x + ml + in_l, y + mt + bg_top, bl + pl + w + pr + br - in_l - in_r, 1, bg);
			if (b->n != l->canvas_bg) {
				bgp = bg_item(l, s, b->n);
				if (bgp >= 0 && bgi >= 0)
					l->p->items[bgi].flags |= DLF_TILE;
			}
		}
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
	} else if (b->flex || b->grid) {
		int32_t hd = AUTO;
		if (!b->anon) {
			hd = len(l, s, EP_HEIGHT, AUTO);
			if (hd != AUTO && kw(s, EP_BOX_SIZING) == KW_BORDER_BOX)
				hd -= bt + bb + pt + pb;
			if (hd != AUTO && hd < 0)
				hd = 0;
		}
		ch = b->grid ? grid_layout(l, b, cx, cy, w, hd, &fb) : flex_layout(l, b, cx, cy, w, hd, &fb);
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
			l->p->items[bgi].h = bt + pt + ch + pb + bb - bg_top - bg_bottom;
		{
			int32_t in[4];
			in[0] = in_l;
			in[1] = bg_top;
			in[2] = in_r;
			in[3] = bg_bottom;
			bg_place(l, bgp, s, x + ml, y + mt, bl + pl + w + pr + br, bt + pt + ch + pb + bb, bl, bt, br, bb,
				 in);
		}
		{
			int32_t bx = x + ml, by = y + mt, bw = bl + pl + w + pr + br, bh = bt + pt + ch + pb + bb;
			bd[0] = fill(l, bx, by, bw, bt, fade(color(s, EP_BORDER_TOP_COLOR), l->alpha));
			bd[1] = fill(l, bx, by + bh - bb, bw, bb, fade(color(s, EP_BORDER_BOTTOM_COLOR), l->alpha));
			bd[2] = fill(l, bx, by + bt, bl, bh - bt - bb, fade(color(s, EP_BORDER_LEFT_COLOR), l->alpha));
			bd[3] = fill(l, bx + bw - br, by + bt, br, bh - bt - bb, fade(color(s, EP_BORDER_RIGHT_COLOR), l->alpha));
#ifndef EW_ST
			if (b->n != NULL && !b->anon)
				block_rect(l, b->n, bx, by, bw, bh);
#endif
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
	l->last_bg = bgi >= 0 ? bgi : bgp;
	memcpy(l->last_bd, bd, sizeof bd);
	l->alpha = saved_alpha;
	l->clip = saved_clip;
	*mb_out = mb;
	*first = fb;
	return mt + bt + pt + ch + pb + bb + mb;
}

/* ---------------------------------------------------------------- the page */

/* the items by strips of the page, so that drawing a part of it looks
   only at the items there */
static void index_items(page *p)
{
	long i, k, n = 0, bottom = p->height;
	for (i = 0; i < p->nitems; i++)
		if (p->items[i].y + p->items[i].h > bottom)
			bottom = p->items[i].y + p->items[i].h;
	p->nstrips = bottom / PAGE_STRIP + 1;
	p->strip = calloc((size_t) p->nstrips + 1, sizeof *p->strip);
	if (p->strip == NULL)
		return;
	/* how many in each strip, then where each strip's start */
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		long s0 = d->y < 0 ? 0 : d->y / PAGE_STRIP, s1 = d->y + d->h <= 0 ? 0 : (d->y + d->h - 1) / PAGE_STRIP;
		for (k = s0; k <= s1; k++)
			p->strip[k + 1]++;
		n += s1 - s0 + 1;
	}
	for (k = 0; k < p->nstrips; k++)
		p->strip[k + 1] += p->strip[k];
	p->sidx = malloc((size_t) (n ? n : 1) * sizeof *p->sidx);
	if (p->sidx == NULL) {
		free(p->strip);
		p->strip = NULL;
		return;
	}
	{
		long *at = malloc((size_t) p->nstrips * sizeof *at);
		if (at == NULL) {
			free(p->strip);
			free(p->sidx);
			p->strip = p->sidx = NULL;
			return;
		}
		memcpy(at, p->strip, (size_t) p->nstrips * sizeof *at);
		for (i = 0; i < p->nitems; i++) {
			const dl_item *d = &p->items[i];
			long s0 = d->y < 0 ? 0 : d->y / PAGE_STRIP, s1 = d->y + d->h <= 0 ? 0 : (d->y + d->h - 1) / PAGE_STRIP;
			for (k = s0; k <= s1; k++)
				p->sidx[at[k]++] = i;
		}
		free(at);
	}
}

page *layout_page(doc *d, const LAYOUT_OPT *o)
{
	L l;
	page *p = calloc(1, sizeof *p);
	struct arena *a = calloc(1, sizeof *a);
	box *root;
	dnode *html = d->html;
	int32_t mb, fb = AUTO;
	long cbg = -1;

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
	p->cbg = -1;
	l.alpha = 255;
	l.zoom = o->zoom > 0 ? o->zoom : 1024;
	l.text = o->text > 0 ? o->text : 1024;
	l.cbw = o->width;		/* the page: the containing block of what is absolute in nothing positioned */
	l.last_bg = -1;
	if (html == NULL || html->e.style == NULL) {
		p->height = 0;
		return p;
	}
	/* the canvas: the root's background, else the body's; one less than
	   solid on white, as browsers show what is under a canvas (CSS
	   Backgrounds 3, The Canvas Background, leaves it to them) */
	{
		uint32_t hb = color(html->e.style, EP_BACKGROUND_COLOR);
		if ((hb >> 24) != 0 || html->e.style->k[EP_BACKGROUND_IMAGE] == EK_PTR ||
		    html->e.style->k[EP_BACKGROUND_IMAGE] == EK_IMAGE) {
			if ((hb >> 24) != 0) {
				p->bg = over(hb, 0xffffffff) & 0xffffff;
				l.bg_taken = html;	/* painted once, there */
			}
			l.canvas_bg = html;
		} else if (d->body != NULL && d->body->e.style != NULL) {
			uint32_t bb = color(d->body->e.style, EP_BACKGROUND_COLOR);
			if ((bb >> 24) != 0) {
				p->bg = over(bb, 0xffffffff) & 0xffffff;
				l.bg_taken = d->body;
			}
			l.canvas_bg = d->body;
		}
		/* its picture over the whole page, first */
		if (l.canvas_bg != NULL && kw(l.canvas_bg->e.style, EP_DISPLAY) != KW_NONE)
			cbg = bg_item(&l, l.canvas_bg->e.style, l.canvas_bg);
	}
	root = new_box(&l, BX_BLOCK, html, html->e.style, NULL);
	if (root != NULL) {
		if (kw(html->e.style, EP_DISPLAY) != KW_NONE) {
			pseudo(&l, root, html, html->e.style->before);
			build(&l, root, html);
		}
		fixup(&l, root);
		p->height = layout_block(&l, root, 0, 0, o->width, 0, &mb, &fb);
		bg_place(&l, cbg, l.canvas_bg != NULL ? l.canvas_bg->e.style : NULL, 0, 0, o->width,
			 p->height, 0, 0, 0, 0, NULL);
		p->cbg = cbg;
	}
	free(l.it);
	free(l.fl);
	if (l.fail) {
		page_free(p);
		return NULL;
	}
	index_items(p);
	return p;
}

void page_free(page *p)
{
	if (p == NULL)
		return;
	free(p->items);
	free(p->links);
	free(p->ctls);
#ifndef EW_ST
	free(p->blocks);
#endif
	free(p->strip);
	free(p->sidx);
	if (p->mem != NULL) {
		afree(p->mem);
		free(p->mem);
	}
	free(p);
}

#ifndef EW_ST
dnode *page_node_at(const page *p, int32_t x, int32_t y)
{
	long i, best = -1;
	for (i = p->nitems - 1; i >= 0; i--) {
		const dl_item *d = &p->items[i];
		if (d->node != NULL && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w && y >= d->y &&
		    y < d->y + d->h)
			return d->node;
	}
	/* the innermost block there: the last laid out of those holding it
	   (a block's rectangle is put when its children are done); not one
	   a click goes through (pointer-events: none) */
	for (i = 0; i < p->nblocks; i++) {
		const struct pg_block *b = &p->blocks[i];
		if (b->n->e.style != NULL && no_hit(b->n->e.style))
			continue;
		if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h &&
		    (best < 0 || (int64_t) b->w * b->h <= (int64_t) p->blocks[best].w * p->blocks[best].h))
			best = i;
	}
	return best >= 0 ? p->blocks[best].n : NULL;
}

int page_node_rect(const page *p, const dnode *n, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	long i;
	int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	int found = 0;
	for (i = 0; i < p->nblocks; i++)
		if (p->blocks[i].n == n) {
			*x = p->blocks[i].x;
			*y = p->blocks[i].y;
			*w = p->blocks[i].w;
			*h = p->blocks[i].h;
			return 1;
		}
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		const dnode *k;
		for (k = d->node; k != NULL && k != n; k = k->parent)
			;
		if (k == NULL || d->node == NULL)
			continue;
		if (!found || d->x < x0)
			x0 = d->x;
		if (!found || d->y < y0)
			y0 = d->y;
		if (!found || d->x + d->w > x1)
			x1 = d->x + d->w;
		if (!found || d->y + d->h > y1)
			y1 = d->y + d->h;
		found = 1;
	}
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
	return found;
}
#endif

/* (the last item drawn there first; not one a click goes through) */
int page_link_at(const page *p, int32_t x, int32_t y)
{
	long i;
	for (i = p->nitems - 1; i >= 0; i--) {
		const dl_item *d = &p->items[i];
		if (d->link && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w && y >= d->y &&
		    y < d->y + d->h)
			return d->link - 1;
	}
	return -1;
}

fctl *page_ctl_at(const page *p, int32_t x, int32_t y, const dl_item **it)
{
	long i;
	for (i = p->nitems - 1; i >= 0; i--) {
		const dl_item *d = &p->items[i];
		if (d->ctl && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w && y >= d->y &&
		    y < d->y + d->h) {
			if (it != NULL)
				*it = d;
			return p->ctls[d->ctl - 1];
		}
	}
	return NULL;
}

const dl_item *page_ctl_item(const page *p, const fctl *c)
{
	long i;
	int k;
	for (k = 0; k < p->nctls; k++)
		if (p->ctls[k] == c)
			break;
	if (k == p->nctls)
		return NULL;
	/* the control's own item, not a label's text */
	for (i = 0; i < p->nitems; i++)
		if (p->items[i].ctl == k + 1 && (p->items[i].type == DL_CTL || p->items[i].type == DL_IMAGE))
			return &p->items[i];
	return NULL;
}

/* in document order, without recursion */
static int32_t anchor_in(const dnode *top, const char *id)
{
	const dnode *n = top;
	for (;;) {
		if (n->type == DN_ELEMENT && n->y >= 0) {
			const char *v = dn_attr(n, "id");
			if ((v != NULL && strcmp(v, id) == 0) ||
			    (n->tag == T_A && (v = dn_attr(n, "name")) != NULL && strcmp(v, id) == 0))
				return n->y;
		}
		if (n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != top && n->next == NULL)
			n = n->parent;
		if (n == top)
			return -1;
		n = n->next;
	}
}

int32_t page_anchor(const page *p, const char *id)
{
	if (p->d == NULL || p->d->root == NULL || id == NULL || *id == 0)
		return -1;
	return anchor_in(p->d->root, id);
}
