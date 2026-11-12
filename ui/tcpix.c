/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tcpix.c - a true-colour screen's pixels. EWeb draws a page in 5-6-5
 * (the Falcon's own: ui/screen.c), or at 32 bits on a screen of 8 bits
 * a colour (a card's 24 or 32, the FireBee's); a screen of another form
 * than the canvas's (a card's 15, 24 or 32 bits, NVDI's or fVDI's) has
 * each row made its form as it is copied to the screen (DESIGN.md,
 * Screens deeper than the Falcon's).
 *
 * vq_scrninfo says where each colour's bits go, a bit's place in the
 * pixel for each (work_out[16..], [32..], [48..], [64..]). Its documents
 * differ on whether they are listed from the most or the least
 * significant, so they are taken as sorted: the lowest place is the
 * least significant bit, whichever way the VDI lists them (every
 * screen keeps a colour's bits in order). work_out[14]'s bit 7 is the
 * bytes the other way round (EdDI 1.1: a card on an Intel bus).
 */
#include <stdio.h>
#include <string.h>

#include "tcpix.h"

/* bits n of where each colour's go: the screens with no vq_scrninfo
   (or one that makes no sense), as their planes have them */
static void standard(short planes, short n[4], short at[4][8])
{
	static const short k15[3][2] = { { 10, 5 }, { 5, 5 }, { 0, 5 } };
	static const short k16[3][2] = { { 11, 5 }, { 5, 6 }, { 0, 5 } };
	static const short k24[3][2] = { { 16, 8 }, { 8, 8 }, { 0, 8 } };
	const short (*k)[2] = planes == 15 ? k15 : planes == 16 ? k16 : k24;
	short c, j;
	for (c = 0; c < 3; c++) {
		n[c] = k[c][1];
		for (j = 0; j < n[c]; j++)
			at[c][j] = (short) (k[c][0] + j);
	}
	n[3] = 0;
}

/* vq_scrninfo's places, sorted; 0 if they don't make a screen of
   bytes bytes a pixel (a colour of 1 to 8 bits, no bit twice) */
static int from_info(const short *info, short bytes, short n[4], short at[4][8])
{
	static const short base[4] = { 16, 32, 48, 64 };
	uint32_t used = 0;
	short c, j, k;
	for (c = 0; c < 4; c++) {
		n[c] = info[8 + c];
		if (n[c] < (c < 3) || n[c] > 8)
			return 0;
		for (j = 0; j < n[c]; j++) {
			short p = info[base[c] + j];
			if (p < 0 || p >= bytes * 8 || (used >> p & 1))
				return 0;
			used |= 1UL << p;
			for (k = j; k > 0 && at[c][k - 1] > p; k--)
				at[c][k] = at[c][k - 1];
			at[c][k] = p;
		}
	}
	return 1;
}

/* a part of src bits (5, 6 or 8) at each value, widened to 8 bits, its top
   n of them at the screen's places */
static void table(uint32_t *t, short src, short n, const short *at)
{
	short v, j;
	for (v = 0; v < 1 << src; v++) {
		unsigned e = src == 8 ? (unsigned) v : (unsigned) (v << (8 - src) | v >> (2 * src - 8));
		unsigned d = e >> (8 - n);
		uint32_t px = 0;
		for (j = 0; j < n; j++)
			if (d >> j & 1)
				px |= 1UL << at[j];
		t[v] = px;
	}
}

int tc_form(TC_FORM *f, short planes, const short *info)
{
	short n[4], at[4][8], j;
	memset(f, 0, sizeof *f);
	if (planes == 15 || planes == 16)
		f->bytes = 2;
	else if (planes == 24 || planes == 32)
		f->bytes = (short) (planes / 8);
	else
		return 0;
	if (info == NULL || !from_info(info, f->bytes, n, at))
		standard(planes, n, at);
	f->intel = info != NULL && (info[14] & 0x80);
	table(f->r, 5, n[0], at[0]);
	table(f->g, 6, n[1], at[1]);
	table(f->b, 5, n[2], at[2]);
	for (j = 0; j < n[3]; j++)
		f->fill |= 1UL << at[3][j];
	for (j = 0; j < 3; j++)
		f->bits[j] = n[j];
#ifndef EW_ST
	if (f->bytes >= 3 && n[0] == 8 && n[1] == 8 && n[2] == 8) {
		f->deep = 1;
		table(f->r8, 8, 8, at[0]);
		table(f->g8, 8, 8, at[1]);
		table(f->b8, 8, 8, at[2]);
	}
#endif
	/* the Falcon's 5-6-5 (red's top bit the pixel's: as NVDI has it,
	   green's least bit sometimes the overlay's and not listed) */
	f->same = f->bytes == 2 && n[0] == 5 && at[0][4] == 15 && at[0][0] == 11 && n[2] == 5 && at[2][0] == 0;
	return 1;
}

/* the bits of v from the lowest, as places in info from at (n of them) */
static short places(short *info, short at, uint32_t v)
{
	short p, n = 0;
	for (p = 0; p < 32; p++)
		if (v >> p & 1)
			info[at + n++] = p;
	return n;
}

int tc_seen(short *info, short planes, const uint8_t seen[3][4])
{
	static const short base[4] = { 16, 32, 48, 64 };
	short bytes = planes <= 16 ? 2 : (short) (planes / 8), turned, c, k;
	for (turned = 0; turned < 2; turned++) {
		uint32_t v[3], all, run;
		int ok = 1;
		for (c = 0; c < 3; c++) {
			v[c] = 0;
			for (k = 0; k < bytes; k++)
				v[c] |= (uint32_t) seen[c][turned ? bytes - 1 - k : k] << (8 * (bytes - 1 - k));
		}
		all = v[0] & v[1] & v[2];	/* (alpha: set in each) */
		for (c = 0; c < 3 && ok; c++) {
			uint32_t m = v[c] & ~all;
			if (m == 0 || (m & v[(c + 1) % 3] & ~all) != 0)
				ok = 0;		/* none, or shared with another */
			else {
				run = m;
				while (!(run & 1))
					run >>= 1;
				if ((run & (run + 1)) != 0 || run > 255)
					ok = 0;	/* not one run of 8 bits at most */
			}
		}
		if (!ok)
			continue;
		for (k = 8; k < 128; k++)
			info[k] = k >= 16 ? -1 : 0;
		for (c = 0; c < 3; c++)
			info[8 + c] = places(info, base[c], v[c] & ~all);
		info[11] = places(info, base[3], all);
		if (info[11] > 8)
			continue;
		info[14] = (short) (turned ? 0x80 : 0);
		return 1;
	}
	return 0;
}

void tc_row(const TC_FORM *f, const uint16_t *src, uint8_t *dst, long n)
{
	long i;
	if (f->same) {
		if (!f->intel) {
			memcpy(dst, src, (size_t) n * 2);
		} else {
			for (i = 0; i < n; i++) {
				dst[2 * i] = (uint8_t) src[i];
				dst[2 * i + 1] = (uint8_t) (src[i] >> 8);
			}
		}
		return;
	}
	for (i = 0; i < n; i++) {
		unsigned v = src[i];
		uint32_t px = f->r[v >> 11] | f->g[v >> 5 & 63] | f->b[v & 31] | f->fill;
		switch (f->bytes) {
		case 2:
			if (f->intel) {
				dst[0] = (uint8_t) px;
				dst[1] = (uint8_t) (px >> 8);
			} else {
				dst[0] = (uint8_t) (px >> 8);
				dst[1] = (uint8_t) px;
			}
			dst += 2;
			break;
		case 3:
			if (f->intel) {
				dst[0] = (uint8_t) px;
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) (px >> 16);
			} else {
				dst[0] = (uint8_t) (px >> 16);
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) px;
			}
			dst += 3;
			break;
		default:
			if (f->intel) {
				dst[0] = (uint8_t) px;
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) (px >> 16);
				dst[3] = (uint8_t) (px >> 24);
			} else {
				dst[0] = (uint8_t) (px >> 24);
				dst[1] = (uint8_t) (px >> 16);
				dst[2] = (uint8_t) (px >> 8);
				dst[3] = (uint8_t) px;
			}
			dst += 4;
			break;
		}
	}
}

