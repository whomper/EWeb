/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * screen.c - the page on the screen. The display list is drawn into
 * memory (src/gfx/canvas.c) a band of rows at a time and copied to the
 * screen with vro_cpyfm, from ST RAM (the VDI can't read TT RAM):
 *
 * - 1 to 8 planes: the canvas holds a pixel value a byte, the screen's
 *   nearest colour to each of the page's; the bytes are made planes
 *   (standard format) and vr_trnfm makes them the screen's.
 * - The Falcon's true colour: the canvas is the screen's pixels already,
 *   RRRRRGGG GGGBBBBB (bytes swapped where NVDI says the card has them so).
 *
 * The pens' colours are the screen's as they are (EWeb sets none), each
 * pixel value's VDI colour from vq_scrninfo where the VDI answers it,
 * else TOS's standard order (as EHypView's colour.c).
 */
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "canvas.h"
#include "img.h"

#define BAND 32			/* rows drawn and copied at a time */

static int truecolour, swapped;
static short ncol;			/* 2, 4, 16, 256 */
static unsigned char pal[256][3];	/* each pixel value's colour */
static unsigned char nearest_of[4096];	/* 12-bit colour -> pixel value + 1, 0 not worked out */
static PIC_PAL pic_pal;			/* the same colours, for the pictures' pens */
static long band_w;			/* pixels, a multiple of 16 */
static unsigned char *chunky;		/* BAND rows, a byte a pixel (planar modes) */
static unsigned short *std_buf, *dev_buf;	/* ST RAM */

static unsigned char from1000(short v)
{
	return (unsigned char) (v < 0 ? 0 : v > 1000 ? 255 : (v * 255L + 500) / 1000);
}

