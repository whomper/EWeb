/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * layout.h - EWeb's layout (fresh code, Erez's decision of 18 Oct 2026):
 * a styled document (src/doc) becomes boxes, lines and a display list of
 * fills, text runs and pictures in device pixels, ready to draw
 * (src/gfx). Block and inline layout, line breaking with EWeb's fonts, lists, floats,
 * tables, flexbox, grid, margins, padding, borders, backgrounds, links, and
 * Hebrew words reordered right to left.
 */
#ifndef EW_LAYOUT_H
#define EW_LAYOUT_H

#include <stdint.h>

#include "doc.h"
#include "font.h"

typedef struct {
	int32_t width;			/* the page's width: the window's work area, device px */
	int32_t zoom;			/* device px per CSS px, x1024 (819: 0.8) */
	int32_t text;			/* the text's size, x1024: fonts and line heights only
					   (Larger text: the pictures stay as they are); 0: 1024 */
	/* has this link (href, resolved by the caller) been visited? NULL: no */
	int (*visited)(void *ud, const char *href);
	/* a picture: its handle for an <img>'s src or a background's url()
	   (as the page has it; NULL: pictures aren't shown, an <img>'s alt
	   text is), and its size in CSS px once it is known (0 x 0 until its
	   file has come). css: a url() of the page's CSS, whose query is
	   UTF-8 (CSS parses it without the page's encoding) */
	void *(*image)(void *ud, const char *src, int css, int32_t *w, int32_t *h);
	void *ud;
	/* the page laid out before of the same document (NULL: none): its
	   boxes' scrolling kept (an element's box scrolled as far again) */
	const struct page *was;
	/* a box's scroll bar's width, device px (EWEB.PRG: the AES's slider
	   column, panebar's pb_width; the guidelines' 5.1); 0: SBAR_W */
	int32_t sbar;
} LAYOUT_OPT;

/* DL_SUM: a details element's summary (the HTML Standard, 4.11): its
   box, drawn not at all (rgb's AA 0), where a click opens and closes det.
   DL_CLIP: a box that hides what overflows it (CSS Overflow 3), drawn not
   at all: the items in it (clip) are drawn and clicked within it.
   DL_SBAR: a box's scroll bar (overflow auto or scroll, when there is more
   than it shows), over its padding box's right edge (base 0) or foot
   (base 1); len is 1 + its scroller's index into page.scr */
enum { DL_FILL, DL_TEXT, DL_IMAGE, DL_CTL, DL_GRAD, DL_SUM, DL_CLIP, DL_SBAR };
#define DLF_RTL   1			/* a right-to-left word: its letters drawn last to first */
#define DLF_UNDER 2			/* text-decoration */
#define DLF_OVER  4
#define DLF_STRIKE 8
#define DLF_NOSIZE 16			/* a picture laid out before its file's size was known */
#define DLF_ALT 32			/* and with no size at all: its alt text's room (the text
					   is an item of its own), nothing drawn */
#define DLF_TILE 64			/* DL_IMAGE: a background picture, tiled over the item
					   (tile); DL_GRAD: always; DL_FILL: its picture or
					   gradient is the next item */
#define DLF_NOHIT 128			/* pointer-events: none: a click goes through it to what
					   is under it (its link, its control) */

/* a background picture's or gradient's tiles: one at (item's x + dx,
   y + dy), w x h device px, and the others on from it across (rx) and
   down (ry) */
typedef struct dl_tile {
	int32_t dx, dy, w, h;
	uint8_t rx, ry;
	uint8_t grow;			/* a gradient as tall as its box: it grows with it
					   (a table's row, a flex line) */
} dl_tile;

typedef struct dl_item {
	int32_t x, y, w, h;		/* device px on the page; text: its line box part */
	uint32_t rgb;			/* 0xAARRGGBB: AA how solid it is drawn, 255 solid, 0 not
					   at all (a transparent colour, opacity 0: the item is
					   kept for clicks); a picture's AA is its opacity, a
					   control's its opacity (its frame, a box's tick, a
					   button's dot, the keyboard's frame), with its text's
					   colour (ink) */
	uint8_t type, flags;
	uint16_t link;			/* 0: none, else 1 + index into page.links */
	int16_t base;			/* text and controls: the baseline, from y */
	uint16_t ctl;			/* 1 + index into page.ctls: a form control's content
					   (DL_CTL, drawn from its state), or a picture that is
					   a submit button (DL_IMAGE); 0: none */
	uint16_t clip;			/* 1 + index into page.clips: the box hiding what
					   overflows it that the item is drawn and clicked
					   within (and that box's, within its own); 0: none */
	ew_font *font;
	union {
		const char *text;	/* UTF-8, kept by the page */
		const dl_tile *tile;	/* DL_IMAGE with DLF_TILE (NULL while its size isn't
					   known), DL_GRAD (NULL: none drawn) */
		uint32_t ink;		/* DL_CTL: its text's colour, 0xAARRGGBB as rgb (the
					   CSS colour's alpha, then the opacity it is in) */
		dnode *det;		/* DL_SUM: the details element */
	};
	uint32_t len;			/* DL_CTL: a textarea's line step */
	void *img;			/* DL_IMAGE: the picture's handle (LAYOUT_OPT.image), drawn at w x h
					   (a tile's size with DLF_TILE: DL_PIC_W, DL_PIC_H);
					   DL_GRAD: its gradient (const ec_grad *, lengths in
					   device px, currentcolor made the colour), as solid as
					   rgb's AA says (its box's opacity) */
#ifndef EW_ST
	dnode *node;			/* the element it is of: a click's target for scripts */
#endif
} dl_item;

