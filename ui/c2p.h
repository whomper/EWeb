/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * c2p.h - 16 of the canvas's pixels (a byte each) as the Atari's
 * interleaved planes' words, by merges between registers: a pair's bits
 * at one distance swapped through a mask, t = ((a >> s) ^ b) & m,
 * b ^= t, a ^= t << s (or the same leftwards). Seven instructions a
 * pair, and the planes' words come out whole in the registers, two a
 * long, in the order they are stored. The stages were found by a search
 * over the bits' places (a pixel's 4 bits and a plane's 3 swapped round)
 * and are checked against the bits taken one by one in
 * tests/screen/c2ptest.c (ui/screen.c's to_planes on the 68030)
 */
#ifndef EWEB_C2P_H
#define EWEB_C2P_H

#include <stdint.h>

/* rightwards: b's bits under m swapped with a's s places to their left */
#define C2P_MR(a, b, s, m) do { uint32_t t_ = ((a >> (s)) ^ b) & (m); b ^= t_; a ^= t_ << (s); } while (0)
/* leftwards: b's bits under m swapped with a's s places to their right */
#define C2P_ML(a, b, s, m) do { uint32_t t_ = ((a << (s)) ^ b) & (m); b ^= t_; a ^= t_ >> (s); } while (0)

/* 8 planes: q the 16 pixels as 4 longs (pixel 0 in q[0]'s high byte),
   out the 8 words (plane 0's first, pixel 0 in each one's high bit) as 4
   longs; five stages of two merges */
static inline __attribute__((always_inline)) void c2p8(const uint32_t *q, uint32_t *out)
{
	uint32_t a = q[0], b = q[1], c = q[2], d = q[3];
	C2P_ML(a, c, 8, 0xff00ff00UL);
	C2P_ML(b, d, 8, 0xff00ff00UL);
	C2P_ML(a, c, 1, 0xaaaaaaaaUL);
	C2P_ML(b, d, 1, 0xaaaaaaaaUL);
	C2P_MR(a, c, 16, 0x0000ffffUL);
	C2P_MR(b, d, 16, 0x0000ffffUL);
	C2P_MR(a, c, 2, 0x33333333UL);
	C2P_MR(b, d, 2, 0x33333333UL);
	C2P_ML(a, b, 4, 0xf0f0f0f0UL);
	C2P_ML(c, d, 4, 0xf0f0f0f0UL);
	out[0] = b;	/* planes 0 and 1 */
	out[1] = d;	/* 2 and 3 */
	out[2] = a;	/* 4 and 5 */
	out[3] = c;	/* 6 and 7 */
}

/* 4 planes (or 2: out[0] alone): the pixels' low halves two to a byte
   (pixel i's high, i + 8's low), then five merges of one pair; out the
   4 words as 2 longs */
static inline __attribute__((always_inline)) void c2p4(const uint32_t *q, uint32_t *out)
{
	uint32_t x = (q[0] & 0x0f0f0f0fUL) << 4 | (q[2] & 0x0f0f0f0fUL);
	uint32_t y = (q[1] & 0x0f0f0f0fUL) << 4 | (q[3] & 0x0f0f0f0fUL);
	C2P_ML(x, y, 4, 0xf0f0f0f0UL);
	C2P_ML(x, y, 8, 0xff00ff00UL);
	C2P_ML(x, y, 1, 0xaaaaaaaaUL);
	C2P_MR(x, y, 16, 0x0000ffffUL);
	C2P_MR(x, y, 2, 0x33333333UL);
	out[0] = x;	/* planes 0 and 1 */
	out[1] = y;	/* 2 and 3 */
}

#endif
