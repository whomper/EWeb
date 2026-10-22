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
} LAYOUT_OPT;

enum { DL_FILL, DL_TEXT, DL_IMAGE, DL_CTL };
#define DLF_RTL   1			/* a right-to-left word: its letters drawn last to first */
#define DLF_UNDER 2			/* text-decoration */
#define DLF_OVER  4
#define DLF_STRIKE 8
#define DLF_NOSIZE 16			/* a picture laid out before its file's size was known */
#define DLF_ALT 32			/* and with no size at all: its alt text's room (the text
					   is an item of its own), nothing drawn */
#define DLF_TILE 64			/* DL_IMAGE: a background picture, tiled over the item
					   (tile); DL_FILL: its picture is the next item */

/* a background picture's tiles: one at (item's x + dx, y + dy), w x h
   device px, and the others on from it across (rx) and down (ry) */
typedef struct dl_tile {
	int32_t dx, dy, w, h;
	uint8_t rx, ry;
} dl_tile;

typedef struct dl_item {
	int32_t x, y, w, h;		/* device px on the page; text: its line box part */
	uint32_t rgb;			/* 0xRRGGBB */
	uint8_t type, flags;
	uint16_t link;			/* 0: none, else 1 + index into page.links */
	int16_t base;			/* text and controls: the baseline, from y */
	uint16_t ctl;			/* 1 + index into page.ctls: a form control's content
					   (DL_CTL, drawn from its state), or a picture that is
					   a submit button (DL_IMAGE); 0: none */
	ew_font *font;
	union {
		const char *text;	/* UTF-8, kept by the page */
		const dl_tile *tile;	/* DL_IMAGE with DLF_TILE (NULL while its size isn't known) */
	};
	uint32_t len;			/* DL_CTL: a textarea's line step */
	void *img;			/* DL_IMAGE: the picture's handle (LAYOUT_OPT.image), drawn at w x h
					   (a tile's size with DLF_TILE: DL_PIC_W, DL_PIC_H) */
#ifndef EW_ST
	dnode *node;			/* the element it is of: a click's target for scripts */
#endif
} dl_item;

typedef struct page {
	doc *d;				/* not owned */
	dl_item *items;
	long nitems, cap;
	char **links;			/* each link's href as the page has it */
	int nlinks, linkcap;
	struct fctl **ctls;		/* the form controls shown (their state is the document's) */
	int nctls, ctlcap;
	/* the items by where they are: the page in strips of PAGE_STRIP rows,
	   each strip's items (indexes into items, in order) from sidx[strip[k]]
	   to sidx[strip[k + 1]]; NULL when out of memory (then all are looked at) */
	long *strip, *sidx;
	long nstrips;
	int32_t width, height;		/* device px */
	uint32_t bg;			/* what the page is drawn on (0xRRGGBB) */
#ifndef EW_ST
	/* each element's block (border box, device px), for scripts */
	struct pg_block {
		dnode *n;
		int32_t x, y, w, h;
	} *blocks;
	long nblocks, blockcap;
#endif
	struct arena *mem;
	LAYOUT_OPT opt;
	int deep;			/* nested deeper than the stack holds: the rest left out */
} page;

#define PAGE_STRIP 256

/* the size a picture item's picture is drawn at */
#define DL_PIC_W(d) ((d)->flags & DLF_TILE ? ((d)->tile ? (d)->tile->w : 0) : (d)->w)
#define DL_PIC_H(d) ((d)->flags & DLF_TILE ? ((d)->tile ? (d)->tile->h : 0) : (d)->h)

/* lays the document out; NULL only if out of memory */
page *layout_page(doc *d, const LAYOUT_OPT *o);
void page_free(page *p);

/* the link under (x, y) on the page: an index into links, or -1 */
int page_link_at(const page *p, int32_t x, int32_t y);
/* the form control under (x, y), and its item; NULL: none */
struct fctl *page_ctl_at(const page *p, int32_t x, int32_t y, const dl_item **it);
/* a control's item on the page (its content's place), or NULL when it
   isn't shown */
const dl_item *page_ctl_item(const page *p, const struct fctl *c);
#ifndef EW_ST
/* for scripts: the element under (x, y) (the innermost), or NULL; an
   element's place on the page (its block, or what its items cover): 0
   when it isn't shown */
dnode *page_node_at(const page *p, int32_t x, int32_t y);
int page_node_rect(const page *p, const dnode *n, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
#endif
/* where the element with that id (or an <a name>) is, or -1 */
int32_t page_anchor(const page *p, const char *id);

#endif
