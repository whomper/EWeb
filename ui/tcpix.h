/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tcpix.h - a true-colour screen's pixels: how many bytes, where each
 * colour's bits go (vq_scrninfo's answer), and the canvas's 5-6-5 rows
 * made that form (ui/screen.c; tests/screen/tctest.c)
 */
#ifndef TCPIX_H
#define TCPIX_H

#include <stdint.h>

typedef struct {
	short bytes;		/* a pixel's: 2, 3 or 4 */
	short intel;		/* its bytes the other way round */
	short same;		/* the canvas's own 5-6-5 (bytes turned where intel): nothing to widen */
	short bits[3];		/* red's, green's, blue's */
	uint32_t r[32], g[64], b[32];	/* each 5-6-5 part's bits where the screen keeps them */
	uint32_t fill;		/* bits set in every pixel (alpha) */
} TC_FORM;

/* the form of a screen of planes bits a pixel (15, 16, 24 or 32), from
   vq_scrninfo's 272 words (NULL where the VDI hasn't it); 0: not one */
int tc_form(TC_FORM *f, short planes, const short *info);

/* vq_scrninfo's words 8..11, 14 and 16..79 (each colour's bits) made
   from what the screen showed: pure red, green and blue drawn by the VDI
   and read back (seen[c], a pixel's bytes as the screen keeps them),
   taken in the bytes' order or the other way round, whichever gives
   each colour one run of bits of its own; 0 if neither does */
int tc_seen(short *info, short planes, const uint8_t seen[3][4]);

/* n pixels of the canvas's (5-6-5) as the screen keeps them */
void tc_row(const TC_FORM *f, const uint16_t *src, uint8_t *dst, long n);

/* "32 bits, red 16..23" and so on, for the log */
const char *tc_name(const TC_FORM *f);

#endif
