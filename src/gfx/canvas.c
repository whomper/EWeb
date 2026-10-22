/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* canvas.c - the display list drawn into memory (see canvas.h). */
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "form.h"

/* a control's text is drawn within its box: canvas px, x0 <= x < x1 */
static int32_t clip_x0 = -0x7fffffff, clip_x1 = 0x7fffffff;

/* how solid the item being drawn is (its colour's top byte): 255 solid,
   0 not at all, else what it draws is mixed with what is under it */
static unsigned item_a = 255;

/* an alpha as this canvas draws it: at 8 bits with no palette to mix
   with, solid or not at all */
static unsigned drawn_a(const canvas *c, unsigned a)
{
	return c->bpp == 8 && c->pal == NULL ? (a >= 128 ? 255 : 0) : a;
}

static uint32_t pixel(canvas *c, uint32_t rgb)
{
	return c->bpp == 8 ? c->pen(c->ud, rgb) : RGB565(rgb);
}

/* (x, y, w, h) in canvas pixels, clipped: 1 if anything is left */
static inline int clip(canvas *c, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		*h += *y;
		*y = 0;
	}
	if (*x + *w > c->w)
		*w = c->w - *x;
	if (*y + *h > c->h)
		*h = c->h - *y;
	return *w > 0 && *h > 0;
}

/* (x, y, w, h) in canvas pixels, clipped */
static void fill_px(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pv)
{
	int32_t i, j;
	if (!clip(c, &x, &y, &w, &h))
		return;
	for (j = 0; j < h; j++) {
		if (c->bpp == 8) {
			memset(c->pix + (y + j) * c->stride + x, (int) pv, (size_t) w);
		} else {
			/* a word a pixel (the rows start on even addresses), as the
			   machine stores it: the Falcon's high byte first */
			uint8_t *p = c->pix + (y + j) * c->stride + 2 * x;
			uint8_t b[2] = { (uint8_t) (pv >> 8), (uint8_t) pv };
			uint16_t v, *q = (uint16_t *) (void *) p;
			memcpy(&v, b, 2);
			for (i = 0; i < w; i++)
				q[i] = v;
		}
	}
}

/* the ordered dither's thresholds, 4 by 4 (Bayer) */
static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* ---------------------------------------------------------------- translucency */

/* An item less than solid (CSS Color 4's alpha and opacity) is mixed
   with what is drawn under it, source-over (Compositing and Blending 1:
   new * a + old * (1 - a)), read back in every mode. At 16 bits each
   pixel is mixed in 32nds. At 8 bits each pixel value's colour comes
   from the palette, is mixed, and the screen's nearest pen taken, once
   a value for the item's colour and alpha (mix_to[]), so a pixel costs
   a look-up; on a screen whose fills are patterns (monochrome) the
   mix's grey is patterned as a solid fill's. What a colour's values
   became is kept for a few colours (mixtab: runs in a few colours one
   after another, as muted text beside links, work them out once), and
   mix_to[] holds the one in use: a change of colour empties the
   values the last one set and puts back those the next one has. What
   is under is mostly one colour: a run of pixels as the last ones takes
   their result, a long at a time. */
enum { MIX_PEN, MIX_SHADE };
#ifdef EW_ST
#define MIX_TABLES 2
#define MIX_KEEP 16			/* (the ST's screens: 16 pens at most; with more
					   on another screen, what isn't kept is worked
					   out again) */
#else
#define MIX_TABLES 4
#define MIX_KEEP 256
#endif
typedef struct {
	uint32_t rgb;			/* the colour mixed in, 0xRRGGBB */
	unsigned a;			/* how much of it, 0..255 (256: none yet) */
	int kind;			/* MIX_PEN, MIX_SHADE */
	int n;				/* values worked out and kept: from[k] became to[k] */
	int lost;			/* more of them in mix_to[] than are kept */
	uint8_t from[MIX_KEEP], to[MIX_KEEP];
} mixtab;
static mixtab mixtabs[MIX_TABLES];
/* 8 bits: the colour in use, each value's pen (or its grey's level); -1
   not worked out yet. An array, not a pointer: the 68030's code indexes
   it in one instruction */
static int16_t mix_to[256];
static struct {
	/* the screen the tables were made for */
	int ready;
	const uint8_t (*pal)[3];
	uint8_t (*pen)(void *, uint32_t);
	int (*shade)(void *, uint32_t, uint8_t *, uint8_t *);
	void *ud;
	mixtab *t;			/* 8 bits: the colour in mix_to[] */
	int next;			/* the table made again when none is the item's */
	uint8_t dark, light;		/* MIX_SHADE: the pattern's pens */
	uint32_t n;			/* 16 bits: the colour spread out (below), times a5 */
	unsigned a5;			/* a in 32nds */
	uint32_t in;			/* and the last pixel mixed, and what it became */
	uint16_t out;
} mix;

/* a 5-6-5 pixel spread out with room above each part for a multiply
   by 32: 00000GGG GGG00000 RRRRR000 000BBBBB */
#define SPREAD(v) (((uint32_t) (v) | (uint32_t) (v) << 16) & 0x07E0F81FUL)

static uint32_t mix_rgb(uint32_t rgb, const uint8_t *u, unsigned a)
{
	unsigned b = 255 - a;
	return (((rgb >> 16 & 255) * a + u[0] * b + 127) / 255) << 16 |
	       (((rgb >> 8 & 255) * a + u[1] * b + 127) / 255) << 8 | ((rgb & 255) * a + u[2] * b + 127) / 255;
}

/* the mix of rgb at item_a ready; 0: nothing to draw (too little to show) */
static int mix_init(canvas *c, uint32_t rgb, int kind)
{
	if (c->bpp == 16) {
		mix.a5 = (item_a * 32 + 127) / 255;
		mix.n = SPREAD(RGB565(rgb)) * mix.a5;
		mix.in = 0x10000;	/* (none yet) */
		return mix.a5 > 0;
	}
	if (!mix.ready || mix.pal != c->pal || mix.pen != c->pen || mix.shade != c->shade || mix.ud != c->ud) {
		int k;
		mix.ready = 1;
		mix.pal = c->pal;
		mix.pen = c->pen;
		mix.shade = c->shade;
		mix.ud = c->ud;
		mix.t = NULL;
		memset(mix_to, 0xff, sizeof mix_to);
		for (k = 0; k < MIX_TABLES; k++) {
			mixtabs[k].a = 256;
			mixtabs[k].n = 0;
			mixtabs[k].lost = 0;
		}
	}
	if (mix.t == NULL || mix.t->rgb != rgb || mix.t->a != item_a || mix.t->kind != kind) {
		mixtab *t = mixtabs;
		int k;
		if (mix.t != NULL && mix.t->lost) {
			memset(mix_to, 0xff, sizeof mix_to);
			mix.t->lost = 0;
		} else if (mix.t != NULL) {
			for (k = 0; k < mix.t->n; k++)
				mix_to[mix.t->from[k]] = -1;
		}
		while (t < mixtabs + MIX_TABLES && !(t->rgb == rgb && t->a == item_a && t->kind == kind))
			t++;
		if (t == mixtabs + MIX_TABLES) {
			t = &mixtabs[mix.next];
			mix.next = (mix.next + 1) % MIX_TABLES;
			t->rgb = rgb;
			t->a = item_a;
			t->kind = kind;
			t->n = 0;
			t->lost = 0;
		}
		for (k = 0; k < t->n; k++)
			mix_to[t->from[k]] = t->to[k];
		mix.t = t;
	}
	return 1;
}

/* 8 bits: what pixel value p becomes, the first time it is met */
static int mix_new(canvas *c, uint8_t p)
{
	mixtab *t = mix.t;
	uint32_t m = mix_rgb(t->rgb, c->pal[p], t->a);
	int v;
	if (t->kind == MIX_SHADE) {
		uint8_t dark, light;
		v = c->shade(c->ud, m, &dark, &light);
		if (v < 0)
			v = 0;
	} else {
		v = c->pen(c->ud, m);
	}
	mix_to[p] = (int16_t) v;
	if (t->n < MIX_KEEP) {
		t->from[t->n] = p;
		t->to[t->n++] = (uint8_t) v;
	} else {
		t->lost = 1;
	}
	return v;
}

/* 16 bits: the pixel at p (the Falcon's high byte first) mixed, in
   bytes: the Atari's build has no memcpy built in */
static inline void mix16(uint8_t *p)
{
	unsigned u = (unsigned) p[0] << 8 | p[1];
	uint32_t o;
	if (u != mix.in) {
		o = ((mix.n + SPREAD(u) * (32 - mix.a5)) >> 5) & 0x07E0F81FUL;
		mix.in = u;
		mix.out = (uint16_t) (o | o >> 16);
	}
	p[0] = (uint8_t) (mix.out >> 8);
	p[1] = (uint8_t) mix.out;
}

/* 8 bits: the pixel at p mixed, as mix_init made ready (an int16_t
   tested for its sign: no extending on the 68030) */
static inline void mix8(canvas *c, uint8_t *p)
{
	int16_t v = mix_to[*p];
	*p = (uint8_t) (v >= 0 ? v : mix_new(c, *p));
}

/* (x, y, w, h) in canvas pixels, clipped, mixed as mix_init made ready:
   a long at a time (4 pixels, or 2 at 16 bits) from a long's boundary,
   one as the last getting the last's result, as what is under is mostly
   one colour; each depth its own loops */
static void mix_px(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h)
{
	int32_t i, j;
	if (!clip(c, &x, &y, &w, &h))
		return;
	for (j = y; j < y + h; j++) {
		uint8_t *o = c->pix + j * c->stride, *p, *end;
		uint32_t last, to, *q, *qe;
		if (c->bpp == 16) {
			p = o + 2 * x;
			end = p + 2 * w;
			if ((uintptr_t) p & 2) {
				mix16(p);
				p += 2;
			}
			q = (uint32_t *) (void *) p;
			qe = q + (end - p) / 4;
			while (q < qe) {
				last = *q;
				mix16((uint8_t *) q);
				mix16((uint8_t *) q + 2);
				to = *q++;
				while (q < qe && *q == last)
					*q++ = to;
			}
			if ((uint8_t *) qe < end)
				mix16((uint8_t *) qe);
			continue;
		}
		if (mix.t->kind == MIX_SHADE) {
			/* the pattern fixed to the page, as a solid fill's */
			const uint8_t *b = bayer[(j + c->oy) & 3];
			for (i = x; i < x + w; i++) {
				int16_t v = mix_to[o[i]];
				if (v < 0)
					v = mix_new(c, o[i]);
				o[i] = b[(i + c->ox) & 3] < v ? mix.light : mix.dark;
			}
			continue;
		}
		p = o + x;
		end = p + w;
		while (p < end && ((uintptr_t) p & 3))
			mix8(c, p++);
		q = (uint32_t *) (void *) p;
		qe = q + (end - p) / 4;
		while (q < qe) {
			last = *q;
			mix8(c, (uint8_t *) q);
			mix8(c, (uint8_t *) q + 1);
			mix8(c, (uint8_t *) q + 2);
			mix8(c, (uint8_t *) q + 3);
			to = *q++;
			while (q < qe && *q == last)
				*q++ = to;
		}
		for (p = (uint8_t *) qe; p < end; p++)
			mix8(c, p);
	}
}

