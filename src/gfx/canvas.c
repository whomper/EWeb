/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* canvas.c - the display list drawn into memory (see canvas.h). */
#include <string.h>

#include "canvas.h"

static void put(canvas *c, int32_t x, int32_t y, uint32_t pv)
{
	if (c->bpp == 8) {
		c->pix[y * c->stride + x] = (uint8_t) pv;
	} else {
		uint8_t *p = c->pix + y * c->stride + 2 * x;
		p[0] = (uint8_t) (pv >> 8);
		p[1] = (uint8_t) pv;
	}
}

static uint32_t pixel(canvas *c, uint32_t rgb)
{
	return c->bpp == 8 ? c->pen(c->ud, rgb) : RGB565(rgb);
}

/* (x, y, w, h) in canvas pixels, clipped */
static void fill_px(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pv)
{
	int32_t i, j;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > c->w)
		w = c->w - x;
	if (y + h > c->h)
		h = c->h - y;
	if (w <= 0 || h <= 0)
		return;
	for (j = 0; j < h; j++) {
		if (c->bpp == 8) {
			memset(c->pix + (y + j) * c->stride + x, (int) pv, (size_t) w);
		} else {
			uint8_t *p = c->pix + (y + j) * c->stride + 2 * x;
			for (i = 0; i < w; i++) {
				*p++ = (uint8_t) (pv >> 8);
				*p++ = (uint8_t) pv;
			}
		}
	}
}

/* the ordered dither's thresholds, 4 by 4 (Bayer) */
static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

void cv_fill(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
	uint8_t dark, light;
	int level;
	if (c->bpp == 8 && c->shade != NULL && (level = c->shade(c->ud, rgb, &dark, &light)) >= 0) {
		/* a pattern fixed to the page, so that it doesn't move as it scrolls */
		int32_t i, j, x0 = x - c->ox, y0 = y - c->oy, x1, y1;
		x1 = x0 + w > c->w ? c->w : x0 + w;
		y1 = y0 + h > c->h ? c->h : y0 + h;
		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		for (j = y0; j < y1; j++) {
			const uint8_t *b = bayer[(j + c->oy) & 3];
			uint8_t *p = c->pix + j * c->stride;
			for (i = x0; i < x1; i++)
				p[i] = b[(i + c->ox) & 3] < level ? light : dark;
		}
		return;
	}
	fill_px(c, x - c->ox, y - c->oy, w, h, pixel(c, rgb));
}

static void glyph(canvas *c, const GLYPH *g, int32_t x, int32_t y, uint32_t pv)
{
	int32_t gx = x + g->x, gy = y + g->y, row, col, stride = (g->w + 7) / 8;
	int32_t c0 = 0, c1 = g->w, r0 = 0, r1 = g->h;
	if (g->bits == NULL)
		return;
	if (gx < 0)
		c0 = -gx;
	if (gx + c1 > c->w)
		c1 = c->w - gx;
	if (gy < 0)
		r0 = -gy;
	if (gy + r1 > c->h)
		r1 = c->h - gy;
	for (row = r0; row < r1; row++) {
		const uint8_t *b = g->bits + row * stride;
		for (col = c0; col < c1; col++)
			if (b[col >> 3] & (0x80 >> (col & 7)))
				put(c, gx + col, gy + row, pv);
	}
}

void cv_text(canvas *c, ew_font *f, int32_t x, int32_t base, const char *s, uint32_t n,
	     uint32_t rgb, int flags)
{
	const char *e = s + n, *p = s;
	uint32_t pv = pixel(c, rgb);
	int32_t px = x - c->ox, py = base - c->oy, x0 = px;
	GLYPH g;

	if (py - f->ascent - 2 >= c->h || py + f->descent + 2 < 0)
		return;
	if (flags & DLF_RTL) {
		/* letters last to first: a Hebrew word read right to left */
		uint32_t cps[64];
		int k = 0;
		while (p < e && k < 64)
			cps[k++] = utf8_next(&p, e);
		while (k-- > 0) {
			font_glyph(f, cps[k], &g);
			glyph(c, &g, px, py, pv);
			px += g.adv;
		}
	} else {
		while (p < e && px < c->w) {
			font_glyph(f, utf8_next(&p, e), &g);
			if (px + g.adv + 8 > 0)
				glyph(c, &g, px, py, pv);
			px += g.adv;
		}
	}
	if (flags & (DLF_UNDER | DLF_OVER | DLF_STRIKE)) {
		int32_t t = f->px >= 20 ? 2 : 1, w = px - x0;
		if (flags & DLF_UNDER)
			fill_px(c, x0, py + 1 + (f->descent > 3 ? 1 : 0), w, t, pv);
		if (flags & DLF_STRIKE)
			fill_px(c, x0, py - f->ascent / 3, w, t, pv);
		if (flags & DLF_OVER)
			fill_px(c, x0, py - f->ascent, w, t, pv);
	}
}