#ifndef EW_ST
void tc_row32(const TC_FORM *f, const uint8_t *src, uint8_t *dst, long n)
{
	long i;
	if (f->bytes == 4) {
		for (i = 0; i < n; i++, src += 4, dst += 4) {
			uint32_t px = f->r8[src[1]] | f->g8[src[2]] | f->b8[src[3]] | f->fill;
			if (f->intel) {
				dst[0] = (uint8_t) px;
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) (px >> 16);
				dst[3] = (uint8_t) (px >> 24);
			} else {
				dst[0] = (uint8_t) (px >> 24);
				dst[1] = (uint8_t) (px >> 16);
				dst[2] = (uint8_t) (px >> 8);
				dst[3] = (uint8_t) px;
			}
		}
	} else {
		for (i = 0; i < n; i++, src += 4, dst += 3) {
			uint32_t px = f->r8[src[1]] | f->g8[src[2]] | f->b8[src[3]] | f->fill;
			if (f->intel) {
				dst[0] = (uint8_t) px;
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) (px >> 16);
			} else {
				dst[0] = (uint8_t) (px >> 16);
				dst[1] = (uint8_t) (px >> 8);
				dst[2] = (uint8_t) px;
			}
		}
	}
}
#endif

/* the lowest place a part's bits go (where it starts) */
static short lowest(const uint32_t *t, short top)
{
	uint32_t m = t[top];
	short p = 0;
	while (m != 0 && !(m & 1)) {
		m >>= 1;
		p++;
	}
	return p;
}

const char *tc_name(const TC_FORM *f)
{
	static char s[128];
	snprintf(s, sizeof s, "%d bits a pixel, red %d at %d, green %d at %d, blue %d at %d%s%s", f->bytes * 8,
		 f->bits[0], lowest(f->r, 31), f->bits[1], lowest(f->g, 63), f->bits[2], lowest(f->b, 31),
		 f->intel ? ", Intel order" : "", f->same ? " (5-6-5 as drawn)" :
#ifndef EW_ST
		 f->deep ? " (drawn at 32 bits)" :
#endif
		 "");
	return s;
}