void cv_fill(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
	uint8_t dark, light;
	int level;
	if (item_a == 0)
		return;
	if (c->bpp == 8 && c->shade != NULL && (level = c->shade(c->ud, rgb, &dark, &light)) >= 0) {
		/* a pattern fixed to the page, so that it doesn't move as it scrolls */
		int32_t i, j, x0 = x - c->ox, y0 = y - c->oy, x1, y1;
		if (item_a != 255) {
			if (mix_init(c, rgb, MIX_SHADE)) {
				mix.dark = dark;
				mix.light = light;
				mix_px(c, x0, y0, w, h);
			}
			return;
		}
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
	if (item_a != 255) {
		if (mix_init(c, rgb, MIX_PEN))
			mix_px(c, x - c->ox, y - c->oy, w, h);
		return;
	}
	fill_px(c, x - c->ox, y - c->oy, w, h, pixel(c, rgb));
}

/* where a glyph at (x, y) falls on the canvas, within a control's box:
   its columns c0..c1 and rows r0..r1 */
static inline void glyph_clip(const canvas *c, const GLYPH *g, int32_t gx, int32_t gy, int32_t *c0, int32_t *c1,
			      int32_t *r0, int32_t *r1)
{
	*c0 = 0;
	*c1 = g->w;
	*r0 = 0;
	*r1 = g->h;
	if (gx < 0)
		*c0 = -gx;
	if (gx + *c1 > c->w)
		*c1 = c->w - gx;
	if (gx + *c0 < clip_x0)
		*c0 = clip_x0 - gx;
	if (gx + *c1 > clip_x1)
		*c1 = clip_x1 - gx;
	if (gy < 0)
		*r0 = -gy;
	if (gy + *r1 > c->h)
		*r1 = c->h - gy;
}

/* a glyph's pixels set in pv */
static void glyph(canvas *c, const GLYPH *g, int32_t x, int32_t y, uint32_t pv)
{
	int32_t gx = x + g->x, gy = y + g->y, row, col, stride = (g->w + 7) / 8, c0, c1, r0, r1;
	if (g->bits == NULL)
		return;
	glyph_clip(c, g, gx, gy, &c0, &c1, &r0, &r1);
	/* a row's address once; the depth's own loop */
	for (row = r0; row < r1; row++) {
		const uint8_t *b = g->bits + row * stride;
		uint8_t *o = c->pix + (gy + row) * c->stride;
		if (c->bpp == 8) {
			o += gx;
			for (col = c0; col < c1; col++)
				if (b[col >> 3] & (0x80 >> (col & 7)))
					o[col] = (uint8_t) pv;
		} else {
			o += 2 * gx;
			for (col = c0; col < c1; col++)
				if (b[col >> 3] & (0x80 >> (col & 7))) {
					o[2 * col] = (uint8_t) (pv >> 8);
					o[2 * col + 1] = (uint8_t) pv;
				}
		}
	}
}

/* a glyph's pixels mixed, as mix_init made ready (an item less than
   solid): kept apart, so that solid text's loops stay as they were */
static void glyph_mix(canvas *c, const GLYPH *g, int32_t x, int32_t y, uint32_t pv)
{
	int32_t gx = x + g->x, gy = y + g->y, row, col, stride = (g->w + 7) / 8, c0, c1, r0, r1;
	if (g->bits == NULL)
		return;
	glyph_clip(c, g, gx, gy, &c0, &c1, &r0, &r1);
	if (c->bpp == 8) {
		/* (an int16_t tested for its sign: no extending on the 68030) */
		for (row = r0; row < r1; row++) {
			const uint8_t *b = g->bits + row * stride;
			uint8_t *o = c->pix + (gy + row) * c->stride + gx;
			for (col = c0; col < c1; col++)
				if (b[col >> 3] & (0x80 >> (col & 7))) {
					int16_t v = mix_to[o[col]];
					if (v < 0)
						v = (int16_t) mix_new(c, o[col]);
					o[col] = (uint8_t) v;
				}
		}
		return;
	}
	for (row = r0; row < r1; row++) {
		const uint8_t *b = g->bits + row * stride;
		uint8_t *o = c->pix + (gy + row) * c->stride + 2 * gx;
		for (col = c0; col < c1; col++)
			if (b[col >> 3] & (0x80 >> (col & 7)))
				mix16(o + 2 * col);
	}
}

void cv_text(canvas *c, ew_font *f, int32_t x, int32_t base, const char *s, uint32_t n,
	     uint32_t rgb, int flags)
{
	const char *e = s + n, *p = s;
	uint32_t pv;
	int32_t px = x - c->ox, py = base - c->oy, x0 = px;
	int mixing = item_a != 255;
	/* solid or mixed: chosen once a run */
	void (*draw)(canvas *, const GLYPH *, int32_t, int32_t, uint32_t) = mixing ? glyph_mix : glyph;
	GLYPH g;

	if (py - f->ascent - 2 >= c->h || py + f->descent + 2 < 0 || item_a == 0)
		return;
	if (mixing && !mix_init(c, rgb, MIX_PEN))
		return;
	pv = mixing ? 0 : pixel(c, rgb);
	if (flags & DLF_RTL) {
		/* letters last to first: a Hebrew word read right to left */
		uint32_t cps[64];
		int k = 0;
		while (p < e && k < 64)
			cps[k++] = utf8_next(&p, e);
		while (k-- > 0) {
			font_glyph(f, cps[k], &g);
			draw(c, &g, px, py, pv);
			px += g.adv;
		}
	} else {
		while (p < e && px < c->w) {
			font_glyph(f, utf8_next(&p, e), &g);
			if (px + g.adv + 8 > 0)
				draw(c, &g, px, py, pv);
			px += g.adv;
		}
	}
	if (flags & (DLF_UNDER | DLF_OVER | DLF_STRIKE)) {
		int32_t t = f->px >= 20 ? 2 : 1, w = px - x0, k, ly[3];
		int nl = 0;
		if (flags & DLF_UNDER)
			ly[nl++] = py + 1 + (f->descent > 3 ? 1 : 0);
		if (flags & DLF_STRIKE)
			ly[nl++] = py - f->ascent / 3;
		if (flags & DLF_OVER)
			ly[nl++] = py - f->ascent;
		for (k = 0; k < nl; k++)
			if (mixing)
				mix_px(c, x0, ly[k], w, t);
			else
				fill_px(c, x0, ly[k], w, t, pv);
	}
}

void cv_picture(canvas *c, const PIC *pic, const uint8_t *pens, int32_t x, int32_t y)
{
	int32_t px = x - c->ox, py = y - c->oy, x0 = 0, y0 = 0, x1 = pic->w, y1 = pic->h, i, j;
	unsigned op = item_a;		/* its opacity */
	/* what there is to draw with: its pens in the modes with a palette,
	   its pixels in true colour (one read from the disk cache for a
	   palette has no pixels) */
	if (c->bpp == 8 ? pens == NULL : pic->px == NULL)
		return;
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
			if (op != 255) {
				/* less than opaque: drawn where the opacity is above the
				   ordered dither's threshold, a pattern fixed to the page
				   (a pen can't be mixed with another at no cost) */
				const uint8_t *b = bayer[(py + j + c->oy) & 3];
				uint8_t on[4];
				int q;
				for (q = 0; q < 4; q++)
					on[q] = (uint8_t) (op > b[q] * 16u + 8);
				for (i = x0; i < x1; i++)
					if (on[(px + i + c->ox) & 3] && (a == NULL || a[i] >= 128))
						o[i] = s[i];
			} else if (a == NULL) {
				memcpy(o + x0, s + x0, (size_t) (x1 - x0));
			} else {
				for (i = x0; i < x1; i++)
					if (a[i] >= 128)
						o[i] = s[i];
			}
		} else {
			uint8_t *o = c->pix + (py + j) * c->stride + 2 * px;
			const uint16_t *s = pic->px + k;
			if (a == NULL && op == 255) {
				for (i = x0; i < x1; i++) {
					o[2 * i] = (uint8_t) (s[i] >> 8);
					o[2 * i + 1] = (uint8_t) s[i];
				}
				continue;
			}
			for (i = x0; i < x1; i++) {
				unsigned v = s[i];
				if ((a != NULL && a[i] != 255) || op != 255) {
					unsigned u, al = a != NULL ? a[i] : 255, r, g, b;
					if (op != 255)
						al = (al * (op + 1)) >> 8;
					if (al == 0)
						continue;
					u = (unsigned) o[2 * i] << 8 | o[2 * i + 1];
					/* each component mixed: (new * a + old * (255 - a)) / 256, rounded */
					r = (((v >> 11) * al + (u >> 11) * (255 - al) + 128) >> 8) & 31;
					g = ((((v >> 5) & 63) * al + ((u >> 5) & 63) * (255 - al) + 128) >> 8) & 63;
					b = (((v & 31) * al + (u & 31) * (255 - al) + 128) >> 8) & 31;
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
		cv_text(c, d->font, d->x + 3, d->y + 2 + d->font->ascent, d->text, d->len, d->rgb & 0xffffff, 0);
}

/* ---------------------------------------------------------------- form controls */

/* a line of a control's text in the order it is shown: Hebrew right to
   left, numbers and Latin left to right within it. A short form of the
   Unicode bidirectional algorithm: the paragraph's direction from its
   first strong letter, levels for the letters and numbers, the neutrals
   between them taking the direction on both sides (else the
   paragraph's), then each run at a level or above reversed, from the
   highest level down */
typedef struct {
	int n;			/* letters */
	int rtl;		/* the paragraph runs right to left */
	int plain;		/* all left to right: drawn as it is */
	uint32_t *cp;		/* each letter, in logical order */
	long *off;		/* its byte in the text; off[n]: the end */
	uint8_t *lvl;		/* its level: odd is right to left */
	int *vis;		/* at visual position k: the letter's logical index */
	int *pos;		/* the letter i's visual position */
	int32_t *x;		/* where visual position k starts; x[n]: the width */
} VLINE;

enum { B_L, B_R, B_N, B_EN };

static int bclass(uint32_t u)
{
	if ((u >= 0x590 && u <= 0x5ff) || (u >= 0xfb1d && u <= 0xfb4f))
		return B_R;
	if (u >= '0' && u <= '9')
		return B_EN;
	if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= 0xc0 && u < 0x590 && u != 0xd7 && u != 0xf7))
		return B_L;
	return B_N;
}

/* a bracket in right-to-left text is drawn the other way round */
static uint32_t mirror(uint32_t u)
{
	switch (u) {
	case '(': return ')';
	case ')': return '(';
	case '[': return ']';
	case ']': return '[';
	case '{': return '}';
	case '}': return '{';
	case '<': return '>';
	case '>': return '<';
	}
	return u;
}

static int utf8_put(uint32_t u, char *b)
{
	if (u < 0x80) {
		b[0] = (char) u;
		return 1;
	}
	if (u < 0x800) {
		b[0] = (char) (0xc0 | (u >> 6));
		b[1] = (char) (0x80 | (u & 0x3f));
		return 2;
	}
	if (u < 0x10000) {
		b[0] = (char) (0xe0 | (u >> 12));
		b[1] = (char) (0x80 | ((u >> 6) & 0x3f));
		b[2] = (char) (0x80 | (u & 0x3f));
		return 3;
	}
	b[0] = (char) (0xf0 | (u >> 18));
	b[1] = (char) (0x80 | ((u >> 12) & 0x3f));
	b[2] = (char) (0x80 | ((u >> 6) & 0x3f));
	b[3] = (char) (0x80 | (u & 0x3f));
	return 4;
}

static void vfree(VLINE *v)
{
	free(v->cp);
	free(v->off);
	free(v->lvl);
	free(v->vis);
	free(v->pos);
	free(v->x);
	memset(v, 0, sizeof *v);
}

/* the strong direction a letter gives its neighbours: 1 right to left
   (numbers too, in right-to-left text) */
static int strong_r(const VLINE *v, int i)
{
	int c = bclass(v->cp[i]);
	return c == B_R || (c == B_EN && v->lvl[i] == 2);
}

/* 0 when out of memory */
static int vline(VLINE *v, ew_font *f, const char *s, long len)
{
	const char *p, *e = s + len;
	int i, k, n = 0, maxl = 0, last, lv;
	int32_t x = 0;
	memset(v, 0, sizeof *v);
	for (p = s; p < e; n++)
		utf8_next(&p, e);
	v->n = n;
	v->cp = malloc((size_t) (n + 1) * sizeof *v->cp);
	v->off = malloc((size_t) (n + 1) * sizeof *v->off);
	v->lvl = malloc((size_t) (n + 1));
	v->vis = malloc((size_t) (n + 1) * sizeof *v->vis);
	v->pos = malloc((size_t) (n + 1) * sizeof *v->pos);
	v->x = malloc((size_t) (n + 1) * sizeof *v->x);
	if (v->cp == NULL || v->off == NULL || v->lvl == NULL || v->vis == NULL || v->pos == NULL || v->x == NULL) {
		vfree(v);
		return 0;
	}
	for (p = s, i = 0; p < e; i++) {
		v->off[i] = p - s;
		v->cp[i] = utf8_next(&p, e);
	}
	v->off[n] = len;
	for (i = 0; i < n; i++) {
		int c = bclass(v->cp[i]);
		if (c == B_L || c == B_R) {
			v->rtl = c == B_R;
			break;
		}
	}
	/* the letters' and numbers' levels; the neutrals' after */
	last = v->rtl ? B_R : B_L;
	for (i = 0; i < n; i++) {
		int c = bclass(v->cp[i]);
		if (c == B_R) {
			v->lvl[i] = 1;
			last = B_R;
		} else if (c == B_L) {
			v->lvl[i] = (uint8_t) (v->rtl ? 2 : 0);
			last = B_L;
		} else if (c == B_EN) {
			v->lvl[i] = (uint8_t) (v->rtl || last == B_R ? 2 : 0);
		} else {
			v->lvl[i] = 0xff;
		}
	}
	for (i = 0; i < n; i = k) {
		int before, after;
		if (v->lvl[i] != 0xff) {
			k = i + 1;
			continue;
		}
		for (k = i; k < n && v->lvl[k] == 0xff; k++)
			;
		/* within a number: 45.50, 1,000, 12:30, 1/2; and a sign by it,
		   10%, $5, -3 */
		if (k - i == 1 && i > 0 && k < n && bclass(v->cp[i - 1]) == B_EN && bclass(v->cp[k]) == B_EN &&
		    strchr(".,:/", (int) v->cp[i]) != NULL) {
			v->lvl[i] = v->lvl[i - 1];
			continue;
		}
		if (k - i == 1 && ((i > 0 && bclass(v->cp[i - 1]) == B_EN) || (k < n && bclass(v->cp[k]) == B_EN)) &&
		    (strchr("%$#+-", (int) v->cp[i]) != NULL || v->cp[i] == 0xb0 || v->cp[i] == 0x20aa ||
		     v->cp[i] == 0x20ac || v->cp[i] == 0xa3)) {
			v->lvl[i] = i > 0 && bclass(v->cp[i - 1]) == B_EN ? v->lvl[i - 1] : v->lvl[k];
			continue;
		}
		before = i > 0 ? strong_r(v, i - 1) : v->rtl;
		after = k < n ? strong_r(v, k) : v->rtl;
		lv = before == after ? (before ? 1 : (v->rtl ? 2 : 0)) : v->rtl;
		while (i < k)
			v->lvl[i++] = (uint8_t) lv;
	}
	for (i = 0; i < n; i++) {
		v->vis[i] = i;
		if (v->lvl[i] > maxl)
			maxl = v->lvl[i];
	}
	v->plain = maxl == 0;
	for (lv = maxl; lv >= 1; lv--)
		for (i = 0; i < n; i = k + 1) {
			int a, b;
			if (v->lvl[v->vis[i]] < lv) {
				k = i;
				continue;
			}
			for (k = i; k + 1 < n && v->lvl[v->vis[k + 1]] >= lv; k++)
				;
			for (a = i, b = k; a < b; a++, b--) {
				int t = v->vis[a];
				v->vis[a] = v->vis[b];
				v->vis[b] = t;
			}
		}
	for (k = 0; k < n; k++) {
		int j = v->vis[k];
		v->pos[j] = k;
		v->x[k] = x;
		x += font_advance(f, v->lvl[j] & 1 ? mirror(v->cp[j]) : v->cp[j]);
	}
	v->x[n] = x;
	return 1;
}

static void vdraw(canvas *cv, const VLINE *v, ew_font *f, const char *s, int32_t x, int32_t base, uint32_t rgb)
{
	int k;
	if (v->plain) {
		cv_text(cv, f, x, base, s, (uint32_t) v->off[v->n], rgb, 0);
		return;
	}
	for (k = 0; k < v->n; k++) {
		int i = v->vis[k];
		char b[4];
		cv_text(cv, f, x + v->x[k], base, b, (uint32_t) utf8_put(v->lvl[i] & 1 ? mirror(v->cp[i]) : v->cp[i], b),
			rgb, 0);
	}
}

/* where the caret before a byte of the text is shown */
static int32_t vcaret(const VLINE *v, long byte)
{
	int i;
	if (v->n == 0)
		return 0;
	for (i = 0; i < v->n && v->off[i] < byte; i++)
		;
	if (i < v->n)
		return v->lvl[i] & 1 ? v->x[v->pos[i] + 1] : v->x[v->pos[i]];
	i = v->n - 1;
	return v->lvl[i] & 1 ? v->x[v->pos[i]] : v->x[v->pos[i] + 1];
}

/* the byte the caret goes before for a click at x */
static long vhit(const VLINE *v, int32_t x)
{
	int k, i;
	if (v->n == 0)
		return 0;
	for (k = 0; k < v->n - 1 && x >= v->x[k + 1]; k++)
		;
	i = v->vis[k];
	if (v->lvl[i] & 1)
		return x >= (v->x[k] + v->x[k + 1]) / 2 ? v->off[i] : v->off[i + 1];
	return x < (v->x[k] + v->x[k + 1]) / 2 ? v->off[i] : v->off[i + 1];
}

/* the text a typed control shows: its value, a password as bullets
   (malloc'ed then, else c->v); *dlen its bytes */
static const char *shown(const fctl *c, long *dlen, char **own)
{
	*own = NULL;
	if (c->kind == FC_PASSWORD) {
		long i, k = 0;
		char *t = malloc((size_t) c->len * 3 + 1);
		if (t == NULL) {
			*dlen = 0;
			return "";
		}
		for (i = 0; i < c->len; i++)
			if (((unsigned char) c->v[i] & 0xc0) != 0x80) {
				memcpy(t + k, "\xe2\x80\xa2", 3);
				k += 3;
			}
		t[k] = 0;
		*own = t;
		*dlen = k;
		return t;
	}
	*dlen = c->len;
	return c->v ? c->v : "";
}

/* a byte of the value as a byte of what is shown */
static long to_shown(const fctl *c, long i)
{
	long k, n = 0;
	if (c->kind != FC_PASSWORD)
		return i;
	for (k = 0; k < i && k < c->len; k++)
		if (((unsigned char) c->v[k] & 0xc0) != 0x80)
			n += 3;
	return n;
}

/* and back */
static long from_shown(const fctl *c, long j)
{
	long k, n = 0;
	if (c->kind != FC_PASSWORD)
		return j;
	for (k = 0; k < c->len; k++)
		if (((unsigned char) c->v[k] & 0xc0) != 0x80) {
			if (n >= j)
				return k;
			n += 3;
		}
	return c->len;
}

int cv_wrap(const char *s, long n, ew_font *f, int32_t w, long *starts, int max)
{
	long i = 0;
	int k = 0;
	if (max <= 0)
		return 0;
	starts[k++] = 0;
	while (i < n && k < max) {
		long sp = -1, j = i;
		int32_t x = 0;
		while (j < n && s[j] != '\n') {
			const char *q = s + j;
			int32_t a = font_advance(f, utf8_next(&q, s + n));
			if (x + a > w && j > i)
				break;
			x += a;
			if (s[j] == ' ')
				sp = j + 1;
			j = q - s;
		}
		if (j >= n)
			break;
		if (s[j] == '\n')
			i = j + 1;		/* a line break */
		else
			i = sp > i ? sp : j;	/* wrapped at the last space, else where it filled */
		starts[k++] = i;
	}
	return k;
}

#define MAXLINES 2048

/* the line (of a textarea's lines) a byte is on */
static int line_of(const long *st, int nl, long b)
{
	int k = 0;
	while (k + 1 < nl && st[k + 1] <= b)
		k++;
	return k;
}

/* a textarea's lines: each one's first byte (malloc'ed); how many */
static int lines_of(const dl_item *d, const fctl *c, long **st)
{
	*st = malloc(MAXLINES * sizeof **st);
	if (*st == NULL)
		return 0;
	return cv_wrap(c->v ? c->v : "", c->len, d->font, d->w - 4, *st, MAXLINES);
}

/* line k of a textarea, without its line break and the spaces it was
   wrapped at: its end */
static long line_end(const char *t, const long *st, int nl, int k, long tl)
{
	long e = k + 1 < nl ? st[k + 1] : tl;
	while (e > st[k] && (t[e - 1] == '\n' || (k + 1 < nl && t[e - 1] == ' ')))
		e--;
	return e;
}

/* where a line of a typed control's text starts: right-to-left text
   that fits goes against the right edge */
static int32_t line_x(const dl_item *d, const VLINE *v, int32_t scroll)
{
	if (v->rtl && v->x[v->n] <= d->w - 6)
		return d->x + d->w - 3 - v->x[v->n];
	return d->x + 2 - scroll;
}

/* a textarea's first line's baseline, or a field's */
static int32_t first_base(const dl_item *d)
{
	return d->len ? d->y + 2 + d->font->ascent : d->y + d->base;
}

static void caret(canvas *cv, const dl_item *d, int32_t x, int32_t base)
{
	if (x >= d->x && x < d->x + d->w)
		cv_fill(cv, x, base - d->font->ascent, 1, d->font->ascent + d->font->descent, 0x000000);
}

/* a control's placeholder, as solid as the control (op), not its text */
static void hint(canvas *cv, const dl_item *d, const char *ph, int32_t base, unsigned op)
{
	VLINE v;
	unsigned a = item_a;
	item_a = op;
	if (vline(&v, d->font, ph, (long) strlen(ph))) {
		vdraw(cv, &v, d->font, ph, line_x(d, &v, 0), base, 0x909090);
		vfree(&v);
	}
	item_a = a;
}

/* a field: one line, scrolled sideways (c->first, px) to show the caret */
static void field(canvas *cv, const dl_item *d, fctl *c, unsigned op)
{
	char *own;
	long tl;
	const char *t = shown(c, &tl, &own), *ph = form_placeholder(c);
	int32_t base = first_base(d), room = d->w - 6, cx;
	VLINE v;
	if (tl == 0 && ph != NULL) {
		hint(cv, d, ph, base, op);
		if (c->focus)
			caret(cv, d, d->x + 2, base);
		free(own);
		return;
	}
	if (!vline(&v, d->font, t, tl)) {
		free(own);
		return;
	}
	cx = vcaret(&v, to_shown(c, c->cur));
	if (v.x[v.n] <= room) {
		c->first = 0;
	} else if (c->focus) {
		if (cx - c->first > room)
			c->first = cx - room;
		if (cx < c->first)
			c->first = cx;
		if (c->first > v.x[v.n] - room)
			c->first = v.x[v.n] - room;
	}
	vdraw(cv, &v, d->font, t, line_x(d, &v, c->first), base, d->rgb & 0xffffff);
	if (c->focus)
		caret(cv, d, line_x(d, &v, c->first) + cx, base);
	vfree(&v);
	free(own);
}

/* a textarea: its lines from c->first, scrolled to show the caret */
static void textarea(canvas *cv, const dl_item *d, fctl *c, unsigned op)
{
	const char *t = c->v ? c->v : "", *ph = form_placeholder(c);
	long *st;
	int nl, k, cl, rows = (d->h - 4) / (int32_t) d->len;
	int32_t base = first_base(d), step = (int32_t) d->len;
	VLINE v;
	if (c->len == 0 && ph != NULL) {
		hint(cv, d, ph, base, op);
		if (c->focus)
			caret(cv, d, d->x + 2, base);
		return;
	}
	nl = lines_of(d, c, &st);
	if (nl == 0) {
		free(st);
		return;
	}
	if (rows < 1)
		rows = 1;
	cl = line_of(st, nl, c->cur);
	if (c->focus) {
		if (c->first > cl)
			c->first = cl;
		if (cl >= c->first + rows)
			c->first = cl - rows + 1;
	}
	if (c->first > nl - 1)
		c->first = nl - 1;
	if (c->first < 0)
		c->first = 0;
	for (k = (int) c->first; k < nl && k < c->first + rows; k++, base += step) {
		long e = line_end(t, st, nl, k, c->len);
		if (!vline(&v, d->font, t + st[k], e - st[k]))
			break;
		vdraw(cv, &v, d->font, t + st[k], line_x(d, &v, 0), base, d->rgb & 0xffffff);
		if (c->focus && k == cl)
			caret(cv, d, line_x(d, &v, 0) + vcaret(&v, c->cur - st[k]), base);
		vfree(&v);
	}
	free(st);
}

/* a label, at x or (centred) in the middle of the item */
static void label(canvas *cv, const dl_item *d, const char *s, int32_t x, int centre)
{
	VLINE v;
	if (!vline(&v, d->font, s, (long) strlen(s)))
		return;
	if (centre)
		x = d->x + (d->w - v.x[v.n]) / 2;
	vdraw(cv, &v, d->font, s, x, d->y + d->base, d->rgb & 0xffffff);
	vfree(&v);
}

static void control(canvas *cv, const dl_item *d, fctl *c)
{
	char lab[256];
	/* its text and caret as solid as its colour (ink; caret-color is
	   the text's), the rest as the control (item_a, its opacity): a
	   box's tick and a button's dot, which browsers draw in colours of
	   their own, a placeholder, the keyboard's frame */
	unsigned op = item_a;
	item_a = c->kind == FC_CHECKBOX || c->kind == FC_RADIO ? op : drawn_a(cv, d->ink >> 24);
	clip_x0 = d->x - cv->ox;
	clip_x1 = d->x + d->w - cv->ox;
	switch (c->kind) {
	case FC_TEXT:
	case FC_PASSWORD:
		field(cv, d, c, op);
		break;
	case FC_TEXTAREA:
		textarea(cv, d, c, op);
		break;
	case FC_SELECT: {
		static const char arrow[] = "\xe2\x96\xbc";
		int32_t aw = (int32_t) font_width(d->font, arrow, 3);
		form_label(c, lab, sizeof lab);
		clip_x1 = d->x + d->w - aw - 4 - cv->ox;
		label(cv, d, lab, d->x + 3, 0);
		clip_x1 = d->x + d->w - cv->ox;
		cv_text(cv, d->font, d->x + d->w - aw - 3, d->y + d->base, arrow, 3, d->rgb & 0xffffff, 0);
		break;
	}
	default:		/* buttons, boxes */
		form_label(c, lab, sizeof lab);
		label(cv, d, lab, 0, 1);
	}
	clip_x0 = -0x7fffffff;
	clip_x1 = 0x7fffffff;
	item_a = op;
	/* the keyboard on a button, a box, a list: a frame inside */
	if (c->focus && !form_typed(c)) {
		cv_fill(cv, d->x + 1, d->y + 1, d->w - 2, 1, 0x000000);
		cv_fill(cv, d->x + 1, d->y + d->h - 2, d->w - 2, 1, 0x000000);
		cv_fill(cv, d->x + 1, d->y + 1, 1, d->h - 2, 0x000000);
		cv_fill(cv, d->x + d->w - 2, d->y + 1, 1, d->h - 2, 0x000000);
	}
}

long cv_ctl_hit(const dl_item *d, fctl *c, int32_t px, int32_t py)
{
	VLINE v;
	long r = c->len;
	if (d->len) {
		long *st;
		int nl = lines_of(d, c, &st), k;
		if (nl > 0) {
			k = (int) c->first + (py - (first_base(d) - d->font->ascent)) / (int32_t) d->len;
			if (k < 0)
				k = 0;
			if (k >= nl)
				k = nl - 1;
			if (vline(&v, d->font, c->v + st[k], line_end(c->v, st, nl, k, c->len) - st[k])) {
				r = st[k] + vhit(&v, px - line_x(d, &v, 0));
				vfree(&v);
			}
		}
		free(st);
	} else {
		char *own;
		long tl;
		const char *t = shown(c, &tl, &own);
		if (vline(&v, d->font, t, tl)) {
			r = from_shown(c, vhit(&v, px - line_x(d, &v, c->first)));
			vfree(&v);
		}
		free(own);
	}
	return r;
}

long cv_ctl_line(const dl_item *d, fctl *c, int how)
{
	long *st, r = c->cur;
	int nl, cl;
	VLINE v;
	if (!d->len)
		return how == CV_LINE_START || how == CV_UP ? 0 : c->len;
	nl = lines_of(d, c, &st);
	if (nl == 0) {
		free(st);
		return c->cur;
	}
	cl = line_of(st, nl, c->cur);
	if (how == CV_LINE_START) {
		r = st[cl];
	} else if (how == CV_LINE_END) {
		r = line_end(c->v, st, nl, cl, c->len);
	} else {
		int to = cl + (how == CV_UP ? -1 : 1);
		if (to < 0) {
			r = 0;
		} else if (to >= nl) {
			r = c->len;
		} else if (vline(&v, d->font, c->v + st[cl], line_end(c->v, st, nl, cl, c->len) - st[cl])) {
			int32_t x = line_x(d, &v, 0) + vcaret(&v, c->cur - st[cl]);
			vfree(&v);
			if (vline(&v, d->font, c->v + st[to], line_end(c->v, st, nl, to, c->len) - st[to])) {
				r = st[to] + vhit(&v, x - line_x(d, &v, 0));
				vfree(&v);
			}
		}
	}
	free(st);
	return r;
}

/* the first of a run of tiles from a (one every step) at or before lo */
static int32_t first_tile(int32_t a, int32_t step, int32_t lo)
{
	int32_t m;
	if (a <= lo) {
		m = (lo - a) % step;
		return lo - m;
	}
	m = (a - lo) % step;
	return m ? lo - (step - m) : lo;
}

/* a background picture: its tiles where the item and the canvas meet.
   An opaque one's first rows of tiles are drawn, the rest copied from
   the rows a tile above */
static void tiles(canvas *c, const dl_item *d)
{
	const dl_tile *t = d->tile;
	const PIC *pic = NULL;
	const uint8_t *pens = NULL;
	canvas sub;
	int32_t x0 = d->x, y0 = d->y, x1 = d->x + d->w, y1 = d->y + d->h, sx, sy, tx, ty, ylim;
	int bytes = c->bpp / 8;
	if (t == NULL || c->picture == NULL || !c->picture(c->ud, d->img, t->w, t->h, &pic, &pens) ||
	    pic == NULL || (c->bpp == 8 && pens == NULL) || pic->w <= 0 || pic->h <= 0)
		return;
	if (x0 < c->ox)
		x0 = c->ox;
	if (y0 < c->oy)
		y0 = c->oy;
	if (x1 > c->ox + c->w)
		x1 = c->ox + c->w;
	if (y1 > c->oy + c->h)
		y1 = c->oy + c->h;
	if (x0 >= x1 || y0 >= y1)
		return;
	sub = *c;
	sub.pix = c->pix + (long) (y0 - c->oy) * c->stride + (long) (x0 - c->ox) * bytes;
	sub.ox = x0;
	sub.oy = y0;
	sub.w = x1 - x0;
	sub.h = y1 - y0;
	sx = d->x + t->dx;
	sy = d->y + t->dy;
	if (t->rx)
		sx = first_tile(sx, pic->w, x0);
	if (t->ry)
		sy = first_tile(sy, pic->h, y0);
	/* drawn: all of them, or an opaque picture's to a tile below y0 (one
	   mixed with what is under it is drawn whole) */
	ylim = t->ry && pic->alpha == NULL && item_a == 255 && y1 > y0 + pic->h ? y0 + pic->h : y1;
	for (ty = sy; ty < ylim; ty += pic->h) {
		for (tx = sx; tx < x1; tx += pic->w) {
			cv_picture(&sub, pic, pens, tx, ty);
			if (!t->rx)
				break;
		}
		if (!t->ry)
			break;
	}
	for (ty = ylim; ty < y1; ty++)
		memcpy(sub.pix + (long) (ty - y0) * c->stride, sub.pix + (long) (ty - y0 - pic->h) * c->stride,
		       (size_t) (x1 - x0) * bytes);
}

/* ---------------------------------------------------------------- gradients */

/* CSS gradients (CSS Images 3 and 4) as background items (DL_GRAD): the
   stops placed on the gradient line, ray or turn (grad_place) and the
   colour between them (stop_colour), then drawn as the machine can
   afford: the Falcon's every kind, dithered (below), the ST's linear
   ones at right angles a row or a column at a time. */

/* (x, y, w, h) in a colour (0xAARRGGBB), as solid as its alpha and the
   item's: a gradient where it can't be drawn (its stops alike, a
   repeating one of no length, out of memory), or a run of the ST's rows */
static void grad_fill(canvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t argb)
{
	unsigned a = item_a;
	item_a = drawn_a(c, (a * (argb >> 24) + 127) / 255);
	if (item_a)
		cv_fill(c, x, y, w, h, argb & 0xffffff);
	item_a = a;
}

#ifdef EW_ST
#define GSTOPS 32			/* (the CSS engine's most) */
#else
#define GSTOPS 64
#endif

/* the stops as drawn: their places (24.8 px along the line or the ray,
   65536ths of a turn; a ramp's in its span) and colours (0xAARRGGBB); a
   hint has a place only */
typedef struct gstops {
	int ns;
	int32_t pos[GSTOPS];
	uint32_t col[GSTOPS];
	uint8_t hint[GSTOPS];
} gstops;

#ifndef EW_ST
/* (the ST edition mixes the stops on either side of a hint evenly: the
   hint's power curve is 700 bytes of its code) */

/* log2 of 1 + k/32 and 2 to the k/32, 16.16 */
static const int32_t log2_tab[33] = {
	0, 2909, 5732, 8473, 11136, 13727, 16248, 18704,
	21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346,
	38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207,
	52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047,
	65536
};
static const int32_t exp2_tab[33] = {
	65536, 66971, 68438, 69936, 71468, 73032, 74632, 76266,
	77936, 79642, 81386, 83169, 84990, 86851, 88752, 90696,
	92682, 94711, 96785, 98905, 101070, 103283, 105545, 107856,
	110218, 112631, 115098, 117618, 120194, 122825, 125515, 128263,
	131072
};

/* log2 of x / 65536 (x 1..65536), 16.16: 0 or less */
static int32_t log2_16(uint32_t x)
{
	int e = 16, k;
	uint32_t f;
	while (x < 65536) {
		x <<= 1;
		e--;
	}
	/* x / 65536 in 1..2: 32 steps of 2048 */
	f = x - 65536;
	k = (int) (f >> 11);
	return (int32_t) ((e - 16) * 65536 + log2_tab[k] +
			  (int32_t) (((log2_tab[k + 1 > 32 ? 32 : k + 1] - log2_tab[k]) * (int32_t) (f & 2047)) >> 11));
}

/* 2 to the y (16.16, 0 or less), 16.16 */
static uint32_t exp2_16(int32_t y)
{
	int32_t n = (-y) >> 16, f;
	uint32_t r;
	int k;
	if (y >= 0)
		return 65536;
	if (n >= 16)
		return 0;
	f = y + ((n + 1) << 16);		/* in 0..65536 */
	k = (int) (f >> 11);
	r = (uint32_t) (exp2_tab[k] + (((exp2_tab[k + 1 > 32 ? 32 : k + 1] - exp2_tab[k]) * (f & 2047)) >> 11));
	return r >> (n + 1);
}
#endif

/* the colour at p (as the stops' positions) between the stops on either
   side (CSS Images 4, 3.5): mixed premultiplied; a hint between them
   puts the half and half mix there, a power curve either side (3.5.3;
   not the ST edition's, above) */
static uint32_t stop_colour(const gstops *r, int32_t p)
{
	int i, a = -1, h = -1, b = -1;
	uint32_t c1, c2, w, al, A;
	int32_t s1, s2;
	unsigned k, ch[3];
	for (i = 0; i < r->ns; i++) {
		if (r->hint[i]) {
			h = i;
			continue;
		}
		if (r->pos[i] > p) {
			b = i;
			break;
		}
		a = i;
		h = -1;
	}
	if (a < 0)
		return r->col[b];
	if (b < 0)
		return r->col[a];
	s1 = r->pos[a];
	s2 = r->pos[b];
	{
		/* p's share of the way, 65536ths: in 32 bits, the span made
		   under 2^15 (on the 68000 a 64-bit division is a loop of 64
		   turns, and the ST's rows each need one) */
		uint32_t u = (uint32_t) (p - s1), v = (uint32_t) (s2 - s1);
		while (v >= 32768) {
			u >>= 1;
			v >>= 1;
		}
		w = (u << 16) / v;
	}
#ifdef EW_ST
	(void) h;
#else
	if (h >= 0) {
		/* (a hint before its first stop: one placed among stops spread
		   after it) */
		int32_t hp = r->pos[h] <= s1 ? 0 : (int32_t) ((int64_t) (r->pos[h] - s1) * 65536 / (s2 - s1));
		if (hp <= 0)
			w = 65536;
		else if (hp >= 65536)
			w = 0;
		else if (w > 0) {
			/* w = f ^ (log .5 / log hp) */
			int32_t lh = log2_16((uint32_t) hp), lf = log2_16(w);
			w = lh == 0 ? 0 : exp2_16((int32_t) ((int64_t) lf * 65536 / -lh));
		}
	}
#endif
	c1 = r->col[a];
	c2 = r->col[b];
	w = (w + 128) >> 8;			/* 0..256 */
	if ((c1 & c2) >> 24 == 255) {
		/* both solid: the same, with no division */
		for (k = 0; k < 3; k++) {
			unsigned sh = 16 - 8 * k;
			ch[k] = (unsigned) (((c1 >> sh & 255) * (256 - w) + (c2 >> sh & 255) * w + 128) >> 8);
		}
		return 0xff000000UL | ch[0] << 16 | ch[1] << 8 | ch[2];
	}
	A = (c1 >> 24) * (256 - w) + (c2 >> 24) * w;
	if (A == 0)
		return 0;
	al = (A + 128) >> 8;
	for (k = 0; k < 3; k++) {
		unsigned sh = 16 - 8 * k;
		ch[k] = (unsigned) (((c1 >> sh & 255) * (c1 >> 24) * (256 - w) + (c2 >> sh & 255) * (c2 >> 24) * w +
				     A / 2) / A);
	}
	return al << 24 | ch[0] << 16 | ch[1] << 8 | ch[2];
}

/* the stops' places, fixed up (CSS Images 3, 3.4.3): the first colour
   stop's at the start and the last's at the end when not given, none
   before one before it, the colour stops between two placed ones spread
   evenly between them; their colours into r. len: the line's length
   (or the ray's, or a turn), what a percentage is of */
static void grad_place(const ec_grad *g, int32_t len, gstops *r)
{
	int i, j, k, first = -1, last = -1;
	int32_t most = INT32_MIN;
	r->ns = g->n;
	for (i = 0; i < g->n; i++) {
		const ec_gstop *s = &g->s[i];
		if (s->flags & EC_GS_AT) {
			/* (kept within 2^29: no difference of two overflows) */
			int64_t p = (s->at.px >> 2) + (int64_t) s->at.pct * len / 102400;
			r->pos[i] = (int32_t) (p < -(1L << 29) ? -(1L << 29) : p > (1L << 29) ? (1L << 29) : p);
		} else {
			r->pos[i] = INT32_MIN;
		}
		r->col[i] = s->flags & EC_GS_HINT ? 0 : s->argb;
		r->hint[i] = (uint8_t) !!(s->flags & EC_GS_HINT);
		if (!r->hint[i]) {
			if (first < 0)
				first = i;
			last = i;
		}
	}
	if (r->pos[first] == INT32_MIN)
		r->pos[first] = 0;
	if (r->pos[last] == INT32_MIN)
		r->pos[last] = len;
	for (i = 0; i < g->n; i++)
		if (r->pos[i] != INT32_MIN) {
			if (r->pos[i] < most)
				r->pos[i] = most;
			most = r->pos[i];
		}
	for (i = first; i < last; i = j) {
		int gap = 0;
		for (j = i + 1; r->hint[j] || r->pos[j] == INT32_MIN; j++)
			if (!r->hint[j])
				gap++;
		for (k = i + 1, gap++; k < j; k++)
			if (!r->hint[k]) {
				int n = 0, m;
				for (m = i + 1; m <= k; m++)
					n += !r->hint[m];
				r->pos[k] = r->pos[i] + (int32_t) ((int64_t) (r->pos[j] - r->pos[i]) * n / gap);
			}
	}
}

#ifdef EW_ST
/* The ST edition: a linear gradient at right angles (to a side, or an
   angle a multiple of 90deg, as most of the web's are) a row or a column
   at a time, each in its colour as a fill is drawn (the grey pattern in
   monochrome, the nearest pen in 4 and 16 colours), rows the screen
   shows alike one fill; any other in its colours' average. Rows alike
   are of one key, and a run's colour is its key's alone, not its first
   row's: a run starts where the band or the scroll's rows start, and a
   colour from there would change with them (seams). The key: a solid
   colour's grey level in monochrome (the pattern is all a fill draws),
   its 5-6-5 value in true colour, else its 12-bit colour (pen()'s), the
   colour then the 12-bit one's (each nibble twice: black and white stay
   so). This is 2 KB of 68000 code; the Falcon's drawing, below, would be
   7 to 11 KB, and the 1 MB ST had 11 KB to spare before a page as big as
   tests/layout/tables.html no longer fitted (DESIGN.md, CSS gradients) */
static void grad(canvas *c, const dl_item *d)
{
	const ec_grad *g = d->img;
	const dl_tile *t = d->tile;
	gstops s;
	int32_t a0, a1, b0, b1, at, step, len, p0, span, i, run;
	int dir = -1, down, sh, mono, lv;
	uint32_t prev = 0, col = 0, pcol = 0;
	uint8_t dk, lt;
	if (t == NULL || g == NULL || g->n == 0 || t->w <= 0 || t->h <= 0)
		return;
	/* its way: 0 up, 1 right, 2 down, 3 left */
	if (g->type == EC_GRAD_LINEAR && !g->approx) {
		if (g->to == EC_TO_TOP || g->to == EC_TO_RIGHT || g->to == EC_TO_BOTTOM || g->to == EC_TO_LEFT)
			dir = g->to == EC_TO_TOP ? 0 : g->to == EC_TO_RIGHT ? 1 : g->to == EC_TO_BOTTOM ? 2 : 3;
		else if (!g->to && g->angle % (90 * 1024) == 0)
			dir = (int) (g->angle / (90 * 1024)) & 3;
	}
	if (dir < 0) {
		grad_fill(c, d->x, d->y, d->w, d->h, grad_average(g, 0));
		return;
	}
	down = !(dir & 1);
	/* along it: the rows (or columns) a0..a1 on the canvas, its tiles'
	   from at, a step apart; across it b0..b1 (the item is its one tile
	   where it isn't repeated) */
	a0 = down ? d->y : d->x;
	a1 = a0 + (down ? d->h : d->w);
	b0 = down ? d->x : d->y;
	b1 = b0 + (down ? d->w : d->h);
	at = down ? d->y + t->dy : d->x + t->dx;
	step = down ? t->h : t->w;
	if (a0 < (down ? c->oy : c->ox))
		a0 = down ? c->oy : c->ox;
	if (a1 > (down ? c->oy + c->h : c->ox + c->w))
		a1 = down ? c->oy + c->h : c->ox + c->w;
	if (a0 >= a1)
		return;
	if (down ? t->ry : t->rx)
		at = first_tile(at, step, a0);
	/* (a tile of 2^22 px or more: its places a 2^sh'th as fine, within
	   32 bits) */
	for (sh = 0; (step >> sh) >= (1L << 22); sh++)
		;
	len = (step >> sh) * 256;
	grad_place(g, len, &s);
	p0 = s.pos[0];				/* (no hint is first or last) */
	span = s.pos[s.ns - 1] - p0;
	if (g->repeat && span < 256) {
		/* a period of no length (CSS Images 3, 3.6) */
		grad_fill(c, d->x, d->y, d->w, d->h, grad_average(g, 0));
		return;
	}
	mono = c->bpp == 8 && c->shade != NULL && c->shade(c->ud, 0, &dk, &lt) >= 0;
	for (i = run = a0; i <= a1; i++) {
		uint32_t key = 0;
		if (i < a1) {
			/* the row's (or column's) middle on the line: its colour */
			int32_t q = ((i - at) % step >> sh) * 256 + 128;
			uint32_t argb;
			if (dir == 0 || dir == 3)
				q = len - q;	/* (from the bottom, or the right) */
			if (g->repeat)
				q = p0 + ((q - p0) % span + span) % span;
			argb = stop_colour(&s, q);
			key = argb & 0xf0f0f0f0UL;
			col = key | key >> 4;
			if (argb >> 24 == 255) {
				/* (keys none of the 12-bit ones are) */
				if (c->bpp == 16)
					key = 0x01000000UL | RGB565(argb);
				else if (mono && (lv = c->shade(c->ud, argb & 0xffffff, &dk, &lt)) >= 0)
					key = 0x02000000UL | (uint32_t) lv;
				if (key >> 28 == 0)
					col = argb;
			}
			if (i == a0) {
				prev = key;
				pcol = col;
			}
			if (key == prev)
				continue;
		}
		if (down)
			grad_fill(c, b0, run, b1 - b0, i - run, pcol);
		else
			grad_fill(c, run, b0, i - run, b1 - b0, pcol);
		run = i;
		prev = key;
		pcol = col;
	}
}

#else
/* The Falcon's: each pixel's place along the gradient line (linear),
   the ray (radial: the ellipse's distance) or the turn (conic) picks an
   entry of a ramp, the gradient's colours at RAMP_MAX points at most and
   no more than one a pixel, worked out once and kept for the last few
   gradients drawn (the window is drawn in bands of 32 rows, each band
   drawing every gradient it meets). An entry is its colour made for the
   screen as a 4 x 4 pattern on the canvas's ordered thresholds, fixed to
   the page as fills' patterns are, so that a gradient drawn in bands or
   scrolled has no seams (Floyd-Steinberg, the pictures' dither, carries
   each pixel's error to the next and so can't be drawn a band at a time
   without seams, and keeping its pens for a body's gradient, as a
   picture's are kept, would take a megabyte): 16 pens that make the
   colour together (Knoll's pattern dither: each the screen's nearest to
   the colour plus the error the pens before it left, sorted by
   brightness), the fills' grey pattern on a monochrome screen, or in
   true colour 16 5-6-5 values a step apart, so that a gentle gradient
   isn't banded. A linear gradient at right angles is drawn a row at a
   time (one entry: a long at a time) or a column at a time (the entries
   worked out once for all its rows); the others a pixel at a time, from
   an entry index stepped along each row. A translucent entry is mixed
   with what is under it in true colour, and at 8 bits drawn where its
   alpha is above the transposed thresholds (as pictures at an opacity
   are). */
#define RAMP_MAX 256
#define RAMPS 4

/* what a ramp's entries are spread over: the span between the first and
   last stops (an entry in the middle of each step, and one more at each
   end: the colours before and after it, so that a sharp edge at either
   end falls where it is), a repeating gradient's period, a turn */
enum { R_SPAN, R_REPEAT, R_TURN };

typedef struct ramp {
	/* what it was made of: the stops' positions (along the span, 0..65536;
	   R_TURN: in the turn, 65536ths) and colours, the item's alpha, the
	   screen */
	int n;				/* entries; 0: none */
	int mode, rep;
	unsigned a;
	int bpp;
	uint8_t (*pen)(void *, uint32_t);
	int (*shade)(void *, uint32_t, uint8_t *, uint8_t *);
	const uint8_t (*pal)[3];
	void *ud;
	gstops s;
	/* what it is */
	unsigned long used;
	int solid;			/* every entry is */
	uint32_t avg;			/* its colour where it can't be drawn */
	uint16_t *v;			/* n x 16: each entry's pattern, by threshold (at 16
					   bits in memory order) */
	uint32_t *spread;		/* 16 bits: an entry's 5-6-5 colour spread out (SPREAD),
					   times its alpha */
	uint8_t *lvl;			/* how solid: 8 bits in 16ths, 16 bits in 32nds */
	uint32_t *lu;			/* 16 bits: a translucent entry's last pixel under
					   it (0x10000: none yet) */
	uint16_t *lo;			/* and what that became (both in memory order) */
	uint8_t *made;
} ramp;

static ramp ramps[RAMPS];
static unsigned long ramp_clock;
static uint8_t *gidx;			/* a row's entries */
static int32_t gidx_n;
static uint32_t *gcol;			/* 16 bits, a translucent gradient across: each
					   column's last pixel under it (0x10000: none),
					   then what that became */
static int32_t gcol_n;

/* sin of 0..90 degrees, 1.14 fixed point */
static const int16_t sin_tab[91] = {
	0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126,
	3406, 3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402,
	6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397,
	9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982,
	12176, 12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044,
	14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296, 15396, 15491,
	15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225, 16262,
	16294, 16322, 16344, 16362, 16374, 16382, 16384
};

/* sin of an angle in degrees (22.10), 1.14 fixed point */
static int32_t sin14(int32_t deg)
{
	int32_t d, f, s;
	int neg = 0;
	deg %= 360 * 1024;
	if (deg < 0)
		deg += 360 * 1024;
	if (deg >= 180 * 1024) {
		deg -= 180 * 1024;
		neg = 1;
	}
	if (deg > 90 * 1024)
		deg = 180 * 1024 - deg;
	d = deg >> 10;
	f = deg & 1023;
	s = sin_tab[d];
	if (f)
		s += ((sin_tab[d + 1] - s) * f) >> 10;
	return neg ? -s : s;
}

/* the square root of v, rounded down */
static uint32_t isqrt64(uint64_t v)
{
	uint64_t r = 0, b = (uint64_t) 1 << 62;
	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return (uint32_t) r;
}

static uint32_t isqrt32(uint32_t v)
{
	uint32_t r = 0, b = 1UL << 30;
	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

/* 8 bits: the 16 pens of a colour's pattern (Knoll's pattern dither):
   each the screen's nearest to the colour plus the error the pens before
   it left, so that together they make it; sorted by brightness, so that
   the thresholds spread the light and the dark ones evenly */
static void knoll(canvas *c, uint32_t rgb, uint16_t *v)
{
	int r = (int) (rgb >> 16 & 255), g = (int) (rgb >> 8 & 255), b = (int) (rgb & 255);
	int er = 0, eg = 0, eb = 0, k, j, lum[16];
	for (k = 0; k < 16; k++) {
		int wr = r + er, wg = g + eg, wb = b + eb, l;
		const uint8_t *q;
		uint8_t p;
		wr = wr < 0 ? 0 : wr > 255 ? 255 : wr;
		wg = wg < 0 ? 0 : wg > 255 ? 255 : wg;
		wb = wb < 0 ? 0 : wb > 255 ? 255 : wb;
		p = c->pen(c->ud, (uint32_t) wr << 16 | (uint32_t) wg << 8 | (uint32_t) wb);
		q = c->pal[p];
		er += r - q[0];
		eg += g - q[1];
		eb += b - q[2];
		l = 2 * q[0] + 5 * q[1] + q[2];
		for (j = k; j > 0 && lum[j - 1] > l; j--) {
			lum[j] = lum[j - 1];
			v[j] = v[j - 1];
		}
		lum[j] = l;
		v[j] = p;
	}
}

/* a 5-6-5 value in memory order (the Falcon's: high byte first) */
static uint16_t mem16(unsigned v)
{
	uint8_t b[2];
	uint16_t r;
	b[0] = (uint8_t) (v >> 8);
	b[1] = (uint8_t) v;
	memcpy(&r, b, 2);
	return r;
}

/* entry k of the ramp, made for the screen */
static void ramp_make(canvas *c, ramp *r, int k)
{
	uint16_t *v = r->v + (k << 4);
	uint32_t argb, rgb;
	unsigned a;
	int t;
	if (r->mode == R_SPAN && (k == 0 || k == r->n - 1)) {
		argb = r->s.col[k ? r->s.ns - 1 : 0];	/* (no hint is first or last) */
	} else {
		int32_t p = r->mode == R_SPAN ? (int32_t) ((((int64_t) k - 1) * 2 + 1) * 32768 / (r->n - 2)) :
			    r->mode == R_REPEAT ? (int32_t) ((((int64_t) k << 17) + 65536) / (2 * r->n)) :
			    k * (65536 / RAMP_MAX) + 32768 / RAMP_MAX;
		if (r->mode == R_TURN && r->rep) {
			/* the stops again every period */
			int32_t p0 = r->s.pos[0], per = r->s.pos[r->s.ns - 1] - p0;
			p = p0 + (int32_t) (((p - p0) % per + per) % per);
		}
		argb = stop_colour(&r->s, p);
	}
	a = r->a == 255 ? argb >> 24 : ((argb >> 24) * r->a + 127) / 255;
	rgb = argb & 0xffffff;
	if (c->bpp == 16) {
		unsigned cr = rgb >> 16 & 255, cg = rgb >> 8 & 255, cb = rgb & 255;
		for (t = 0; t < 16; t++) {
			unsigned r5 = (16 * cr + 8 * t + 4) >> 7, g6 = (16 * cg + 4 * t + 2) >> 6, b5 = (16 * cb + 8 * t + 4) >> 7;
			v[t] = mem16((r5 > 31 ? 31 : r5) << 11 | (g6 > 63 ? 63 : g6) << 5 | (b5 > 31 ? 31 : b5));
		}
		r->lvl[k] = (uint8_t) ((a * 32 + 127) / 255);
		r->spread[k] = SPREAD(RGB565(rgb)) * r->lvl[k];
	} else {
		uint8_t dark, light;
		int level;
		if (c->shade != NULL && (level = c->shade(c->ud, rgb, &dark, &light)) >= 0) {
			/* the fills' pattern */
			for (t = 0; t < 16; t++)
				v[t] = t < level ? light : dark;
		} else if (c->pal != NULL) {
			knoll(c, rgb, v);
		} else {
			uint8_t p = c->pen(c->ud, rgb);
			for (t = 0; t < 16; t++)
				v[t] = p;
		}
		r->lvl[k] = (uint8_t) (c->pal != NULL ? (a * 16 + 127) / 255 : a >= 128 ? 16 : 0);
	}
	r->made[k] = 1;
}

/* the ramp for want's stops, kept or made (its entries made when first
   drawn); NULL when out of memory */
static ramp *ramp_get(canvas *c, const ramp *want)
{
	ramp *r, *old = ramps;
	size_t n;
	int i;
	for (r = ramps; r < ramps + RAMPS; r++) {
		if (r->n == want->n && r->mode == want->mode && r->rep == want->rep && r->s.ns == want->s.ns &&
		    r->a == want->a && r->bpp == c->bpp && r->pen == c->pen && r->shade == c->shade &&
		    r->pal == c->pal && r->ud == c->ud && !memcmp(r->s.pos, want->s.pos, want->s.ns * sizeof r->s.pos[0]) &&
		    !memcmp(r->s.col, want->s.col, want->s.ns * sizeof r->s.col[0]) &&
		    !memcmp(r->s.hint, want->s.hint, (size_t) want->s.ns)) {
			r->used = ++ramp_clock;
			return r;
		}
		if (r->used < old->used)
			old = r;
	}
	r = old;
	free(r->v);
	memcpy(r, want, sizeof *r);
	r->bpp = c->bpp;
	r->pen = c->pen;
	r->shade = c->shade;
	r->pal = c->pal;
	r->ud = c->ud;
	r->used = ++ramp_clock;
	n = (size_t) r->n;
	r->v = malloc(n * 16 * sizeof *r->v + n * sizeof *r->spread + n * sizeof *r->lu + n * sizeof *r->lo + 2 * n);
	if (r->v == NULL) {
		r->n = 0;
		return NULL;
	}
	r->spread = (uint32_t *) (void *) (r->v + n * 16);
	r->lu = r->spread + n;
	r->lo = (uint16_t *) (void *) (r->lu + n);
	r->lvl = (uint8_t *) (r->lo + n);
	r->made = r->lvl + n;
	memset(r->made, 0, n);
	for (i = 0; i < r->n; i++)
		r->lu[i] = 0x10000;
	r->solid = r->a == 255;
	for (i = 0; i < r->s.ns; i++)
		if (!r->s.hint[i] && (r->s.col[i] >> 24) != 255)
			r->solid = 0;
	return r;
}

static void ramp_all(canvas *c, ramp *r)
{
	int k;
	for (k = 0; k < r->n; k++)
		if (!r->made[k])
			ramp_make(c, r, k);
}

/* n longs at q, of w0 and w1 by turns: four a turn, counted down (on
   the 68030 a move each and two to count) */
static inline void longs(uint32_t *q, int32_t n, uint32_t w0, uint32_t w1)
{
	int32_t k;
	for (k = n >> 2; k > 0; k--) {
		q[0] = w0;
		q[1] = w1;
		q[2] = w0;
		q[3] = w1;
		q += 4;
	}
	for (k = n & 3; k > 0; k -= 2) {
		*q++ = w0;
		if (k > 1)
			*q++ = w1;
	}
}

/* n pixels at o in a pattern of four (pixel i: p[i & 3], memory order
   at 16 bits), a long at a time where they are aligned */
static void pat_run(uint8_t *o, int bpp, int32_t n, const uint16_t *p)
{
	int32_t i = 0;
	uint32_t w0, w1;
	if (bpp == 8) {
		uint8_t b[4];
		while (i < n && ((uintptr_t) (o + i) & 3)) {
			o[i] = (uint8_t) p[i & 3];
			i++;
		}
		if (n - i >= 4) {
			b[0] = (uint8_t) p[i & 3];
			b[1] = (uint8_t) p[(i + 1) & 3];
			b[2] = (uint8_t) p[(i + 2) & 3];
			b[3] = (uint8_t) p[(i + 3) & 3];
			memcpy(&w0, b, 4);
			longs((uint32_t *) (void *) (o + i), (n - i) >> 2, w0, w0);
			i += (n - i) & ~3;
		}
		for (; i < n; i++)
			o[i] = (uint8_t) p[i & 3];
	} else {
		uint16_t *s = (uint16_t *) (void *) o, h[4];
		if (i < n && ((uintptr_t) s & 2)) {
			s[0] = p[0];
			i++;
		}
		if (n - i >= 4) {
			h[0] = p[i & 3];
			h[1] = p[(i + 1) & 3];
			h[2] = p[(i + 2) & 3];
			h[3] = p[(i + 3) & 3];
			memcpy(&w0, h, 4);
			memcpy(&w1, h + 2, 4);
			longs((uint32_t *) (void *) (s + i), ((n - i) >> 2) * 2, w0, w1);
			i += (n - i) & ~3;
		}
		for (; i < n; i++)
			s[i] = p[i & 3];
	}
}

/* while a tile's entries are worked out (grad_map): where they go, a
   row of the tile w long, in place of being drawn */
static uint8_t *gmap, *gmap_buf;
static int32_t gmap_w, gmap_n;
static int gmap_fail;

/* the least multiple of v and 4: where tiles of v and the pattern come
   round together */
static int32_t lcm4(int32_t v)
{
	return v % 4 == 0 ? v : v % 2 == 0 ? 2 * v : 4 * v;
}

/* canvas row y from x, n pixels, all of entry k */
static void grad_run(canvas *c, ramp *r, int k, int32_t x, int32_t y, int32_t n)
{
	const uint8_t *b = bayer[(y + c->oy) & 3];
	const uint16_t *v;
	uint8_t *o = c->pix + (long) y * c->stride + (c->bpp == 16 ? 2 * x : x);
	int32_t i;
	if (n <= 0)
		return;
	if (gmap != NULL) {
		memset(gmap + y * gmap_w + x, k, (size_t) n);
		return;
	}
	if (!r->made[k])
		ramp_make(c, r, k);
	v = r->v + (k << 4);
	if (r->solid || r->lvl[k] == (c->bpp == 16 ? 32 : 16)) {
		uint16_t p[4];
		for (i = 0; i < 4; i++)
			p[i] = v[b[(x + c->ox + i) & 3]];
		pat_run(o, c->bpp, n, p);
		return;
	}
	if (r->lvl[k] == 0)
		return;
	if (c->bpp == 16) {
		/* mixed as a translucent fill is (mix16, the entry's alpha in
		   32nds): a run of pixels as the last ones takes their result,
		   a long at a time */
		mix.a5 = r->lvl[k];
		mix.n = r->spread[k];
		mix.in = 0x10000;
		mix_px(c, x, y, n, 1);
		return;
	}
	{
		/* its pens where its alpha is above the transposed thresholds:
		   a pattern of four put in a long at a time through its mask */
		uint8_t pb[4], mb[4], pp[4], mm[4];
		uint32_t P, M, *q, *qe;
		int j, any = 0;
		for (j = 0; j < 4; j++) {
			int32_t xi = x + c->ox + j;
			mb[j] = bayer[xi & 3][(y + c->oy) & 3] < r->lvl[k] ? 0xff : 0;
			pb[j] = (uint8_t) (v[b[xi & 3]] & mb[j]);
			any |= mb[j];
		}
		if (!any)
			return;
		for (i = 0; i < n && ((uintptr_t) (o + i) & 3); i++)
			if (mb[i & 3])
				o[i] = pb[i & 3];
		if (n - i >= 4) {
			for (j = 0; j < 4; j++) {
				pp[j] = pb[(i + j) & 3];
				mm[j] = (uint8_t) ~mb[(i + j) & 3];
			}
			memcpy(&P, pp, 4);
			memcpy(&M, mm, 4);
			q = (uint32_t *) (void *) (o + i);
			for (qe = q + ((n - i) >> 2); q < qe; q++)
				*q = (*q & M) | P;
			i += (n - i) & ~3;
		}
		for (; i < n; i++)
			if (mb[i & 3])
				o[i] = pb[i & 3];
	}
}

/* 16 bits: a translucent ramp's row, n pixels at s, entry idx[i] each,
   by runs of one entry (a pattern of dots on nothing and a fade are
   mostly runs): none drawn of no alpha, a solid one's pens put in (b
   the row's thresholds, ph the first pixel's place in them), the others
   mixed (mix16's sums), each entry's last pixel under it and what that
   became kept from run to run (lu, lo: what is under is mostly one
   colour, so an entry is mixed once, not at each of its runs a few
   pixels wide). A function of its own: its variables kept in
   registers */
static __attribute__((noinline)) void mix_row16(ramp *r, uint16_t *s, int32_t n, const uint8_t *idx,
						      const uint8_t *b, int32_t ph)
{
	int32_t i, e, j;
	for (i = 0; i < n; i = e) {
		unsigned k = idx[i], l = r->lvl[k];
		for (e = i + 1; e < n && idx[e] == k; e++)
			;
		if (l == 0)
			continue;
		if (l == 32) {
			const uint16_t *pv = r->v + (k << 4);
			for (j = i; j < e; j++)
				s[j] = pv[b[(ph + j) & 3]];
			continue;
		}
		{
			uint32_t lu = r->lu[k], sk = r->spread[k], f = 32u - l;
			uint16_t lo = r->lo[k];
			for (j = i; j < e; j++) {
				uint32_t u = s[j];
				if (u == lu) {
					s[j] = lo;
				} else {
					uint8_t *q = (uint8_t *) (s + j);
					unsigned w = (unsigned) q[0] << 8 | q[1];
					uint32_t m = ((sk + SPREAD(w) * f) >> 5) & 0x07E0F81FUL;
					w = (unsigned) (m | m >> 16);
					q[0] = (uint8_t) (w >> 8);
					q[1] = (uint8_t) w;
					lu = u;
					lo = s[j];
				}
			}
			r->lu[k] = lu;
			r->lo[k] = lo;
		}
	}
}

/* 16 bits: a row of a translucent gradient across, as mix_row16 but
   with what each column's pixel under became kept (gcol: its rows are
   alike, so a column is mixed again only where what is under it
   changes; a pixel is then a load, a compare and a store, as a
   translucent fill's) */
static __attribute__((noinline)) void mix_cols16(ramp *r, uint16_t *s, int32_t n, const uint8_t *idx,
						       const uint8_t *b, int32_t ph)
{
	uint32_t *cu = gcol;
	uint16_t *co = (uint16_t *) (void *) (gcol + n);
	int32_t i;
	for (i = 0; i < n; i++) {
		uint32_t u = s[i];
		unsigned k;
		if (u == cu[i]) {
			s[i] = co[i];
			continue;
		}
		k = idx[i];
		if (r->lvl[k] == 32) {
			s[i] = r->v[(k << 4) + b[(ph + i) & 3]];
		} else {
			uint8_t *q = (uint8_t *) (s + i);
			unsigned w = (unsigned) q[0] << 8 | q[1];
			uint32_t m = ((r->spread[k] + SPREAD(w) * (32u - r->lvl[k])) >> 5) & 0x07E0F81FUL;
			w = (unsigned) (m | m >> 16);
			q[0] = (uint8_t) (w >> 8);
			q[1] = (uint8_t) w;
			cu[i] = u;
			co[i] = s[i];
		}
	}
}

/* canvas row y from x, n pixels, entry idx[i] each */
static void grad_row(canvas *c, ramp *r, int32_t x, int32_t y, int32_t n, const uint8_t *idx)
{
	const uint8_t *b = bayer[(y + c->oy) & 3];
	const uint16_t *v = r->v;
	uint8_t *o = c->pix + (long) y * c->stride;
	int32_t i, ph = (x + c->ox) & 3;
	unsigned b0 = b[ph], b1 = b[(ph + 1) & 3], b2 = b[(ph + 2) & 3], b3 = b[(ph + 3) & 3];
	if (gmap != NULL) {
		memcpy(gmap + y * gmap_w + x, idx, (size_t) n);
		return;
	}
	if (r->solid) {
		/* each depth its own loop, four pixels a turn */
		if (c->bpp == 8) {
			o += x;
			for (i = 0; i + 4 <= n; i += 4) {
				o[i] = (uint8_t) v[(idx[i] << 4) + b0];
				o[i + 1] = (uint8_t) v[(idx[i + 1] << 4) + b1];
				o[i + 2] = (uint8_t) v[(idx[i + 2] << 4) + b2];
				o[i + 3] = (uint8_t) v[(idx[i + 3] << 4) + b3];
			}
			for (; i < n; i++)
				o[i] = (uint8_t) v[(idx[i] << 4) + b[(ph + i) & 3]];
		} else {
			uint16_t *s = (uint16_t *) (void *) (o + 2 * x);
			for (i = 0; i + 4 <= n; i += 4) {
				s[i] = v[(idx[i] << 4) + b0];
				s[i + 1] = v[(idx[i + 1] << 4) + b1];
				s[i + 2] = v[(idx[i + 2] << 4) + b2];
				s[i + 3] = v[(idx[i + 3] << 4) + b3];
			}
			for (; i < n; i++)
				s[i] = v[(idx[i] << 4) + b[(ph + i) & 3]];
		}
		return;
	}
	if (c->bpp == 8) {
		/* each pixel its pen where its entry's alpha is above its
		   transposed threshold (t0..t3), four a turn */
		const uint8_t *lv = r->lvl;
		int ty = (y + c->oy) & 3;
		unsigned t0 = bayer[ph][ty], t1 = bayer[(ph + 1) & 3][ty], t2 = bayer[(ph + 2) & 3][ty],
			 t3 = bayer[(ph + 3) & 3][ty];
		int k;
		o += x;
		for (i = 0; i + 4 <= n; i += 4) {
			k = idx[i];
			if (lv[k] > t0)
				o[i] = (uint8_t) v[(k << 4) + b0];
			k = idx[i + 1];
			if (lv[k] > t1)
				o[i + 1] = (uint8_t) v[(k << 4) + b1];
			k = idx[i + 2];
			if (lv[k] > t2)
				o[i + 2] = (uint8_t) v[(k << 4) + b2];
			k = idx[i + 3];
			if (lv[k] > t3)
				o[i + 3] = (uint8_t) v[(k << 4) + b3];
		}
		for (; i < n; i++) {
			k = idx[i];
			if (lv[k] > bayer[(ph + i) & 3][ty])
				o[i] = (uint8_t) v[(k << 4) + b[(ph + i) & 3]];
		}
		return;
	}
	mix_row16(r, (uint16_t *) (void *) (o + 2 * x), n, idx, b, ph);
}

/* canvas row y from x, n pixels of a linear gradient: entry (v >> 16) &
   mask, v stepped by vx a pixel; a solid ramp's pens put straight in,
   each depth its own loop, four pixels a turn (no entries kept between:
   a pixel is a shift, a mask and a look-up; runs of one entry, each a
   pattern written a long at a time, measured a quarter slower on the
   68030 for entries 15 px wide), a translucent one's through idx */
static void lin_row(canvas *c, ramp *r, int32_t x, int32_t y, int32_t n, int32_t v, int32_t vx, int32_t mask,
		    uint8_t *idx)
{
	const uint8_t *b = bayer[(y + c->oy) & 3];
	const uint16_t *pv = r->v;
	int32_t i, ph = (x + c->ox) & 3;
	unsigned b0 = b[ph], b1 = b[(ph + 1) & 3], b2 = b[(ph + 2) & 3], b3 = b[(ph + 3) & 3];
	if (n <= 0)
		return;
	if (!r->solid) {
		for (i = 0; i < n; i++, v += vx)
			idx[i] = (uint8_t) ((v >> 16) & mask);
		grad_row(c, r, x, y, n, idx);
		return;
	}
	if (c->bpp == 8) {
		uint8_t *o = c->pix + (long) y * c->stride + x;
		for (i = 0; i + 4 <= n; i += 4) {
			o[i] = (uint8_t) pv[(((v >> 16) & mask) << 4) + b0];
			v += vx;
			o[i + 1] = (uint8_t) pv[(((v >> 16) & mask) << 4) + b1];
			v += vx;
			o[i + 2] = (uint8_t) pv[(((v >> 16) & mask) << 4) + b2];
			v += vx;
			o[i + 3] = (uint8_t) pv[(((v >> 16) & mask) << 4) + b3];
			v += vx;
		}
		for (; i < n; i++, v += vx)
			o[i] = (uint8_t) pv[(((v >> 16) & mask) << 4) + b[(ph + i) & 3]];
	} else {
		uint16_t *o = (uint16_t *) (void *) (c->pix + (long) y * c->stride + 2 * x);
		for (i = 0; i + 4 <= n; i += 4) {
			o[i] = pv[(((v >> 16) & mask) << 4) + b0];
			v += vx;
			o[i + 1] = pv[(((v >> 16) & mask) << 4) + b1];
			v += vx;
			o[i + 2] = pv[(((v >> 16) & mask) << 4) + b2];
			v += vx;
			o[i + 3] = pv[(((v >> 16) & mask) << 4) + b3];
			v += vx;
		}
		for (; i < n; i++, v += vx)
			o[i] = pv[(((v >> 16) & mask) << 4) + b[(ph + i) & 3]];
	}
}

/* t squared, a threshold's (16-bit: under 65536); 0 below 0 */
static inline uint32_t sq16(int32_t t)
{
	return t < 0 ? 0 : (uint32_t) (uint16_t) t * (uint16_t) t;
}

/* canvas row y from x, n pixels of a radial gradient: a pixel's square
   distance a^2 + bb in 2^-f entries (a stepped by dA, 16.16: one 16-bit
   multiply), its entry k kept from pixel to pixel against the squares of
   k's and k + 1's thresholds, t and t + step (mostly the same as the
   pixel before, or one either side); a repeating gradient's k & mask, a
   gradient's otherwise from 0 to last; a solid ramp's pens put straight
   in, a translucent one's through idx */
static __attribute__((noinline)) void rad_row(canvas *c, ramp *r, int32_t x, int32_t y, int32_t n, int32_t A,
						    int32_t dA, uint32_t bb, int32_t k, int32_t t, int32_t step, int rep,
						    uint8_t *idx)
{
	const uint8_t *b = bayer[(y + c->oy) & 3];
	const uint16_t *pv = r->v;
	uint32_t T = sq16(t), Tn = sq16(t + step);
	int32_t i, last = r->n - 1, j = (x + c->ox) & 3, e;
	/* (a function of its own, and a loop for each way the pixels go:
	   its variables kept in registers) */
#define RAD_ENTRY() do { \
		int16_t a = (int16_t) (A >> 16); \
		uint32_t D = (uint32_t) ((int32_t) a * a) + bb; \
		while (D >= Tn) { \
			k++; \
			t += step; \
			T = Tn; \
			Tn = sq16(t + step); \
		} \
		while (D < T) { \
			k--; \
			t -= step; \
			Tn = T; \
			T = sq16(t); \
		} \
		e = rep ? k & last : (uint32_t) k <= (uint32_t) last ? k : k < 0 ? 0 : last; \
		A += dA; \
	} while (0)
	if (!r->solid) {
		for (i = 0; i < n; i++) {
			RAD_ENTRY();
			idx[i] = (uint8_t) e;
		}
		grad_row(c, r, x, y, n, idx);
	} else {
		/* four pixels a turn, each its threshold's pen */
		unsigned b0 = b[j], b1 = b[(j + 1) & 3], b2 = b[(j + 2) & 3], b3 = b[(j + 3) & 3];
		if (c->bpp == 8) {
			uint8_t *o = c->pix + (long) y * c->stride + x;
			for (i = 0; i + 4 <= n; i += 4) {
				RAD_ENTRY();
				o[i] = (uint8_t) pv[(e << 4) + b0];
				RAD_ENTRY();
				o[i + 1] = (uint8_t) pv[(e << 4) + b1];
				RAD_ENTRY();
				o[i + 2] = (uint8_t) pv[(e << 4) + b2];
				RAD_ENTRY();
				o[i + 3] = (uint8_t) pv[(e << 4) + b3];
			}
			for (; i < n; i++) {
				RAD_ENTRY();
				o[i] = (uint8_t) pv[(e << 4) + b[(j + i) & 3]];
			}
		} else {
			uint16_t *o = (uint16_t *) (void *) (c->pix + (long) y * c->stride + 2 * x);
			for (i = 0; i + 4 <= n; i += 4) {
				RAD_ENTRY();
				o[i] = pv[(e << 4) + b0];
				RAD_ENTRY();
				o[i + 1] = pv[(e << 4) + b1];
				RAD_ENTRY();
				o[i + 2] = pv[(e << 4) + b2];
				RAD_ENTRY();
				o[i + 3] = pv[(e << 4) + b3];
			}
			for (; i < n; i++) {
				RAD_ENTRY();
				o[i] = pv[(e << 4) + b[(j + i) & 3]];
			}
		}
	}
#undef RAD_ENTRY
}

/* atan of 0..1 in 64ths, in 65536ths of a turn */
static const uint16_t atan_tab[65] = {
	0, 163, 326, 489, 651, 813, 975, 1136, 1297, 1457, 1617, 1775,
	1933, 2090, 2246, 2401, 2555, 2708, 2860, 3010, 3159, 3307, 3453, 3599,
	3742, 3884, 4025, 4164, 4302, 4438, 4572, 4705, 4836, 4966, 5094, 5220,
	5344, 5467, 5589, 5708, 5826, 5943, 6058, 6171, 6282, 6392, 6500, 6607,
	6712, 6815, 6917, 7018, 7117, 7214, 7310, 7405, 7498, 7589, 7679, 7768,
	7856, 7942, 8026, 8110, 8192
};

/* canvas row y from x, n pixels of a conic gradient on one side of its
   centre: each pixel's angle from its offsets (ax across, from xa a step
   of dx a pixel, and au up, both shifted down sh), an eighth of a turn
   from atan's table (to the nearest 64th: a quarter of an entry), then
   base + sgn * it; entries of 256ths of a turn. A solid ramp's pens
   put straight in, a translucent one's through idx */
static __attribute__((noinline)) void con_span(canvas *c, ramp *r, int32_t x, int32_t y, int32_t n, int32_t xa,
						     int32_t dx, int32_t au, int sh, unsigned base, int sgn, uint8_t *idx)
{
	const uint8_t *b = bayer[(y + c->oy) & 3];
	const uint16_t *pv = r->v;
	int32_t i, ph = (x + c->ox) & 3;
	unsigned e;
#define CON_ENTRY() do { \
		uint32_t ax = (uint32_t) xa >> sh, t; \
		if (ax <= (uint32_t) au) \
			t = au ? atan_tab[((ax << 16) / (uint32_t) au + 512) >> 10] : 0; \
		else \
			t = 16384 - atan_tab[(((uint32_t) au << 16) / ax + 512) >> 10]; \
		e = ((base + (sgn > 0 ? t : 0u - t)) & 0xffff) >> 8; \
		xa += dx; \
	} while (0)
	if (!r->solid) {
		for (i = 0; i < n; i++) {
			CON_ENTRY();
			idx[i] = (uint8_t) e;
		}
		grad_row(c, r, x, y, n, idx);
	} else if (c->bpp == 8) {
		uint8_t *o = c->pix + (long) y * c->stride + x;
		for (i = 0; i < n; i++) {
			CON_ENTRY();
			o[i] = (uint8_t) pv[(e << 4) + b[(ph + i) & 3]];
		}
	} else {
		uint16_t *o = (uint16_t *) (void *) (c->pix + (long) y * c->stride + 2 * x);
		for (i = 0; i < n; i++) {
			CON_ENTRY();
			o[i] = pv[(e << 4) + b[(ph + i) & 3]];
		}
	}
#undef CON_ENTRY
}

