/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * font.c - EWEB.FNT: the table of faces is read at start, a face's
 * glyphs when it is first used (all of them take 1.8 MB; a page uses a
 * handful of faces).
 */
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "plat.h"

static char fpath[200];
static uint16_t ncodes, nfaces;
static uint16_t *codes;
static ew_font *faces;
static uint32_t *foff, *fsize;
static int16_t ascii[96];		/* code index of 0x20..0x7f, -1 if none */
static int qmark;			/* the index of '?' */

static unsigned be16(const uint8_t *p) { return (unsigned) p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

void font_free(void)
{
	int i;
	for (i = 0; i < nfaces; i++)
		free(faces[i].data);
	free(faces);
	free(codes);
	free(foff);
	free(fsize);
	faces = NULL;
	codes = NULL;
	foff = fsize = NULL;
	nfaces = ncodes = 0;
}

static int read_at(int h, long off, void *buf, long n)
{
	return pf_seek(h, off) == off && pf_read(h, buf, n) == n;
}

int font_init(const char *path)
{
	uint8_t head[12], *dir = NULL;
	int h, i, ok = 0;

	font_free();
	if (strlen(path) >= sizeof(fpath))
		return 0;
	strcpy(fpath, path);
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	if (!read_at(h, 0, head, 12) || memcmp(head, "EWFNT1", 6) != 0)
		goto done;
	nfaces = be16(head + 8);
	ncodes = be16(head + 10);
	codes = malloc(ncodes * sizeof *codes);
	dir = malloc((long) ncodes * 2 + (long) nfaces * 12);
	faces = calloc(nfaces, sizeof *faces);
	foff = malloc(nfaces * sizeof *foff);
	fsize = malloc(nfaces * sizeof *fsize);
	if (!codes || !dir || !faces || !foff || !fsize || nfaces == 0 || ncodes == 0)
		goto done;
	if (!read_at(h, 12, dir, (long) ncodes * 2 + (long) nfaces * 12))
		goto done;
	for (i = 0; i < ncodes; i++)
		codes[i] = be16(dir + 2 * i);
	for (i = 0; i < nfaces; i++) {
		const uint8_t *d = dir + 2 * ncodes + 12 * i;
		faces[i].family = d[0];
		faces[i].style = d[1];
		faces[i].px = d[2];
		foff[i] = be32(d + 4);
		fsize[i] = be32(d + 8);
	}
	for (i = 0; i < 96; i++)
		ascii[i] = -1;
	qmark = 0;
	for (i = 0; i < ncodes; i++) {
		if (codes[i] >= 0x20 && codes[i] < 0x80)
			ascii[codes[i] - 0x20] = (int16_t) i;
		if (codes[i] == '?')
			qmark = i;
	}
	ok = 1;
done:
	free(dir);
	pf_close(h);
	if (!ok)
		font_free();
	return ok;
}

static int load(ew_font *f)
{
	int i = (int) (f - faces), h;
	if (f->data || f->failed)
		return f->data != NULL;
	f->data = malloc(fsize[i]);
	h = pf_open(fpath, PF_READ);
	if (!f->data || h < 0 || !read_at(h, foff[i], f->data, fsize[i]) || (long) fsize[i] < 4 + 10L * ncodes) {
		free(f->data);
		f->data = NULL;
		f->failed = 1;
	} else {
		f->ascent = f->data[0];
		f->descent = f->data[1];
	}
	if (h >= 0)
		pf_close(h);
	return f->data != NULL;
}

ew_font *font_get(int family, int style, int px)
{
	ew_font *best = NULL;
	int i, bd = 1 << 30;

	for (i = 0; i < nfaces; i++) {
		int d;
		if (faces[i].family != family || faces[i].style != style)
			continue;
		/* the nearest size; between two, the smaller (text fits) */
		d = faces[i].px > px ? 2 * (faces[i].px - px) + 1 : 2 * (px - faces[i].px);
		if (d < bd) {
			bd = d;
			best = &faces[i];
		}
	}
	if (best == NULL && nfaces > 0)
		best = &faces[0];
	if (best != NULL && !best->data && !best->failed && load(best) == 0) {
		/* a face it can't read: its metrics from its size, no glyphs */
		best->ascent = (uint8_t) (best->px * 4 / 5 + 1);
		best->descent = (uint8_t) (best->px / 4 + 1);
	}
	return best;
}

static int index_of(uint32_t cp)
{
	int lo = 0, hi = ncodes - 1;
	if (cp >= 0x20 && cp < 0x80 && ascii[cp - 0x20] >= 0)
		return ascii[cp - 0x20];
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		if (codes[mid] == cp)
			return mid;
		if (codes[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

void font_glyph(ew_font *f, uint32_t cp, GLYPH *g)
{
	int i;
	const uint8_t *m;
	uint32_t off;

	memset(g, 0, sizeof *g);
	if (f == NULL || f->data == NULL)
		return;
	i = index_of(cp);
	if (i < 0) {
		if (cp == 0xA0 || cp == 0x2007 || cp == 0x202F || (cp >= 0x2000 && cp <= 0x200A))
			i = index_of(' ');
		else if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xAD || cp == 0xFEFF ||
			 (cp >= 0x200E && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
			 (cp >= 0x2066 && cp <= 0x2069) || (cp >= 0x591 && cp <= 0x5C7))
			return;		/* nothing to draw: format characters, Hebrew points */
		else
			i = qmark;
	}
	m = f->data + 4 + 10 * i;
	g->adv = m[0];
	g->x = (int8_t) m[1];
	g->y = (int8_t) m[2];
	g->w = m[3];
	g->h = m[4];
	off = be32(m + 6);
	g->bits = off ? f->data + off : NULL;
}

int font_advance(ew_font *f, uint32_t cp)
{
	GLYPH g;
	font_glyph(f, cp, &g);
	return g.adv;
}

uint32_t utf8_next(const char **p, const char *end)
{
	const unsigned char *s = (const unsigned char *) *p;
	uint32_t c = *s++, n, min;

	if (c < 0x80) {
		*p = (const char *) s;
		return c;
	}
	if (c >= 0xC2 && c < 0xE0) { n = 1; c &= 0x1F; min = 0x80; }
	else if (c >= 0xE0 && c < 0xF0) { n = 2; c &= 0x0F; min = 0x800; }
	else if (c >= 0xF0 && c < 0xF5) { n = 3; c &= 0x07; min = 0x10000; }
	else { *p = (const char *) s; return 0xFFFD; }
	if ((const char *) s + n > end) {
		*p = (const char *) s;
		return 0xFFFD;
	}
	while (n--) {
		if ((*s & 0xC0) != 0x80) {
			*p = (const char *) s;
			return 0xFFFD;
		}
		c = c << 6 | (*s++ & 0x3F);
	}
	*p = (const char *) s;
	return c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000) ? 0xFFFD : c;
}

long font_width(ew_font *f, const char *s, size_t n)
{
	const char *e = s + n;
	long w = 0;
	while (s < e)
		w += font_advance(f, utf8_next(&s, e));
	return w;
}
