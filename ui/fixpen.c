/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fixpen.c - the pen for one of EWeb's own pictures (the toolbar's icons,
 * the throbber) with EWeb's colours on: the nearest of the pens whose
 * colours stay (the system's 16 and pagecol.c's fixed 203, as a page's
 * pictures take, pic_pal_init), none of the 36 that each page sets. The
 * icons' looks are made once and kept; with screen.c's pen() a pixel on a
 * page's pen changed colour with the next page, and was black where that
 * page set none (Erez's Stop icon, 20 Nov 2026). Each colour is kept whole:
 * pen()'s cache, by 4 bits a part, made greys 12 apart one pen, and the
 * icons are drawn on the very pens (tools/mktbicons.py).
 *
 * A file of its own, linked last: screen.o stays as 1.22 laid it out (a
 * function added there moved its drawing code, and the stock Falcon's home
 * page took 10 ms more; the speed pairs, 20 Nov 2026).
 */
#include <stdlib.h>
#include <string.h>
#include "pagecol.h"
#include "ui.h"

#define FIXED_N 512			/* a few hundred colours ask */

/* the colours asked (+ 1 << 24, 0 none) and their pixel values: made when
   first needed, in 256 colours alone (2.5 KB less in BSS kept the stock
   Falcon's NVDI its big page's reload) */
static unsigned long *asked;
static unsigned char *px;

unsigned char scr_fixed_pen(unsigned long rgb, const unsigned char (*pal)[3])
{
	unsigned long key = (rgb & 0xffffffUL) | 1UL << 24;
	unsigned h = (unsigned) ((rgb ^ rgb >> 9 ^ rgb >> 17) & (FIXED_N - 1));
	long best = 0x7fffffffL;
	short i, pick = 0, probe;
	int r = (int) (rgb >> 16 & 255), g = (int) (rgb >> 8 & 255), b = (int) (rgb & 255);
	if (asked == NULL && (asked = malloc(FIXED_N * (sizeof *asked + 1))) != NULL) {
		memset(asked, 0, FIXED_N * sizeof *asked);
		px = (unsigned char *) (asked + FIXED_N);
	}
	for (probe = 0; asked != NULL && probe < FIXED_N && asked[h] != 0; probe++) {
		/* (kept: the fixed pens' colours are the same whenever EWeb's are on) */
		if (asked[h] == key)
			return px[h];
		h = (h + 1) & (FIXED_N - 1);
	}
	for (i = 0; i < EW_PIC_PENS; i++) {
		long dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
		long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
		if (d < best) {
			best = d;
			pick = i;
		}
	}
	if (asked != NULL && probe < FIXED_N) {	/* (full, or no memory: worked out each time) */
		asked[h] = key;
		px[h] = (unsigned char) pick;
	}
	return (unsigned char) pick;
}