/* where a gradient's pixels are along it, for a tile of w x h: 24.8 px
   along the line (linear, from its start) or the ray (radial, from the
   centre: the ellipse's distance, in its horizontal radius's px), or a
   65536th of a turn (conic) */
typedef struct gline {
	int32_t len;			/* the line's or ray's length; a turn */
	int32_t sx, sy;			/* linear: its direction, 1.14 */
	int32_t cx, cy, rx, ry;		/* radial, conic: the centre; radial: the radii, 24.8 px */
	int32_t from;			/* conic: where the turn starts, 65536ths */
} gline;

/* a length of a box w px wide (or high), 24.8 */
static int32_t glen256(const ec_glen *l, int32_t base)
{
	return (l->px >> 2) + (int32_t) ((int64_t) l->pct * base / 400);
}

/* 0: nothing of it shows (a radial gradient of no size) */
static int grad_line(const ec_grad *g, int32_t w, int32_t h, gline *gl)
{
	memset(gl, 0, sizeof *gl);
	if (g->type == EC_GRAD_LINEAR) {
		/* CSS Images 3, 3.1.1: through the box's centre at the angle,
		   as long as reaches its corners */
		if (g->to & (EC_TO_TOP | EC_TO_BOTTOM) && g->to & (EC_TO_LEFT | EC_TO_RIGHT)) {
			/* to a corner: at right angles to the diagonal between the other two */
			int32_t d = (int32_t) isqrt64((uint64_t) ((int64_t) w * w + (int64_t) h * h));
			gl->sx = (int32_t) ((int64_t) h * 16384 / d);
			gl->sy = (int32_t) ((int64_t) w * 16384 / d);
			if (g->to & EC_TO_LEFT)
				gl->sx = -gl->sx;
			if (g->to & EC_TO_TOP)
				gl->sy = -gl->sy;
		} else {
			int32_t deg = g->to & EC_TO_TOP ? 0 : g->to & EC_TO_RIGHT ? 90 * 1024 :
				      g->to & EC_TO_BOTTOM ? 180 * 1024 : g->to & EC_TO_LEFT ? 270 * 1024 :
				      g->angle % (360 * 1024);
			gl->sx = sin14(deg);
			gl->sy = -sin14(deg + 90 * 1024);
		}
		gl->len = (int32_t) (((int64_t) w * (gl->sx < 0 ? -gl->sx : gl->sx) +
				      (int64_t) h * (gl->sy < 0 ? -gl->sy : gl->sy)) >> 6);
		return gl->len > 0;
	}
	gl->cx = glen256(&g->x, w);
	gl->cy = glen256(&g->y, h);
	if (g->type == EC_GRAD_CONIC) {
		gl->len = 65536;
		gl->from = (int32_t) ((int64_t) g->angle * 64 / 360);
		return 1;
	}
	/* CSS Images 3, 3.2.2: the ending shape */
	if (g->extent == EC_EXT_LENGTH) {
		gl->rx = glen256(&g->rx, w);
		gl->ry = g->circle ? gl->rx : glen256(&g->ry, h);
	} else {
		int32_t dl = gl->cx < 0 ? -gl->cx : gl->cx, dr = w * 256 - gl->cx, dt = gl->cy < 0 ? -gl->cy : gl->cy,
			db = h * 256 - gl->cy, dx, dy;
		int far = g->extent == EC_EXT_FARTHEST_SIDE || g->extent == EC_EXT_FARTHEST_CORNER;
		if (dr < 0)
			dr = -dr;
		if (db < 0)
			db = -db;
		dx = far ? (dl > dr ? dl : dr) : (dl < dr ? dl : dr);
		dy = far ? (dt > db ? dt : db) : (dt < db ? dt : db);
		if (g->extent == EC_EXT_CLOSEST_SIDE || g->extent == EC_EXT_FARTHEST_SIDE) {
			if (g->circle)
				dx = dy = far ? (dx > dy ? dx : dy) : (dx < dy ? dx : dy);
			gl->rx = dx;
			gl->ry = dy;
		} else if (g->circle) {
			gl->rx = gl->ry = (int32_t) isqrt64((uint64_t) ((int64_t) dx * dx + (int64_t) dy * dy));
		} else {
			/* through the corner, shaped as the sides' would be: sqrt 2 times */
			gl->rx = (int32_t) ((int64_t) dx * 46341 >> 15);
			gl->ry = (int32_t) ((int64_t) dy * 46341 >> 15);
		}
	}
	/* an ending shape of no size (CSS Images 3, 3.2.3) as one of the
	   least: a circle a 256th of a px round (its stops' percentages all
	   0, those in px where they are), an ellipse of no width one a 256th
	   of a px wide and as tall as can be (a linear gradient across,
	   mirrored at the centre); one of no height none (its last colour,
	   or its average if it repeats) */
	if (gl->rx <= 0) {
		gl->rx = 1;
		gl->ry = g->circle ? 1 : 1L << 30;
	}
	gl->len = gl->rx;
	return gl->ry > 0;
}

