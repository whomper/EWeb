/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * svgrender - an SVG file drawn as EWeb draws it (src/svg), to a PNG with
 * its alpha, for tests/svg/run.py to set against another renderer:
 *
 *     svgrender [-s W,H] FILE.SVG OUT.PNG     (W x H: the file's own size)
 *     svgrender -z FILE.SVG                   (its own size, printed)
 *     svgrender -n [-s W,H] FILE.SVG          (drawn, nothing written: for
 *                                               counting its instructions)
 *     svgrender -g [-s W,H] FILE.SVG          (its pixels on white as greys,
 *                                               a row a line in hex: run.py)
 *
 * -T: read by NetSurf's libsvgtiny instead (tiny.c), the yardstick.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svg.h"

PIC *tiny_decode(const char *data, long len, int w, int h, int *why);

static void be32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}

static unsigned long crc(unsigned long c, const unsigned char *p, size_t n)
{
	size_t i;
	int k;
	c = ~c & 0xffffffffUL;
	for (i = 0; i < n; i++) {
		c ^= p[i];
		for (k = 0; k < 8; k++)
			c = c & 1 ? (c >> 1) ^ 0xedb88320UL : c >> 1;
	}
	return ~c & 0xffffffffUL;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
	unsigned char h[8];
	unsigned long c;
	be32(h, (unsigned long) n);
	memcpy(h + 4, type, 4);
	fwrite(h, 1, 8, f);
	fwrite(data, 1, n, f);
	c = crc(0, h + 4, 4);
	c = crc(c, data, n);
	be32(h, c);
	fwrite(h, 1, 4, f);
}

/* the picture as RGBA, its deflate stored */
static int write_png(const char *path, const PIC *p)
{
	FILE *f = fopen(path, "wb");
	size_t row = 1 + 4 * (size_t) p->w, raw = row * p->h, i, z = 0, nblocks = (raw + 65534) / 65535;
	unsigned char *rb = malloc(raw), *zb = malloc(raw + 5 * nblocks + 6), head[13];
	unsigned long a = 1, b = 0;
	int x, y;
	if (f == NULL || rb == NULL || zb == NULL)
		return 0;
	for (y = 0; y < p->h; y++) {
		unsigned char *r = rb + y * row;
		r[0] = 0;
		for (x = 0; x < p->w; x++) {
			unsigned v = p->px[(long) y * p->w + x];
			r[1 + 4 * x] = (unsigned char) ((v >> 8 & 0xf8) | (v >> 13));
			r[2 + 4 * x] = (unsigned char) ((v >> 3 & 0xfc) | (v >> 9 & 3));
			r[3 + 4 * x] = (unsigned char) ((v << 3 & 0xf8) | (v >> 2 & 7));
			r[4 + 4 * x] = p->alpha ? p->alpha[(long) y * p->w + x] : 255;
		}
	}
	zb[z++] = 0x78;
	zb[z++] = 0x01;
	for (i = 0; i < raw; i += 65535) {
		size_t n = raw - i < 65535 ? raw - i : 65535;
		zb[z++] = i + n >= raw;
		zb[z++] = (unsigned char) n;
		zb[z++] = (unsigned char) (n >> 8);
		zb[z++] = (unsigned char) ~n;
		zb[z++] = (unsigned char) (~n >> 8);
		memcpy(zb + z, rb + i, n);
		z += n;
	}
	for (i = 0; i < raw; i++) {
		a = (a + rb[i]) % 65521;
		b = (b + a) % 65521;
	}
	be32(zb + z, b << 16 | a);
	z += 4;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	be32(head, (unsigned long) p->w);
	be32(head + 4, (unsigned long) p->h);
	head[8] = 8;
	head[9] = 6;
	head[10] = head[11] = head[12] = 0;
	chunk(f, "IHDR", head, 13);
	chunk(f, "IDAT", zb, z);
	chunk(f, "IEND", head, 0);
	fclose(f);
	free(rb);
	free(zb);
	return 1;
}

int main(int argc, char **argv)
{
	int i = 1, size_only = 0, none = 0, tiny = 0, grey = 0, why;
	long w = 0, h = 0, n;
	char *data;
	FILE *f;
	PIC *p;
	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc)
			sscanf(argv[++i], "%ld,%ld", &w, &h);
		else if (!strcmp(argv[i], "-z"))
			size_only = 1;
		else if (!strcmp(argv[i], "-n"))
			none = 1;
		else if (!strcmp(argv[i], "-T"))
			tiny = 1;
		else if (!strcmp(argv[i], "-g"))
			grey = 1;
	}
	if (i >= argc || (!size_only && !none && !grey && i + 1 >= argc)) {
		fprintf(stderr, "svgrender [-s W,H] FILE.SVG OUT.PNG | -z FILE.SVG\n");
		return 2;
	}
	if ((f = fopen(argv[i], "rb")) == NULL) {
		perror(argv[i]);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc((size_t) n + 1);
	if (data == NULL || fread(data, 1, (size_t) n, f) != (size_t) n)
		return 1;
	fclose(f);
	if (w <= 0 || h <= 0) {
		if (!svg_size(data, n, &w, &h)) {
			fprintf(stderr, "%s: not SVG\n", argv[i]);
			return 1;
		}
	}
	if (size_only) {
		printf("%ld %ld\n", w, h);
		return 0;
	}
	p = tiny ? tiny_decode(data, n, (int) w, (int) h, &why) : svg_decode(data, n, (int) w, (int) h, &why);
	if (p == NULL) {
		fprintf(stderr, "%s: %s\n", argv[i], pic_why_text(why));
		return 1;
	}
	if (grey) {
		/* each pixel over white, its grey (as the eye weighs the three) */
		int x, y;
		printf("%d %d\n", p->w, p->h);
		for (y = 0; y < p->h; y++) {
			for (x = 0; x < p->w; x++) {
				long k = (long) y * p->w + x;
				unsigned v = p->px[k], a = p->alpha ? p->alpha[k] : 255;
				unsigned r = (v >> 8 & 0xf8) | (v >> 13), g = (v >> 3 & 0xfc) | (v >> 9 & 3), b = (v << 3 & 0xf8) | (v >> 2 & 7);
				unsigned l = (r * 77 + g * 150 + b * 29) >> 8;
				printf("%02x", (l * a + 255 * (255 - a) + 127) / 255);
			}
			printf("\n");
		}
	} else if (!none && !write_png(argv[i + 1], p))
		return 1;
	pic_free(p);
	free(data);
	return 0;
}
