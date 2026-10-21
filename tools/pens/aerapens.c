/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aerapens.c - a picture made into an Atari screen's pens, on the Pi's
 * gateway (AERA-Gateway's webgate), with the very code the Atari runs:
 * common/src/pic.c's decode, scaler and dither. Built twice: aerapens-lj
 * as EWEB.PRG decodes (libjpeg-turbo, -DPIC_LIBJPEG; the 68030's way, the
 * DSP's dither being the same pens to the bit) and aerapens-tj as
 * EWEBST.PRG does (TJpgDec, jpegdc). The pens are the Atari's byte for
 * byte (tests/pic/pens.py runs both against the same code as 68000 and
 * 68030 code under QEMU).
 *
 *   aerapens W H N [MAXPX] < IN > OUT
 *
 * IN is the screen's N colours (2 to 256, R G B bytes each), then the
 * picture (JPEG, PNG or GIF). W x H is the size shown; H 0: W wide at
 * most (its own width if smaller), as high as its shape says. MAXPX: the
 * most pixels its file may have (40 million). OUT, all numbers
 * big-endian (EWeb's protocol, docs/DESIGN.md there):
 *
 *   0  "EWPN"
 *   4  1 (the form), the bits a pen (1, 2, 4, 8), flags (1: a mask
 *      follows the pens), N - 1
 *   8  width, height, the picture's own width and height (2 bytes each)
 *   16 the colours' CRC-32 (zlib's), 4 bytes
 *   20 the pens, a row at a time, high bits first, each row whole bytes;
 *      then, with flag 1, the mask the same way (a bit a pixel: 1 where
 *      it is shown, its alpha 128 or more)
 *
 * Exit 0, or 1 (wrong arguments or input), 2 (not decoded: a kind it
 * isn't, damaged, too big), 3 (out of memory); a line on stderr says why.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"

static unsigned long crc32(const unsigned char *d, long n)
{
	unsigned long c = 0xFFFFFFFFUL;
	long i;
	int k;
	for (i = 0; i < n; i++) {
		c ^= d[i];
		for (k = 0; k < 8; k++)
			c = c & 1 ? (c >> 1) ^ 0xEDB88320UL : c >> 1;
	}
	return c ^ 0xFFFFFFFFUL;
}

static unsigned char *read_all(FILE *f, long *n)
{
	long size = 1 << 16, len = 0;
	unsigned char *d = malloc((size_t) size);
	size_t got;
	while (d != NULL && (got = fread(d + len, 1, (size_t) (size - len), f)) > 0) {
		len += (long) got;
		if (len == size) {
			unsigned char *m = size < (1L << 30) ? realloc(d, (size_t) size * 2) : NULL;
			if (m == NULL) {
				free(d);
				return NULL;
			}
			d = m;
			size *= 2;
		}
	}
	*n = len;
	return d;
}

static void put16(unsigned char *o, long v)
{
	o[0] = (unsigned char) (v >> 8);
	o[1] = (unsigned char) v;
}

int main(int argc, char **argv)
{
	long w, h, n, maxpx = 40000000L, len, sw, sh, row, mrow, size, y, x;
	unsigned char *in, *out, *pens, *o;
	unsigned long crc;
	int depth, why;
	PIC_PAL pal;
	PIC *p;
	if (argc < 4 || argc > 5) {
		fprintf(stderr, "usage: aerapens W H N [MAXPX] < colours+picture > pens\n");
		return 1;
	}
	w = atol(argv[1]);
	h = atol(argv[2]);
	n = atol(argv[3]);
	if (argc > 4)
		maxpx = atol(argv[4]);
	if (w < 1 || w > 4096 || h < 0 || h > 4096 || n < 2 || n > 256 || maxpx < 1) {
		fprintf(stderr, "aerapens: W 1 to 4096, H 0 to 4096, N 2 to 256\n");
		return 1;
	}
	in = read_all(stdin, &len);
	if (in == NULL) {
		fprintf(stderr, "aerapens: out of memory\n");
		return 3;
	}
	if (len < n * 3 + 8) {
		fprintf(stderr, "aerapens: no picture after the colours\n");
		return 1;
	}
	if (!pic_size(in + n * 3, len - n * 3, &sw, &sh) || sw < 1 || sh < 1 || sw > 0xFFFF || sh > 0xFFFF) {
		fprintf(stderr, "aerapens: %s, not a picture it takes\n", pic_kind_name(pic_kind(in + n * 3, len - n * 3)));
		return 2;
	}
	if (h == 0) {
		/* W wide at most, its shape kept */
		if (w > sw)
			w = sw;
		h = (sh * w + sw / 2) / sw;
		if (h < 1)
			h = 1;
		if (h > 4096) {
			fprintf(stderr, "aerapens: too tall at that width\n");
			return 2;
		}
	}
	memset(&pal, 0, sizeof pal);
	pic_pal_init(&pal, (int) n, (const unsigned char (*)[3]) in);
	crc = crc32(in, n * 3);
	p = pic_decode(in + n * 3, len - n * 3, (int) w, (int) h, maxpx, &why);
	if (p == NULL) {
		fprintf(stderr, "aerapens: %s\n", pic_why_text(why));
		return why == PIC_MEMORY ? 3 : 2;
	}
	pens = malloc((size_t) (w * h));
	depth = n <= 2 ? 1 : n <= 4 ? 2 : n <= 16 ? 4 : 8;
	row = (w * depth + 7) / 8;
	mrow = p->alpha != NULL ? (w + 7) / 8 : 0;
	size = 20 + h * (row + mrow);
	out = calloc(1, (size_t) size);
	if (pens == NULL || out == NULL) {
		fprintf(stderr, "aerapens: out of memory\n");
		return 3;
	}
	pic_dither(p, &pal, pens);
	memcpy(out, "EWPN", 4);
	out[4] = 1;
	out[5] = (unsigned char) depth;
	out[6] = p->alpha != NULL ? 1 : 0;
	out[7] = (unsigned char) (n - 1);
	put16(out + 8, w);
	put16(out + 10, h);
	put16(out + 12, sw);
	put16(out + 14, sh);
	out[16] = (unsigned char) (crc >> 24);
	out[17] = (unsigned char) (crc >> 16);
	out[18] = (unsigned char) (crc >> 8);
	out[19] = (unsigned char) crc;
	o = out + 20;
	for (y = 0; y < h; y++, o += row) {
		const unsigned char *s = pens + y * w;
		for (x = 0; x < w; x++) {
			long bit = x * depth;
			o[bit >> 3] |= (unsigned char) (s[x] << (8 - depth - (bit & 7)));
		}
	}
	if (p->alpha != NULL)
		for (y = 0; y < h; y++, o += mrow)
			for (x = 0; x < w; x++)
				if (p->alpha[y * w + x] >= 128)
					o[x >> 3] |= (unsigned char) (0x80 >> (x & 7));
	if (fwrite(out, 1, (size_t) size, stdout) != (size_t) size || fflush(stdout) != 0) {
		fprintf(stderr, "aerapens: can't write\n");
		return 1;
	}
	return 0;
}