/* canvas rows y0..y1 of a tile at tx, ty (canvas px), from x0 to x1 */
static void grad_tile(canvas *c, ramp *r, const ec_grad *g, const gline *gl, int32_t w, int32_t h, int32_t p0,
		      int32_t span, int32_t tx, int32_t ty, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int32_t n = x1 - x0, y, last = r->n - 1, mul = r->mode == R_REPEAT ? r->n : r->n - 2;
	int rep = r->mode == R_REPEAT;
	uint8_t *idx;
	if (n <= 0 || y0 >= y1)
		return;
	if (n > gidx_n) {
		free(gidx);
		gidx = malloc((size_t) n);
		gidx_n = gidx != NULL ? n : 0;
		if (gidx == NULL)
			return;
	}
	idx = gidx;
	if (g->type == EC_GRAD_LINEAR) {
		/* the place along the line, times 128: (2x + 1 - w) sx + (2y + 1 - h) sy
		   (1.14 by half px) + 64 len, from the start; the entry: its place
		   in the span times mul (and one, past the colour before it) */
		int64_t k = (int64_t) mul * 512, q, vrow, vy;
		int32_t vx = (int32_t) ((int64_t) 2 * gl->sx * k / span), rnd = r->mode == R_SPAN ? 65536 : 0;
		/* the first row's from its place, the others a step on, in
		   32.32 entries (a row drawn alone the same as one in a band) */
		q = ((int64_t) (2 * (x0 - tx) + 1 - w) * gl->sx + (int64_t) (2 * (y0 - ty) + 1 - h) * gl->sy +
		     64 * (int64_t) gl->len - 128 * (int64_t) p0) * k;
		vrow = q / span * 65536 + q % span * 65536 / span;
		vy = (int64_t) 2 * gl->sy * k * 65536 / span;
		for (y = y0; y < y1; y++, vrow += vy) {
			int64_t v64 = (vrow >> 16) + rnd;
			int32_t v;
			if (rep)
				v64 &= ((int64_t) r->n << 16) - 1;
			else
				v64 = v64 < -(1L << 30) ? -(1L << 30) : v64 > (1L << 30) ? (1L << 30) : v64;
			v = (int32_t) v64;
			if (vx == 0) {
				/* along the rows: a row one entry */
				int e = v >> 16;
				grad_run(c, r, rep ? e & last : e < 0 ? 0 : e > last ? last : e, x0, y, n);
				continue;
			}
			if (gl->sy == 0 && r->solid && y >= y0 + 4) {
				/* down the columns: each row the one four rows up */
				int bytes = c->bpp / 8;
				memcpy(c->pix + (long) y * c->stride + x0 * bytes,
				       c->pix + (long) (y - 4) * c->stride + x0 * bytes, (size_t) n * bytes);
				continue;
			}
			if (rep) {
				lin_row(c, r, x0, y, n, v, vx, last, idx);
				continue;
			}
			{
				/* the pixels before the span and after it, the colours
				   there in runs; those in it stepped along */
				int32_t hi = (last + 1) << 16, a, e;
				int64_t sa, se;
				if (vx > 0) {
					sa = v >= 0 ? 0 : ((int64_t) -v + vx - 1) / vx;
					se = v >= hi ? 0 : ((int64_t) hi - v + vx - 1) / vx;
				} else {
					sa = v < hi ? 0 : ((int64_t) v - hi) / -vx + 1;
					se = v < 0 ? 0 : (int64_t) v / -vx + 1;
				}
				a = (int32_t) (sa < n ? sa : n);
				e = (int32_t) (se < n ? se : n);
				if (e < a)
					e = a;
				grad_run(c, r, vx > 0 ? 0 : last, x0, y, a);
				lin_row(c, r, x0 + a, y, e - a, v + a * vx, vx, 255, idx);
				grad_run(c, r, vx > 0 ? last : 0, x0 + e, y, n - e);
			}
		}
		return;
	}
	if (g->type == EC_GRAD_CONIC) {
		/* each pixel's angle from up, clockwise, from its offsets from
		   the centre: an eighth of a turn from atan's table, the rest by
		   which way it lies */
		for (y = y0; y < y1; y++) {
			/* the pixels left of the centre, then those right of it:
			   up, and which side, the same for each */
			int32_t u = gl->cy - (256 * (y - ty) + 128), xr = 256 * (x0 - tx) + 128 - gl->cx, sh = 0, au, m, nl;
			unsigned from = (unsigned) gl->from;
			m = u < 0 ? -u : u;
			au = (xr < 0 ? -xr : xr) + 256 * n;
			if (au > m)
				m = au;
			while ((m >> sh) >= 32768)
				sh++;
			au = (u < 0 ? -u : u) >> sh;
			nl = xr >= 0 ? 0 : (-xr + 255) / 256;
			if (nl > n)
				nl = n;
			con_span(c, r, x0, y, nl, -xr, -256, au, sh, (u >= 0 ? 65536u : 32768u) - from, u >= 0 ? -1 : 1, idx);
			con_span(c, r, x0 + nl, y, n - nl, xr + 256 * nl, 256, au, sh, (u >= 0 ? 0u : 32768u) - from,
				 u >= 0 ? 1 : -1, idx);
		}
		return;
	}
	{
		/* radial: the entry is (the distance along the ray) times
		   mul / span, less p0's (and one more, past the colour before
		   it); worked out in 2^-f entries: a pixel's offset across, a,
		   stepped along the row; its distance's square, a^2 + b^2, kept
		   under 2^31 (16-bit multiplies); and the entry k it falls in
		   kept from pixel to pixel, as the next one's is mostly the same
		   or one either side, by the squares of k's and k + 1's
		   thresholds, t and tn (CSS Images 3, 3.2) */
		int64_t kf16, off, A0, most;
		int32_t f, step, dA, sat, xa = x0, xb = x1, A1;
		/* the farthest an entry worth working out lies, in entries */
		off = (int64_t) p0 * mul / span;
		if (rep) {
			int32_t ex = (x0 - tx) * 256 + 128 - gl->cx, ex2 = (x1 - 1 - tx) * 256 + 128 - gl->cx,
				ey = (y0 - ty) * 256 + 128 - gl->cy, ey2 = (y1 - 1 - ty) * 256 + 128 - gl->cy;
			int64_t mx = ex < 0 ? -(int64_t) ex : ex, my = ey < 0 ? -(int64_t) ey : ey;
			if ((ex2 < 0 ? -(int64_t) ex2 : ex2) > mx)
				mx = ex2 < 0 ? -(int64_t) ex2 : ex2;
			if ((ey2 < 0 ? -(int64_t) ey2 : ey2) > my)
				my = ey2 < 0 ? -(int64_t) ey2 : ey2;
			my = my * gl->rx / gl->ry;
			most = (mx + my) * mul / span + 2;
		} else {
			most = r->n + (off < 0 ? -off : off) + 2;
		}
		for (f = 13; f >= 0 && (most << f) >= 32767; f--)
			;
		if (f < 0) {
			/* (thousands of entries away: as one colour) */
			if (gmap != NULL)
				gmap_fail = 1;
			else
				grad_fill(c, x0 + c->ox, y0 + c->oy, n, y1 - y0, r->avg);
			return;
		}
		/* entries (by 2^f) a 24.8 px, 16.16 */
		kf16 = ((int64_t) mul << (16 + f)) / span;
		step = 1 << f;
		sat = (int32_t) (most << f);
		off = (((int64_t) p0 * kf16) >> 16) - (r->mode == R_SPAN ? step : 0);
		dA = (int32_t) (256 * kf16);
		/* a across the first pixel, the same in every row */
		A0 = (int64_t) (256 * (x0 - tx) + 128 - gl->cx) * kf16;
		if (!rep) {
			/* the pixels whose a is under the threshold: beyond it, on
			   either side, the last colour */
			int64_t lim = (int64_t) sat << 16, at = A0;
			if (at <= -lim) {
				int64_t skip = (-lim - at) / dA + 1;
				xa = x0 + (int32_t) (skip < n ? skip : n);
			}
			at = A0 + (int64_t) (xa - x0) * dA;
			if (xa < x1 && at + (int64_t) (x1 - 1 - xa) * dA >= lim) {
				int64_t keep = at >= lim ? 0 : (lim - at + dA - 1) / dA;
				xb = xa + (int32_t) (keep < x1 - xa ? keep : x1 - xa);
			}
		}
		A1 = (int32_t) (A0 + (int64_t) (xa - x0) * dA);
		for (y = y0; y < y1; y++) {
			/* b down, the ellipse made a circle (a row drawn alone the
			   same as one in a band) */
			int64_t b64 = ((int64_t) (256 * (y - ty) + 128 - gl->cy) * gl->rx / gl->ry * kf16) >> 16;
			int32_t b, k, t;
			int16_t a;
			uint32_t B;
			if (!rep) {
				if (b64 >= sat || b64 <= -sat || xa >= xb) {
					grad_run(c, r, last, x0, y, n);
					continue;
				}
				grad_run(c, r, last, x0, y, xa - x0);
				grad_run(c, r, last, xb, y, x1 - xb);
			}
			b = (int32_t) b64;
			B = (uint32_t) ((int32_t) (int16_t) b * (int16_t) b);
			/* the entry at the first pixel, from its distance */
			a = (int16_t) (A1 >> 16);
			t = (int32_t) isqrt32((uint32_t) ((int32_t) a * a) + B);
			k = (int32_t) (((int64_t) t - off) >> f);	/* (down) */
			rad_row(c, r, xa, y, xb - xa, A1, dA, B, k, k * step + (int32_t) off, step, rep, idx);
		}
	}
}

