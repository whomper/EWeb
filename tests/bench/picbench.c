/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * picbench.c - a picture decoded as EWeb decodes it (common/src/pic.c),
 * each step timed: PICBENCH.TTP (EWEB.PRG's way: libjpeg-turbo, the DSP)
 * and PICBST.TTP (EWEBST.PRG's: TJpgDec, jpegdc) on the Atari
 * (plat_tos.c, the 200 Hz clock); tests/bench/picbench.py runs them in
 * Hatari.
 *
 *     PICBENCH.TTP [FILE WIDTH HEIGHT [COLOURS [TIMES]]]
 *
 * decodes FILE at WIDTH x HEIGHT (0 0: its own size), then dithers it to
 * COLOURS pens (2: black and white, 16: the ST's system colours, 0: none),
 * TIMES times, and prints each step's time. PICBENCH decodes a JPEG on
 * the 68030 and then, on a Falcon, with the DSP (after its check, which
 * isn't timed). In PICBST, a JPEG that TJpgDec can't read (progressive)
 * is read by jpegdc: its open and read are timed too. Without arguments
 * it takes PHOTO.JPG (tests/bench/mkphoto.py) at its size and at 192 x
 * 128, as a release has it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"
#include "pic.h"
#ifdef PIC_LIBJPEG
#include "dspjpeg.h"
#else
#include "jpegdc.h"
#endif

static unsigned long ms(unsigned long t)
{
	return t * 1000UL / TICKS_PER_SEC;
}

static int bench(const char *file, int w, int h, int colours, int times)
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
	int k, why;

	data = load(file, &len);
	if (data == NULL) {
		out("%s: can't read\n", file);
		return 1;
	}
	if (colours == 2)
		pic_pal_init(&pal, 2, mono);
	else if (colours == 16)
		pic_pal_init(&pal, 16, st16);
	out("%s: %lu bytes, %s, at %dx%d, %d colours\n", file, (unsigned long) len,
	    pic_kind_name(pic_kind((const unsigned char *) data, (long) len)), w, h, colours);
#ifdef PIC_LIBJPEG
	static int checked;
	if (!checked && dspjpeg_present()) {
		/* the DSP's check, on its first JPEG */
		PIC *p;
		checked = 1;
		pic_dsp = &dspjpeg_port;
		p = pic_decode((const unsigned char *) data, (long) len, w, h, 4L << 20, &why);
		pic_free(p);
		out("the DSP: %s\n", strcmp(pic_dsp_state(), "DSP") ? "failed its check" : "checked");
		if (strcmp(pic_dsp_state(), "DSP"))
			pic_dsp = NULL;
	}
	times *= pic_dsp != NULL ? 2 : 1;
#endif
	for (k = 0; k < times; k++) {
		unsigned long t0, t1, t2, t3;
		PIC *p;
#ifdef PIC_LIBJPEG
		const PIC_DSP *dsp = pic_dsp;
		if (k & 1)
			out("with the DSP: ");
		else if (dsp != NULL)
			out("on the 68030: ");
		pic_dsp = k & 1 ? dsp : NULL;
#endif
		t0 = ticks();
		p = pic_decode((const unsigned char *) data, (long) len, w, h, 4L << 20, &why);
		t1 = ticks();
#ifdef PIC_LIBJPEG
		pic_dsp = dsp;
#endif
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
#ifdef PIC_LIBJPEG
		/* libjpeg's scale: n/8 */
		out("decode %lu ms (%dx%d, %s, %d/8), dither %lu ms\n", ms(t1 - t0), p->w, p->h, pic_kind_name(p->how),
		    p->scale, ms(t2 - t1));
#else
		out("decode %lu ms (%dx%d, %s, 1/%d), dither %lu ms\n", ms(t1 - t0), p->w, p->h,
		    p->how == PIC_JPEG_AVG ? "block averages" : pic_kind_name(p->how), 1 << p->scale,
		    ms(t2 - t1));
#endif
#ifndef PIC_LIBJPEG
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
#else
		(void) t3;
#endif
		pic_free(p);
	}
	free(data);
	return 0;
}

int picbench(int argc, char **argv)
{
	int r;
	if (argc >= 4)
		return bench(argv[1], atoi(argv[2]), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 0,
			     argc > 5 ? atoi(argv[5]) : 1);
	/* no arguments: PHOTO.JPG at its size, and at 192 x 128 */
	r = bench("PHOTO.JPG", 0, 0, 0, 1);
	if (r == 0)
		r = bench("PHOTO.JPG", 192, 128, 0, 1);
	if (r != 0)
		out("usage: picbench FILE WIDTH HEIGHT [COLOURS [TIMES]] (none: PHOTO.JPG)\n");
	return r;
}
