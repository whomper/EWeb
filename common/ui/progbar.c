/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The family's progress bar (progbar.h; the guidelines 5.9).
 */
#include "gemui.h"
#include "progbar.h"

static short npen = 1, pen[PB_SHADES];

/* the blue's ends, in the VDI's 0 to 1000 */
static const short dark[3] = { 0, 40, 330 };
static const short light[3] = { 620, 800, 1000 };

static long dist(const short *a, const short *b)
{
	long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return 3 * dr * dr + 4 * dg * dg + 2 * db * db;	/* (pen_nearest's weights) */
}

/* black: the darkest of the first n pens */
static short black_pen(short h, short n)
{
	short p, best = 1, rgb[3];
	long bl = 0x7fffffffL;
	for (p = 0; p < n; p++) {
		long l;
		vq_color(h, p, rgb);
		l = 299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2];
		if (l < bl) {
			bl = l;
			best = p;
		}
	}
	return best;
}

void progbar_pens(short h, short ground)
{
	short g[3], rgb[3], p, i;
	vq_color(h, ground, g);
	if (planes == 8) {
		/* 256 colours: the blues the palette has, dark to light, spread
		   along the bar (pens 16 and up hold whatever palette is loaded:
		   TOS 4's has no light blue, NVDI's and EWeb's do; found again when
		   it changes, the app's call), never the ground's own. A palette
		   without a blue takes each step's nearest pen. */
		static short blue[256];
		static long bl[256];
		short nb = 0, j;
		for (p = 0; p < 256; p++) {
			vq_color(h, p, rgb);
			if (p == ground || dist(rgb, g) < 3000L)
				continue;	/* (it wouldn't show) */
			/* blue, from deep blue to sky blue: more blue than green by a
			   tenth at least, and no more red than green (no purple) */
			if (rgb[2] - rgb[1] < 100 || rgb[0] > rgb[1] + 60)
				continue;
			bl[nb] = 299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2];
			/* one pen a colour: the same blue twice is one */
			for (j = 0; j < nb; j++)
				if (bl[j] == bl[nb])
					break;
			if (j < nb)
				continue;
			for (j = nb; j > 0 && bl[j - 1] > bl[nb]; j--)
				;
			{
				long l = bl[nb];
				short k;
				for (k = nb; k > j; k--) {
					bl[k] = bl[k - 1];
					blue[k] = blue[k - 1];
				}
				bl[j] = l;
				blue[j] = p;
			}
			nb++;
		}
		if (nb > 0) {
			/* PB_SHADES of them, evenly from the darkest to the lightest */
			npen = nb < PB_SHADES ? nb : PB_SHADES;
			for (i = 0; i < npen; i++)
				pen[i] = blue[npen > 1 ? (long) (nb - 1) * i / (npen - 1) : 0];
		} else {
			long bd[PB_SHADES];
			short want[PB_SHADES][3];
			for (i = 0; i < PB_SHADES; i++) {
				int c;
				for (c = 0; c < 3; c++)
					want[i][c] = (short) (dark[c] + (long) (light[c] - dark[c]) * i / (PB_SHADES - 1));
				bd[i] = 0x7fffffffL;
				pen[i] = 1;
			}
			for (p = 0; p < 256; p++) {
				vq_color(h, p, rgb);
				if (p == ground || dist(rgb, g) < 3000L)
					continue;
				for (i = 0; i < PB_SHADES; i++) {
					long d = dist(rgb, want[i]);
					if (d < bd[i]) {
						bd[i] = d;
						pen[i] = p;
					}
				}
			}
			npen = PB_SHADES;
		}
	} else if (planes == 4) {
		/* 16 colours: pens 1 to 15 in order, less any the ground's
		   colour (white, the bar's light grey) */
		npen = 0;
		for (p = 1; p < 16; p++) {
			vq_color(h, p, rgb);
			if (p == ground || dist(rgb, g) < 3000L)
				continue;
			pen[npen++] = p;
		}
		if (npen == 0)
			pen[npen++] = black_pen(h, 16);
	} else {
		/* mono, 4 colours, true colour: black */
		npen = 1;
		pen[0] = black_pen(h, planes < 4 ? (short) (1 << planes) : 16);
	}
}

void progbar_fill(short h, short x, short y, short w, short hh, short from, short to)
{
	short i, a, b, pxy[4];
	if (from < 0)
		from = 0;
	if (to > PB_FULL)
		to = PB_FULL;
	if (from >= to || w <= 0 || hh <= 0)
		return;
	a = (short) ((long) w * from / PB_FULL);
	b = (short) ((long) w * to / PB_FULL);
	if (a >= b)
		return;
	vsf_interior(h, 1);	/* FIS_SOLID */
	vsf_perimeter(h, 0);
	vswr_mode(h, 1);	/* MD_REPLACE */
	for (i = 0; i < npen; i++) {
		/* each pen's place along the bar: from s to e */
		short s = (short) ((long) w * i / npen), e = (short) ((long) w * (i + 1) / npen);
		if (s < a)
			s = a;
		if (e > b)
			e = b;
		if (s >= e)
			continue;
		vsf_color(h, pen[i]);
		pxy[0] = x + s;
		pxy[1] = y;
		pxy[2] = x + e - 1;
		pxy[3] = y + hh - 1;
		v_bar(h, pxy);
	}
}

short progbar_npens(void)
{
	return npen;
}

short progbar_pen(short i)
{
	return i >= 0 && i < npen ? pen[i] : -1;
}
