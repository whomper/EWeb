/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * canvas.h - drawing a page's display list into memory: chunky pixels,
 * a byte each (a colour register, for the planar modes, which the GEM
 * side converts) or the Falcon's 16-bit true colour. An item less than
 * solid (CSS Color 4's alpha and opacity, the top byte of its colour) is
 * mixed with what is under it; one of no alpha isn't drawn at all.
 */
#ifndef EW_CANVAS_H
#define EW_CANVAS_H

#include <stdint.h>

#include "layout.h"
#include "pic.h"

typedef struct canvas {
	uint8_t *pix;
	int32_t w, h;
	long stride;			/* bytes from one row to the next */
	int bpp;			/* 8 or 16 */
	int32_t ox, oy;			/* the page's point at pixel (0, 0) */
	/* 8 bits: the pixel value for a colour (0xRRGGBB) */
	uint8_t (*pen)(void *ud, uint32_t rgb);
	/* 8 bits, optional: a fill's colour as a pattern of two pens (a
	   monochrome screen's greys), how many of 16 pixels are light; NULL
	   or -1: solid in pen() */
	int (*shade)(void *ud, uint32_t rgb, uint8_t *dark, uint8_t *light);
	/* 8 bits: each pixel value's colour (red, green, blue), for mixing a
	   translucent fill or text with what is under it; NULL: not known (one
	   at least half solid is drawn solid, the others not at all) */
	const uint8_t (*pal)[3];
	/* a picture (a DL_IMAGE's handle) at w x h: 1 and its pixels, and
	   at 8 bits its pens (pic_dither's, to the screen's colours); 0 when
	   it isn't there (yet): its place is framed, its alt text in it.
	   NULL: never */
	int (*picture)(void *ud, void *img, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens);
	void *ud;
	/* a stretch of the page's text marked (a selection, the words found):
	   filled in mark_rgb (0xRRGGBB) behind it; marked 0: none */
	int marked;
	pg_range mark;
	uint32_t mark_rgb;
	/* 1: a box's scroll bars are the screen's to draw (EWEB.PRG: the
	   AES's, panebar, over the copy; the guidelines' 5.1), not drawn here */
	int aes_bars;
} canvas;

/* the Falcon's true colour pixel: RRRRRGGG GGGBBBBB */
#define RGB565(rgb) ((uint16_t) ((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

/* a fill, page coordinates (0xRRGGBB): solid, or within cv_page as
   solid as the item being drawn */
void cv_fill(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
/* a picture at (x, y), page coordinates, clipped to the canvas: solid
   where its alpha is, mixed with what is under it at 16 bits (at 8, a
   pixel at least half solid is drawn) */
void cv_picture(canvas *c, const PIC *pic, const uint8_t *pens, int32_t x, int32_t y);
/* the page's background, then every item that falls on the canvas */
void cv_page(canvas *c, const page *p);
/* form controls (DL_CTL items) are drawn from their state, form.h: */
struct fctl;
/* text in lines at most w wide, broken at its line breaks and spaces
   (a textarea's): each line's first byte into starts; how many */
int cv_wrap(const char *s, long n, ew_font *f, int32_t w, long *starts, int max);
/* the byte of a typed control's value nearest (x, y) on the page */
long cv_ctl_hit(const dl_item *d, struct fctl *c, int32_t x, int32_t y);
/* where the caret goes: the start or end of its line, or the line above
   or below (a field's: its start or end) */
enum { CV_LINE_START, CV_LINE_END, CV_UP, CV_DOWN };
long cv_ctl_line(const dl_item *d, struct fctl *c, int how);
/* one text run at x with its baseline at base, in page coordinates
   (0xRRGGBB, solid as cv_fill's) */
void cv_text(canvas *c, ew_font *f, int32_t x, int32_t base, const char *s, uint32_t n,
	     uint32_t rgb, int flags);

#endif