typedef struct page {
	doc *d;				/* not owned */
	dl_item *items;
	long nitems, cap;
	char **links;			/* each link's href as the page has it (and its
					   download attribute: page_link_download) */
	int nlinks, linkcap;
	struct fctl **ctls;		/* the form controls shown (their state is the document's) */
	int nctls, ctlcap;
	/* the order the items are drawn in (CSS 2.1, Appendix E: positioned
	   boxes over what isn't, by z-index), when it isn't theirs: ranges of
	   them (k0 to k1) one after the other, each's first the r0th drawn;
	   NULL: in their order. Their order stays the document's (find, copy) */
	struct pg_seg {
		long k0, k1, r0;
	} *seg;
	long nseg;
	/* the items by where they are: the page in strips of PAGE_STRIP rows,
	   each strip's items (their places in the order drawn, page_drawn, in
	   that order) from sidx[strip[k]] to sidx[strip[k + 1]]; NULL when out
	   of memory (then all are looked at) */
	long *strip, *sidx;
	long nstrips;
	int32_t width, height;		/* device px */
	/* DL_CLIP items (a box with overflow hidden or clip: its padding box,
	   an axis it doesn't clip as wide as can be): their indexes into items */
	long *clips;
	long nclips, clipcap;
	/* the boxes that scroll (overflow auto or scroll with more than they
	   show): each its DL_CLIP item, how far it scrolls and is scrolled
	   (device px), its element, and its bars (DL_SBAR items, -1: none) */
	struct pg_scroll {
		long clip, bar[2];
		int32_t mx, my, sx, sy;
		int32_t ex, ey;		/* how far what is in it goes from its padding box's
					   left and top (0: it doesn't scroll that way) */
		dnode *n;
	} *scr;
	int nscr, scrcap;
	int wide;			/* (the window may scroll sideways: index_items) */
	int32_t right;			/* how far right what it draws goes (>= width; a
					   window scrolls sideways that far), width when the
					   root or the body hides what overflows sideways */
	uint32_t bg;			/* what the page is drawn on (0xRRGGBB) */
	long cbg;			/* the canvas's background picture or gradient item (the
					   root's or the body's), drawn on past the page's foot
					   to the window's; -1: none */
#ifndef EW_ST
	/* each element's block (border box, device px), for scripts */
	struct pg_block {
		dnode *n;
		int32_t x, y, w, h;
		uint16_t clip;		/* the boxes hiding what overflows them it is in (dl_item.clip) */
	} *blocks;
	long nblocks, blockcap;
#endif
	struct arena *mem;
	LAYOUT_OPT opt;
	int deep;			/* nested deeper than the stack holds: the rest left out */
	unsigned long serial;		/* each layout's own (a place in one is another's no more) */
} page;

#define PAGE_STRIP 256

/* the size a picture item's picture is drawn at */
#define DL_PIC_W(d) ((d)->flags & DLF_TILE ? ((d)->tile ? (d)->tile->w : 0) : (d)->w)
#define DL_PIC_H(d) ((d)->flags & DLF_TILE ? ((d)->tile ? (d)->tile->h : 0) : (d)->h)

/* a gradient's colour where it can't be drawn: its stops' colours
   mixed evenly (premultiplied), 0xAARRGGBB; cur is currentcolor's */
uint32_t grad_average(const ec_grad *g, uint32_t cur);

/* the rectangle x0, y0 to x1, y1 cut to the boxes hiding what overflows
   them that clip (a dl_item's) names: 0 if none of it is left */
int page_clip(const page *p, unsigned clip, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);

/* the item drawn rth (page.seg) */
long page_drawn(const page *p, long r);

/* lays the document out; NULL only if out of memory */
page *layout_page(doc *d, const LAYOUT_OPT *o);
void page_free(page *p);

#define SBAR_W 10			/* a box's scroll bar's width (device px) where
					   LAYOUT_OPT.sbar doesn't say */
#define PAGE_SBAR(p) ((p)->opt.sbar > 0 ? (p)->opt.sbar : SBAR_W)

