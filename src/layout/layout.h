/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * layout.h - EWeb's layout (fresh code, Erez's decision of 18 Oct 2026):
 * a styled document (src/doc) becomes boxes, lines and a display list of
 * fills, text runs and pictures in device pixels, ready to draw
 * (src/gfx). Block and inline layout, line breaking with EWeb's fonts, lists, rows (table
 * rows and flex rows, cells side by side), margins, padding, borders,
 * backgrounds, links, and Hebrew words reordered right to left.
 */
#ifndef EW_LAYOUT_H
#define EW_LAYOUT_H

#include <stdint.h>

#include "doc.h"
#include "font.h"

typedef struct {
	int32_t width;			/* the page's width: the window's work area, device px */
	int32_t zoom;			/* device px per CSS px, x1024 (819: 0.8) */
	/* has this link (href, resolved by the caller) been visited? NULL: no */
	int (*visited)(void *ud, const char *href);
	/* a picture: its handle for an <img>'s src (as the page has it; NULL:
	   pictures aren't shown, its alt text is), and its size in CSS px
	   once it is known (0 x 0 until its file has come) */
	void *(*image)(void *ud, const char *src, int32_t *w, int32_t *h);
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
	const char *text;		/* UTF-8, kept by the page */
	uint32_t len;			/* DL_CTL: a textarea's line step */
	void *img;			/* DL_IMAGE: the picture's handle (LAYOUT_OPT.image), drawn at w x h */
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
	struct arena *mem;
	LAYOUT_OPT opt;
} page;

#define PAGE_STRIP 256

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
/* where the element with that id (or an <a name>) is, or -1 */
int32_t page_anchor(const page *p, const char *id);

#endif
