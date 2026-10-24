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
#define NBD 5				/* a block's border items (top, bottom, left, right) and
					   its clip (DL_CLIP), for a row or a flex line to stretch */

/* ---------------------------------------------------------------- memory */

struct achunk { struct achunk *next; size_t used, size; char mem[]; };
struct arena { struct achunk *head; };

/* what a box or a run needs: 4 on the 68030 (2 would do), 8 where
   pointers are 8 bytes (the build machine's checks) */
#define AALIGN (sizeof(void *) > 4 ? 8 : 4)
#ifdef EW_ST
#define ACHUNK 8192	/* the ST edition: smaller pieces, its pages' memory
			   being what Back and the pictures don't get */
#define DL_FIRST 128	/* the display list's first piece: 1024 items were 45 KB,
			   most of a small page's memory */
#define IT_FIRST 64	/* and the line items' (15 KB) */
#else
#define ACHUNK 16384
#define DL_FIRST 1024
#define IT_FIRST 256
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

/* BX_MARK: a list item's marker inside it (list-style-position: inside),
   its text made when it is laid out (the item's number is known then) */
enum { BX_BLOCK, BX_INLINE, BX_TEXT, BX_BR, BX_ATOM, BX_MARK };
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
	uint8_t list;			/* LB_*: a list item; a details' summary */
	uint8_t anon;			/* an anonymous block: no box of its own */
	uint8_t atom;			/* AT_* */
	uint8_t tablerow;
	uint8_t table;			/* a table: shrinks to fit, its rows share column widths */
	uint8_t repl;			/* a picture as a block (floated, display: block): its one child the atom */
	uint8_t ib;			/* an inline-level block (inline-block, -flex, -grid, -table):
					   an atom in its line, laid out as a block (ib_layout) */
	uint16_t link;			/* 1 + link index, or 0 */
	uint16_t ctl;			/* 1 + index into page.ctls, or 0 */
	dnode *n;			/* the element, or NULL */
	const ec_style *st;		/* its style (an anonymous box or text: the parent's) */
	box *parent, *first, *last, *next;
	const char *text;		/* text, or an atom's label */
	uint32_t len;
	int32_t aw, ah;			/* an atom's size, device px (AUTO: from its label) */
	int32_t num;			/* a list item's number; a marker's counter
					   style when it has one of its own (KW_*) */
	void *img;			/* AT_IMAGE: the picture's handle; LB_SUM: the
					   details element (dnode *) */
	int32_t iw, ih;			/* and its size, CSS px (0: not known yet) */
};

/* box.list: a list item (display: list-item); the summary a click opens
   and closes a details by (the HTML Standard, 4.11), whatever its display */
#define LB_ITEM 1
#define LB_SUM 2

/* ---------------------------------------------------------------- the context */

/* IT_FLOAT: a float in the text (its box in atom), placed by the line
   it is met on; it takes no room on the line itself */
enum { IT_WORD, IT_SPACE, IT_BREAK, IT_ATOM, IT_GAP, IT_FLOAT };

typedef struct {
	uint8_t kind;
	uint8_t flags;			/* DLF_* */
	uint8_t canbreak;		/* a space a line may end at; 2: a word of white-space
					   pre or nowrap, no line ending before it or in it; 3: a
					   word a line may end before (a place inside a word, UAX
					   #14: inword); 4: so, at a soft hyphen (a hyphen drawn
					   at the line's end) */
	uint8_t collapse;		/* a space dropped at the start or end of a line;
					   IT_FLOAT: placed */
	uint8_t hidden;			/* visibility: hidden */
	uint8_t dir;			/* bidi: 0 neutral, 1 left to right, 2 right to left, 3 a
					   number (word_dir); drawn right to left (its level odd):
					   DLF_RTL in flags (visual_order) */
	uint16_t link;
	uint16_t ctl;			/* a label's text: its control */
	ew_font *font;
	uint32_t color, bg;		/* 0xAARRGGBB, as solid as drawn (its alpha, then the
					   opacity it is in); bg 0: none */
	uint8_t alpha;			/* the opacity it is in, 0..255 (CSS Color 4) */
	uint8_t ib;			/* IT_ATOM of an inline-level block: 1 to be laid out,
					   2 laid out (ib_layout), len its IBX's index + 1 */
	const char *text;
	uint32_t len;			/* IT_FLOAT: its width once measured (0: not yet) */
	int32_t w;
	int32_t asc, desc;		/* above and below the baseline, leading included */
	int32_t shift;			/* vertical-align: + down */
	box *atom;
	int32_t x;			/* where it went on its line */
	dnode *det;			/* in a summary laid out inline: its details (its
					   click areas are the line's, DL_SUM) */
#ifndef EW_ST
	dnode *node;			/* the element it is in (for scripts' events) */
#endif
} ITEM;

/* a border of a table's cell where borders collapse (CSS 2.1, 17.6.2):
   its width (0: none), colour and style (which of two wins) */
typedef struct {
	int32_t w;
	uint32_t c;
	int st;
} BEDGE;

/* an inline-block laid out at (0, 0) (ib_layout): its margin box's
   height, and the display items k0..k1 and blocks b0..b1 it made, moved
   to its place on its line (ib_place). Kept beside the items, not in
   them: every word is an item. */
typedef struct {
	int32_t h;
	long k0, k1, b0, b1;
	long f0, f1;			/* the boxes in it left for later (place_later) */
} IBX;

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
	unsigned long polls;		/* boxes made and laid out (doc_poll) */
	struct {
		const ec_family *f;
		int fam;
	} fams[8];			/* font_of's family for a list of names, as found */
	int nfams;
	int32_t cw;			/* the width lines are broken in (AUTO: measuring) */
	int minw;			/* measuring the narrowest a box goes (its longest word), not its widest */
	int lined;			/* the items have a picture placed by its line's top or bottom */
	int floats;			/* the items have a float in the text (IT_FLOAT) */
	int sums;			/* the items have an inline summary's (ITEM.det) */
	int ibs;			/* the items have an inline-block (ITEM.ib) */
	IBX *ibx;			/* their places, a stack: lines() lets go of its own */
	long nibx, ibxcap;
	/* the last block laid out: its background and border items (-1:
	   none), for a row to stretch its cells to the row's height */
	long last_bg, last_bd[NBD];
	/* the table being laid out: per column, the rows a cell from above
	   still takes, and for the cell's first column where it began */
	struct span {
		int rows;
		int head;		/* the cell's first column */
		int32_t top, h;		/* its row's top and its height */
		long bg, bd[NBD], k0, k1;	/* its background, borders and items */
		int va;			/* its vertical-align */
	} *spans;
	/* the table being laid out when its borders collapse (else NULL):
	   its own borders (top, right, bottom, left), the rows laid out, and
	   per column the bottom border of the cell last laid out there (the
	   cell below draws it, as its top) and the right one of the cell
	   ending there (the cell beside draws it, as its left) */
	struct coll {
		BEDGE t[4];
		long rows;
		int first, last;	/* the row being laid out is the table's first, last */
		BEDGE bot[MAXCOLS], right[MAXCOLS];
	} *coll;
	const BEDGE *cbd;		/* a cell's borders as they collapse, for its */
	const box *cbd_box;		/* layout_block (row_layout) */
	/* positioned boxes' containing block: the nearest positioned
	   ancestor's padding box (the page at first) */
	int32_t cbx, cby, cbw;
	FLT *fl;			/* the floats placed; those of the context being laid out */
	int nfl, flcap, fl0;		/* from fl0 on */
	int32_t fl_top;			/* no float of this context goes higher than the last */
	int32_t *cols;			/* the table being laid out: its column widths */
	int ncols;
	int32_t sp_h, sp_v;		/* and its border-spacing */
	/* a page in visual Hebrew (ISO-8859-8): its letters stored in the
	   order they are shown, so lines aren't reordered (visual_order) */
	int visual;
	unsigned alpha;			/* the opacity of the box being laid out, times its
					   ancestors', 0..255 */
	uint32_t clip;			/* a background it is in painted through its text
					   (background-clip: text): the colour that text is
					   drawn in when its own is transparent; 0: none */
	int32_t last_base;		/* the last line's baseline laid out (an inline-block's) */
	long last_k;			/* its line's first item (moved with it: shift_range, cell_shift) */
	uint16_t ovf, cbovf;		/* the items' clip (dl_item.clip) now, and the
					   containing block's (cbx): a positioned box's */
	uint16_t ctl_in;		/* a label's control: its blocks' text's (a block or
					   inline-block label, or an inline-block in one) */
	/* boxes laid out at (0, 0) and then moved where they go (inline-
	   blocks, floats, absolutely positioned boxes): how many are being
	   laid out, and how many were when the containing block (cbx) was
	   set. An absolutely positioned box in them whose containing block is
	   outside is left until they are moved (place_abs, place_later). */
	int mov, cbmov;
	struct later {
		box *c;			/* NULL: placed */
		int32_t sx, sy;		/* where it would have been, before the move */
		unsigned alpha;		/* the opacity, the text's paint and the label it is in */
		uint32_t clip;
		uint16_t ctl;
	} *later;
	long nlater, latercap;
	/* the positioned boxes laid out, in the document's order: each its
	   items (k0 to k1) and z-index, for the order they are drawn in
	   (draw_order) */
	struct lay {
		long k0, k1;
		int32_t z;		/* LAY_AUTO: auto */
	} *lay;
	long nlay, laycap;
} L;

#define LAY_AUTO INT32_MIN

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

/* the face's family for a list of names: the first EWeb knows the kind
   of, else the generic family */
static int family_of(const ec_family *f)
{
	int fam = -1, i;

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
	return fam;
}

static ew_font *font_of(L *l, const ec_style *s)
{
	int fam, sty = 0, i;
	const ec_family *f = s->k[EP_FONT_FAMILY] == EK_PTR ? s->v[EP_FONT_FAMILY].p : NULL;

	/* (a page has a few lists of names, each asked for at every run of
	   text: each looked through once a layout) */
	for (i = 0; i < l->nfams && i < 8 && l->fams[i].f != f; i++)
		;
	if (i < l->nfams && i < 8) {
		fam = l->fams[i].fam;
	} else {
		fam = family_of(f);
		l->fams[l->nfams % 8].f = f;
		l->fams[l->nfams % 8].fam = fam;
		l->nfams++;
	}
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
		long cap = p->cap ? p->cap * 2 : DL_FIRST;
		dl_item *n = realloc(p->items, cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return NULL;
		}
		p->items = n;
		p->cap = cap;
	}
	memset(&p->items[p->nitems], 0, sizeof p->items[0]);
	p->items[p->nitems].clip = l->ovf;
	return &p->items[p->nitems++];
}

/* a box that hides what overflows it: a DL_CLIP item for its padding
   box (placed by clip_set once its size is known), and the items made
   in it from now on drawn and clicked within it (l->ovf). -1 when there
   are as many as can be (65535: the rest not clipped) */
static long clip_open(L *l)
{
	page *p = l->p;
	dl_item *d;
	if (p->nclips >= 65535)
		return -1;
	if (p->nclips == p->clipcap) {
		long cap = p->clipcap ? p->clipcap * 2 : 32;
		long *n = realloc(p->clips, cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return -1;
		}
		p->clips = n;
		p->clipcap = cap;
	}
	d = dl_add(l);
	if (d == NULL)
		return -1;
	d->type = DL_CLIP;
	d->flags = DLF_NOHIT;
	p->clips[p->nclips++] = p->nitems - 1;
	l->ovf = (uint16_t) p->nclips;
	return p->nitems - 1;
}

/* a positioned box (position not static) about to be laid out: its
   items from now on (k0; k1 set when it is done) and its z-index, for
   draw_order. -1 when there is no room (it is drawn where it is) */
static long lay_open(L *l, const ec_style *s)
{
	struct lay *y;
	if (l->nlay == l->laycap) {
		long cap = l->laycap ? l->laycap * 2 : 16;
		struct lay *n = realloc(l->lay, cap * sizeof *n);
		if (n == NULL)
			return -1;
		l->lay = n;
		l->laycap = cap;
	}
	y = &l->lay[l->nlay];
	y->k0 = l->p->nitems;
	y->k1 = -1;
	y->z = s->k[EP_Z_INDEX] == EK_INT ? s->v[EP_Z_INDEX].i : LAY_AUTO;
	return l->nlay++;
}

#define CLIP_FAR (1L << 28)	/* an axis not clipped: as far as can be */

/* its place: the padding box (x, y, w, h) on the axes it clips */
static void clip_set(L *l, long k, int cx, int cy, int32_t x, int32_t y, int32_t w, int32_t h)
{
	dl_item *d = &l->p->items[k];
	d->x = cx ? x : -CLIP_FAR;
	d->w = cx ? (w > 0 ? w : 0) : 2 * CLIP_FAR;
	d->y = cy ? y : -CLIP_FAR;
	d->h = cy ? (h > 0 ? h : 0) : 2 * CLIP_FAR;
}

int page_clip(const page *p, unsigned clip, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
	unsigned k = clip;
	int n = 0;
	while (k != 0 && k <= (unsigned long) p->nclips && n++ < 256) {
		const dl_item *c = &p->items[p->clips[k - 1]];
		if (c->x > *x0)
			*x0 = c->x;
		if (c->y > *y0)
			*y0 = c->y;
		if (c->x + c->w < *x1)
			*x1 = c->x + c->w;
		if (c->y + c->h < *y1)
			*y1 = c->y + c->h;
		k = c->clip;
	}
	return *x0 < *x1 && *y0 < *y1;
}

/* is (x, y) within the boxes hiding what overflows them that d is in */
static int in_clips(const page *p, const dl_item *d, int32_t x, int32_t y)
{
	int32_t x0 = x, y0 = y, x1 = x + 1, y1 = y + 1;
	return d->clip == 0 || page_clip(p, d->clip, &x0, &y0, &x1, &y1);
}

/* ---------------------------------------------------------------- boxes that scroll */

static int scrolls(int o)
{
	return o == KW_AUTO || o == KW_SCROLL;
}

/* how far right and down what is in a box goes (INT32_MIN: nothing): the
   items with its clip (n) in their chain, each cut to the boxes on the
   way that hide what overflows them */
static void extent(L *l, long ck, uint16_t n, int32_t *x1, int32_t *y1)
{
	const page *p = l->p;
	long k;
	*x1 = *y1 = INT32_MIN;
	for (k = ck + 1; k < p->nitems; k++) {
		const dl_item *d = &p->items[k];
		unsigned c = d->clip;
		int32_t a0, b0, a1, b1;
		int depth = 0;
		if (c == 0 || d->type == DL_CLIP || d->w <= 0 || d->h <= 0)
			continue;
		a0 = d->x;
		b0 = d->y;
		a1 = d->x + d->w;
		b1 = d->y + d->h;
		while (c != n && c != 0 && c <= (unsigned long) p->nclips && depth++ < 256) {
			const dl_item *cc = &p->items[p->clips[c - 1]];
			a0 = cc->x > a0 ? cc->x : a0;
			b0 = cc->y > b0 ? cc->y : b0;
			a1 = cc->x + cc->w < a1 ? cc->x + cc->w : a1;
			b1 = cc->y + cc->h < b1 ? cc->y + cc->h : b1;
			c = cc->clip;
		}
		if (c != n || a0 >= a1 || b0 >= b1)
			continue;
		*x1 = a1 > *x1 ? a1 : *x1;
		*y1 = b1 > *y1 ? b1 : *y1;
	}
}

/* a box with overflow auto or scroll (on x, y), its clip ck placed: when
   what is in it goes further right than its padding box (x1, its padding
   added) or down (y1), it scrolls: page.scr, with a bar for each such axis,
   placed when the page is done (scrollers_place: a row or a flex line may
   stretch the box yet). Not a hidden box (nothing of it to scroll by) */
static void scroller(L *l, box *b, long ck, int x, int y, int32_t x1, int32_t y1)
{
	page *p = l->p;
	const dl_item *c = &p->items[ck];
	int32_t ex = x && x1 != INT32_MIN ? x1 - c->x : 0, ey = y && y1 != INT32_MIN ? y1 - c->y : 0;
	struct pg_scroll *sc;
	int axis;
	if (ex - c->w <= 2 && ey - c->h <= 2)
		return;	/* (a pixel or two, a glyph's box below its line: not) */
	if (p->nscr == p->scrcap) {
		int cap = p->scrcap ? p->scrcap * 2 : 8;
		struct pg_scroll *n = realloc(p->scr, (size_t) cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return;
		}
		p->scr = n;
		p->scrcap = cap;
	}
	sc = &p->scr[p->nscr++];
	sc->clip = ck;
	sc->ex = ex;
	sc->ey = ey;
	sc->mx = sc->my = sc->sx = sc->sy = 0;
	sc->n = b->n;
	sc->bar[0] = sc->bar[1] = -1;
	for (axis = 0; axis < 2; axis++) {
		dl_item *d;
		if (axis ? ex == 0 : ey == 0)
			continue;
		d = dl_add(l);
		if (d == NULL)
			return;
		d->type = DL_SBAR;
		d->rgb = fade(0xff000000, l->alpha);
		d->base = (int16_t) axis;
		d->len = (uint32_t) p->nscr;
		sc->bar[axis] = p->nitems - 1;
	}
}

/* how far one way scrolls: what is in it (e) past len, else 0 */
static int32_t reach(int32_t e, int32_t len)
{
	return e != 0 && e - len > 2 ? e - len : 0;
}

/* the boxes that scroll, the page done: how far each scrolls (a bar takes
   its room from the other way), and its bars over its padding box's right
   edge and foot (one it doesn't need: nothing, no bar) */