/* the pixel value VDI colour i draws with, in TOS's standard order */
static short std_pixel(short i)
{
	static const unsigned char order[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
	if (ncol == 2)
		return i & 1;
	if (ncol == 4)
		return i == 1 ? 3 : i == 2 ? 1 : i == 3 ? 2 : i & 3;
	if (i >= 16)
		return ncol == 256 && i == 255 ? 15 : i;
	return ncol == 256 && i == 1 ? 255 : order[i];
}

static void *st_alloc(long n)
{
	void *p = Mxalloc(n, 0);
	if ((long) p == -32)		/* TOS 1.x: no Mxalloc, and no other RAM */
		p = Malloc(n);
	return (long) p > 0 ? p : NULL;
}

int scr_init(void)
{
	static short info[272];
	short rgb[3], i, have;

	band_w = (scr_w + 15) & ~15L;
	have = vq_scrninfo(vdi_h, info);
	if (planes == 16) {
		truecolour = 1;
		/* NVDI on a card with the bytes the other way round (Intel) */
		swapped = have && (info[14] & 2);
		dev_buf = st_alloc(band_w * 2 * BAND);
		return dev_buf != NULL;
	}
	if (planes != 1 && planes != 2 && planes != 4 && planes != 8)
		return 0;
	ncol = (short) (1 << planes);
	for (i = 0; i < ncol; i++)
		pal[i][0] = pal[i][1] = pal[i][2] = 0;
	/* each pixel value's colour: the VDI colour that draws with it */
	for (i = (ncol == 256 ? 256 : 16); i-- > 0;) {
		short px = have ? (short) (info[16 + i] & (ncol - 1)) : std_pixel(i);
		if (i >= ncol && ncol < 16)
			continue;
		vq_color(vdi_h, i, rgb);
		pal[px][0] = from1000(rgb[0]);
		pal[px][1] = from1000(rgb[1]);
		pal[px][2] = from1000(rgb[2]);
	}
	/* the pictures are made of these pens (img.c, Floyd-Steinberg) */
	pic_pal_init(&pic_pal, ncol, (const unsigned char (*)[3]) pal);
	img_pal = &pic_pal;
	img_pal_changed();
	chunky = malloc(band_w * BAND);
	std_buf = st_alloc(band_w / 8 * planes * BAND);
	dev_buf = st_alloc(band_w / 8 * planes * BAND);
	return chunky != NULL && std_buf != NULL && dev_buf != NULL;
}

const char *scr_mode(void)
{
	if (truecolour)
		return swapped ? "true colour (Intel order)" : "true colour";
	switch (ncol) {
	case 2: return "monochrome";
	case 4: return "4 colours";
	case 16: return "16 colours";
	}
	return "256 colours";
}

/* the screen's nearest colour to 0xRRGGBB, as a pixel value */
static uint8_t pen(void *ud, uint32_t rgb)
{
	unsigned key = (unsigned) ((rgb >> 12 & 0xf00) | (rgb >> 8 & 0xf0) | (rgb >> 4 & 0xf));
	long best = 0x7fffffffL;
	short i, pick = 0;
	int r, g, b;
	if (nearest_of[key])
		return (uint8_t) (nearest_of[key] - 1);
	r = (int) (rgb >> 16 & 255);
	g = (int) (rgb >> 8 & 255);
	b = (int) (rgb & 255);
	if (ncol == 2) {
		/* black or white by brightness: text stays readable on light grey */
		long y = 299L * r + 587L * g + 114L * b;
		long y0 = 299L * pal[0][0] + 587L * pal[0][1] + 114L * pal[0][2];
		long y1 = 299L * pal[1][0] + 587L * pal[1][1] + 114L * pal[1][2];
		long mid = (y0 + y1) / 2;
		pick = (y >= mid) == (y0 >= y1) ? 0 : 1;
	} else {
		for (i = 0; i < ncol; i++) {
			long dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
			long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
			if (d < best) {
				best = d;
				pick = i;
			}
		}
	}
	nearest_of[key] = (unsigned char) (pick + 1);
	return (uint8_t) pick;
}

/* a monochrome screen's fills: grey as a pattern of black and white by
   its brightness (pure white and black stay solid) */
static int shade(void *ud, uint32_t rgb, uint8_t *dark, uint8_t *light)
{
	long y = 299L * (rgb >> 16 & 255) + 587L * (rgb >> 8 & 255) + 114L * (rgb & 255);
	long y0 = 299L * pal[0][0] + 587L * pal[0][1] + 114L * pal[0][2];
	long y1 = 299L * pal[1][0] + 587L * pal[1][1] + 114L * pal[1][2];
	int white = y0 >= y1 ? 0 : 1;
	if (ncol != 2)
		return -1;
	*light = (uint8_t) white;
	*dark = (uint8_t) !white;
	return (int) ((y * 16 + 127500L) / 255000L);
}

/* a picture as decoded at its size (img.c), and its pens in the modes
   with a palette */
static int picture(void *ud, void *img, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens)
{
	(void) ud;
	return img_picture(img, w, h, pic, pens);
}

/* the chunky band (h rows) as planes, standard format: plane after plane */
static void to_planes(short h)
{
	long words = band_w / 16, plane = words * h, r, g;
	short k, bit;
	for (r = 0; r < h; r++) {
		const unsigned char *src = chunky + r * band_w;
		unsigned short *row = std_buf + r * words;
		for (g = 0; g < words; g++, src += 16) {
			unsigned short w[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			for (bit = 0; bit < 16; bit++) {
				unsigned char v = src[bit];
				unsigned short m = (unsigned short) (0x8000 >> bit);
				for (k = 0; v; k++, v >>= 1)
					if (v & 1)
						w[k] |= m;
			}
			for (k = 0; k < planes; k++)
				row[k * plane + g] = w[k];
		}
	}
}

void scr_page(const page *p, const GRECT *area, int32_t px, int32_t py)
{
	MFDB src, dst, scr;
	short y0;
	canvas c;

	if (area->w <= 0 || area->h <= 0)
		return;
	memset(&scr, 0, sizeof scr);
	memset(&c, 0, sizeof c);
	c.w = area->w > band_w ? (int32_t) band_w : area->w;
	c.ox = px;
	c.pen = pen;
	c.shade = shade;
	c.picture = picture;
	for (y0 = 0; y0 < area->h; y0 += BAND) {
		short h = area->h - y0 > BAND ? BAND : area->h - y0, pxy[8];
		c.h = h;
		c.oy = py + y0;
		if (truecolour) {
			c.pix = (uint8_t *) dev_buf;
			c.stride = band_w * 2;
			c.bpp = 16;
		} else {
			c.pix = chunky;
			c.stride = band_w;
			c.bpp = 8;
		}
		if (p != NULL) {
			cv_page(&c, p);
		} else if (truecolour) {
			memset(dev_buf, 0xff, band_w * 2 * h);
		} else {
			memset(chunky, pen(NULL, 0xffffff), band_w * h);
		}
		memset(&dst, 0, sizeof dst);
		dst.fd_addr = dev_buf;
		dst.fd_w = (short) band_w;
		dst.fd_h = h;
		dst.fd_wdwidth = (short) (band_w / 16);
		dst.fd_nplanes = planes;
		if (truecolour) {
			if (swapped) {
				unsigned short *w = dev_buf;
				long n = band_w * h;
				while (n-- > 0) {
					*w = (unsigned short) (*w << 8 | *w >> 8);
					w++;
				}
			}
		} else {
			to_planes(h);
			src = dst;
			src.fd_addr = std_buf;
			src.fd_stand = 1;
			vr_trnfm(vdi_h, &src, &dst);
		}
		pxy[0] = 0;
		pxy[1] = 0;
		pxy[2] = c.w - 1;
		pxy[3] = h - 1;
		pxy[4] = area->x;
		pxy[5] = area->y + y0;
		pxy[6] = area->x + c.w - 1;
		pxy[7] = area->y + y0 + h - 1;
		vro_cpyfm(vdi_h, 3, pxy, &dst, &scr);
	}
}
