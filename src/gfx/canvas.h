/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * canvas.h - drawing a page's display list into memory: chunky pixels,
 * a byte each (a colour register, for the planar modes, which the GEM
 * side converts) or the Falcon's 16-bit true colour.
 */
#ifndef EW_CANVAS_H
#define EW_CANVAS_H

#include <stdint.h>

#include "layout.h"

typedef struct canvas {
	uint8_t *pix;
	int32_t w, h;
	long stride;			/* bytes from one row to the next */
	int bpp;			/* 8 or 16 */
	int32_t ox, oy;			/* the page's point at pixel (0, 0) */
	/* 8 bits: the pixel value for a colour (0xRRGGBB) */
	uint8_t (*pen)(void *ud, uint32_t rgb);
	void *ud;
} canvas;

/* the Falcon's true colour pixel: RRRRRGGG GGGBBBBB */
#define RGB565(rgb) ((uint16_t) ((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

void cv_fill(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
/* the page's background, then every item that falls on the canvas */
void cv_page(canvas *c, const page *p);
/* one text run at x with its baseline at base, in page coordinates */
void cv_text(canvas *c, ew_font *f, int32_t x, int32_t base, const char *s, uint32_t n,
	     uint32_t rgb, int flags);

#endif
