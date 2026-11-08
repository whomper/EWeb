/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pagecol.c - EWeb's own 256 colours, and a page's own colours on the
 * last EW_PAGE of them (pagecol.h).
 */
#include <string.h>

#include "pagecol.h"
#include "ecss.h"

void ew_own_colour(int k, uint8_t rgb[3])
{
	/* the greys past the cube's six, more of them light (pages'
	   backgrounds) */
	static const uint8_t grey[23] = { 13, 26, 38, 64, 77, 89, 115, 128, 140, 166, 179, 191,
					  212, 220, 226, 231, 235, 238, 241, 244, 247, 250, 253 };
	if (k < 180) {
		/* 6 levels of red and green, 5 of blue (the eye's least: the 36
		   pens it gives up are the page's) */
		rgb[0] = (uint8_t) (k / 30 * 51);
		rgb[1] = (uint8_t) (k / 5 % 6 * 51);
		rgb[2] = (uint8_t) (k % 5 * 255 / 4);
	} else if (k < EW_FIXED) {
		rgb[0] = rgb[1] = rgb[2] = grey[k - 180];
	} else {
		rgb[0] = rgb[1] = rgb[2] = 0;
	}
}

/* the candidates: each colour once, how much of the page wants it */
#define CANDS 512
typedef struct {
	uint32_t rgb;
	uint32_t weight;
	uint32_t err;			/* to the nearest pen or colour chosen */
} cand;

static long dist(uint32_t a, const uint8_t *b)
{
	long dr = (long) (a >> 16 & 255) - b[0], dg = (long) (a >> 8 & 255) - b[1], db = (long) (a & 255) - b[2];
	return 2 * dr * dr + 4 * dg * dg + 3 * db * db;	/* (ui/screen.c's pen()) */
}

/* argb as it shows on bg (0: too faint to count) */
static int shown(uint32_t argb, uint32_t bg, uint32_t *rgb)
{
	unsigned a = argb >> 24, b = 255 - a, k;
	uint32_t v = 0;
	if (a < 64)
		return 0;
	for (k = 0; k < 24; k += 8)
		v |= (((argb >> k & 255) * a + (bg >> k & 255) * b + 127) / 255) << k;
	*rgb = v;
	return 1;
}

static void add(cand *c, int *nc, uint32_t rgb, uint32_t weight)
{
	int i;
	if (weight == 0)
		return;
	for (i = 0; i < *nc; i++)
		if (c[i].rgb == rgb) {
			c[i].weight = c[i].weight > 0xffffffffUL - weight ? 0xffffffffUL : c[i].weight + weight;
			return;
		}
	if (*nc < CANDS) {
		c[*nc].rgb = rgb;
		c[*nc].weight = weight;
		(*nc)++;
	}
}

static uint32_t mix(uint32_t a, uint32_t b, unsigned t)	/* t of 4 toward b */
{
	uint32_t v = 0;
	unsigned k;
	for (k = 0; k < 32; k += 8)
		v |= (((a >> k & 255) * (4 - t) + (b >> k & 255) * t + 2) / 4) << k;
	return v;
}

int page_colours(const page *p, const uint8_t (*pal)[3], int n, uint32_t *out, int max)
{
	static cand c[CANDS];
	int nc = 0, i, j, got = 0;
	long k;
	uint32_t bg = p->bg & 0xffffff, rgb;
	/* the background: a pen of its own if any colour is */
	add(c, &nc, bg, 1UL << 24);
	for (k = 0; k < p->nitems; k++) {
		const dl_item *d = &p->items[k];
		uint32_t area = (uint32_t) (d->w > 0 && d->h > 0 ? (d->w > 4096 ? 4096 : d->w) * (d->h > 4096 ? 4096 : d->h) : 0);
		switch (d->type) {
		case DL_FILL:
			if (shown(d->rgb, bg, &rgb))
				add(c, &nc, rgb, area);
			break;
		case DL_TEXT:
			/* (its strokes: about a third of its box) */
			if (shown(d->rgb, bg, &rgb))
				add(c, &nc, rgb, area / 3);
			break;
		case DL_CTL:
			if (shown(d->ink, bg, &rgb))
				add(c, &nc, rgb, area / 3);
			break;
		case DL_GRAD: {
			/* its stops and three colours between each two (a smooth
			   gradient's steps), each a share of its area */
			const ec_grad *g = d->img;
			uint32_t prev = 0, a = d->rgb >> 24, share;
			int have = 0, ns = 0, s;
			if (g == NULL || d->tile == NULL)
				break;
			for (s = 0; s < g->n; s++)
				ns += !(g->s[s].flags & EC_GS_HINT);
			if (ns == 0)
				break;
			share = area / (uint32_t) (ns * 4);
			for (s = 0; s < g->n; s++) {
				uint32_t argb = g->s[s].argb;
				if (g->s[s].flags & EC_GS_HINT)
					continue;
				argb = (argb & 0xffffff) | ((argb >> 24) * a / 255) << 24;
				if (have) {
					unsigned t;
					for (t = 1; t < 4; t++)
						if (shown(mix(prev, argb, t), bg, &rgb))
							add(c, &nc, rgb, share);
				}
				if (shown(argb, bg, &rgb))
					add(c, &nc, rgb, share);
				prev = argb;
				have = 1;
			}
			break;
		}
		}
	}
	/* each one's distance to the nearest pen */
	for (i = 0; i < nc; i++) {
		long best = 0x7fffffffL;
		for (j = 0; j < n; j++) {
			long e = dist(c[i].rgb, pal[j]);
			if (e < best)
				best = e;
		}
		c[i].err = (uint32_t) best;
	}
	/* the most wanted first: weight by how far, until none is far
	   (about 4 levels a part off: as close as a pen of its own would
	   look) */
	while (got < max) {
		int pick = -1;
		uint64_t bestw = 0;
		for (i = 0; i < nc; i++) {
			uint64_t w;
			if (c[i].err <= 9 * 16)
				continue;
			w = (uint64_t) c[i].weight * (c[i].err > 40000 ? 40000 : c[i].err);
			if (w > bestw) {
				bestw = w;
				pick = i;
			}
		}
		if (pick < 0)
			break;
		out[got++] = c[pick].rgb;
		{
			uint8_t q[3];
			q[0] = (uint8_t) (c[pick].rgb >> 16);
			q[1] = (uint8_t) (c[pick].rgb >> 8);
			q[2] = (uint8_t) c[pick].rgb;
			for (i = 0; i < nc; i++) {
				long e = dist(c[i].rgb, q);
				if ((uint32_t) e < c[i].err)
					c[i].err = (uint32_t) e;
			}
		}
	}
	return got;
}