void cv_picture(canvas *c, const PIC *pic, const uint8_t *pens, int32_t x, int32_t y)
{
	int32_t px = x - c->ox, py = y - c->oy, x0 = 0, y0 = 0, x1 = pic->w, y1 = pic->h, i, j;
	if (px < 0)
		x0 = -px;
	if (py < 0)
		y0 = -py;
	if (px + x1 > c->w)
		x1 = c->w - px;
	if (py + y1 > c->h)
		y1 = c->h - py;
	for (j = y0; j < y1; j++) {
		long k = (long) j * pic->w;
		const uint8_t *a = pic->alpha != NULL ? pic->alpha + k : NULL;
		if (c->bpp == 8) {
			uint8_t *o = c->pix + (py + j) * c->stride + px;
			const uint8_t *s = pens + k;
			if (a == NULL) {
				memcpy(o + x0, s + x0, (size_t) (x1 - x0));
			} else {
				for (i = x0; i < x1; i++)
					if (a[i] >= 128)
						o[i] = s[i];
			}
		} else {
			uint8_t *o = c->pix + (py + j) * c->stride + 2 * px;
			const uint16_t *s = pic->px + k;
			for (i = x0; i < x1; i++) {
				unsigned v = s[i];
				if (a != NULL && a[i] != 255) {
					unsigned u, al = a[i], r, g, b;
					if (al == 0)
						continue;
					u = (unsigned) o[2 * i] << 8 | o[2 * i + 1];
					/* each component mixed: (new * a + old * (255 - a)) / 256 */
					r = (((v >> 11) * al + (u >> 11) * (255 - al)) >> 8) & 31;
					g = ((((v >> 5) & 63) * al + ((u >> 5) & 63) * (255 - al)) >> 8) & 63;
					b = (((v & 31) * al + (u & 31) * (255 - al)) >> 8) & 31;
					v = r << 11 | g << 5 | b;
				}
				o[2 * i] = (uint8_t) (v >> 8);
				o[2 * i + 1] = (uint8_t) v;
			}
		}
	}
}

/* a picture not there yet: a frame, with its alt text when it fits */
static void placeholder(canvas *c, const dl_item *d)
{
	cv_fill(c, d->x, d->y, d->w, 1, 0xc0c0c0);
	cv_fill(c, d->x, d->y + d->h - 1, d->w, 1, 0xc0c0c0);
	cv_fill(c, d->x, d->y, 1, d->h, 0xc0c0c0);
	cv_fill(c, d->x + d->w - 1, d->y, 1, d->h, 0xc0c0c0);
	if (d->len > 0 && d->font != NULL && d->h >= d->font->ascent + d->font->descent + 4 &&
	    (int32_t) font_width(d->font, d->text, d->len) <= d->w - 6)
		cv_text(c, d->font, d->x + 3, d->y + 2 + d->font->ascent, d->text, d->len, d->rgb, 0);
}

void cv_page(canvas *c, const page *p)
{
	long i;
	int32_t top = c->oy, bottom = c->oy + c->h, left = c->ox, right = c->ox + c->w;

	cv_fill(c, c->ox, c->oy, c->w, c->h, p->bg);
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		if (d->y >= bottom || d->y + d->h <= top || d->x >= right || d->x + d->w <= left)
			continue;
		if (d->type == DL_FILL) {
			cv_fill(c, d->x, d->y, d->w, d->h, d->rgb);
		} else if (d->type == DL_IMAGE) {
			const PIC *pic = NULL;
			const uint8_t *pens = NULL;
			if (d->flags & DLF_ALT)
				continue;	/* its alt text is drawn instead */
			if (c->picture != NULL && c->picture(c->ud, d->img, d->w, d->h, &pic, &pens) && pic != NULL &&
			    (c->bpp != 8 || pens != NULL))
				cv_picture(c, pic, pens, d->x, d->y);
			else
				placeholder(c, d);
		} else
			cv_text(c, d->font, d->x, d->y + d->base, d->text, d->len, d->rgb, d->flags);
	}
}