/* scrolls box k of page.scr to (sx, sy), within its reach: the items in it
   moved, the strips made again. Returns 1 if it moved, with the box's
   padding box (what to draw again) in x, y, w, h */
int page_scroll(page *p, int k, int32_t sx, int32_t sy, int32_t *x, int32_t *y, int32_t *w, int32_t *h);

/* the boxes that scroll that element n is in (outermost first) scrolled so
   that y to y + h (where it is on the page now) shows in each: at its top
   (top), else as little as will do. Returns 1 if one moved */
int page_reveal(page *p, const dnode *n, int32_t y, int32_t h, int top);

/* a place in a page's text: a text item (DL_TEXT, its index) and a byte
   of it */
typedef struct pg_pos {
	long item;
	uint32_t off;
} pg_pos;

/* words on the page (Edit > Find, find.c), case aside (ASCII, Latin-1)
   and a space for any white space, a line's end or the gap between two
   runs: the first match starting at or after from (NULL: the top), back:
   the last before it, going round. Its start in *a, its end in *b, which
   it is in *nth (from 1); how many there are in all, 0 none, -1 out of
   memory */
long page_find(const page *p, const char *what, const pg_pos *from, int back, pg_pos *a, pg_pos *b, long *nth);

/* a stretch of the page's text (a selection, the words found): from a
   to b, a first in the display list (a line's runs are in it in the
   order they are read) */
typedef struct pg_range {
	pg_pos a, b;
} pg_range;

/* the stretch between two places, either first */
void page_range(const page *p, const pg_pos *x, const pg_pos *y, pg_range *r);
/* the part of item i (len bytes) in it, its bytes s to e: 0 none */
int page_range_part(const pg_range *r, long i, uint32_t len, uint32_t *s, uint32_t *e);
/* the place in the text nearest (x, y) (on a line there before one above
   or below, then the nearest to the side; text hidden by a box hiding
   what overflows it not; never before a character with no width): 2 if
   it is text under (x, y), 1 if elsewhere, 0 when the page has no text */
int page_pos_at(const page *p, int32_t x, int32_t y, pg_pos *at);
/* the word a place is in or just after (its letters and digits, and any
   character past ASCII), in its run: from a to b */
void page_word(const page *p, const pg_pos *at, pg_pos *a, pg_pos *b);
/* a place as the characters before it that aren't white space, in the
   order the page is read: the same text after the page has been laid
   out again (a picture came, the window was sized), its lines ending
   elsewhere. page_text_place is the place again (end: just after the
   index'th character, else at the next): 0 when the page has no text */
long page_text_index(const page *p, const pg_pos *at);
int page_text_place(const page *p, long index, int end, pg_pos *at);
/* a stretch's text, or the page's (r NULL), as it is read (UTF-8, its
   length in *n): a space between two runs apart on a line, a line break
   (CR LF) between lines, an empty line before a run lower than two of
   its lines down; the page's ends with a line break. NULL: no memory */
char *page_copy_text(const page *p, const pg_range *r, long *n);

/* page_reveal for item i (by the boxes hiding what overflows them that
   it is in: any edition) */
int page_reveal_item(page *p, long i, int top);

/* the scroll bar at (x, y): its item, else NULL */
const dl_item *page_bar_at(const page *p, int32_t x, int32_t y);

/* a bar's thumb: from t0 to t1 along it (device px from its top or left) */
void page_bar_thumb(const page *p, const dl_item *bar, int32_t *t0, int32_t *t1);

/* the link under (x, y) on the page: an index into links, or -1 */
int page_link_at(const page *p, int32_t x, int32_t y);
/* a link's download attribute (<a download>): its value, NULL for none */
const char *page_link_download(const page *p, int link);
/* the form control under (x, y), and its item; NULL: none */
struct fctl *page_ctl_at(const page *p, int32_t x, int32_t y, const dl_item **it);
/* a control's item on the page (its content's place), or NULL when it
   isn't shown */
const dl_item *page_ctl_item(const page *p, const struct fctl *c);
/* the details element whose summary is under (x, y) (the innermost: a
   details in a summary), or NULL */
dnode *page_summary_at(const page *p, int32_t x, int32_t y);
#ifndef EW_ST
/* for scripts: the element under (x, y) (the innermost), or NULL; an
   element's place on the page (its block, or what its items cover): 0
   when it isn't shown */
dnode *page_node_at(const page *p, int32_t x, int32_t y);
int page_node_rect(const page *p, const dnode *n, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
#endif
/* where the element with that id (or an <a name>) is, or -1 (not laid
   out, or folded away in a closed details); the boxes that scroll that
   it is in scrolled to it */
int32_t page_anchor(page *p, const char *id);
/* that element (the first laid out, or folded away: dn_reveal opens its
   details), or NULL */
dnode *page_anchor_node(const page *p, const char *id);

#endif
