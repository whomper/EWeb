/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * c2ptest.c - ui/c2p.h: 16 pixels made the interleaved planes' words by
 * c2p8 and c2p4, each checked against the words made a bit at a time
 * (pixel p's bit k at plane k's bit 15 - p), over every pixel's value in
 * every place and 2 million runs of random pixels
 */
#include <stdio.h>
#include <string.h>

#include "c2p.h"

static int fails;
static long checks;

/* the words for planes 0 to np - 1, a bit at a time */
static void slow(const uint8_t *px, int np, uint16_t *w)
{
	int k, p;
	for (k = 0; k < np; k++) {
		w[k] = 0;
		for (p = 0; p < 16; p++)
			w[k] |= (uint16_t) (((px[p] >> k) & 1) << (15 - p));
	}
}

static void one(const uint8_t *px)
{
	uint32_t q[4], out[4];
	uint16_t want[8];
	int k, np;
	for (k = 0; k < 4; k++)
		q[k] = (uint32_t) px[4 * k] << 24 | (uint32_t) px[4 * k + 1] << 16 |
		       (uint32_t) px[4 * k + 2] << 8 | px[4 * k + 3];
	for (np = 2; np <= 8; np += 2) {
		if (np == 6)
			continue;
		slow(px, np, want);
		if (np == 8)
			c2p8(q, out);
		else
			c2p4(q, out);
		checks++;
		for (k = 0; k < np; k++) {
			uint16_t got = (uint16_t) (k & 1 ? out[k >> 1] : out[k >> 1] >> 16);
			if (got != want[k]) {
				if (fails++ < 10)
					printf("FAIL  %d planes: plane %d %04x, wanted %04x\n", np, k, got, want[k]);
				break;
			}
		}
	}
}

int main(void)
{
	uint8_t px[16];
	uint32_t seed = 12345;
	long n;
	int p, v;
	for (p = 0; p < 16; p++)
		for (v = 0; v < 256; v++) {
			memset(px, 0, sizeof px);
			px[p] = (uint8_t) v;
			one(px);
			memset(px, 0xff, sizeof px);
			px[p] = (uint8_t) v;
			one(px);
		}
	for (n = 0; n < 2000000; n++) {
		for (p = 0; p < 16; p++) {
			seed = seed * 1103515245u + 12345u;
			px[p] = (uint8_t) (seed >> 16);
		}
		one(px);
	}
	printf("c2ptest: %ld checks, %d failed\n", checks, fails);
	return fails != 0;
}
