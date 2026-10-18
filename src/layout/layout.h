/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * layout.h - EWeb's layout (fresh code, Erez's decision of 18 Oct 2026):
 * a styled document (src/doc) becomes boxes, lines and a display list of
 * fills and text runs in device pixels, ready to draw (src/gfx). Block
 * and inline layout, line breaking with EWeb's fonts, lists, rows (table
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
	void *ud;
} LAYOUT_OPT;

enum { DL_FILL, DL_TEXT };
#define DLF_RTL   1			/* a right-to-left word: its letters drawn last to first */
#define DLF_UNDER 2			/* text-decoration */
#define DLF_OVER  4
#define DLF_STRIKE 8

typedef struct dl_item {
	int32_t x, y, w, h;		/* device px on the page; text: its line box part */
	uint32_t rgb;			/* 0xRRGGBB */
	uint8_t type, flags;
	uint16_t link;			/* 0: none, else 1 + index into page.links */
	int16_t base;			/* text: the baseline, from y */
	ew_font *font;
	const char *text;		/* UTF-8, kept by the page */
	uint32_t len;
} dl_item;

typedef struct page {
	doc *d;				/* not owned */
	dl_item *items;
	long nitems, cap;
	char **links;			/* each link's href as the page has it */
	int nlinks, linkcap;
	int32_t width, height;		/* device px */
	uint32_t bg;			/* what the page is drawn on (0xRRGGBB) */
	struct arena *mem;
	LAYOUT_OPT opt;
} page;

/* lays the document out; NULL only if out of memory */
page *layout_page(doc *d, const LAYOUT_OPT *o);
void page_free(page *p);

/* the link under (x, y) on the page: an index into links, or -1 */
int page_link_at(const page *p, int32_t x, int32_t y);
/* where the element with that id (or an <a name>) is, or -1 */
int32_t page_anchor(const page *p, const char *id);

#endif
