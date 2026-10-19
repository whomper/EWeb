/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* canvas.c - the display list drawn into memory (see canvas.h). */
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "form.h"

/* a control's text is drawn within its box: canvas px, x0 <= x < x1 */
static int32_t clip_x0 = -0x7fffffff, clip_x1 = 0x7fffffff;

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
	if (gx + c0 < clip_x0)
		c0 = clip_x0 - gx;
	if (gx + c1 > clip_x1)
		c1 = clip_x1 - gx;
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

/* a field: one line, scrolled sideways (c->first, px) to show the caret */
static void field(canvas *cv, const dl_item *d, fctl *c)
{
	char *own;
	long tl;
	const char *t = shown(c, &tl, &own), *ph = form_placeholder(c);
	int32_t base = first_base(d), room = d->w - 6, cx;
	VLINE v;
	if (tl == 0 && ph != NULL) {
		if (vline(&v, d->font, ph, (long) strlen(ph))) {
			vdraw(cv, &v, d->font, ph, line_x(d, &v, 0), base, 0x909090);
			vfree(&v);
		}
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
	vdraw(cv, &v, d->font, t, line_x(d, &v, c->first), base, d->rgb);
	if (c->focus)
		caret(cv, d, line_x(d, &v, c->first) + cx, base);
	vfree(&v);
	free(own);
}

/* a textarea: its lines from c->first, scrolled to show the caret */
static void textarea(canvas *cv, const dl_item *d, fctl *c)
{
	const char *t = c->v ? c->v : "", *ph = form_placeholder(c);
	long *st;
	int nl, k, cl, rows = (d->h - 4) / (int32_t) d->len;
	int32_t base = first_base(d), step = (int32_t) d->len;
	VLINE v;
	if (c->len == 0 && ph != NULL) {
		if (vline(&v, d->font, ph, (long) strlen(ph))) {
			vdraw(cv, &v, d->font, ph, line_x(d, &v, 0), base, 0x909090);
			vfree(&v);
		}
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
		vdraw(cv, &v, d->font, t + st[k], line_x(d, &v, 0), base, d->rgb);
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
	vdraw(cv, &v, d->font, s, x, d->y + d->base, d->rgb);
	vfree(&v);
}

static void control(canvas *cv, const dl_item *d, fctl *c)
{
	char lab[256];
	clip_x0 = d->x - cv->ox;
	clip_x1 = d->x + d->w - cv->ox;
	switch (c->kind) {
	case FC_TEXT:
	case FC_PASSWORD:
		field(cv, d, c);
		break;
	case FC_TEXTAREA:
		textarea(cv, d, c);
		break;
	case FC_SELECT: {
		static const char arrow[] = "\xe2\x96\xbc";
		int32_t aw = (int32_t) font_width(d->font, arrow, 3);
		form_label(c, lab, sizeof lab);
		clip_x1 = d->x + d->w - aw - 4 - cv->ox;
		label(cv, d, lab, d->x + 3, 0);
		clip_x1 = d->x + d->w - cv->ox;
		cv_text(cv, d->font, d->x + d->w - aw - 3, d->y + d->base, arrow, 3, d->rgb, 0);
		break;
	}
	default:		/* buttons, boxes */
		form_label(c, lab, sizeof lab);
		label(cv, d, lab, 0, 1);
	}
	clip_x0 = -0x7fffffff;
	clip_x1 = 0x7fffffff;
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
		} else if (d->type == DL_CTL) {
			if (d->ctl > 0 && d->ctl <= p->nctls)
				control(c, d, p->ctls[d->ctl - 1]);
		} else
			cv_text(c, d->font, d->x, d->y + d->base, d->text, d->len, d->rgb, d->flags);
	}
}