/* the entries of a tile w x h's first mh rows worked out into gmap_buf
   (translucent tiles drawn a row at a time from them); 0: not (too big,
   out of memory) */
static int grad_map(canvas *c, ramp *r, const ec_grad *g, const gline *gl, int32_t w, int32_t h, int32_t mh,
		    int32_t p0, int32_t span)
{
	if ((int64_t) w * mh > 65536)
		return 0;
	if (w * mh > gmap_n) {
		free(gmap_buf);
		gmap_buf = malloc((size_t) (w * mh));
		gmap_n = gmap_buf != NULL ? w * mh : 0;
		if (gmap_buf == NULL)
			return 0;
	}
	gmap = gmap_buf;
	gmap_w = w;
	gmap_fail = 0;
	grad_tile(c, r, g, gl, w, h, p0, span, 0, 0, 0, 0, w, mh);
	gmap = NULL;
	return !gmap_fail && gidx != NULL;
}

/* the entries a span of span (24.8 px) needs for its colours to step
   no more than two levels (of 255) an entry: as many as its steepest
   part does, a sharp edge (stops in one place) or a hint (its curve)
   all there can be. A gentle gradient (#fff to #eee) is a few entries,
   not one a pixel: each is 16 of the screen's pens to choose */
static int32_t ramp_need(const gstops *s, int32_t span)
{
	int64_t most = 1;
	int i, j = -1, k;
	for (i = 0; i < s->ns; i++) {
		int d, step = 0;
		if (s->hint[i])
			return RAMP_MAX;
		if (j >= 0) {
			for (k = 0; k < 32; k += 8) {
				d = (int) (s->col[i] >> k & 255) - (int) (s->col[j] >> k & 255);
				if (d < 0)
					d = -d;
				if (d > step)
					step = d;
			}
			if (step && s->pos[i] <= s->pos[j])
				return RAMP_MAX;
			if (step && (int64_t) span * step / (2 * (int64_t) (s->pos[i] - s->pos[j])) + 1 > most)
				most = (int64_t) span * step / (2 * (int64_t) (s->pos[i] - s->pos[j])) + 1;
		}
		j = i;
	}
	return most > RAMP_MAX ? RAMP_MAX : (int32_t) most;
}