static void scrollers_place(page *p)
{
	int k, i, axis;
	for (k = 0; k < p->nscr; k++) {
		struct pg_scroll *sc = &p->scr[k];
		const dl_item *c = &p->items[sc->clip];
		int32_t mx = 0, my = 0;
		for (i = 0; i < 2; i++) {
			mx = reach(sc->ex, c->w - (my ? SBAR_W : 0));
			my = reach(sc->ey, c->h - (mx ? SBAR_W : 0));
		}
		sc->mx = mx;
		sc->my = my;
		for (axis = 0; axis < 2; axis++) {
			dl_item *d;
			if (sc->bar[axis] < 0)
				continue;
			d = &p->items[sc->bar[axis]];
			if (axis ? mx == 0 : my == 0) {
				d->rgb = 0;	/* (not needed: not drawn, nor clicked) */
				d->w = d->h = 0;
				sc->bar[axis] = -1;
			} else if (axis == 0) {
				d->x = c->x + c->w - SBAR_W;
				d->y = c->y;
				d->w = SBAR_W;
				d->h = c->h - (mx ? SBAR_W : 0);
			} else {
				d->x = c->x;
				d->y = c->y + c->h - SBAR_W;
				d->w = c->w - (my ? SBAR_W : 0);
				d->h = SBAR_W;
			}
		}
	}
}

/* is clip number n in the chain of clip k */
static int in_chain(const page *p, unsigned k, unsigned n)
{
	int depth = 0;
	while (k != 0 && k <= (unsigned long) p->nclips && depth++ < 256) {
		if (k == n)
			return 1;
		k = p->items[p->clips[k - 1]].clip;
	}
	return 0;
}

static void index_items(page *p, int wide, long cbg);

int page_scroll(page *p, int k, int32_t sx, int32_t sy, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	struct pg_scroll *sc;
	const dl_item *c;
	unsigned n;
	int32_t dx, dy;
	long i;
	if (p == NULL || k < 0 || k >= p->nscr)
		return 0;
	sc = &p->scr[k];
	sx = sx < 0 ? 0 : sx > sc->mx ? sc->mx : sx;
	sy = sy < 0 ? 0 : sy > sc->my ? sc->my : sy;
	dx = sx - sc->sx;
	dy = sy - sc->sy;
	c = &p->items[sc->clip];
	*x = c->x;
	*y = c->y;
	*w = c->w;
	*h = c->h;
	if (dx == 0 && dy == 0)
		return 0;
	sc->sx = sx;
	sc->sy = sy;
	/* what is in it: its clip's number among the clips in their chain */
	for (n = 1; n <= (unsigned long) p->nclips && p->clips[n - 1] != sc->clip; n++)
		;
	for (i = sc->clip + 1; i < p->nitems; i++) {
		dl_item *d = &p->items[i];
		if (d->clip != 0 && in_chain(p, d->clip, n)) {
			d->x -= dx;
			d->y -= dy;
		}
	}
#ifndef EW_ST
	for (i = 0; i < p->nblocks; i++)	/* (the elements' places, for scripts and clicks) */
		if (p->blocks[i].clip != 0 && in_chain(p, p->blocks[i].clip, n)) {
			p->blocks[i].x -= dx;
			p->blocks[i].y -= dy;
		}
#endif
	free(p->strip);
	free(p->sidx);
	p->strip = p->sidx = NULL;
	index_items(p, p->wide, p->cbg);
	return 1;
}

int page_reveal(page *p, const dnode *n, int32_t y, int32_t h, int top)
{
	int k[16], m = 0, i, moved = 0;
	const dnode *a;
	if (p == NULL || n == NULL || p->nscr == 0)
		return 0;
	/* its boxes that scroll, innermost first */
	for (a = n->parent; a != NULL && m < 16; a = a->parent)
		for (i = p->nscr - 1; i >= 0; i--)
			if (p->scr[i].n == a && m < 16) {
				k[m++] = i;
				break;
			}
	while (m-- > 0) {
		struct pg_scroll *sc = &p->scr[k[m]];
		const dl_item *c = &p->items[sc->clip];
		int32_t to = sc->sy, was = sc->sy, x0, y0, w0, h0;
		if (top || y < c->y)
			to = sc->sy + (y - c->y);
		else if (y + h > c->y + c->h)
			to = sc->sy + (y + h - (c->y + c->h));
		if (page_scroll(p, k[m], sc->sx, to, &x0, &y0, &w0, &h0)) {
			y -= sc->sy - was;
			moved = 1;
		}
	}
	return moved;
}

const dl_item *page_bar_at(const page *p, int32_t x, int32_t y)
{
	int k, a;
	if (p == NULL)
		return NULL;
	for (k = p->nscr - 1; k >= 0; k--)
		for (a = 0; a < 2; a++) {
			const dl_item *d;
			if (p->scr[k].bar[a] < 0)
				continue;
			d = &p->items[p->scr[k].bar[a]];
			if ((d->rgb >> 24) != 0 && x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h &&
			    in_clips(p, d, x, y))
				return d;
		}
	return NULL;
}

