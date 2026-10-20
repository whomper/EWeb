/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * picbench.c - a picture decoded as EWeb decodes it (common/src/pic.c),
 * each step timed: PICBENCH.TTP on the Atari (plat_tos.c, the 200 Hz
 * clock), tests/bench/picbench.py runs it in Hatari.
 *
 *     PICBENCH.TTP FILE WIDTH HEIGHT [COLOURS [TIMES]]
 *
 * decodes FILE at WIDTH x HEIGHT (0 0: its own size), then dithers it to
 * COLOURS pens (2: black and white, 16: the ST's system colours, 0: none),
 * TIMES times, and prints each step's time. A JPEG that TJpgDec can't
 * read (progressive) is read by jpegdc: its open and read are timed too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"
#include "pic.h"
#include "jpegdc.h"

static unsigned long ms(unsigned long t)
{
	return t * 1000UL / TICKS_PER_SEC;
}

int picbench(int argc, char **argv)
{
	static const unsigned char st16[16][3] = {
		{ 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
		{ 0, 255, 255 }, { 170, 170, 170 }, { 85, 85, 85 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 170, 0 },
		{ 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 0, 0, 0 }
	};
	static const unsigned char mono[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };
	static PIC_PAL pal;
	char *data;
	size_t len;
	int w, h, colours, times, k, why;

	if (argc < 4) {
		out("usage: picbench FILE WIDTH HEIGHT [COLOURS [TIMES]]\n");
		return 2;
	}
	w = atoi(argv[2]);
	h = atoi(argv[3]);
	colours = argc > 4 ? atoi(argv[4]) : 0;
	times = argc > 5 ? atoi(argv[5]) : 1;
	data = load(argv[1], &len);
	if (data == NULL) {
		out("%s: can't read\n", argv[1]);
		return 1;
	}
	if (colours == 2)
		pic_pal_init(&pal, 2, mono);
	else if (colours == 16)
		pic_pal_init(&pal, 16, st16);
	out("%s: %lu bytes, %s, at %dx%d, %d colours\n", argv[1], (unsigned long) len,
	    pic_kind_name(pic_kind((const unsigned char *) data, (long) len)), w, h, colours);
	for (k = 0; k < times; k++) {
		unsigned long t0 = ticks(), t1, t2, t3;
		PIC *p = pic_decode((const unsigned char *) data, (long) len, w, h, 4L << 20, &why);
		t1 = ticks();
		if (p == NULL) {
			out("not decoded: %s\n", pic_why_text(why));
			free(data);
			return 1;
		}
		t2 = t3 = t1;
		if (colours) {
			unsigned char *pens = malloc((size_t) p->w * p->h);
			if (pens != NULL) {
				pic_dither(p, &pal, pens);
				free(pens);
			}
			t2 = ticks();
		}
		out("decode %lu ms (%dx%d, %s, 1/%d), dither %lu ms\n", ms(t1 - t0), p->w, p->h,
		    p->how == PIC_JPEG_AVG ? "block averages" : pic_kind_name(p->how), 1 << p->scale,
		    ms(t2 - t1));
		if (p->how == PIC_JPEG_AVG) {
			/* jpegdc's steps on their own */
			int jw, jh;
			int jwhy;
			JDC *c;
			t3 = ticks();
			c = jdc_open((const unsigned char *) data, (long) len, &jw, &jh, &jwhy);
			t1 = ticks();
			if (c != NULL) {
				jdc_read(c);
				t2 = ticks();
				jdc_free(c);
				out("  jpegdc: open %lu ms, read %lu ms\n", ms(t1 - t3), ms(t2 - t1));
			}
		}
		pic_free(p);
	}
	free(data);
	return 0;
}