/* a gradient item: its tiles where the item and the canvas meet */
static void grad(canvas *c, const dl_item *d)
{
	const ec_grad *g = d->img;
	const dl_tile *t = d->tile;
	ramp want, *r;
	gline gl;
	int32_t x0 = d->x, y0 = d->y, x1 = d->x + d->w, y1 = d->y + d->h, w, h, sx, sy, tx, ty, xlim, ylim, p0, span;
	int i, first = -1, last = -1, bytes = c->bpp / 8, across;
	uint32_t avg;
	if (t == NULL || g == NULL || g->n == 0 || t->w <= 0 || t->h <= 0)
		return;
	if (x0 < c->ox)
		x0 = c->ox;
	if (y0 < c->oy)
		y0 = c->oy;
	if (x1 > c->ox + c->w)
		x1 = c->ox + c->w;
	if (y1 > c->oy + c->h)
		y1 = c->oy + c->h;
	if (x0 >= x1 || y0 >= y1)
		return;
	w = t->w;
	h = t->h;
	for (i = 0; i < g->n; i++)
		if (!(g->s[i].flags & EC_GS_HINT)) {
			if (first < 0)
				first = i;
			last = i;
		}
	avg = grad_average(g, 0);
	if (g->approx) {
		/* (its stops can't be placed: CSS's min() of px and %, more
		   stops than are kept) */
		grad_fill(c, d->x, d->y, d->w, d->h, avg);
		return;
	}
	if (!grad_line(g, w, h, &gl)) {
		/* a radial gradient of no height (CSS Images 3, 3.2.3): the
		   last colour, or a repeating one's average */
		grad_fill(c, d->x, d->y, d->w, d->h, g->repeat ? avg : g->s[last].argb);
		return;
	}

	memset(&want, 0, sizeof want);
	want.a = item_a;
	want.rep = g->repeat;
	grad_place(g, gl.len, &want.s);
	p0 = want.s.pos[first];
	span = want.s.pos[last] - p0;
	if (g->type == EC_GRAD_CONIC) {
		want.mode = R_TURN;
		want.n = RAMP_MAX;
		if (g->repeat && span < 65536 / RAMP_MAX) {
			grad_fill(c, d->x, d->y, d->w, d->h, avg);
			return;
		}
	} else {
		if (g->repeat) {
			/* a period of no length (CSS Images 3, 3.6): its average */
			int n;
			if (span < 256) {
				grad_fill(c, d->x, d->y, d->w, d->h, avg);
				return;
			}
			for (n = 2; n * 2 <= RAMP_MAX && n * 2 * 256 <= span; n *= 2)
				;
			want.mode = R_REPEAT;
			want.n = n;
		} else {
			/* an entry a pixel at most, and no more than its colours
			   need; one when the colours change in less than a pixel (a
			   sharp edge), the last colour's */
			int32_t n = ramp_need(&want.s, span);
			if (span < 256)
				span = 256;
			if (n > span >> 8)
				n = span >> 8;
			want.mode = R_SPAN;
			want.n = 2 + (n > RAMP_MAX - 2 ? RAMP_MAX - 2 : n);
		}
		for (i = 0; i < want.s.ns; i++)
			want.s.pos[i] = (int32_t) (((int64_t) want.s.pos[i] - p0) * 65536 / span);
	}
	r = ramp_get(c, &want);
	if (r == NULL) {
		grad_fill(c, d->x, d->y, d->w, d->h, avg);
		return;
	}
	r->avg = avg;
	if (!(g->type == EC_GRAD_LINEAR && gl.sx == 0))
		ramp_all(c, r);		/* (a row at a time makes its entry as it goes) */
	sx = d->x + t->dx;
	sy = d->y + t->dy;
	if (t->rx)
		sx = first_tile(sx, w, x0);
	if (t->ry)
		sy = first_tile(sy, h, y0);
	if (g->type == EC_GRAD_LINEAR && gl.sx == 0) {
		/* rows each of one entry: a row across all the tiles at once */
		for (ty = sy; ty < y1; ty += h) {
			int32_t e = ty > y0 ? ty : y0, f = ty + h < y1 ? ty + h : y1;
			grad_tile(c, r, g, &gl, w, h, p0, span, sx - c->ox, ty - c->oy, x0 - c->ox, e - c->oy, x1 - c->ox,
				  f - c->oy);
			if (!t->ry)
				break;
		}
		return;
	}
	across = g->type == EC_GRAD_LINEAR && gl.sy == 0;
	if (!r->solid && (across || (t->rx && x1 - x0 > w) || (t->ry && y1 - y0 > h)) &&
	    grad_map(c, r, g, &gl, w, h, across ? 1 : h, p0, span)) {
		/* translucent tiles (mixed with what is under them), or a
		   translucent gradient across (its rows alike): their entries
		   worked out for one tile (a row of it), each row drawn from
		   them */
		int32_t n = x1 - x0, y;
		if (n > gidx_n) {
			free(gidx);
			gidx = malloc((size_t) n);
			gidx_n = gidx != NULL ? n : 0;
		}
		if (gidx != NULL && across && c->bpp == 16 && n > gcol_n) {
			free(gcol);
			gcol = malloc((size_t) n * (sizeof *gcol + sizeof(uint16_t)));
			gcol_n = gcol != NULL ? n : 0;
		}
		if (gidx != NULL) {
			int32_t u = ((x0 - sx) % w + w) % w;
			int cols = across && c->bpp == 16 && gcol != NULL;
			for (y = y0; y < y1; y++) {
				/* the row's entries: the tile's row from u, then its
				   whole rows, each copy twice the last */
				const uint8_t *row = gmap_buf + (across ? 0 : ((y - sy) % h + h) % h * w);
				int32_t k = w - u < n ? w - u : n, m, a = k;
				memcpy(gidx, row + u, (size_t) k);
				if (k < n) {
					m = w < n - k ? w : n - k;
					memcpy(gidx + k, row, (size_t) m);
					for (k += m; k < n; k += m) {
						m = k - a < n - k ? k - a : n - k;
						memcpy(gidx + k, gidx + a, (size_t) m);
					}
				}
				if (cols) {
					if (y == y0)
						for (k = 0; k < n; k++)
							gcol[k] = 0x10000;
					mix_cols16(r, (uint16_t *) (void *) (c->pix + (long) (y - c->oy) * c->stride +
									      2 * (x0 - c->ox)), n, gidx,
						   bayer[y & 3], x0 & 3);
				} else {
					grad_row(c, r, x0 - c->ox, y - c->oy, n, gidx);
				}
			}
			return;
		}
	}
	/* the tiles drawn, a solid one's only to where its tiles and the
	   pattern (4 x 4) come round together across and down, the rest
	   copied on from there */
	xlim = r->solid && t->rx && x1 - x0 > lcm4(w) ? x0 + lcm4(w) : x1;
	ylim = r->solid && t->ry && y1 - y0 > lcm4(h) ? y0 + lcm4(h) : y1;
	for (ty = sy; ty < ylim; ty += h) {
		for (tx = sx; tx < xlim; tx += w) {
			int32_t a = tx > x0 ? tx : x0, b = tx + w < xlim ? tx + w : xlim, e = ty > y0 ? ty : y0,
				f = ty + h < ylim ? ty + h : ylim;
			grad_tile(c, r, g, &gl, w, h, p0, span, tx - c->ox, ty - c->oy, a - c->ox, e - c->oy, b - c->ox,
				  f - c->oy);
			if (!t->rx)
				break;
		}
		if (!t->ry)
			break;
	}
	if (xlim < x1)
		for (ty = y0; ty < ylim; ty++) {
			/* (each copy twice the last, from the row's start) */
			uint8_t *o = c->pix + (long) (ty - c->oy) * c->stride + (long) (x0 - c->ox) * bytes;
			int32_t done = xlim - x0, k;
			for (; done < x1 - x0; done += k) {
				k = x1 - x0 - done < done ? x1 - x0 - done : done;
				memcpy(o + (long) done * bytes, o, (size_t) k * bytes);
			}
		}
	for (ty = ylim; ty < y1; ty++)
		memcpy(c->pix + (long) (ty - c->oy) * c->stride + (long) (x0 - c->ox) * bytes,
		       c->pix + (long) (ty - (ylim - y0) - c->oy) * c->stride + (long) (x0 - c->ox) * bytes,
		       (size_t) (x1 - x0) * bytes);
}
#endif