void page_bar_thumb(const page *p, const dl_item *bar, int32_t *t0, int32_t *t1)
{
	const struct pg_scroll *sc = &p->scr[bar->len - 1];
	int32_t len = bar->base ? bar->w : bar->h, seen = bar->base ? p->items[sc->clip].w : p->items[sc->clip].h;
	int32_t reach = bar->base ? sc->mx : sc->my, at = bar->base ? sc->sx : sc->sy, t;
	t = seen + reach > 0 ? (int32_t) ((int64_t) len * seen / (seen + reach)) : len;
	if (t < SBAR_W)
		t = SBAR_W < len ? SBAR_W : len;
	*t0 = reach > 0 ? (int32_t) ((int64_t) (len - t) * at / reach) : 0;
	*t1 = *t0 + t;
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
	p->blocks[p->nblocks].clip = l->ovf;
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

/* where a click opens and closes the details det: its summary's box,
   or a part of a line of a summary laid out inline */
static void sum_rect(L *l, dnode *det, int32_t x, int32_t y, int32_t w, int32_t h)
{
	dl_item *d;
	if (w <= 0 || h <= 0 || (d = dl_add(l)) == NULL)
		return;
	d->type = DL_SUM;
	d->x = x;
	d->y = y;
	d->w = w;
	d->h = h;
	d->rgb = 0;		/* (not drawn) */
	d->det = det;
}

/* a details' summary's box (not one a click goes through, pointer-events:
   none, nor a hidden one) */
static void sum_item(L *l, const box *b, int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (!no_hit(b->st) && kw(b->st, EP_VISIBILITY) != KW_HIDDEN)
		sum_rect(l, b->img, x, y, w, h);
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

/* a link: its href, and after its NUL its download attribute (1 and the
   value, else 0: page_link_download) */
static uint16_t add_link(L *l, const char *href, const char *download)
{
	page *p = l->p;
	size_t hn = strlen(href), dn = download != NULL ? strlen(download) : 0;
	char *s;
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
	s = amalloc(l->a, hn + dn + 3);
	if (s == NULL)
		return 0;
	memcpy(s, href, hn);
	s[hn] = 0;
	s[hn + 1] = download != NULL;
	memcpy(s + hn + 2, download != NULL ? download : "", dn + 1);
	p->links[p->nlinks] = s;
	return (uint16_t) (++p->nlinks);
}

/* an <a>'s href and download attribute, found in one pass (a look for
   each went through its attributes twice more) */
static const char *link_attrs(const dnode *n, const char **download)
{
	const char *href = NULL;
	uint16_t i;
	*download = NULL;
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		if (al == 4 && href == NULL && memcmp(an, "href", 4) == 0)
			href = n->attrs[i].val;
		else if (al == 8 && *download == NULL && memcmp(an, "download", 8) == 0)
			*download = n->attrs[i].val;
	}
	return href;
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

/* doc_poll's turn, every 256 boxes made or laid out: stopped, the layout
   fails as when out of memory */
static int stopped(L *l)
{
	if ((++l->polls & 255) == 0 && doc_poll != NULL && !doc_poll(DOC_POLL_LAYOUT))
		l->fail = 1;
	return l->fail;
}

static box *new_box(L *l, int kind, dnode *n, const ec_style *st, box *parent)
{
	box *b;
	if (stopped(l))
		return NULL;
	b = amalloc(l->a, sizeof *b);
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
   so that typing in it draws it again without laying the page out; a
   floated one (and an image button as a block) in a block of its own
   round it, as an <img> */
static void control(L *l, box *parent, dnode *n, const ec_style *st)
{
	fctl *c = n->ctl;
	ew_font *f = font_of(l, st);
	char lab[256];
	int32_t w, h = AUTO, line = f->ascent + f->descent + 4;
	box *b;

	if (c == NULL || c->kind == FC_HIDDEN)
		return;
	if (kw(st, EP_FLOAT) != KW_NONE && block_level(kw(st, EP_DISPLAY))) {
		parent = new_box(l, BX_BLOCK, n, st, parent);
		if (parent == NULL)
			return;
	}
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
			im = l->p->opt.image(l->p->opt.ud, src, 0, &iw, &ih);
		if (im != NULL) {
			if (parent->n == n) {
				parent->repl = 1;	/* the picture is the block */
			} else if (block_level(kw(st, EP_DISPLAY))) {
				parent = new_box(l, BX_BLOCK, n, st, parent);
				if (parent == NULL)
					return;
				parent->repl = 1;
			}
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
			h = l->p->opt.image(l->p->opt.ud, src, 0, &iw, &ih);
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
	{
		/* inline-block and its kin, in the flow: a block that is an atom
		   in its line (floated or out of the flow, a block as any) */
		int ib = (d == KW_INLINE_BLOCK || d == KW_INLINE_FLEX || d == KW_INLINE_GRID ||
			  d == KW_INLINE_TABLE) && kw(st, EP_FLOAT) == KW_NONE &&
			 kw(st, EP_POSITION) != KW_ABSOLUTE && kw(st, EP_POSITION) != KW_FIXED;
		b = new_box(l, block_level(d) || ib ? BX_BLOCK : BX_INLINE, c, st, parent);
		if (b == NULL)
			return;
		b->ib = (uint8_t) ib;
		if (ib && parent->link)
			b->link = parent->link;	/* a button in a link: the link's */
	}
	if (c->tag == T_A) {
		const char *dl, *href = link_attrs(c, &dl);
		if (href != NULL)
			b->link = add_link(l, href, dl);
	}
	b->list = d == KW_LIST_ITEM ? LB_ITEM : 0;
	if (d == KW_LIST_ITEM && kw(st, EP_LIST_STYLE_POSITION) == KW_INSIDE && kw(st, EP_LIST_STYLE_TYPE) != KW_NONE)
		new_box(l, BX_MARK, NULL, st, b);	/* the marker first, before ::before (CSS Lists 3, 3.1) */
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

/* a details element (the HTML Standard, 15.5.4: its two slots): its
   summary is shown always, the rest only when it is open. Without a
   summary it has one of the layout's own, "Details" after the triangle,
   in its style. The summary's box is where a click opens and closes it
   (LB_SUM). Returns 1 when it is closed; *sum: its summary */
static int details_start(L *l, box *parent, dnode *n, const ec_style *pst, dnode **sum)
{
	int open = dn_attr(n, "open") != NULL;
	*sum = dn_summary(n);
	if (*sum == NULL) {
		box *b = new_box(l, BX_BLOCK, NULL, pst, parent), *m;
		if (b != NULL) {
			b->anon = 1;
			b->list = LB_SUM;
			b->img = n;
			m = new_box(l, BX_MARK, NULL, pst, b);
			if (m != NULL)
				m->num = open ? KW_DISCLOSURE_OPEN : KW_DISCLOSURE_CLOSED;
			add_text_box(l, b, pst, "Details", 7);
		}
	}
	return !open;
}

/* a details' summary (det's first summary child) built: its box is where
   a click opens and closes det (LB_SUM). One with display: contents has
   no box, but a click on what is in it is one in it all the same (the
   HTML Standard's activation behaviour): its blocks are marked, and each
   run of its inline boxes goes in an anonymous inline of its own, laid
   out as its parent's run, whose parts on the lines are where a click
   opens and closes det, as an inline summary's are (collect) */
static void build_summary(L *l, box *parent, dnode *sum, dnode *det)
{
	box *before = parent->last, *b, *prev;
	build_element(l, parent, sum);
	b = before != NULL ? before->next : parent->first;
	if (sum->e.style == NULL || kw(sum->e.style, EP_DISPLAY) != KW_CONTENTS) {
		for (; b != NULL; b = b->next)
			if (b->n == sum) {	/* (none when display: none) */
				b->list |= LB_SUM;
				b->img = det;
				break;
			}
		return;
	}
	for (prev = before; b != NULL; prev = b, b = b->next) {
		box *w, *end, *k;
		if (b->kind == BX_BLOCK) {
			b->list |= LB_SUM;
			b->img = det;
			continue;
		}
		for (end = b; end->next != NULL && end->next->kind != BX_BLOCK; end = end->next)
			;
		w = amalloc(l->a, sizeof *w);
		if (w == NULL) {
			l->fail = 1;
			return;
		}
		memset(w, 0, sizeof *w);
		w->kind = BX_INLINE;
		w->anon = 1;
		w->list = LB_SUM;
		w->img = det;
		w->st = parent->st;
		w->parent = parent;
		w->link = parent->link;
		w->aw = w->ah = AUTO;
		w->first = b;
		w->last = end;
		for (k = b; ; k = k->next) {
			k->parent = w;
			if (k == end)
				break;
		}
		w->next = end->next;
		end->next = NULL;
		if (prev != NULL)
			prev->next = w;
		else
			parent->first = w;
		if (w->next == NULL)
			parent->last = w;
		b = w;
	}
}

static void build(L *l, box *parent, dnode *n)
{
	dnode *c = n->first, *sum = NULL;
	const ec_style *pst = n->type == DN_ELEMENT && n->e.style ? n->e.style : parent->st;
	if (EC_DEEP()) {
		l->p->deep = 1;		/* the stack's floor: no deeper */
		return;
	}
	if (n->tag == T_DETAILS && n->ns == DN_NS_HTML) {
		int closed = details_start(l, parent, n, pst, &sum);
		/* its summary first, whatever comes before it (the summary's
		   slot comes first); closed, nothing else */
		if (sum != NULL)
			build_summary(l, parent, sum, n);
		if (closed)
			return;
	}
	for (; c != NULL && !l->fail; c = c->next) {
		if (c == sum)
			continue;
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

static inline int floated(const box *c)
{
	return c->kind == BX_BLOCK && !c->anon && c->n != NULL && kw(c->st, EP_FLOAT) != KW_NONE;
}

/* a float among a block's (or an inline's) text: it goes with the line
   it is met on (CSS 2.1, 9.5.1: at that line's top, or below it when it
   doesn't fit there), so the text round it isn't broken into anonymous
   blocks there; but not in a flex or grid container (its children are
   items) nor among a table's parts */
static inline int text_float(const box *b, const box *c)
{
	return !b->row && !b->flex && !b->grid && !b->table && floated(c);
}

static void fixup(L *l, box *b)
{
	box *c, *prev, *next;
	int has_block = 0, has_inline = 0, has_float = 0, tf;
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
				if (k->kind == BX_BLOCK && !k->ib && !text_float(c, k))
					c->kind = BX_BLOCK;
		}
	}
	if (b->kind != BX_BLOCK)
		return;
	if (b->flex || b->grid) {
		/* a flex or grid container's element children are its items, blocks
		   (its text and pictures go in anonymous items) */
		for (c = b->first; c != NULL; c = c->next) {
			c->ib = 0;	/* (an inline-block item: an item as any) */
			if (c->kind == BX_INLINE && c->n != NULL) {
				c->kind = BX_BLOCK;
				fixup(l, c);
			}
		}
	}
	tf = !b->row && !b->flex && !b->grid && !b->table;	/* text_float's */
	for (c = b->first; c != NULL; c = c->next) {
		if (c->kind == BX_BLOCK && !c->ib) {
			if (tf && floated(c))
				has_float = 1;
			else
				has_block = 1;
		} else if (c->ib) {
			has_inline = 1;
		} else if (c->kind != BX_TEXT || !blank(c->text, c->len) || keeps_space(c->st)) {
			has_inline = 1;
		}
	}
	if (!has_block && (has_inline || !has_float) && !b->row && !b->flex && !b->grid) {
		/* text (the floats in it with their lines), or nothing */
		b->inl = 1;
		return;
	}
	if (!has_inline) {
		/* only blocks: white space between them goes */
		prev = NULL;
		for (c = b->first; c != NULL; c = next) {
			next = c->next;
			if (c->kind == BX_BLOCK && !c->ib) {
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
	/* runs of inline boxes (and the floats among them) go in anonymous
	   blocks */
	prev = NULL;
	for (c = b->first; c != NULL; c = next) {
		box *a, *k, *end;
		next = c->next;
		if (c->kind == BX_BLOCK && !c->ib && !(has_float && floated(c))) {
			prev = c;
			continue;
		}
		end = c;
		while (end->next != NULL && (end->next->kind != BX_BLOCK || end->next->ib ||
					     (has_float && floated(end->next))))
			end = end->next;
		next = end->next;
		/* a run of nothing but collapsible white space (and floats):
		   the white space dropped, the floats left as blocks */
		for (k = c; ; k = k->next) {
			if ((k->kind != BX_BLOCK || k->ib) &&
			    (k->kind != BX_TEXT || !blank(k->text, k->len) || keeps_space(k->st)))
				break;
			if (k == end) {
				k = NULL;
				break;
			}
		}
		if (k == NULL) {
			for (k = c; ; k = k->next) {
				if (k->kind == BX_BLOCK) {
					if (prev)
						prev->next = k;
					else
						b->first = k;
					prev = k;
				}
				if (k == end)
					break;
			}
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
		long cap = l->itcap ? l->itcap * 2 : IT_FIRST;
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

static int lb_letter(uint32_t c);

/* the direction of a word: 2 if it has Hebrew (or Arabic) letters, 1 if
   it has other letters, 3 if digits and no letter (a number), else 0 */
static int word_dir(const char *s, uint32_t n)
{
	const char *e = s + n;
	int ltr = 0, num = 0;
	while (s < e) {
		uint32_t c = utf8_next(&s, e);
		if ((c >= 0x590 && c < 0x900) || (c >= 0xFB1D && c < 0xFE00) || (c >= 0xFE70 && c < 0xFF00))
			return 2;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c < 0x590 && c != 0xD7 && c != 0xF7) ||
		    (c >= 0x900 && lb_letter(c)))
			ltr = 1;
		else if (c >= '0' && c <= '9')
			num = 1;
	}
	return ltr ? 1 : num ? 3 : 0;
}

/* the same in the page: in visual Hebrew a Hebrew word is drawn as its
   letters are stored, first to last (not DLF_RTL) */
static int word_dir_in(const L *l, const char *s, uint32_t n)
{
	int d = word_dir(s, n);
	return d == 2 && l->visual ? 1 : d;
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
	dnode *det;			/* in a summary laid out inline: its details */
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
	r->ctl = parent ? parent->ctl : l->ctl_in;
	r->det = parent ? parent->det : NULL;
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

/* dir: a word's (word_dir), -1 to work it out */
static void put_item(L *l, const RUNSTATE *r, int kind, const char *s, uint32_t n, int32_t w, int dir)
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
	it->det = r->det;
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
		it->dir = (uint8_t) (dir >= 0 ? dir : word_dir_in(l, s, n));
}

/* ---------------------------------------------------------------- places inside a word a line may end at

   UAX #14 (Unicode line breaking) for what a page's words have: after a
   hyphen or a slash between a letter or digit and a letter ("well-known",
   "and/or", a long address), after an en dash, either side of an em dash,
   either side of an ideograph (Chinese, Japanese, Korean, which have no
   spaces), at a zero width space or a soft hyphen (neither drawn; a line
   ending at a soft hyphen shows a hyphen); never before closing
   punctuation or after opening (the classes CL, CP, NS, EX, IS and OP:
   Japanese kinsoku), nor by a quotation mark (QU). Spaces are the
   breaks text_items makes already */

static int lb_letter(uint32_t c)
{
	return ((c | 32) >= 'a' && (c | 32) <= 'z') ||
	       (c >= 0xC0 && c != 0xD7 && c != 0xF7 && !(c >= 0x2000 && c < 0x2C00) && !(c >= 0x3000 && c < 0x3040));
}

static int lb_cjk(uint32_t c)
{
	return (c >= 0x2E80 && c < 0xA000) || (c >= 0xAC00 && c < 0xD7B0) || (c >= 0xF900 && c < 0xFB00) ||
	       (c >= 0xFE30 && c < 0xFE50) || (c >= 0xFF00 && c < 0xFF61) || (c >= 0x20000 && c < 0x40000);
}

/* no line ends before c: closing punctuation, small kana, quotation marks */
static int lb_no_before(uint32_t c)
{
	switch (c) {
	case ')': case ']': case '}': case ',': case '.': case ':': case ';': case '!': case '?':
	case '"': case '\'': case 0x2018: case 0x2019: case 0x201C: case 0x201D: case 0x2026:
	case 0x3001: case 0x3002: case 0x3005: case 0x3009: case 0x300B: case 0x300D: case 0x300F:
	case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B: case 0x301E: case 0x301F:
	case 0x303B: case 0x309B: case 0x309C: case 0x309D: case 0x309E: case 0x30A0: case 0x30FB:
	case 0x30FC: case 0x30FD: case 0x30FE:
	case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083:
	case 0x3085: case 0x3087: case 0x308E: case 0x3095: case 0x3096:
	case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3:
	case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6:
	case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F:
	case 0xFF3D: case 0xFF5D: case 0xFF60: case 0xFF61: case 0xFF63: case 0xFF64: case 0xFF9E:
	case 0xFF9F:
		return 1;
	}
	return c >= 0x31F0 && c < 0x3200;		/* (small katakana for Ainu) */
}

/* no line ends after c: opening punctuation, quotation marks */
static int lb_no_after(uint32_t c)
{
	switch (c) {
	case '(': case '[': case '{': case '"': case '\'': case 0x2018: case 0x2019: case 0x201C:
	case 0x201D: case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014:
	case 0x3016: case 0x3018: case 0x301A: case 0x301D: case 0xFF08: case 0xFF3B: case 0xFF5B:
	case 0xFF5F: case 0xFF62:
		return 1;
	}
	return 0;
}

/* text_items' bytes: 1 white space (a word's end), else bits: 2 a hyphen
   or a slash, 4 the first of a character from U+0080 to U+00BF (the soft
   hyphen) or from U+2000 (the dashes, the zero width space, ideographs),
   8 the first of a Hebrew one, 16 an ASCII letter, 32 a digit, 64 other
   ASCII punctuation, 128 the first of any other (a letter of Latin, Greek,
   Cyrillic...). A word with 2 or 4, with 8 and a letter or digit
   (Hebrew and a number or Latin in one word), or with punctuation and
   letters of the other direction than the text's ("2026." or "EWeb." in
   Hebrew, "!שלום" in English), goes to word_pieces; any other is copied
   as it is */
#define LB_SPLIT(seen, rtl) (((seen) & 6) != 0 || (((seen) & 8) && ((seen) & 48)) || \
			     (((seen) & 64) && ((seen) & ((rtl) ? 48 : 8))))
static const uint8_t lb_class[256] = {
	[' '] = 1, ['\t'] = 1, ['\n'] = 1, ['\r'] = 1, ['\f'] = 1, ['-'] = 2, ['/'] = 2,
	['!' ... ','] = 64, ['.'] = 64, [':' ... '@'] = 64, ['['... '`'] = 64, ['{' ... '~'] = 64,
	['0' ... '9'] = 32, ['A' ... 'Z'] = 16, ['a' ... 'z'] = 16,
	[0xc2] = 4, [0xc3 ... 0xd5] = 128, [0xd6] = 8, [0xd7] = 8, [0xd8 ... 0xe1] = 128, [0xe2 ... 0xff] = 4
};

/* in a number (UAX #9's ET, CS, ES): a sign beside its digits, or one
   between two */
static int num_sign(uint32_t c)
{
	return c == '%' || c == '$' || c == '#' || c == '+' || c == 0xB0 || c == 0xA3 || c == 0xA5 || c == 0x20AA ||
	       c == 0x20AC || c == 0x2030;
}



/* the code point of s that ends before *j, in the word from w; 0 at its
   start. *j: where it begins */
static uint32_t lb_before(const char *s, uint32_t w, uint32_t *j)
{
	uint32_t k = *j;
	const char *p;
	if (k <= w)
		return 0;
	while (--k > w && ((unsigned char) s[k] & 0xC0) == 0x80)
		;
	p = s + k;
	k = *j;
	*j = (uint32_t) (p - s);
	return utf8_next(&p, s + k);
}

/* a line may end between a and b, the code points either side (a2 the
   one before a, 0 for none) */
static int lb_inword(uint32_t a2, uint32_t a, uint32_t b)
{
	if (a == '-' || a == '/')
		return (lb_letter(a2) || (a2 >= '0' && a2 <= '9')) && lb_letter(b);
	if (a == 0x2013)		/* en dash: after it */
		return b != 0x2013 && !lb_no_before(b);
	if (a == 0x2014 || b == 0x2014)	/* em dash: either side, not between two */
		return a != b && !lb_no_before(b) && !lb_no_after(a);
	if (lb_cjk(a) || lb_cjk(b))
		return !lb_no_before(b) && !lb_no_after(a);
	return 0;
}

/* white-space pre or nowrap: no line ends in it */
static int no_wrap(const ec_style *s)
{
	int w = s != NULL ? kw(s, EP_WHITE_SPACE) : 0;
	return w == KW_PRE || w == KW_NOWRAP;
}

/* a letter of direction d in p..e (1 left to right, its digits too; 2
   Hebrew, and digits, which act as right to left on what is between
   them: UAX #9, N1) */
static int lb_more(const char *p, const char *e, int d)
{
	while (p < e) {
		uint32_t c = utf8_next(&p, e);
		if ((c >= 0x590 && c < 0x900) ? d == 2 :
		    (c >= '0' && c <= '9') || (d == 1 && (c < 0x80 ? (lb_class[c] & 16) != 0 : lb_letter(c))))
			return 1;
	}
	return 0;
}

/* t[a..b) as a word of the line, a line ending before it as cb says */
static void word_piece(L *l, const RUNSTATE *r, char *t, uint32_t a, uint32_t b, int cb)
{
	put_item(l, r, IT_WORD, t + a, b - a, (int32_t) font_width(r->font, t + a, b - a), -1);
	if (!l->fail)
		l->it[l->nit - 1].canbreak = (uint8_t) cb;
}

/* a word with a hyphen, a slash or a character looked at, s[i..e), into t
   from o: in pieces where a line may end inside it, each piece's canbreak
   the place before it (3, 4: at a soft hyphen), its soft hyphens and zero
   width spaces left out; and where Hebrew meets a number or Latin, each
   piece of one direction for visual_order. Where t ends */
static __attribute__((noinline)) uint32_t word_pieces(L *l, const RUNSTATE *r, const char *s, uint32_t i, uint32_t e,
						       char *t, uint32_t o, int tt, int wrap, int rtl,
						       int *line_start)
{
	uint32_t j = i, start = o, nst = o, pb = 0, est = o;
	int cb = 0, brk = 0, slow = 0, pdir = 0;
	while (j < e) {
		const char *q = s + j;
		uint32_t b = utf8_next(&q, s + e);
		int was = slow, d = (b >= 0x590 && b < 0x900) ? 2 : (b < 0x80 ? (lb_class[b] & 48) != 0 : lb_letter(b));
		if (b == 0xAD || b == 0x200B) {
			/* a soft hyphen, a zero width space: not drawn, a place
			   to end a line */
			if (o > start && brk == 0)
				brk = b == 0xAD ? 4 : 3;
			j = (uint32_t) (q - s);
			slow = 1;
			continue;
		}
		/* after a hyphen, a slash, a dash or an ideograph, or before
		   one: where a line may end */
		slow = b == '-' || b == '/' || b == 0x2013 || b == 0x2014 || lb_cjk(b);
		if (brk == 0 && o > start && (was || slow)) {
			uint32_t p = j, a = lb_before(s, i, &p), a2 = lb_before(s, i, &p);
			if (lb_inword(a2, a, b))
				brk = 3;
		}
		if (brk != 0 && wrap) {
			word_piece(l, r, t, start, o, cb);
			cb = brk;
			start = o;
			pdir = 0;
		} else if (d != 0 && pdir != 0 && d != pdir) {
			/* Hebrew and a number or Latin in one word: a piece of
			   each direction, no line ending between them (2), the
			   neutrals between them with the Hebrew (as UAX #9 has
			   them beside a number) */
			uint32_t cut = pdir == 1 && nst > start ? nst : num_sign(pb) && est > start ? est : o;
			word_piece(l, r, t, start, cut, wrap ? cb : 2);
			cb = 2;
			start = cut;
		} else if (o > start && d == 0 && pdir == (rtl ? 1 : 2) && !(num_sign(b) && pb >= '0' && pb <= '9') &&
			   !lb_more(q, s + e, pdir)) {
			/* punctuation after letters of the other direction than
			   the text's, none of theirs after it ("2026." or "EWeb."
			   in Hebrew, "!שלום" in English): a piece of its own,
			   which UAX #9 puts by the text around it */
			word_piece(l, r, t, start, o, wrap ? cb : 2);
			cb = 2;
			start = o;
			pdir = 0;
		} else if (o > start && d == (rtl ? 1 : 2) && pdir == 0 && !num_sign(pb)) {
			/* and before them ("(10" in Hebrew) */
			word_piece(l, r, t, start, o, wrap ? cb : 2);
			cb = 2;
			start = o;
		}
		brk = 0;
		if (b < 0x80) {
			char ch = (char) b;
			if (tt == KW_UPPERCASE && ch >= 'a' && ch <= 'z')
				ch -= 32;
			else if (tt == KW_LOWERCASE && ch >= 'A' && ch <= 'Z')
				ch += 32;
			t[o++] = ch;
			j++;
		} else {
			while (s + j < q)
				t[o++] = s[j++];
		}
		if (d != 0) {
			pdir = d;
			nst = o;
		}
		if (!num_sign(b))
			est = o;	/* (where the signs before a number begin: "$5") */
		pb = b;
	}
	if (o == start && cb == 0)
		return o;	/* (only soft hyphens: nothing) */
	word_piece(l, r, t, start, o, wrap ? cb : 2);	/* (2: no line ends before it, or in it) */
	*line_start = 0;
	return o;
}

/* text into words and spaces, collapsed as its white-space says, copied
   so that a node's words and spaces follow on (one run to draw) */
static void text_items(L *l, const RUNSTATE *r, const char *s, uint32_t n, int *line_start)
{
	int ws = kw(r->st, EP_WHITE_SPACE), tt = kw(r->st, EP_TEXT_TRANSFORM);
	int keep = ws == KW_PRE || ws == KW_PRE_WRAP || ws == KW_BREAK_SPACES;
	int rtl = kw(r->st, EP_DIRECTION) == KW_RTL && !l->visual;
	int newlines = keep || ws == KW_PRE_LINE;
	int wrap = !(ws == KW_PRE || ws == KW_NOWRAP);
	int32_t sw = font_advance(r->font, ' ');
	uint32_t i = 0, o = 0;
	long k;
	char *t = amalloc(l->a, n + 1);

	if (t == NULL) {
		l->fail = 1;
		return;
	}
	while (i < n && !l->fail) {
		unsigned char c = (unsigned char) s[i];
		ITEM *it;
		if (c == '\n' && newlines) {
			put_item(l, r, IT_BREAK, NULL, 0, 0, 0);
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
				put_item(l, r, IT_SPACE, t + o, 1, c == '\t' ? 8 * sw : sw, 0);
				o++;
				i++;
				it = &l->it[l->nit - 1];
				it->canbreak = (uint8_t) wrap;
				*line_start = 0;
				continue;
			}
			while (i < n && is_space((unsigned char) s[i]) && !(s[i] == '\n' && newlines))
				i++;
			/* one space for the white space either side of a float */
			for (k = l->nit - 1; k >= 0 && l->it[k].kind == IT_FLOAT; k--)
				;
			if (!*line_start && !(k >= 0 && l->it[k].kind == IT_SPACE && l->it[k].collapse)) {
				t[o] = ' ';
				put_item(l, r, IT_SPACE, t + o, 1, sw, 0);
				o++;
				it = &l->it[l->nit - 1];
				it->canbreak = (uint8_t) wrap;
				it->collapse = 1;
			}
			continue;
		}
		{
			/* a word: copied as it is, unless it has a hyphen, a
			   slash or a character looked at (lb_class), when it is
			   made again in pieces where a line may end inside it */
			uint32_t j = i, start = o;
			int seen = 0, k;
			if (tt != KW_UPPERCASE && tt != KW_LOWERCASE) {
				/* (most text: a loop without text-transform's tests) */
				while (j < n && (k = lb_class[(unsigned char) s[j]]) != 1) {
					seen |= k;
					t[o++] = s[j++];
				}
			} else {
				while (j < n && (k = lb_class[(unsigned char) s[j]]) != 1) {
					char ch = s[j++];
					seen |= k;
					if (tt == KW_UPPERCASE && ch >= 'a' && ch <= 'z')
						ch -= 32;
					else if (tt == KW_LOWERCASE && ch >= 'A' && ch <= 'Z')
						ch += 32;
					t[o++] = ch;
				}
			}
			if (LB_SPLIT(seen, rtl)) {
				o = word_pieces(l, r, s, i, j, t, start, tt, wrap, rtl, line_start);
			} else {
				/* (its direction from its bytes: only one with
				   Hebrew or another script's letters looked at) */
				put_item(l, r, IT_WORD, t + start, o - start,
					 (int32_t) font_width(r->font, t + start, o - start),
					 seen & 136 ? -1 : seen & 16 ? 1 : seen & 32 ? 3 : 0);
				if (!wrap && !l->fail)
					l->it[l->nit - 1].canbreak = 2;	/* (no line ends before it, or in it) */
				*line_start = 0;
			}
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

static const char *marker_text(const ec_style *st, int fixed, int n, char *buf);

/* an inside marker: its text, and a space after it (the counter style's
   suffix) that the white space after it goes into (as ::marker's
   white-space: pre; CSS Lists 3, 3.1). It isn't a place the line chooses
   to break at, but the first word still goes on the next line when it
   doesn't fit beside the marker, as lines breaks before any word that
   doesn't fit where the line has no space to break at (CSS Text 3 allows
   a break there: the summary's or item's white-space is normal) */
static void mark_items(L *l, const RUNSTATE *r, const box *m, int *line_start)
{
	const box *li = m->parent;
	char buf[24];
	const char *s;
	ITEM *it;
	uint32_t n;

	while (li != NULL && !(li->list & LB_ITEM) && !m->num)
		li = li->parent;
	if (li == NULL || (s = marker_text(li->st, m->num, li->num, buf)) == NULL || *s == 0)
		return;
	n = (uint32_t) strlen(s);
	if (s == buf && (s = astrdup(l->a, buf, n)) == NULL) {
		l->fail = 1;
		return;
	}
	put_item(l, r, IT_WORD, s, n, (int32_t) font_width(r->font, s, n), -1);
	if (!m->num && li->st->k[EP_LIST_STYLE_TYPE] == EK_PTR)
		return;		/* a string: as it is, the white space after it dropped */
	put_item(l, r, IT_SPACE, " ", 1, font_advance(r->font, ' '), 0);
	if (l->fail)
		return;
	it = &l->it[l->nit - 1];
	it->collapse = 1;
	*line_start = 0;
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
		case BX_MARK:
			mark_items(l, pr, c, line_start);
			break;
		case BX_BR:
			put_item(l, pr, IT_BREAK, NULL, 0, 0, 0);
			*line_start = 1;
			break;
		case BX_ATOM: {
			ITEM *it;
			runstate(l, &r, c->st, c->n, pr, c->link ? c->link : pr->link);
#ifndef EW_ST
			if (c->n != NULL)
				r.node = c->n;
#endif
			put_item(l, &r, IT_ATOM, c->text, c->len, atom_w(l, c, r.font), 0);
			it = &l->it[l->nit - 1];
			it->atom = c;
			it->dir = (uint8_t) word_dir_in(l, c->text, c->len);
			it->canbreak = (uint8_t) (no_wrap(c->st) ? 2 : 0);
			if (c->text != NULL && l->cw != AUTO && it->w > l->cw && l->cw > 0) {
				/* a picture's alt text (one without a size yet, or
				   none): no wider than the line, its text cut there
				   (it->len, drawn by draw_atom), as a browser shows
				   it in its box */
				int32_t iw, ih, tw = 0;
				if (c->atom == AT_LABEL ||
				    (c->atom == AT_IMAGE && (image_size(l, c, &iw, &ih), iw <= 0 || ih <= 0))) {
					const char *p = c->text, *e = p + c->len;
					it->w = l->cw;
					while (p < e) {
						const char *q = p;
						int32_t a = font_advance(it->font, utf8_next(&q, e));
						if (tw + a > it->w)
							break;
						tw += a;
						p = q;
					}
					it->len = (uint32_t) (p - c->text);
				}
			}
			if (c->atom == AT_IMAGE) {
				int32_t iw, ih;
				image_size(l, c, &iw, &ih);
				if (iw > 0 && ih > 0) {
					/* on the baseline, or its middle on the text's, or
					   at the line's top or bottom, or its middle on the
					   baseline itself (<img align=middle>), or hanging
					   from the text's top */
					int va = kw(c->st, EP_VERTICAL_ALIGN);
					it->asc = ih;
					it->desc = 0;
					if (va == KW_MIDDLE) {
						it->asc = ih / 2 + (r.font->ascent - r.font->descent) / 2;
						it->desc = ih - it->asc;
					} else if (va == KW_TOP || va == KW_BOTTOM) {
						/* left out of the line's height until it
						   is known (line_edges) */
						it->asc = 0;
						l->lined = 1;
					} else if (va == KW_WEBKIT_BASELINE_MIDDLE) {
						it->asc = ih - ih / 2;
						it->desc = ih / 2;
					} else if (va == KW_TEXT_TOP) {
						it->asc = r.font->ascent;
						it->desc = ih - it->asc;
					} else if (va == KW_TEXT_BOTTOM) {
						it->asc = ih - r.font->descent;
						it->desc = r.font->descent;
					}
					if (it->desc < 0 && va != KW_TEXT_TOP) {
						it->asc += it->desc;
						it->desc = 0;
					}
					if (va != KW_TOP && va != KW_BOTTOM) {
						/* moved with its sup or sub, the line
						   as tall as that needs (its top and
						   bottom: line_edges's, by the line) */
						it->asc -= it->shift;
						it->desc += it->shift;
					}
					it->dir = 0;
				}
			} else if (c->ah != AUTO || c->atom != AT_LABEL) {
				int32_t h = c->ah != AUTO ? c->ah : r.font->ascent + r.font->descent + 4;
				it->asc = h - 2 - r.font->descent;
				it->desc = r.font->descent + 2;
				if (it->asc < r.asc)
					it->asc = r.asc;
				it->asc -= it->shift;
				it->desc += it->shift;
			}
			*line_start = 0;
			break;
		}
		case BX_INLINE: {
			int32_t gl, gr;
			if (c->anon) {
				/* a display: contents summary's run (build_summary):
				   laid out as its parent's, its parts on the lines
				   where a click opens and closes its details; its
				   text is the summary's, for scripts */
				r = *pr;
				r.det = c->img;
#ifndef EW_ST
				r.node = dn_summary(c->img);
#endif
				l->sums = 1;
				collect(l, c, &r, line_start);
				break;
			}
			runstate(l, &r, c->st, c->n, pr, c->link ? c->link : pr->link);
#ifndef EW_ST
			if (c->n != NULL)
				r.node = c->n;
#endif
			if (c->n != NULL && c->n->tag == T_LABEL && c->n->ns == DN_NS_HTML)
				r.ctl = label_for(l, c->n);
			if (c->list & LB_SUM) {
				/* a summary laid out inline (inline-block too): its
				   click areas are its parts on the lines */
				r.det = c->img;
				l->sums = 1;
			}
			gl = len0(l, c->st, EP_MARGIN_LEFT, AUTO) + len0(l, c->st, EP_PADDING_LEFT, AUTO) +
			     (kw(c->st, EP_BORDER_LEFT_STYLE) > KW_HIDDEN ? len0(l, c->st, EP_BORDER_LEFT_WIDTH, AUTO) : 0);
			gr = len0(l, c->st, EP_MARGIN_RIGHT, AUTO) + len0(l, c->st, EP_PADDING_RIGHT, AUTO) +
			     (kw(c->st, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN ? len0(l, c->st, EP_BORDER_RIGHT_WIDTH, AUTO) : 0);
			if (gl > 0) {
				put_item(l, &r, IT_GAP, NULL, 0, gl, 0);
				if (!l->fail && no_wrap(c->st))
					l->it[l->nit - 1].canbreak = 2;
				*line_start = 0;
			}
			collect(l, c, &r, line_start);
			if (gr > 0) {
				put_item(l, &r, IT_GAP, NULL, 0, gr, 0);
				if (!l->fail && no_wrap(c->st))
					l->it[l->nit - 1].canbreak = 2;
			}
			break;
		}
		case BX_BLOCK: {
			ITEM *it;
			if (c->ib) {
				/* an inline-block: an atom laid out as a block when
				   its line's width is known (lines, ib_layout) */
				runstate(l, &r, c->st, c->n, pr, c->link ? c->link : pr->link);
#ifndef EW_ST
				if (c->n != NULL)
					r.node = c->n;
#endif
				put_item(l, &r, IT_ATOM, NULL, 0, 0, 0);
				if (l->fail)
					break;
				it = &l->it[l->nit - 1];
				it->atom = c;
				it->ib = 1;
				it->dir = 0;
				it->canbreak = (uint8_t) (no_wrap(pr->st) ? 2 : 0);
				it->alpha = pr->alpha;	/* the run's, for its inside (ib_layout) */
				l->ibs = 1;
				*line_start = 0;
				break;
			}
			/* a float in the text (fixup's text_float) */
			it = item(l);
			if (it != NULL) {
				it->kind = IT_FLOAT;
				it->atom = c;
				l->floats = 1;
			}
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

/* how many blocks' places (for scripts) there are so far */
static long nblocks(const L *l)
{
#ifndef EW_ST
	return l->p->nblocks;
#else
	(void) l;
	return 0;
#endif
}

/* those from b on moved by (dx, dy), with their items */
static void shift_blocks(L *l, long b, int32_t dx, int32_t dy)
{
#ifndef EW_ST
	for (; b < l->p->nblocks; b++) {
		l->p->blocks[b].x += dx;
		l->p->blocks[b].y += dy;
	}
#else
	(void) l, (void) b, (void) dx, (void) dy;
#endif
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
	if (d == KW_INLINE_BLOCK || d == KW_FLOW_ROOT || d == KW_TABLE_CELL || d == KW_FLEX || d == KW_GRID ||
	    d == KW_INLINE_FLEX || d == KW_INLINE_GRID || d == KW_INLINE_TABLE)
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

/* the items i..j-1 of a line in the order they are drawn, and each one's
   direction (DLF_RTL in its flags): UAX #9 with the items for its characters, each a
   word left to right (Latin, 1), right to left (Hebrew, 2), a number (3)
   or neutral (0: punctuation, spaces): a number after Latin is Latin
   (W7); a neutral between two of the same direction takes it, a number
   counting as right to left, else the line's (N1, N2); their levels (I1,
   I2: a number in Hebrew, or Latin in a right-to-left line, one above),
   and the runs at each level or above reversed, from the highest (L2).
   That is all of it for one level of text (embeddings, dir on an inline
   element, aren't yet). A page in visual Hebrew is already in its order:
   as it is (the HTML Standard's rendering for ISO-8859-8, 15.3.5:
   unicode-bidi: isolate-override; for the whole page, as browsers do it,
   and whatever the line's direction) */
static void visual_order(L *l, long i, long j, int rtl, long *ord)
{
	long k, n = j - i, s, e;
	int last = rtl ? 2 : 1, top = 0, v;
	uint8_t *lv, *c;
	for (k = 0; k < n; k++) {
		ord[k] = i + k;
		l->it[i + k].flags &= (uint8_t) ~DLF_RTL;
	}
	if (l->visual || n <= 0 || (lv = malloc((size_t) n * 2)) == NULL)
		return;
	c = lv + n;
	for (k = 0; k < n; k++) {
		const ITEM *it = &l->it[i + k];
		int d = it->kind == IT_WORD || it->kind == IT_ATOM ? it->dir : 0;
		if (d == 1 || d == 2)
			last = d;
		else if (d == 3 && last == 1)
			d = 1;
		c[k] = (uint8_t) d;
	}
	for (k = 0; k < n; k++) {
		int a, b;
		lv[k] = c[k];
		if (c[k] != 0)
			continue;
		for (s = k - 1; s >= 0 && c[s] == 0; s--)
			;
		for (e = k + 1; e < n && c[e] == 0; e++)
			;
		a = s >= 0 ? (c[s] == 1 ? 1 : 2) : (rtl ? 2 : 1);
		b = e < n ? (c[e] == 1 ? 1 : 2) : (rtl ? 2 : 1);
		lv[k] = (uint8_t) (a == b ? a : rtl ? 2 : 1);
	}
	for (k = 0; k < n; k++) {
		lv[k] = (uint8_t) (rtl ? (lv[k] == 2 ? 1 : 2) : (lv[k] == 2 ? 1 : lv[k] == 3 ? 2 : 0));
		if ((lv[k] & 1) && l->it[i + k].kind == IT_WORD)
			l->it[i + k].flags |= DLF_RTL;	/* (a space is drawn the same either way) */
		if (lv[k] > top)
			top = lv[k];
	}
	for (v = top; v >= 1; v--)
		for (s = 0; s < n; s = e + 1) {
			while (s < n && lv[s] < v)
				s++;
			if (s >= n)
				break;
			for (e = s; e + 1 < n && lv[e + 1] >= v; e++)
				;
			reverse(ord, lv, s, e);
		}
	free(lv);
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
	h = l->p->opt.image(l->p->opt.ud, u->s, 1, &iw, &ih);
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

/* an atom on its line (base: the line's baseline; its asc and desc are
   from there, its sup or sub's shift in them, as a word's are) */
static void draw_atom(L *l, ITEM *it, int32_t x, int32_t base)
{
	box *b = it->atom;
	int32_t h = it->asc + it->desc, y = base - it->asc;
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
		/* (it->len: as much of it as its width holds, collect) */
		text(l, x, base + it->shift, &t, b->text, it->len, it->w);
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
	l->it[k + 1].canbreak = 0;	/* (no soft hyphen before the rest) */
}

/* the pictures of items[i..end) placed by their line's top or bottom
   (vertical-align, CSS 2.1, 10.8.2), which collect left out of the
   line's height: the line as tall as they need, then each at its edge */
static void line_edges(L *l, long i, long end, int32_t *asc, int32_t *desc)
{
	int32_t top = 0, bot = 0, iw, ih;
	long k;
	int pass;
	for (pass = 0; pass < 2; pass++) {
		for (k = i; k < end; k++) {
			ITEM *it = &l->it[k];
			int va;
			if (it->kind != IT_ATOM || it->asc + it->desc != 0 || (it->atom->atom != AT_IMAGE && !it->ib))
				continue;
			va = kw(it->atom->st, EP_VERTICAL_ALIGN);
			if (va != KW_TOP && va != KW_BOTTOM)
				continue;
			if (it->ib)
				ih = it->ib == 2 ? l->ibx[it->len - 1].h : 0;
			else
				image_size(l, it->atom, &iw, &ih);
			if (pass == 0) {
				if (va == KW_TOP && ih > top)
					top = ih;
				else if (va == KW_BOTTOM && ih > bot)
					bot = ih;
				continue;
			}
			it->asc = va == KW_TOP ? *asc : ih - *desc;
			it->desc = ih - it->asc;
		}
		if (pass == 0) {
			if (top > *asc + *desc)
				*desc = top - *asc;
			if (bot > *asc + *desc)
				*asc = bot - *desc;
		}
	}
}

/* the items of the text being laid out or measured, set aside while a
   float in it is measured or laid out (with items of its own) */
typedef struct {
	ITEM *it;
	long nit, cap;
	int lined, floats, sums, ibs;
	int32_t cw;
} STASH;

static void stash(L *l, STASH *s)
{
	s->it = l->it;
	s->nit = l->nit;
	s->cap = l->itcap;
	s->lined = l->lined;
	s->floats = l->floats;
	s->sums = l->sums;
	s->ibs = l->ibs;
	s->cw = l->cw;
	l->it = NULL;
	l->nit = l->itcap = 0;
}

static void unstash(L *l, const STASH *s)
{
	free(l->it);
	l->it = s->it;
	l->nit = s->nit;
	l->itcap = s->cap;
	l->lined = s->lined;
	l->floats = s->floats;
	l->sums = s->sums;
	l->ibs = s->ibs;
	l->cw = s->cw;
}

static int32_t float_width(L *l, box *c, int32_t w);
static void place_float(L *l, box *c, int32_t x, int32_t w, int32_t y, int32_t wf);
static int32_t layout_block(L *l, box *b, int32_t x, int32_t y, int32_t cbw, int fixed_w,
			    int32_t *mb_out, int32_t *first);

/* the float in the text at items[k]: its width in a block w wide
   (kept in its len), or placed in it, its top no higher than y */
static int32_t item_float_w(L *l, long k, int32_t w)
{
	STASH s;
	box *c = l->it[k].atom;
	int32_t wf;
	if (l->it[k].len != 0)
		return (int32_t) l->it[k].len;
	stash(l, &s);
	wf = float_width(l, c, w);
	unstash(l, &s);
	l->it[k].len = (uint32_t) wf;
	return wf;
}

static void item_float(L *l, long k, int32_t x, int32_t w, int32_t y)
{
	STASH s;
	box *c = l->it[k].atom;
	int32_t wf = l->it[k].len != 0 ? (int32_t) l->it[k].len : AUTO;
	l->it[k].collapse = 1;
	stash(l, &s);
	place_float(l, c, x, w, y, wf);
	unstash(l, &s);
}

/* an inline-block (item k) laid out as a block at (0, 0) in a line w
   wide: as wide as it asks, or shrunk to fit (as a float); its baseline
   its last line's (CSS 2.1, 10.8.1; an inline flex, grid or table's its
   first, CSS Flexbox 8.5, Grid 11.5, CSS 2.1 17.5.1), or its bottom
   margin edge when it has none or hides what overflows it, where
   vertical-align puts it. Its inside has the opacity and the label of
   the text round it. */
static void ib_layout(L *l, long k, int32_t w)
{
	STASH s;
	ITEM *it;
	IBX *x;
	box *c = l->it[k].atom;
	int32_t wf, hf, mb, f = AUTO, lb, base;
	long k0 = l->p->nitems, f0 = l->nlater;
#ifndef EW_ST
	long b0 = l->p->nblocks;
#endif
	unsigned alpha = l->alpha;
	uint16_t ctl = l->ctl_in;
	int va;
	l->alpha = l->it[k].alpha;
	l->ctl_in = l->it[k].ctl;
	stash(l, &s);
	wf = float_width(l, c, w);
	l->last_base = AUTO;
	l->mov++;
	hf = layout_block(l, c, 0, 0, wf, 1, &mb, &f);
	l->mov--;
	lb = c->flex || c->grid || c->table ? f : l->last_base;
	unstash(l, &s);
	l->alpha = alpha;
	l->ctl_in = ctl;
	if (l->nibx == l->ibxcap) {
		long cap = l->ibxcap ? l->ibxcap * 2 : 8;
		IBX *n = realloc(l->ibx, cap * sizeof *n);
		if (n == NULL) {
			l->fail = 1;
			return;
		}
		l->ibx = n;
		l->ibxcap = cap;
	}
	x = &l->ibx[l->nibx++];
	x->h = hf;
	x->k0 = k0;
	x->k1 = l->p->nitems;
#ifndef EW_ST
	x->b0 = b0;
	x->b1 = l->p->nblocks;
#else
	x->b0 = x->b1 = 0;
#endif
	x->f0 = f0;
	x->f1 = l->nlater;
	it = &l->it[k];
	it->ib = 2;
	it->len = (uint32_t) l->nibx;
	it->w = wf;
	base = lb == AUTO || lb > hf || kw(c->st, EP_OVERFLOW_X) != KW_VISIBLE ||
	       kw(c->st, EP_OVERFLOW_Y) != KW_VISIBLE ? hf : lb;
	va = kw(c->st, EP_VERTICAL_ALIGN);
	it->asc = base;
	it->desc = hf - base;
	if (va == KW_MIDDLE) {
		it->asc = hf / 2 + (it->font->ascent - it->font->descent) / 2;
		it->desc = hf - it->asc;
	} else if (va == KW_TOP || va == KW_BOTTOM) {
		/* by the line's top or bottom (line_edges) */
		it->asc = it->desc = 0;
		l->lined = 1;
		return;
	} else if (va == KW_TEXT_TOP) {
		it->asc = it->font->ascent;
		it->desc = hf - it->asc;
	} else if (va == KW_TEXT_BOTTOM) {
		it->asc = hf - it->font->descent;
		it->desc = it->font->descent;
	}
	it->asc -= it->shift;
	it->desc += it->shift;
}

/* the places (anchors) of the blocks' nodes in top's box tree moved
   down by dy, without recursion (an inline-block may be deep already);
   those never laid out (-1) left */
static void ib_nodes(box *top, int32_t dy)
{
	box *b = top;
	for (;;) {
		if (b->n != NULL && b->kind == BX_BLOCK && b->n->y >= 0)
			b->n->y += dy;
		if (b->first != NULL && (b->kind == BX_BLOCK || b->kind == BX_INLINE)) {
			b = b->first;
			continue;
		}
		while (b != top && b->next == NULL)
			b = b->parent;
		if (b == top)
			return;
		b = b->next;
	}
}

static void place_later(L *l, long f0, long f1, int32_t dx, int32_t dy);

/* an inline-block laid out (ib_layout) put at x on its line, its top
   where its baseline (it->asc) meets the line's */
static void ib_place(L *l, const ITEM *it, int32_t x, int32_t base)
{
	const IBX *b = &l->ibx[it->len - 1];
	int32_t dy = base - it->asc;
	long k, f0 = b->f0, f1 = b->f1;
	for (k = b->k0; k < b->k1 && k < l->p->nitems; k++) {
		l->p->items[k].x += x;
		l->p->items[k].y += dy;
	}
#ifndef EW_ST
	for (k = b->b0; k < b->b1 && k < l->p->nblocks; k++) {
		l->p->blocks[k].x += x;
		l->p->blocks[k].y += dy;
	}
#endif
	if (dy != 0)
		ib_nodes(it->atom, dy);
	if (f1 > f0) {
		/* laid out now, the line's items kept aside (b is let go) */
		STASH s;
		stash(l, &s);
		place_later(l, f0, f1, x, dy);
		unstash(l, &s);
	}
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

	/* HTML's legacy values align the lines as the plain ones do; the
	   blocks inside are layout_block's */
	if (ta >= KW_WEBKIT_LEFT && ta <= KW_WEBKIT_CENTER)
		ta = ta == KW_WEBKIT_LEFT ? KW_LEFT : ta == KW_WEBKIT_RIGHT ? KW_RIGHT : KW_CENTER;

	/* the inline-blocks first, now that the width is known */
	for (i = 0; l->ibs && i < l->nit && !l->fail; i++)
		if (l->it[i].ib == 1)
			ib_layout(l, i, w);
	i = 0;
	ord = malloc((n ? n : 1) * sizeof *ord);
	if (ord == NULL) {
		l->fail = 1;
		return 0;
	}
	while (i < l->nit) {
		long j, k, last_break = -1, end;
		int32_t lw = 0, avail, asc = strut_a, desc = strut_d, off, base, lx = x, lwid = w, rx, hy = 0;
		int any = 0, broke = 0, justify, pend = 0;

		/* the line's start: collapsible spaces go; a float there is
		   placed first, from the line's top */
		for (;;) {
			while (i < l->nit && l->it[i].kind == IT_SPACE && l->it[i].collapse)
				i++;
			if (i >= l->nit || l->it[i].kind != IT_FLOAT)
				break;
			if (!l->it[i].collapse)
				item_float(l, i, x, w, cy);
			i++;
		}
		if (i >= l->nit)
			break;
		if (l->nfl > l->fl0) {
			/* beside the floats there: below them when not even the
			   first word fits */
			int32_t need = l->it[i].w < w ? l->it[i].w : w, nb;
			int32_t hgt = l->it[i].asc + l->it[i].desc > lh ? l->it[i].asc + l->it[i].desc : lh;
			int tries;
			for (tries = 0; tries < 64; tries++) {
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
			if (it->kind == IT_FLOAT) {
				/* a float met on the line: at the line's top, the line
				   shorter beside it, when it fits beside what is on it
				   already; else below the line, when it is done, and
				   so is every float after it on the line, none higher
				   than one before it (CSS 2.1, 9.5.1, rule 5) */
				if (it->collapse) {
					/* placed already */
				} else if (!pend && (lw == 0 || item_float_w(l, j, w) <= avail - lw)) {
					int32_t hgt = l->it[i].asc + l->it[i].desc;
					item_float(l, j, x, w, cy);
					band(l, cy, cy + (hgt > lh ? hgt : lh), x, x + w, &lx, &rx);
					lwid = rx - lx;
					avail = lwid - (firstline ? indent : 0);
				} else {
					pend = 1;
				}
				continue;
			}
			if (it->kind == IT_SPACE) {
				lw += it->w;
				if (it->canbreak)
					last_break = j;
				continue;
			}
			if (it->canbreak >= 3 && j > i &&
			    (it->canbreak == 3 || lw + font_advance(l->it[j - 1].font, '-') <= avail))
				last_break = j - 1;	/* inside a word (at a soft hyphen, if its hyphen fits) */
			if (lw + it->w > avail && any && (last_break >= i || it->canbreak != 2)) {
				/* at the last space; with none, before this word (not
				   one of white-space pre or nowrap: it goes on the
				   line, past its end) */
				if (last_break >= i)
					j = last_break + 1;
				break;
			}
			if (it->kind == IT_WORD && it->w > avail - lw && lw == 0 && it->canbreak != 2) {
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
		/* trailing spaces don't count (nor floats) */
		while (end > i && ((l->it[end - 1].kind == IT_SPACE && (l->it[end - 1].collapse || l->it[end - 1].canbreak)) ||
				   l->it[end - 1].kind == IT_FLOAT))
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
		/* ended at a soft hyphen: its hyphen drawn (not on a right-to-left
		   line) */
		if (!broke && !rtl && end == j && j > i && j < l->nit && l->it[j].kind == IT_WORD &&
		    l->it[j].canbreak == 4 && l->it[j - 1].kind == IT_WORD && !l->it[j - 1].hidden &&
		    l->it[j - 1].dir != 2) {
			hy = font_advance(l->it[j - 1].font, '-');
			lw += hy;
		}
		if (l->lined)
			line_edges(l, i, end, &asc, &desc);
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
		l->last_base = base;
		l->last_k = l->p->nitems;
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
				if ((it->bg >> 24) != 0 && !it->hidden && it->kind != IT_BREAK && !it->ib) {
					/* a run of one background: one fill (not under an
					   inline-block, which is drawn already) */
					int32_t x0 = it->x, x1 = it->x + it->w;
					while (k + 1 < end - i && l->it[ord[k + 1]].bg == it->bg && !l->it[ord[k + 1]].ib) {
						k++;
						x1 = l->it[ord[k]].x + l->it[ord[k]].w;
					}
					fill(l, x0, base - asc, x1 - x0, asc + desc, it->bg);
				}
			}
			for (k = 0; l->sums && k < end - i; k++) {
				ITEM *it = &l->it[ord[k]];
				if (it->det != NULL && it->kind != IT_BREAK && it->kind != IT_FLOAT && !it->hidden &&
				    !(it->flags & DLF_NOHIT)) {
					/* an inline summary's part of the line: one
					   click area (not where a click goes through
					   it, nor where it is hidden) */
					int32_t x0 = it->x, x1 = it->x + it->w;
					while (k + 1 < end - i && l->it[ord[k + 1]].det == it->det) {
						k++;
						x1 = l->it[ord[k]].x + l->it[ord[k]].w;
					}
					sum_rect(l, it->det, x0, base - asc, x1 - x0, asc + desc);
				}
			}
			/* the items in the order they are read (a right-to-left
			   line's, and Hebrew words in a left-to-right one's, in
			   the list as in the text: find.c reads the list so) */
			for (k = 0; k < end - i; k++) {
				ITEM *it = &l->it[i + k];
				if (it->ib) {
					/* (hidden too: what is visible in it shows, and
					   its places are kept; not when out of memory) */
					if (it->ib == 2) {
						if (l->ibx[it->len - 1].k0 < l->last_k)
							l->last_k = l->ibx[it->len - 1].k0;	/* made first */
						ib_place(l, it, it->x, base);
					}
					continue;
				}
				if (it->hidden)
					continue;
				if (it->kind == IT_ATOM) {
					draw_atom(l, it, it->x, base);
				} else if (it->kind == IT_WORD && (it->flags & DLF_RTL)) {
					text(l, it->x, base + it->shift, it, it->text, it->len, it->w);
				} else if (it->kind == IT_WORD || (it->kind == IT_SPACE && it->flags)) {
					/* one run for words and spaces that follow on in the text */
					const char *s = it->text;
					uint32_t len = it->len;
					int32_t ww = it->w;
					while (k + 1 < end - i) {
						ITEM *nx = &l->it[i + k + 1];
						if ((nx->kind != IT_WORD && nx->kind != IT_SPACE) || justify ||
						    nx->text != s + len || nx->font != it->font || nx->color != it->color ||
						    nx->flags != it->flags || nx->link != it->link || nx->hidden ||
						    nx->shift != it->shift || nx->x != it->x + ww)
							break;	/* (and not one drawn elsewhere on the line) */
						len += nx->len;
						ww += nx->w;
						k++;
					}
					text(l, it->x, base + it->shift, it, s, len, ww);
				}
			}
			if (hy > 0)
				text(l, l->it[end - 1].x + l->it[end - 1].w, base + l->it[end - 1].shift, &l->it[end - 1], "-",
				     1, hy);
		}
		cy += asc + desc;
		/* the line's floats that didn't fit on it: below it */
		for (k = i; pend && k < j; k++)
			if (l->it[k].kind == IT_FLOAT && !l->it[k].collapse)
				item_float(l, k, x, w, cy);
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
	long ibx;
	l->nit = 0;
	l->lined = l->floats = l->sums = l->ibs = 0;
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
	ibx = l->nibx;
	h = lines(l, b, x, y, w, first);
	l->nibx = ibx;	/* its inline-blocks are placed */
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
static int columns(L *l, box *t, int32_t w, int32_t *cols, int32_t sp);
static int row_group(const box *g);
static void bedge(L *l, const box *b, int side, BEDGE *e);
static int collapses(const box *t);
static int32_t spacing(L *l, const ec_style *s, int vertical);

/* the widest line of a block of text, unbroken */
static int32_t inline_measure(L *l, box *b)
{
	RUNSTATE r;
	int line_start = 1;
	long i;
	int32_t lw = 0, best = 0;
	l->nit = 0;
	l->floats = l->ibs = 0;
	l->cw = AUTO;
	runstate(l, &r, b->st, NULL, NULL, b->link);
	collect(l, b, &r, &line_start);
	for (i = 0; (l->floats || l->ibs) && i < l->nit; i++)
		if ((l->floats && l->it[i].kind == IT_FLOAT) || l->it[i].ib) {
			/* a float in the text: beside the text on its line; an
			   inline-block: on it, as wide as its widest (or its
			   narrowest, measuring that) */
			STASH s;
			int32_t fw;
			stash(l, &s);
			fw = measure(l, s.it[i].atom);
			unstash(l, &s);
			l->it[i].w = fw;
		}
	if (l->minw && !keeps_space(b->st) && kw(b->st, EP_WHITE_SPACE) != KW_NOWRAP) {
		/* the narrowest: its widest word or picture, or run of them no
		   line may end in (white-space pre or nowrap: canbreak 2) */
		for (i = 0; i < l->nit; i++) {
			const ITEM *it = &l->it[i];
			if (it->kind == IT_BREAK || (it->kind == IT_SPACE && it->canbreak)) {
				best = lw > best ? lw : best;
				lw = 0;
			} else if (it->kind == IT_SPACE || it->canbreak == 2) {
				lw += it->w;
			} else {
				best = lw > best ? lw : best;
				lw = it->w;
			}
		}
		return lw > best ? lw : best;
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
	if (b->anon || b->tablerow || row_group(b))
		return 0;
	if (collapses(b))
		return len0(l, s, EP_MARGIN_LEFT, AUTO) + len0(l, s, EP_MARGIN_RIGHT, AUTO);
	return len0(l, s, EP_MARGIN_LEFT, AUTO) + len0(l, s, EP_MARGIN_RIGHT, AUTO) +
	       len0(l, s, EP_PADDING_LEFT, AUTO) + len0(l, s, EP_PADDING_RIGHT, AUTO) +
	       (kw(s, EP_BORDER_LEFT_STYLE) > KW_HIDDEN ? len0(l, s, EP_BORDER_LEFT_WIDTH, AUTO) : 0) +
	       (kw(s, EP_BORDER_RIGHT_STYLE) > KW_HIDDEN ? len0(l, s, EP_BORDER_RIGHT_WIDTH, AUTO) : 0);
}

/* a box positioned absolutely (or fixed): out of the flow, so no part of
   its parent's widest (CSS 2.1, 10.3.5) */
static int out_of_flow(const box *c)
{
	return !c->anon && c->kind == BX_BLOCK &&
	       (kw(c->st, EP_POSITION) == KW_ABSOLUTE || kw(c->st, EP_POSITION) == KW_FIXED);
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
			int32_t m = out_of_flow(c) ? 0 : measure(l, c);
			if (m > widest)
				widest = m;
		}
		for (k = 0; k < t->n; k++)
			inner += (t->t[k].mx.k == EC_TR_LEN ? dev(l, t->t[k].mx.v) :
				  t->t[k].mn.k == EC_TR_LEN ? dev(l, t->t[k].mn.v) : widest) + (k ? g : 0);
	} else if (b->table) {
		/* its columns at their widest, and the spacing between them */
		int32_t cols[MAXCOLS], sp = spacing(l, b->st, 0);
		int n = columns(l, b, AUTO, cols, sp), i;
		inner = sp * (n + 1);
		if (collapses(b)) {
			/* its borders in its edge cells: room for them, as
			   when they are its own */
			BEDGE e;
			bedge(l, b, 1, &e);
			inner += e.w;
			bedge(l, b, 3, &e);
			inner += e.w;
		}
		for (i = 0; i < n; i++)
			inner += cols[i];
	} else {
		int k = 0;
		int32_t run = 0;
		for (c = b->first; c != NULL; c = c->next) {
			int32_t m;
			if (out_of_flow(c))
				continue;
			m = measure(l, c);
			k++;
			if (b->row && !(b->flex && b->wrap && l->minw)) {
				inner += m;
				continue;
			}
			if (!l->minw && text_float(b, c)) {
				/* floats side by side at their widest, until one
				   clears or a block comes */
				int clr = kw(c->st, EP_CLEAR);
				run = clr == KW_LEFT || clr == KW_RIGHT || clr == KW_BOTH ? m : run + m;
				m = run;
			} else {
				run = 0;
			}
			if (m > inner)
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

/* a list item's marker (CSS Lists 3; CSS Counter Styles 3, 6 and 7), from
   its style (or the counter style fixed, a summary's of the layout's
   own) and its number n: its text, in buf when it is made; NULL for none.
   A string is the marker as it is; the disclosure triangles (a details'
   summary) point the other way in a right-to-left item */
static const char *marker_text(const ec_style *st, int fixed, int n, char *buf)
{
	int t = fixed ? fixed : kw(st, EP_LIST_STYLE_TYPE);

	if (!fixed && st->k[EP_LIST_STYLE_TYPE] == EK_PTR)
		return ((const ec_str *) st->v[EP_LIST_STYLE_TYPE].p)->s;
	switch (t) {
	case KW_NONE: return NULL;
	case KW_CIRCLE: return "\xe2\x97\xa6";	/* ◦ */
	case KW_SQUARE: return "\xe2\x96\xaa";	/* ▪ */
	case KW_DISCLOSURE_OPEN: return "\xe2\x96\xbe";	/* ▾ */
	case KW_DISCLOSURE_CLOSED:			/* ▸, or ◂ */
		return kw(st, EP_DIRECTION) == KW_RTL ? "\xe2\x97\x82" : "\xe2\x96\xb8";
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
		return buf;
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
		return buf;
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
		return buf;
	}
	default: return "\xe2\x80\xa2";		/* • */
	}
}

/* an outside marker, in the margin before the item's first line */
static void marker(L *l, box *b, int32_t x, int32_t cw, int32_t base)
{
	const ec_style *st = b->st;
	char buf[24];
	const char *s = marker_text(st, 0, b->num, buf);
	ITEM it;
	RUNSTATE r;
	int32_t w;

	if (s == NULL || *s == 0)
		return;
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

/* a table's border-spacing across (0) or down (1): none when its
   borders collapse (CSS 2.1, 17.6.2) */
static int32_t spacing(L *l, const ec_style *s, int vertical)
{
	int k = vertical ? EP_BORDER_SPACING_V : EP_BORDER_SPACING_H;
	if (kw(s, EP_BORDER_COLLAPSE) == KW_COLLAPSE || s->k[k] != EK_PX)
		return 0;
	return dev(l, s->v[k].i);
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

/* a cell over columns i.. (span of them): what it needs beyond their
   widths (and the spacing sp between them) given to them by their
   widths (CSS 2.1, 17.5.2.2) */
static void span_widths(L *l, box *c, int i, int span, int32_t w, int32_t *cols, int32_t sp)
{
	int last = i + span > MAXCOLS ? MAXCOLS : i + span, k;
	int32_t p = cell_pref(l, c, w), have = 0, more, given = 0;
	for (k = i; k < last; k++)
		have += cols[k];
	more = p - have - (last - i - 1) * sp;
	if (more <= 0)
		return;
	for (k = i; k < last; k++) {
		int32_t add = have > 0 ? (int32_t) ((int64_t) more * cols[k] / have) : more / (last - i);
		cols[k] += add;
		given += add;
	}
	cols[last - 1] += more - given;
}

/* the cells over several columns met in pass 0 (columns), for pass 1 */
#define SPANS 64
typedef struct {
	int n, over;			/* over: more than SPANS, pass 1 walks the rows */
	struct {
		box *c;
		int16_t i, span;
	} e[SPANS];
} SPANNED;

/* a row's cells' widths into their columns; occ: per column, the rows a
   cell from a row above still takes (as row_layout skips them). Pass 0
   takes the cells of one column and notes the others in sd; pass 1
   (only when there were more than SPANS) takes those over several
   (span_widths) */
static void col_row(L *l, box *r, int32_t w, int32_t *cols, int *n, int *occ, int pass, int32_t sp,
		    SPANNED *sd, int coll)
{
	box *c;
	int i = 0, k, taken[MAXCOLS];
	int32_t prev_r = 0;		/* where borders collapse, the right border of the cell before */
	for (k = 0; k < MAXCOLS; k++)
		taken[k] = occ[k] > 0;
	for (c = r->first; c != NULL && i < MAXCOLS; c = c->next) {
		int span = colspan(c), rs = rowspan(c);
		while (i < MAXCOLS && taken[i])
			i++;
		if (i >= MAXCOLS)
			break;
		if (span == 1 && pass == 0) {
			/* (where borders collapse, its left may be the wider right
			   of the cell before: room for that) */
			int32_t p = cell_pref(l, c, w) + prev_r;
			if (p > cols[i])
				cols[i] = p;
		} else if (span > 1 && pass == 0) {
			if (sd->n < SPANS) {
				sd->e[sd->n].c = c;
				sd->e[sd->n].i = (int16_t) i;
				sd->e[sd->n++].span = (int16_t) span;
			} else {
				sd->over = 1;
			}
		} else if (span > 1 && pass == 1) {
			span_widths(l, c, i, span, w, cols, sp);
		}
		if (coll && pass == 0) {
			BEDGE e;
			bedge(l, c, 1, &e);
			prev_r = e.w;
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

/* a table's columns at their widest, sp apart; returns their number */
static int columns(L *l, box *t, int32_t w, int32_t *cols, int32_t sp)
{
	box *c, *r;
	int n = 0, occ[MAXCOLS], pass, k, coll = collapses(t);
	SPANNED sd;
	memset(cols, 0, MAXCOLS * sizeof *cols);
	sd.n = sd.over = 0;
	for (pass = 0; pass < 2; pass++) {
		if (pass == 1 && !sd.over) {
			/* the cells over several columns, as noted */
			for (k = 0; k < sd.n; k++)
				span_widths(l, sd.e[k].c, sd.e[k].i, sd.e[k].span, w, cols, sp);
			break;
		}
		memset(occ, 0, sizeof occ);
		for (c = t->first; c != NULL; c = c->next) {
			if (c->tablerow)
				col_row(l, c, w, cols, &n, occ, pass, sp, &sd, coll);
			else if (!c->row)
				for (r = c->first; r != NULL; r = r->next)
					if (r->tablerow)
						col_row(l, r, w, cols, &n, occ, pass, sp, &sd, coll);
		}
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
	if (total > w && tmin <= w) {
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
	if (bd[4] >= 0)
		it[bd[4]].h += dh;	/* its clip (DL_CLIP) */
}

/* a cell's content (its items from k0 to k1, but its background and
   borders) moved down dy: vertical-align middle or bottom */
static void cell_shift(L *l, long k0, long k1, long bg, const long *bd, int32_t dy)
{
	long k;
	int j;
	if (dy <= 0)
		return;
	if (l->last_base != AUTO && l->last_k >= k0 && l->last_k < k1)
		l->last_base += dy;
	for (k = k0; k < k1; k++) {
		if (k == bg || (bg >= 0 && k == bg + 1 && l->p->items[bg].type == DL_FILL &&
				(l->p->items[bg].flags & DLF_TILE)))
			continue;
		for (j = 0; j < NBD && bd[j] != k; j++)
			;
		if (j == NBD)
			l->p->items[k].y += dy;
	}
}

static int rowspan(const box *c)
{
	const char *v = c->n != NULL ? dn_attr(c->n, "rowspan") : NULL;
	int n = v != NULL ? atoi(v) : 1;
	return n < 1 ? 1 : n > 1000 ? 1000 : n;
}

/* ---------------------------------------------------------------- collapsed borders */

/* a row group (thead, tbody, tfoot) */
static int row_group(const box *g)
{
	int d;
	if (g == NULL || g->anon || g->st == NULL)
		return 0;
	d = kw(g->st, EP_DISPLAY);
	return d == KW_TABLE_ROW_GROUP || d == KW_TABLE_HEADER_GROUP || d == KW_TABLE_FOOTER_GROUP;
}

/* a table whose borders collapse into its cells (CSS 2.1, 17.6.2): one
   that asks, with rows of its own (in it, or in its row groups); one
   without keeps its own borders */
static int collapses(const box *t)
{
	const box *c, *r;
	if (!t->table || t->anon || kw(t->st, EP_BORDER_COLLAPSE) != KW_COLLAPSE)
		return 0;
	for (c = t->first; c != NULL; c = c->next) {
		if (c->tablerow)
			return 1;
		if (row_group(c) || c->anon)
			for (r = c->first; r != NULL; r = r->next)
				if (r->tablerow)
					return 1;
	}
	return 0;
}

/* a box's border on a side (0 top, 1 right, 2 bottom, 3 left), for
   collapsing; none for an anonymous box */
static void bedge(L *l, const box *b, int side, BEDGE *e)
{
	static const int st[4] = { EP_BORDER_TOP_STYLE, EP_BORDER_RIGHT_STYLE, EP_BORDER_BOTTOM_STYLE,
				   EP_BORDER_LEFT_STYLE };
	static const int wd[4] = { EP_BORDER_TOP_WIDTH, EP_BORDER_RIGHT_WIDTH, EP_BORDER_BOTTOM_WIDTH,
				   EP_BORDER_LEFT_WIDTH };
	static const int co[4] = { EP_BORDER_TOP_COLOR, EP_BORDER_RIGHT_COLOR, EP_BORDER_BOTTOM_COLOR,
				   EP_BORDER_LEFT_COLOR };
	e->w = 0;
	e->c = 0;
	e->st = KW_NONE;
	if (b == NULL || b->anon || b->st == NULL)
		return;
	e->st = kw(b->st, st[side]);
	if (e->st > KW_HIDDEN) {
		e->w = len0(l, b->st, wd[side], AUTO);
		if (e->w == 0 && b->st->v[wd[side]].i > 0)
			e->w = 1;	/* a thin one: a pixel, not nothing */
		e->c = color(b->st, co[side]);
	}
}

static int brank(int st)
{
	switch (st) {
	case KW_DOUBLE: return 8;
	case KW_SOLID: return 7;
	case KW_DASHED: return 6;
	case KW_DOTTED: return 5;
	case KW_RIDGE: return 4;
	case KW_OUTSET: return 3;
	case KW_GROOVE: return 2;
	case KW_INSET: return 1;
	default: return 0;
	}
}

/* *a becomes the one of two borders on an edge that wins (CSS 2.1,
   17.6.2.1): hidden, else the wider, else by its style; *a, given first
   (a cell before its row, the cell further left or up), on a tie */
static void bwin(BEDGE *a, const BEDGE *b)
{
	if (a->st == KW_HIDDEN)
		return;
	if (b->st == KW_HIDDEN || b->w > a->w || (b->w == a->w && brank(b->st) > brank(a->st)))
		*a = *b;
}

/* the table's rows after row r (in its group, then in the groups
   after), counted up to max */
static int rows_after(const box *r, int max)
{
	const box *k, *g;
	int n = 0;
	for (k = r->next; k != NULL && n < max; k = k->next)
		if (k->tablerow)
			n++;
	g = r->parent;
	if (g == NULL || g->table)
		return n;
	for (g = g->next; g != NULL && n < max; g = g->next) {
		if (g->tablerow)
			n++;
		else if (!g->row)
			for (k = g->first; k != NULL && n < max; k = k->next)
				if (k->tablerow)
					n++;
	}
	return n;
}

/* the borders of a row's cells where the table's collapse: each edge
   drawn once, by the cell after it (its top and left), its winner of the
   borders that meet there (the cells', the row's, the group's, the
   table's); the cells on the table's right and at its foot draw those
   too. ce: per cell, 4 edges (top, right, bottom, left) */
static void collapse_row(L *l, box *row, box **kids, const int *start, int n, const int *above,
			 const int32_t *colx, int32_t cy, BEDGE (*ce)[4], int *cover)
{
	struct coll *t = l->coll;
	const box *g = row->parent;
	BEDGE re[4], ge[4], none = { 0, 0, KW_NONE }, e, own[4];
	int grp = row_group(g), gfirst = 0, glast = 1, i, k, maxrs = 1, after;
	const box *q;
	for (k = 0; k < 4; k++) {
		bedge(l, row, k, &re[k]);
		bedge(l, grp ? g : NULL, k, &ge[k]);
	}
	if (grp) {
		for (q = g->first; q != NULL && !q->tablerow; q = q->next)
			;
		gfirst = q == row;
		for (q = row->next; q != NULL && !q->tablerow; q = q->next)
			;
		glast = q == NULL;
	}
	memset(cover, 0, MAXCOLS * sizeof *cover);
	for (i = 0; i < n; i++) {
		int r = rowspan(kids[i]);
		if (r > maxrs)
			maxrs = r;
		for (k = start[i]; k < start[i] + colspan(kids[i]) && k < l->ncols; k++)
			cover[k] = 1;
	}
	after = rows_after(row, maxrs);
	t->first = t->rows == 0;
	t->last = after == 0;
	for (k = 0; k < l->ncols; k++) {
		if (above[k])
			continue;
		if (!cover[k] && t->rows > 0 && t->bot[k].w > 0)
			/* no cell here: the line under the one above drawn all the same */
			fill(l, colx[k], cy, colx[k + 1] - colx[k], t->bot[k].w, fade(t->bot[k].c, l->alpha));
		if (!cover[k])
			t->bot[k] = none;
		t->right[k] = none;
	}
	for (i = 0; i < n; i++) {
		int c0 = start[i], c1 = c0 + colspan(kids[i]), rs = rowspan(kids[i]), end = after < rs;
		if (c0 >= l->ncols) {
			memset(ce[i], 0, sizeof ce[i]);
			continue;
		}
		if (c1 > l->ncols)
			c1 = l->ncols;
		for (k = 0; k < 4; k++)
			if (kids[i]->anon || kw(kids[i]->st, EP_VISIBILITY) == KW_VISIBLE)
				bedge(l, kids[i], k, &own[k]);
			else
				own[k] = none;	/* a hidden cell's own aren't shown (collapse_gaps) */
		/* its top: the bottom of the cells above it, or the table's */
		if (t->rows == 0) {
			e = own[0];
		} else {
			e = t->bot[c0];
			for (k = c0 + 1; k < c1; k++)
				bwin(&e, &t->bot[k]);
			bwin(&e, &own[0]);
		}
		bwin(&e, &re[0]);
		if (gfirst)
			bwin(&e, &ge[0]);
		if (t->rows == 0)
			bwin(&e, &t->t[0]);
		ce[i][0] = e;
		/* its left: the right of the cell beside it, or the table's */
		if (c0 == 0) {
			e = own[3];
			bwin(&e, &re[3]);
			bwin(&e, &ge[3]);
			bwin(&e, &t->t[3]);
		} else {
			e = t->right[c0 - 1];
			bwin(&e, &own[3]);
		}
		ce[i][3] = e;
		/* its right: the cell beside draws it, if there is one */
		if (c1 >= l->ncols) {
			e = own[1];
			bwin(&e, &re[1]);
			bwin(&e, &ge[1]);
			bwin(&e, &t->t[1]);
			ce[i][1] = e;
		} else {
			ce[i][1] = above[c1] || (i + 1 < n && start[i + 1] == c1) ? none : own[1];
		}
		t->right[c1 - 1] = own[1];
		/* its bottom: the cell below draws it, unless the table ends here */
		e = own[2];
		if (rs == 1) {
			bwin(&e, &re[2]);
			if (glast)
				bwin(&e, &ge[2]);
		}
		if (end)
			bwin(&e, &t->t[2]);
		ce[i][2] = end ? e : none;
		for (k = c0; k < c1; k++)
			t->bot[k] = end ? none : e;
	}
	t->rows++;
}

/* a row's edges collapse_row couldn't give a cell to draw, once its
   height (lineh) is known: the table's own borders beside a place with no
   cell, and the edges a hidden cell owns (its neighbours' borders, which
   show; the cell itself draws nothing) */
static void collapse_gaps(L *l, box **kids, const int *start, int n, const int *above, const int *cover,
			  const int32_t *colx, int32_t cy, int32_t lineh, BEDGE (*ce)[4])
{
	struct coll *t = l->coll;
	int i, k, nc = l->ncols;
	for (k = 0; k < nc; k++) {
		if (above[k] || cover[k])
			continue;
		if (t->first)
			fill(l, colx[k], cy, colx[k + 1] - colx[k], t->t[0].w, fade(t->t[0].c, l->alpha));
		if (t->last)
			fill(l, colx[k], cy + lineh - t->t[2].w, colx[k + 1] - colx[k], t->t[2].w,
			     fade(t->t[2].c, l->alpha));
		if (k == 0)
			fill(l, colx[0], cy, t->t[3].w, lineh, fade(t->t[3].c, l->alpha));
		if (k == nc - 1)
			fill(l, colx[nc] - t->t[1].w, cy, t->t[1].w, lineh, fade(t->t[1].c, l->alpha));
	}
	for (i = 0; i < n; i++) {
		int c0 = start[i], c1;
		int32_t x0, x1;
		if (c0 >= nc || kids[i]->anon || kw(kids[i]->st, EP_VISIBILITY) == KW_VISIBLE)
			continue;
		c1 = c0 + colspan(kids[i]);
		if (c1 > nc)
			c1 = nc;
		x0 = colx[c0];
		x1 = colx[c1];
		fill(l, x0, cy, x1 - x0, ce[i][0].w, fade(ce[i][0].c, l->alpha));
		if (rowspan(kids[i]) > 1)
			continue;	/* (its height not known yet: its top only) */
		fill(l, x0, cy + lineh - ce[i][2].w, x1 - x0, ce[i][2].w, fade(ce[i][2].c, l->alpha));
		fill(l, x0, cy, ce[i][3].w, lineh, fade(ce[i][3].c, l->alpha));
		fill(l, x1 - ce[i][1].w, cy, ce[i][1].w, lineh, fade(ce[i][1].c, l->alpha));
	}
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
	long *k0 = NULL, *k1 = NULL, *kbg = NULL, (*kbd)[NBD] = NULL;
	int32_t *hh = NULL, *fb = NULL, rbase = AUTO;
	int fi = -1;
	BEDGE (*ce)[4] = NULL;		/* its cells' borders where they collapse */
	int cover[MAXCOLS];		/* the columns its cells take */

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
	hh = malloc(2 * n * sizeof *hh);
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
	fb = hh + n;		/* each cell's first baseline, from the row's top */
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
		if (l->coll != NULL) {
			ce = malloc(n * sizeof *ce);
			if (ce == NULL)
				l->fail = 1;
			else
				collapse_row(l, b, kids, start, n, above, colx, cy, ce, cover);
		}
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
		if (ce != NULL) {
			l->cbd = ce[i];
			l->cbd_box = kids[i];
		}
		ch = layout_block(l, kids[i], cx, cy, pw, 1, &mb, &f);
		l->cbd = NULL;
		k1[i] = l->p->nitems;
		kbg[i] = l->last_bg;
		memcpy(kbd[i], l->last_bd, sizeof kbd[i]);
		hh[i] = ch;
		fb[i] = AUTO;
		if (*first == AUTO && f != AUTO) {
			*first = f;
			fi = i;
		}
		/* a cell taking rows below doesn't set this row's height */
		if (ch > lineh && !(cols != NULL && rowspan(kids[i]) > 1 && l->spans != NULL))
			lineh = ch;
		if (b->tablerow && f != AUTO && !kids[i]->anon && kw(kids[i]->st, EP_VERTICAL_ALIGN) == KW_BASELINE &&
		    !(cols != NULL && rowspan(kids[i]) > 1 && l->spans != NULL)) {
			/* vertical-align: baseline (valign=baseline): its first
			   line's baseline on the row's (CSS 2.1, 17.5.3; one
			   taking rows below stays at its top) */
			fb[i] = f - cy;
			if (rbase == AUTO || fb[i] > rbase)
				rbase = fb[i];
		}
		cx += pw + (cols != NULL ? l->sp_h : 0);
	}
	if (rbase != AUTO) {
		/* the baseline cells' content down to the row's baseline, the
		   row as tall as that needs */
		for (i = 0; i < n; i++) {
			int32_t dy = fb[i] != AUTO ? rbase - fb[i] : 0;
			if (dy <= 0)
				continue;
			cell_shift(l, k0[i], k1[i], kbg[i], kbd[i], dy);
			if (hh[i] + dy > lineh)
				lineh = hh[i] + dy;
			if (i == fi)
				*first += dy;
		}
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
	if (ce != NULL)
		collapse_gaps(l, kids, start, n, above, cover, colx, cy, lineh, ce);
	h = cy + lineh - y;
	free(ce);
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

/* a float's width in a block w wide: the width it asks for, or its
   widest within w */
static int32_t float_width(L *l, box *c, int32_t w)
{
	int32_t wf = cell_pref(l, c, w);
	if (wf > w)
		wf = w;
	return wf < 1 ? 1 : wf;
}

/* a float of the block being laid out (its content x..x+w), its top no
   higher than y: laid out at its width (wf, or AUTO: float_width's),
   then put as far left or right as it goes where it fits beside the
   floats already there, else below them */
static void place_float(L *l, box *c, int32_t x, int32_t w, int32_t y, int32_t wf)
{
	int right = kw(c->st, EP_FLOAT) == KW_RIGHT, clr = kw(c->st, EP_CLEAR), tries;
	int32_t hf, mb, f = AUTO, fy, lx = x, rx = x + w, nb, lb = l->last_base;
	long k0 = l->p->nitems, b0 = nblocks(l), f0 = l->nlater, f1, lk = l->last_k;
	FLT *fl;
	if (wf == AUTO)
		wf = float_width(l, c, w);
	l->mov++;
	hf = layout_block(l, c, 0, 0, wf, 1, &mb, &f);
	l->mov--;
	f1 = l->nlater;
	l->last_base = lb;	/* (not in the flow: not an inline-block's baseline) */
	l->last_k = lk;
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
	shift_blocks(l, b0, right ? rx - wf : lx, fy);
	if (c->n != NULL)
		c->n->y += fy;
	place_later(l, f0, f1, right ? rx - wf : lx, fy);
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
	int32_t left, right, top, wf, mb, f = AUTO, ax, ay, lb = l->last_base;
	long k0 = l->p->nitems, b0 = nblocks(l), f0, f1, lk = l->last_k, c0 = l->p->nclips, y0 = l->nlay;
	int s0 = l->p->nscr;
	uint16_t ovf;
	if ((fixed ? 0 : l->cbmov) < l->mov) {
		/* in a box laid out at (0, 0) to be moved, its containing block
		   outside it: placed once that box has moved (place_later) */
		if (l->nlater == l->latercap) {
			long cap = l->latercap ? l->latercap * 2 : 8;
			struct later *n = realloc(l->later, cap * sizeof *n);
			if (n == NULL) {
				l->fail = 1;
				return;
			}
			l->later = n;
			l->latercap = cap;
		}
		l->later[l->nlater].c = c;
		l->later[l->nlater].sx = sx;
		l->later[l->nlater].sy = sy;
		l->later[l->nlater].alpha = l->alpha;
		l->later[l->nlater].clip = l->clip;
		l->later[l->nlater++].ctl = l->ctl_in;
		return;
	}
	left = len(l, c->st, EP_LEFT, cbw);
	right = len(l, c->st, EP_RIGHT, cbw);
	top = len(l, c->st, EP_TOP, AUTO);
	if (len(l, c->st, EP_WIDTH, cbw) == AUTO && left != AUTO && right != AUTO)
		wf = cbw - left - right;	/* stretched between its sides */
	else
		wf = cell_pref(l, c, cbw);
	if (wf > cbw)
		wf = cbw;
	if (wf < 1)
		wf = 1;
	f0 = l->nlater;
	ovf = l->ovf;
	l->ovf = fixed ? 0 : l->cbovf;	/* within its containing block's clips, no others */
	l->mov++;
	layout_block(l, c, 0, 0, wf, 1, &mb, &f);
	l->mov--;
	l->ovf = ovf;
	f1 = l->nlater;
	l->last_base = lb;
	l->last_k = lk;
	if (l->fail)
		return;
	ax = left != AUTO ? cbx + left : right != AUTO ? cbx + cbw - right - wf : sx;
	ay = top != AUTO ? cby + top : sy;
	{
		/* text for screen readers only: a box of a pixel that hides
		   what overflows it, or put far off the page: nothing to see,
		   so it goes (and the clips it opened: none of their items is
		   left) */
		int32_t cw = len(l, c->st, EP_WIDTH, cbw), chh = len(l, c->st, EP_HEIGHT, AUTO);
		int hides = kw(c->st, EP_OVERFLOW_X) != KW_VISIBLE || kw(c->st, EP_OVERFLOW_Y) != KW_VISIBLE;
		if ((hides && ((cw != AUTO && cw <= 2) || (chh != AUTO && chh <= 2))) || ax + wf <= 0 || ay < -1000) {
			l->p->nitems = k0;
			l->p->nclips = c0;
			l->p->nscr = s0;
			l->nlay = y0;
#ifndef EW_ST
			l->p->nblocks = b0;
#endif
			place_later(l, f0, f1, AUTO, AUTO);	/* (what is in it goes too) */
			return;
		}
	}
	shift_items(l, k0, ax, ay);
	shift_blocks(l, b0, ax, ay);
	if (c->n != NULL)
		c->n->y += ay;
	place_later(l, f0, f1, ax, ay);
}

/* the absolutely positioned boxes place_abs left (f0..f1), now that the
   box they are in has moved by (dx, dy): placed where they go, from
   where they would have been (or left again, for a box round that one
   that is still to move); dx AUTO: that box went, and they with it */
static void place_later(L *l, long f0, long f1, int32_t dx, int32_t dy)
{
	long k;
	unsigned alpha = l->alpha;
	uint32_t clip = l->clip;
	uint16_t ctl = l->ctl_in;
	for (k = f0; k < f1 && k < l->nlater; k++) {
		struct later e = l->later[k];
		if (e.c == NULL)
			continue;
		l->later[k].c = NULL;
		if (dx != AUTO) {
			l->alpha = e.alpha;
			l->clip = e.clip;
			l->ctl_in = e.ctl;
			place_abs(l, e.c, e.sx + dx, e.sy + dy);
		}
	}
	l->alpha = alpha;
	l->clip = clip;
	l->ctl_in = ctl;
	/* the placed ones let go, not below f0: a box placed from a range
	   round this one may still have its own above it */
	while (l->nlater > f0 && l->later[l->nlater - 1].c == NULL)
		l->nlater--;
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
	long k0, k1, bg, bd[NBD];
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
	if (l->last_base != AUTO && l->last_k >= k0 && l->last_k < k1)
		l->last_base += dy;	/* an inline-block's last line with them */
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
	long k0, k1, bg, bd[NBD];
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

/* of the room a block in the flow leaves beside it, what goes to its
   left margin (CSS 2.1, 10.3.3): its auto margins share it; with none,
   the end margin takes it, by its container's direction, unless the
   container's text-align is one of HTML's legacy values (<center>, and
   align on div, p and a table's parts), which centre the block or put it
   left or right (the HTML Standard, 15.3.3: "align descendants"; the
   nearest such container wins, as text-align inherits) */
static int32_t room_left(const box *b, int32_t room, int auto_l, int auto_r)
{
	const ec_style *cs;
	if (auto_l)
		return auto_r ? room / 2 : room;
	if (auto_r || b->parent == NULL)
		return 0;
	cs = b->parent->st;
	switch (kw(cs, EP_TEXT_ALIGN)) {
	case KW_WEBKIT_CENTER:
		return room / 2;
	case KW_WEBKIT_LEFT:
		return 0;
	case KW_WEBKIT_RIGHT:
		return room;
	}
	return kw(cs, EP_DIRECTION) == KW_RTL ? room : 0;
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
	int saved_cbmov = l->cbmov, saved_fl0 = l->fl0, own = own_context(b), pos = b->anon ? KW_STATIC : kw(s, EP_POSITION);
	int vis = b->anon || kw(s, EP_VISIBILITY) == KW_VISIBLE;	/* its background and borders drawn */
	uint16_t saved_ctl = l->ctl_in;
	struct coll *coll = NULL, *saved_coll = l->coll;
	uint16_t saved_ovf = l->ovf, saved_cbovf = l->cbovf;
	long ck = -1;			/* its DL_CLIP item, when it hides what overflows it */
	uint16_t ckn = 0;		/* (its number: dl_item.clip of what is in it) */
	int clip_x = 0, clip_y = 0;
	int ox = KW_VISIBLE, oy = KW_VISIBLE;	/* its overflow-x and -y (not an anonymous box's) */
	BEDGE te[4];			/* a table's own borders, when they collapse */
	const BEDGE *ov = NULL;		/* a cell's, as they collapse */
	int collapsed = 0;		/* a table whose borders collapse */
	long bd[NBD] = { -1, -1, -1, -1, -1 }, kstart = l->p->nitems, bstart = nblocks(l);
	struct span *saved_spans = l->spans;
	unsigned saved_alpha = l->alpha;
	uint32_t saved_clip = l->clip;
	int32_t bg_top = 0, bg_bottom = 0;	/* the background colour's fill short of the border box */
	int32_t in_l = 0, in_r = 0;
	int ctlbox = 0;
	long lay = -1;			/* positioned: its l->lay */

	if (stopped(l))
		return 0;
	if (EC_DEEP()) {
		l->p->deep = 1;		/* the stack's floor: this box and what is in it left out */
		*mb_out = 0;
		*first = AUTO;
		return 0;
	}
	if (pos != KW_STATIC)
		lay = lay_open(l, s);
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
		if (b->n != NULL && b->n->ctl != NULL && !b->repl) {
			/* a floated form control's block (control): the control
			   draws its own frame and ground, as on a line */
			pl = pr = pt = pb = bl = br = bt = bb = 0;
			ctlbox = 1;
		}
		if (b->tablerow || row_group(b)) {
			/* rows and their groups have no borders, padding or
			   margins (CSS 2.1, 17.6.1; 8.3, 8.4); where borders
			   collapse, theirs are the cells' (collapse_row) */
			ml = mr = mt = mb = pl = pr = pt = pb = bl = br = bt = bb = 0;
		} else if (collapses(b)) {
			/* its borders the cells' at its edges, and no padding
			   (CSS 2.1, 17.6.2) */
			int k;
			for (k = 0; k < 4; k++)
				bedge(l, b, k, &te[k]);
			pl = pr = pt = pb = bl = br = bt = bb = 0;
			collapsed = 1;
		} else if (l->cbd != NULL && l->cbd_box == b) {
			ov = l->cbd;
			bt = ov[0].w;
			br = ov[1].w;
			bb = ov[2].w;
			bl = ov[3].w;
		}
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
	}
	if (b->repl && wauto && !fixed_w) {
		/* a picture: its own width, not its container's */
		int32_t iw, ih;
		l->cw = cbw;
		image_size(l, b->first, &iw, &ih);
		if (iw > 0 && iw < w)
			w = iw;
	}
	if (b->table) {
		/* its columns at their widest; a table without a width shrinks to them */
		int32_t *cols = amalloc(l->a, MAXCOLS * sizeof *cols), mins[MAXCOLS], total, sp;
		int n, i, was = l->minw;
		if (cols == NULL) {
			l->fail = 1;
			return 0;
		}
		table_sh = spacing(l, s, 0);
		table_sv = spacing(l, s, 1);
		l->minw = 1;
		columns(l, b, w, mins, table_sh);
		l->minw = was;
		n = columns(l, b, w, cols, table_sh);
		if (collapsed && n > 0) {
			/* its borders in its edge cells: room for them, as when
			   they are its own */
			cols[0] += te[3].w;
			mins[0] += te[3].w;
			cols[n - 1] += te[1].w;
			mins[n - 1] += te[1].w;
		}
		sp = table_sh * (n + 1);
		total = sp;
		for (i = 0; i < n; i++)
			total += cols[i];
		if (wauto && !fixed_w && total < w)
			w = total;
		{
			/* no narrower than its columns' narrowest (CSS 2.1,
			   17.5.2: a word, a picture, a run of white-space nowrap
			   or pre); wider than the room, the page scrolls sideways */
			int32_t tmin = sp;
			for (i = 0; i < n; i++)
				tmin += mins[i];
			if (w < tmin)
				w = tmin;
		}
		fit_columns(cols, mins, n, w - sp > n ? w - sp : n, !wauto || fixed_w);
		table_cols = cols;
		table_n = n;
	}
	if (!fixed_w && !b->anon && (!wauto || b->table || b->repl)) {
		/* narrower than its container (an auto width fills it): where
		   the room goes */
		int32_t room = cbw - ml - mr - bl - br - pl - pr - w;
		if (room > 0)
			ml += room_left(b, room, auto_l, auto_r);
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
		l->cbmov = l->mov;
	}
	if (b->table) {
		l->spans = calloc(MAXCOLS, sizeof *l->spans);
		if (collapsed && l->spans != NULL) {
			coll = calloc(1, sizeof *coll);
			if (coll == NULL) {
				free(l->spans);
				l->spans = NULL;
			} else {
				memcpy(coll->t, te, sizeof te);
			}
		}
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
		} else if (!ctlbox && vis) {
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
	if (b->n != NULL && !b->anon && b->n->tag == T_LABEL && b->n->ns == DN_NS_HTML)
		l->ctl_in = label_for(l, b->n);	/* a click on its text is one on its control */
	if (!b->anon) {
		/* its overflow (CSS Overflow 3): the root's and the body's are
		   the window's, and rows and their groups have none */
		ox = kw(s, EP_OVERFLOW_X);
		oy = kw(s, EP_OVERFLOW_Y);
		if ((ox != KW_VISIBLE || oy != KW_VISIBLE) &&
		    (b->tablerow || row_group(b) || b->n == l->p->d->html || b->n == l->p->d->body))
			ox = oy = KW_VISIBLE;
		else if (ox == KW_VISIBLE && oy != KW_VISIBLE && oy != KW_CLIP)
			ox = KW_AUTO;	/* (visible beside what isn't visible or clip is auto) */
		else if (oy == KW_VISIBLE && ox != KW_VISIBLE && ox != KW_CLIP)
			oy = KW_AUTO;
	}
	if ((ox != KW_VISIBLE || oy != KW_VISIBLE) && !ctlbox && !b->repl) {
		/* what is in it drawn and clicked within its padding box on the
		   axes that aren't visible; auto and scroll scroll there when
		   there is more than it shows (scroller) */
		clip_x = ox != KW_VISIBLE;
		clip_y = oy != KW_VISIBLE;
		ck = clip_open(l);
		ckn = l->ovf;
	}
	if (pos != KW_STATIC && pos != KW_STICKY)
		l->cbovf = l->ovf;	/* what is positioned in it: within its clips */
	if (b->table) {
		l->cols = table_cols;
		l->ncols = table_n;
		l->sp_h = table_sh;
		l->sp_v = table_sv;
		l->coll = coll;
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
					if (k->list == LB_ITEM)
						cnt++;
				num = st ? atoi(st) + 1 : cnt + 1;
			}
		}
		for (c = b->first; c != NULL && !l->fail; c = c->next) {
			int32_t cmt = c->anon ? 0 : len0(l, c->st, EP_MARGIN_TOP, w), cmb = 0, cf = AUTO, h, top;
			if (c->list == LB_ITEM) {
				/* (not a summary: the HTML Standard's counter-increment:
				   list-item 0, 15.5.4) */
				const char *v = c->n ? dn_attr(c->n, "value") : NULL;
				num = v ? atoi(v) : (reversed ? num - 1 : num + 1);
				c->num = num;
			}
			if (!c->anon && kw(c->st, EP_FLOAT) != KW_NONE) {
				/* out of the flow: beside what follows */
				place_float(l, c, cx, w, edge + (prev_mb > 0 ? prev_mb : 0), AUTO);
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
	if ((b->list & LB_ITEM) && kw(s, EP_LIST_STYLE_POSITION) != KW_INSIDE)
		marker(l, b, cx, w, fb != AUTO ? fb : cy + font_of(l, s)->ascent);
	if (!b->anon) {
		/* (its content taller than its height makes it taller, but
		   where overflow-y isn't visible: there it keeps its height,
		   and what is in it is cut or scrolls) */
		int keep = clip_y, sc = ck >= 0 && vis && (scrolls(ox) || scrolls(oy));
		int32_t sx1 = INT32_MIN, sy1 = INT32_MIN;	/* what is in a box that scrolls: how far it goes */
		if (sc)
			extent(l, ck, ckn, &sx1, &sy1);
		hp = len(l, s, EP_HEIGHT, AUTO);
		if (hp != AUTO) {
			if (kw(s, EP_BOX_SIZING) == KW_BORDER_BOX)
				hp -= bt + bb + pt + pb;
			if (hp > ch || (keep && hp >= 0))
				ch = hp < 0 ? 0 : hp;
		}
		mn = len(l, s, EP_MIN_HEIGHT, AUTO);
		if (mn != AUTO && ch < mn)
			ch = mn;
		mx = len(l, s, EP_MAX_HEIGHT, AUTO);
		if (mx != AUTO && ch > mx && keep)
			ch = mx;
		if (sc && scrolls(ox) && sx1 != INT32_MIN && sx1 - (x + ml + bl + pl + w) > 2 && hp == AUTO &&
		    (mx == AUTO || ch < mx))
			ch += SBAR_W;	/* (a bar across its foot, its height its own: room for it below) */
		if (bgi >= 0)
			l->p->items[bgi].h = bt + pt + ch + pb + bb - bg_top - bg_bottom;
		if (ck >= 0)
			clip_set(l, ck, clip_x, clip_y, x + ml + bl, y + mt + bt, pl + w + pr, pt + ch + pb);
		l->ovf = saved_ovf;	/* (its own background and borders not within it) */
		if (sc)
			scroller(l, b, ck, scrolls(ox), scrolls(oy), sx1 == INT32_MIN ? sx1 : sx1 + pr,
				 sy1 == INT32_MIN ? sy1 : sy1 + pb);
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
			if (vis) {
				bd[0] = fill(l, bx, by, bw, bt, fade(ov ? ov[0].c : color(s, EP_BORDER_TOP_COLOR), l->alpha));
				bd[1] = fill(l, bx, by + bh - bb, bw, bb,
					     fade(ov ? ov[2].c : color(s, EP_BORDER_BOTTOM_COLOR), l->alpha));
				bd[2] = fill(l, bx, by + bt, bl, bh - bt - bb,
					     fade(ov ? ov[3].c : color(s, EP_BORDER_LEFT_COLOR), l->alpha));
				bd[3] = fill(l, bx + bw - br, by + bt, br, bh - bt - bb,
					     fade(ov ? ov[1].c : color(s, EP_BORDER_RIGHT_COLOR), l->alpha));
			}
#ifndef EW_ST
			if (b->n != NULL && !b->anon)
				block_rect(l, b->n, bx, by, bw, bh);
#endif
		}
	}
	if (b->list & LB_SUM)
		sum_item(l, b, x + ml, y + mt, bl + pl + w + pr + br, bt + pt + ch + pb + bb);
	l->cols = saved_cols;
	l->ncols = saved_n;
	l->sp_h = saved_sh;
	l->sp_v = saved_sv;
	l->ctl_in = saved_ctl;
	if (b->table) {
		free(l->spans);
		l->spans = saved_spans;
		free(coll);
		l->coll = saved_coll;
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
		if (dx != 0 || dy != 0) {
			shift_items(l, kstart, dx, dy);
			shift_blocks(l, bstart, dx, dy);
		}
	}
	if (pos != KW_STATIC && pos != KW_STICKY) {
		l->cbmov = saved_cbmov;
		l->cbovf = saved_cbovf;
		l->cbx = saved_cbx;
		l->cby = saved_cby;
		l->cbw = saved_cbw;
	}
	l->ovf = saved_ovf;
	bd[4] = ck;
	l->last_bg = bgi >= 0 ? bgi : bgp;
	memcpy(l->last_bd, bd, sizeof bd);
	l->alpha = saved_alpha;
	l->clip = saved_clip;
	if (lay >= 0 && lay < l->nlay)
		l->lay[lay].k1 = l->p->nitems;
	*mb_out = mb;
	*first = fb;
	return mt + bt + pt + ch + pb + bb + mb;
}

/* ---------------------------------------------------------------- the page */

#define RIGHT_MAX 16384		/* the widest a page scrolls to (device px) */
#define RIGHT_SLACK 8		/* past the window by no more: not scrolled to */

/* ---------------------------------------------------------------- the order drawn in */

#define CTX_DEEP 32		/* stacking contexts in each other: deeper, each drawn whole */

typedef struct {
	const struct lay *y;	/* the boxes (n), and the page as one more (n: a context) */
	long *kid, *sib, *up;	/* each one's first box in it, the next beside it, the one it is in */
	long *list;		/* the boxes drawn after a context's own items (2n: room to sort) */
	struct pg_seg *seg;
	long nseg, cap, n;
	int fail;
} DO;

/* a box drawn after its context's own items (all but z-index below 0:
   those stay where they are, with all that is in them, as before) */
#define LIFTED(o, i) ((o)->y[i].z == LAY_AUTO || (o)->y[i].z >= 0)

static void do_emit(DO *o, long k0, long k1)
{
	if (k0 >= k1 || o->fail)
		return;
	if (o->nseg > 0 && o->seg[o->nseg - 1].k1 == k0) {
		o->seg[o->nseg - 1].k1 = k1;
		return;
	}
	if (o->nseg == o->cap) {
		long cap = o->cap ? o->cap * 2 : 16;
		struct pg_seg *n = realloc(o->seg, cap * sizeof *n);
		if (n == NULL) {
			o->fail = 1;
			return;
		}
		o->seg = n;
		o->cap = cap;
	}
	o->seg[o->nseg].k0 = k0;
	o->seg[o->nseg++].k1 = k1;
}

/* box m's own items: those of the boxes in it drawn later left out */
static void do_own(DO *o, long m)
{
	long k = o->y[m].k0, c;
	for (c = o->kid[m]; c >= 0; c = o->sib[c])
		if (LIFTED(o, c)) {
			do_emit(o, k, o->y[c].k0);
			k = o->y[c].k1;
		}
	do_emit(o, k, o->y[m].k1);
}

/* context m: its own items, then its positioned boxes (those in it, and
   in those with z-index auto, not those in another context): z-index
   auto or 0 in the document's order, then those above by z-index (of the
   same, in the document's order); a context's in turn */
static void do_context(DO *o, long m, long top, int depth)
{
	long c = o->kid[m], n = 0, i, j, pos = 0;
	do_own(o, m);
	while (c >= 0) {
		if (LIFTED(o, c)) {
			o->list[top + n++] = c;
			if (o->y[c].z == LAY_AUTO && o->kid[c] >= 0) {
				c = o->kid[c];
				continue;
			}
		}
		/* on to the next beside it, or beside the one it is in */
		while (c >= 0 && o->sib[c] < 0)
			c = o->up[c] == m ? -1 : o->up[c];
		if (c >= 0)
			c = o->sib[c];
	}
	/* those above 0 after the others, in their order (put aside past
	   the list, then back, sorted) */
	for (i = 0; i < n; i++)
		if (o->y[o->list[top + i]].z > 0)
			o->list[top + n + pos++] = o->list[top + i];
		else
			o->list[top + i - pos] = o->list[top + i];
	for (i = 1; i < pos; i++) {
		long v = o->list[top + n + i];
		for (j = i; j > 0 && o->y[o->list[top + n + j - 1]].z > o->y[v].z; j--)
			o->list[top + n + j] = o->list[top + n + j - 1];
		o->list[top + n + j] = v;
	}
	for (i = 0; i < pos; i++)
		o->list[top + n - pos + i] = o->list[top + n + i];
	for (i = 0; i < n; i++) {
		long x = o->list[top + i];
		if (o->y[x].z == LAY_AUTO)
			do_own(o, x);
		else if (depth < CTX_DEEP)
			do_context(o, x, top + n, depth + 1);
		else
			do_emit(o, o->y[x].k0, o->y[x].k1);
	}
}

/* the order the items are drawn in (CSS 2.1, Appendix E, as far as EWeb
   goes): positioned boxes over what isn't, by z-index (page.seg); NULL
   when that is the document's order, or there is no room for it (then
   they are drawn in it, as before) */
static void draw_order(L *l)
{
	page *p = l->p;
	long n = l->nlay, i, sp = 0, *st, *tail;
	struct lay *y;
	DO o;
	if (n == 0)
		return;
	memset(&o, 0, sizeof o);
	o.n = n;
	/* (one more: the page) */
	y = realloc(l->lay, (size_t) (n + 1) * sizeof *y);
	if (y == NULL)
		return;
	l->lay = y;
	l->laycap = n + 1;
	y[n].k0 = 0;
	y[n].k1 = p->nitems;
	y[n].z = 0;
	o.y = y;
	o.kid = malloc((size_t) (n + 1) * 5 * sizeof *o.kid);
	o.list = malloc((size_t) (2 * n + 1) * sizeof *o.list);
	if (o.kid == NULL || o.list == NULL)
		goto out;
	o.sib = o.kid + n + 1;
	o.up = o.sib + n + 1;
	st = o.up + n + 1;
	tail = st + n + 1;
	for (i = 0; i <= n; i++)
		o.kid[i] = o.sib[i] = tail[i] = -1;
	/* each in the one round it: they come in the document's order, one
	   in another after it; one not done (a page stopped) to the end */
	st[sp++] = n;
	for (i = 0; i < n; i++) {
		long k0 = y[i].k0, k1 = y[i].k1 < 0 || y[i].k1 > p->nitems ? p->nitems : y[i].k1;
		y[i].k1 = k1;
		if (k0 >= k1) {
			y[i].z = -1;	/* (no items: as if it weren't) */
			continue;
		}
		while (sp > 1 && k0 >= y[st[sp - 1]].k1)
			sp--;
		if (k1 > y[st[sp - 1]].k1)
			goto out;	/* (not one in another: never so; drawn in order) */
		o.up[i] = st[sp - 1];
		if (tail[o.up[i]] < 0)	/* (after the last in the one round it) */
			o.kid[o.up[i]] = i;
		else
			o.sib[tail[o.up[i]]] = i;
		tail[o.up[i]] = i;
		st[sp++] = i;
	}
	do_context(&o, n, 0, 0);
	if (!o.fail && (o.nseg > 1 || (o.nseg == 1 && (o.seg[0].k0 != 0 || o.seg[0].k1 != p->nitems)))) {
		long r = 0;
		for (i = 0; i < o.nseg; i++) {
			o.seg[i].r0 = r;
			r += o.seg[i].k1 - o.seg[i].k0;
		}
		if (r == p->nitems) {
			p->seg = o.seg;
			p->nseg = o.nseg;
			o.seg = NULL;
		}
	}
out:
	free(o.kid);
	free(o.list);
	free(o.seg);
}

long page_drawn(const page *p, long r)
{
	long a = 0, b;
	if (p->seg == NULL)
		return r;
	b = p->nseg - 1;
	while (a < b) {
		long m = (a + b + 1) / 2;
		if (p->seg[m].r0 <= r)
			a = m;
		else
			b = m - 1;
	}
	return p->seg[a].k0 + r - p->seg[a].r0;
}

/* the items drawn: range s of them (the order they are drawn in) */
static long nsegs(const page *p)
{
	return p->seg != NULL ? p->nseg : 1;
}

#define SEG_K0(p, s) ((p)->seg != NULL ? (p)->seg[s].k0 : 0)
#define SEG_K1(p, s) ((p)->seg != NULL ? (p)->seg[s].k1 : (p)->nitems)

/* the items by strips of the page, so that drawing a part of it looks
   only at the items there; and how far right they go (page.right: wide,
   else the page's width), the canvas's picture (cbg) aside */
static void index_items(page *p, int wide, long cbg)
{
	long i, k, n = 0, bottom = p->height;
	int32_t right = p->width;
	p->wide = wide;
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		int32_t x0, y0, x1, y1;
		if (d->type == DL_CLIP)
			continue;	/* (not drawn: in no strip) */
		if (d->y + d->h > bottom)
			bottom = d->y + d->h;
		if (d->x + d->w <= right || !wide || i == cbg || d->w <= 0)
			continue;
		/* (what a box hiding what overflows it hides: not shown) */
		x0 = d->x;
		y0 = d->y;
		x1 = d->x + d->w;
		y1 = d->y + d->h;
		if (d->clip && !page_clip(p, d->clip, &x0, &y0, &x1, &y1))
			continue;
		if (x1 > right)
			right = x1;
	}
	/* (no further than something put far off to the right; not for the
	   few pixels a width rounded up goes past the window) */
	p->right = right > RIGHT_MAX ? RIGHT_MAX : right <= p->width + RIGHT_SLACK ? p->width : right;
	p->nstrips = bottom / PAGE_STRIP + 1;
	p->strip = calloc((size_t) p->nstrips + 1, sizeof *p->strip);
	if (p->strip == NULL)
		return;
	/* how many in each strip, then where each strip's start */
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		long s0 = d->y < 0 ? 0 : d->y / PAGE_STRIP, s1 = d->y + d->h <= 0 ? 0 : (d->y + d->h - 1) / PAGE_STRIP;
		if (d->type == DL_CLIP)
			continue;
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
		long s, r = 0;
		memcpy(at, p->strip, (size_t) p->nstrips * sizeof *at);
		for (s = 0; s < nsegs(p); s++)
			for (i = SEG_K0(p, s); i < SEG_K1(p, s); i++, r++) {
				const dl_item *d = &p->items[i];
				long s0 = d->y < 0 ? 0 : d->y / PAGE_STRIP;
				long s1 = d->y + d->h <= 0 ? 0 : (d->y + d->h - 1) / PAGE_STRIP;
				if (d->type == DL_CLIP)
					continue;
				for (k = s0; k <= s1; k++)
					p->sidx[at[k]++] = r;
			}
		free(at);
	}
}

/* the root or the body hides what overflows it sideways (overflow-x
   hidden or clip): the window doesn't scroll that way */
static int hides_x(const dnode *n)
{
	int o;
	if (n == NULL || n->e.style == NULL)
		return 0;
	o = kw(n->e.style, EP_OVERFLOW_X);
	return o == KW_HIDDEN || o == KW_CLIP;
}

static unsigned long page_serial;

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
	p->serial = ++page_serial;
	p->mem = a;
	p->opt = *o;
	p->width = o->width;
	p->bg = 0xffffff;
	p->cbg = -1;
	l.alpha = 255;
	l.last_base = AUTO;
	l.zoom = o->zoom > 0 ? o->zoom : 1024;
	l.text = o->text > 0 ? o->text : 1024;
	l.cbw = o->width;		/* the page: the containing block of what is absolute in nothing positioned */
	l.last_bg = -1;
	l.visual = strcmp(d->charset, "ISO-8859-8") == 0;
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
	if (!l.fail)
		draw_order(&l);
	free(l.it);
	free(l.ibx);
	free(l.later);
	free(l.fl);
	free(l.lay);
	if (l.fail) {
		page_free(p);
		return NULL;
	}
	scrollers_place(p);
	index_items(p, !hides_x(html) && !hides_x(d->body), cbg);
	if (o->was != NULL && o->was->d == d) {
		/* the boxes scrolled on the page before: as far again */
		int i, j;
		for (i = 0; i < o->was->nscr; i++) {
			const struct pg_scroll *w = &o->was->scr[i];
			int32_t x, y, ww, hh;
			if ((w->sx == 0 && w->sy == 0) || w->n == NULL)
				continue;	/* (a ::before's or ::after's: no element to know it by) */
			for (j = 0; j < p->nscr; j++)
				if (p->scr[j].n == w->n) {
					page_scroll(p, j, w->sx, w->sy, &x, &y, &ww, &hh);
					break;
				}
		}
	}
	p->opt.was = NULL;
	return p;
}

void page_free(page *p)
{
	if (p == NULL)
		return;
	free(p->items);
	free(p->clips);
	free(p->scr);
	free(p->links);
	free(p->ctls);
#ifndef EW_ST
	free(p->blocks);
#endif
	free(p->strip);
	free(p->sidx);
	free(p->seg);
	if (p->mem != NULL) {
		afree(p->mem);
		free(p->mem);
	}
	free(p);
}

#ifndef EW_ST
/* a is n or one of its ancestors */
static int holds(const dnode *a, const dnode *n)
{
	for (; n != NULL; n = n->parent)
		if (n == a)
			return 1;
	return 0;
}

dnode *page_node_at(const page *p, int32_t x, int32_t y)
{
	long i, s, best = -1;
	for (s = nsegs(p) - 1; s >= 0; s--)
		for (i = SEG_K1(p, s) - 1; i >= SEG_K0(p, s); i--) {
			const dl_item *d = &p->items[i];
			if (d->node != NULL && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w &&
			    y >= d->y && y < d->y + d->h && in_clips(p, d, x, y))
				return d->node;
		}
	/* the innermost block there: the smallest of those holding it; of
	   the same size a child rather than its parent (a closed details
	   as big as its summary: the summary), else the last laid out (a
	   block's rectangle is put when its children are done); not one a
	   click goes through (pointer-events: none) */
	for (i = 0; i < p->nblocks; i++) {
		const struct pg_block *b = &p->blocks[i];
		int64_t a, ba;
		if (b->n->e.style != NULL && no_hit(b->n->e.style))
			continue;
		if (x < b->x || x >= b->x + b->w || y < b->y || y >= b->y + b->h)
			continue;
		if (b->clip != 0) {
			/* (where a box that hides what overflows it shows it) */
			int32_t x0 = x, y0 = y, x1 = x + 1, y1 = y + 1;
			if (!page_clip(p, b->clip, &x0, &y0, &x1, &y1))
				continue;
		}
		a = (int64_t) b->w * b->h;
		ba = best >= 0 ? (int64_t) p->blocks[best].w * p->blocks[best].h : 0;
		if (best < 0 || a < ba || (a == ba && !holds(b->n, p->blocks[best].n)))
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

const char *page_link_download(const page *p, int link)
{
	const char *s = p->links[link];
	s += strlen(s) + 1;
	return *s ? s + 1 : NULL;
}

/* (the last item drawn there first; not one a click goes through) */
int page_link_at(const page *p, int32_t x, int32_t y)
{
	long i, s;
	for (s = nsegs(p) - 1; s >= 0; s--)
		for (i = SEG_K1(p, s) - 1; i >= SEG_K0(p, s); i--) {
			const dl_item *d = &p->items[i];
			if (d->link && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w && y >= d->y &&
			    y < d->y + d->h && in_clips(p, d, x, y))
				return d->link - 1;
		}
	return -1;
}

fctl *page_ctl_at(const page *p, int32_t x, int32_t y, const dl_item **it)
{
	long i, s;
	for (s = nsegs(p) - 1; s >= 0; s--)
		for (i = SEG_K1(p, s) - 1; i >= SEG_K0(p, s); i--) {
			const dl_item *d = &p->items[i];
			if (d->ctl && !(d->flags & DLF_NOHIT) && x >= d->x && x < d->x + d->w && y >= d->y &&
			    y < d->y + d->h && in_clips(p, d, x, y)) {
				if (it != NULL)
					*it = d;
				return p->ctls[d->ctl - 1];
			}
		}
	return NULL;
}

/* (the smallest there: an inner details' summary in an outer one's,
   whose item comes after it, as a box's comes after its children's; of
   the same size the last laid out) */
dnode *page_summary_at(const page *p, int32_t x, int32_t y)
{
	long i, s, best = -1;
	for (s = nsegs(p) - 1; s >= 0; s--)
		for (i = SEG_K1(p, s) - 1; i >= SEG_K0(p, s); i--) {
			const dl_item *d = &p->items[i];
			if (d->type == DL_SUM && x >= d->x && x < d->x + d->w && y >= d->y && y < d->y + d->h &&
			    in_clips(p, d, x, y) &&
			    (best < 0 || (int64_t) d->w * d->h < (int64_t) p->items[best].w * p->items[best].h))
				best = i;
		}
	return best >= 0 ? p->items[best].det : NULL;
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

/* in document order, without recursion: the first element of that id
   (or an a of that name) that is laid out, or that a closed details folds
   away (its y then from an earlier layout, or none) */
static dnode *anchor_in(dnode *top, const char *id)
{
	dnode *n = top;
	for (;;) {
		if (n->type == DN_ELEMENT) {
			const char *v = dn_attr(n, "id");
			if (((v != NULL && strcmp(v, id) == 0) ||
			     (n->tag == T_A && (v = dn_attr(n, "name")) != NULL && strcmp(v, id) == 0)) &&
			    (n->y >= 0 || dn_folded(n)))
				return n;
		}
		if (n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != top && n->next == NULL)
			n = n->parent;
		if (n == top)
			return NULL;
		n = n->next;
	}
}

dnode *page_anchor_node(const page *p, const char *id)
{
	if (p->d == NULL || p->d->root == NULL || id == NULL || *id == 0)
		return NULL;
	return anchor_in(p->d->root, id);
}

/* how far the boxes that scroll that n is in are scrolled down, together */
static int32_t scrolled_y(const page *p, const dnode *n)
{
	const dnode *a;
	int32_t s = 0;
	int i;
	for (a = n->parent; a != NULL && p->nscr > 0; a = a->parent)
		for (i = 0; i < p->nscr; i++)
			if (p->scr[i].n == a)
				s += p->scr[i].sy;
	return s;
}

int32_t page_anchor(page *p, const char *id)
{
	const dnode *n = page_anchor_node(p, id);
	if (n == NULL || dn_folded(n))
		return -1;
	/* (n->y is where it was laid out: the boxes that scroll that it is in
	   scrolled to it first, and it is above by how far they are) */
	if (p->nscr > 0)
		page_reveal(p, n, n->y - scrolled_y(p, n), 1, 1);
	return n->y - scrolled_y(p, n);
}