/* an item, as solid as item_a */
static void draw_item(canvas *c, const page *p, const dl_item *d)
{
	uint32_t rgb = d->rgb & 0xffffff;
	if (d->type == DL_FILL) {
		cv_fill(c, d->x, d->y, d->w, d->h, rgb);
	} else if (d->type == DL_IMAGE) {
		const PIC *pic = NULL;
		const uint8_t *pens = NULL;
		if (d->flags & DLF_ALT)
			return;		/* its alt text is drawn instead */
		if (d->flags & DLF_TILE) {
			tiles(c, d);
			return;
		}
		if (c->picture != NULL && c->picture(c->ud, d->img, d->w, d->h, &pic, &pens) && pic != NULL &&
		    (c->bpp != 8 || pens != NULL))
			cv_picture(c, pic, pens, d->x, d->y);
		else
			placeholder(c, d);
	} else if (d->type == DL_GRAD) {
		grad(c, d);
	} else if (d->type == DL_CTL) {
		if (d->ctl > 0 && d->ctl <= p->nctls)
			control(c, d, p->ctls[d->ctl - 1]);
	} else {
		cv_text(c, d->font, d->x, d->y + d->base, d->text, d->len, rgb, d->flags);
	}
}

/* one item, if it falls on the canvas */
static void item(canvas *c, const page *p, const dl_item *d)
{
	int32_t top = c->oy, bottom = c->oy + c->h, left = c->ox, right = c->ox + c->w;
	unsigned a;
	if (d->y >= bottom || d->y + d->h <= top || d->x >= right || d->x + d->w <= left)
		return;
	/* no alpha: nothing drawn (a transparent colour, opacity 0), the item
	   there for clicks only; with no palette to mix with, solid or not
	   at all */
	a = drawn_a(c, d->rgb >> 24);
	if (a == 0)
		return;
	item_a = a;
	draw_item(c, p, d);
	item_a = 255;
}

/* the canvas's background picture or gradient (CSS Backgrounds 3,
   2.11.2: it paints the whole canvas, its tiles placed as for the root):
   when its tiles go on down, on past the page's foot to the canvas's, as
   a window taller than its page shows */
static void canvas_bg(canvas *c, const page *p)
{
	dl_item d = p->items[p->cbg];
	if ((d.type == DL_GRAD || (d.type == DL_IMAGE && (d.flags & DLF_TILE))) && d.tile != NULL && d.tile->ry &&
	    c->oy + c->h > d.y + d.h)
		d.h = c->oy + c->h - d.y;
	item(c, p, &d);
}

void cv_page(canvas *c, const page *p)
{
	long i, s0, s1, cbg = p->cbg >= 0 && p->cbg < p->nitems ? p->cbg : -1;

	cv_fill(c, c->ox, c->oy, c->w, c->h, p->bg);
	if (cbg >= 0)
		canvas_bg(c, p);	/* (the first of the items) */
	if (p->strip == NULL || c->h > 60 * PAGE_STRIP) {	/* (a whole long page: all of them) */
		for (i = 0; i < p->nitems; i++)
			if (i != cbg)
				item(c, p, &p->items[i]);
		return;
	}
	/* only the strips the canvas falls on: their lists merged in the
	   items' order, each item once */
	s0 = c->oy < 0 ? 0 : c->oy / PAGE_STRIP;
	s1 = (c->oy + c->h - 1) / PAGE_STRIP;
	if (s1 >= p->nstrips)
		s1 = p->nstrips - 1;
	if (s0 > s1)
		return;
	{
		long at[64], last = -1;
		int k, ns;
		ns = (int) (s1 - s0 + 1);
		for (k = 0; k < ns; k++)
			at[k] = p->strip[s0 + k];
		for (;;) {
			long best = -1;
			int bk = -1;
			for (k = 0; k < ns; k++)
				if (at[k] < p->strip[s0 + k + 1] && (best < 0 || p->sidx[at[k]] < best)) {
					best = p->sidx[at[k]];
					bk = k;
				}
			if (bk < 0)
				break;
			at[bk]++;
			if (best != last && best != cbg)
				item(c, p, &p->items[best]);
			last = best;
		}
	}
}
