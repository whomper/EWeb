/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * screen.c - the page on the screen. The display list is drawn into
 * memory (src/gfx/canvas.c), the whole of the part to draw at once where
 * there is the memory for the screen's height (else a band of rows at a
 * time), and copied to the screen in one go with vro_cpyfm, from ST RAM
 * (the VDI can't read TT RAM): nothing is seen half drawn.
 *
 * - 1 to 8 planes: the canvas holds a pixel value a byte, the screen's
 *   nearest colour to each of the page's; the bytes are made planes
 *   (standard format) and vr_trnfm makes them the screen's.
 * - The Falcon's true colour: the canvas is the screen's pixels already,
 *   RRRRRGGG GGGBBBBB (bytes swapped where NVDI says the card has them so).
 *
 * The pens' colours are the screen's as they are, each pixel value's VDI
 * colour from vq_scrninfo where the VDI answers it, else TOS's standard
 * order (as EHypView's colour.c); in 256 colours EWeb sets its own first
 * (PALETTE=1, Erez's decision of 23 Oct 2026: TOS's have no pale tints):
 * the pixel values TOS's sixteen pens don't draw with (16 to 254) get a
 * 6x6x6 cube and 23 greys, and TOS's are given back at the end.
 */
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "canvas.h"
#include "img.h"

#define BAND 32			/* rows drawn and copied at a time, short of memory */

static int truecolour, swapped;
static short format;			/* the screen's: 0 interleaved planes (the Atari's own), 1 the
					   VDI's standard, 2 packed pixels; -1 not known (vr_trnfm) */
static short ncol;			/* 2, 4, 16, 256 */
static unsigned char pal[256][3];	/* each pixel value's colour */
static unsigned char nearest_of[4096];	/* 12-bit colour -> pixel value + 1, 0 not worked out */
static PIC_PAL pic_pal;			/* the same colours, for the pictures' pens */
static long band_w;			/* pixels, a multiple of 16 */
static short rows;			/* rows the buffers hold: the screen's, or BAND */
static unsigned char *chunky;		/* rows, a byte a pixel (planar modes) */
static unsigned short *std_buf, *dev_buf;	/* ST RAM */
static short scr_info[272], scr_have;	/* vq_scrninfo's answer */
static short saved[256][3];		/* in 256 colours, the pens EWeb set, as they were */
static unsigned char set_pen[256];	/* which ones */
static int own;				/* EWeb's colours are on the screen */

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

static int c2p_init(void);

static void free_bufs(void)
{
	free(chunky);
	if (std_buf != NULL)
		Mfree(std_buf);
	if (dev_buf != NULL)
		Mfree(dev_buf);
	chunky = NULL;
	std_buf = dev_buf = NULL;
}

/* the buffers for n rows: 1, or 0 (none taken) */
static int bufs(short n)
{
	/* the standard planes for vr_trnfm only where the screen has its own format */
	int std = !truecolour && format != 0 && !(format == 2 && planes == 8);
	long st = n > BAND ? (truecolour ? band_w * 2 * n : (std ? 2 : 1) * (band_w / 8 * planes * n)) : 0;
	if (st > 0) {
		/* ST RAM left for the rest of the system: 256 KB at least; and
		   2 MB for the pages (a 4 MB Falcon draws in bands: the screen's
		   height would take 460 KB of its 1.5 MB in 16 colours) */
		long free_st = (long) Mxalloc(-1, 0), free_tt = 0;
		if (free_st == -32)
			free_st = (long) Malloc(-1);
		else if ((free_tt = (long) Mxalloc(-1, 1)) < 0)
			free_tt = 0;
		if (free_st < st + 262144L || free_st + free_tt < st + (truecolour ? 0 : band_w * n) + (2L << 20))
			return 0;
	}
	rows = n;
	if (truecolour) {
		dev_buf = st_alloc(band_w * 2 * n);
	} else {
		chunky = malloc(band_w * n);
		std_buf = std ? st_alloc(band_w / 8 * planes * n) : NULL;
		dev_buf = st_alloc(band_w / 8 * planes * n);
	}
	if (dev_buf != NULL && (truecolour || (chunky != NULL && (std_buf != NULL || !std))))
		return 1;
	free_bufs();
	return 0;
}

/* EWeb's colour for its pen k of 239 (0..1000 a part, the VDI's): the
   cube's 6 levels of each part, then greys, more of them light (pages'
   backgrounds) */
static void own_colour(int k, short rgb[3])
{
	static const unsigned char grey[23] = { 13, 26, 38, 64, 77, 89, 115, 128, 140, 166, 179, 191,
						 212, 220, 226, 231, 235, 238, 241, 244, 247, 250, 253 };
	if (k < 216) {
		rgb[0] = (short) (k / 36 * 200);
		rgb[1] = (short) (k / 6 % 6 * 200);
		rgb[2] = (short) (k % 6 * 200);
	} else
		rgb[0] = rgb[1] = rgb[2] = (short) ((grey[k - 216] * 1000L + 127) / 255);
}

/* EWeb's colours on the pens that draw pixel values 16 to 254, or TOS's
   given back */
static void own_pens(int on)
{
	short i, rgb[3];
	if (on == own || ncol != 256)
		return;
	for (i = 16; i < 256; i++) {
		short px = scr_have ? (short) (scr_info[16 + i] & 255) : std_pixel(i);
		if (px < 16 || px > 254)
			continue;
		if (on) {
			vq_color(vdi_h, i, saved[i]);
			set_pen[i] = 1;
			own_colour(px - 16, rgb);
			vs_color(vdi_h, i, rgb);
		} else if (set_pen[i]) {
			vs_color(vdi_h, i, saved[i]);
			set_pen[i] = 0;
		}
	}
	own = on;
}

/* each pixel value's colour, and the pictures' pens from them */
static void read_pens(void)
{
	short rgb[3], i;
	for (i = 0; i < ncol; i++)
		pal[i][0] = pal[i][1] = pal[i][2] = 0;
	/* each pixel value's colour: the VDI colour that draws with it */
	for (i = (ncol == 256 ? 256 : 16); i-- > 0;) {
		short px = scr_have ? (short) (scr_info[16 + i] & (ncol - 1)) : std_pixel(i);
		if (i >= ncol && ncol < 16)
			continue;
		vq_color(vdi_h, i, rgb);
		pal[px][0] = from1000(rgb[0]);
		pal[px][1] = from1000(rgb[1]);
		pal[px][2] = from1000(rgb[2]);
	}
	memset(nearest_of, 0, sizeof nearest_of);
	/* the pictures are made of these pens (img.c, Floyd-Steinberg) */
	pic_pal_init(&pic_pal, ncol, (const unsigned char (*)[3]) pal);
}

void scr_palette(void)
{
	if (ncol != 256 || truecolour || opt.palette == own)
		return;
	own_pens(opt.palette);
	read_pens();
	img_pal = &pic_pal;
	img_pal_changed();
}

int scr_exit(void)
{
	int was = own;
	own_pens(0);
	return was;
}

int scr_init(void)
{
	short *info = scr_info, have;

	band_w = (scr_w + 15) & ~15L;
	have = scr_have = vq_scrninfo(vdi_h, info);
	format = have ? info[0] : -1;
	if (planes == 16) {
		truecolour = 1;
		/* NVDI on a card with the bytes the other way round (Intel) */
		swapped = have && (info[14] & 2);
		/* the screen's height when there is the ST RAM, keeping some for others */
		return bufs((short) scr_h) || bufs(BAND);
	}
	if (planes != 1 && planes != 2 && planes != 4 && planes != 8)
		return 0;
	ncol = (short) (1 << planes);
	own_pens(ncol == 256 && opt.palette);
	read_pens();
	if (!c2p_init())
		return 0;
	img_pal = &pic_pal;
	img_pal_changed();
	return bufs((short) scr_h) || bufs(BAND);
}

int scr_rows(void)
{
	return rows;
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
	return own ? "256 colours (EWeb's)" : "256 colours";
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

/* chunky to planes, 8 pixels at a time: for pixel i of 8 with value v,
   its bit in each plane's byte, planes 0-3 in lo (plane 0 the top byte)
   and 4-7 in hi; the 8 looked up and ORed */
static unsigned long c2p_lo[8][256], (*c2p_hi)[256];	/* hi: planes 4-7, made for 256 colours only */

static int c2p_init(void)
{
	int i, v, k;
	if (planes > 4 && c2p_hi == NULL && (c2p_hi = malloc(8 * sizeof *c2p_hi)) == NULL)
		return 0;
	for (i = 0; i < 8; i++)
		for (v = 0; v < 256; v++) {
			unsigned long lo = 0, hi = 0;
			for (k = 0; k < 4; k++) {
				if (v >> k & 1)
					lo |= (unsigned long) (0x80 >> i) << (8 * (3 - k));
				if (v >> (k + 4) & 1)
					hi |= (unsigned long) (0x80 >> i) << (8 * (3 - k));
			}
			c2p_lo[i][v] = lo;
			if (c2p_hi != NULL)
				c2p_hi[i][v] = hi;
		}
	return 1;
}

static unsigned long eight(const unsigned long t[8][256], const unsigned char *s)
{
	return t[0][s[0]] | t[1][s[1]] | t[2][s[2]] | t[3][s[3]] | t[4][s[4]] | t[5][s[5]] | t[6][s[6]] | t[7][s[7]];
}

/* the chunky rows (h of them) as planes: interleaved (the screen's own
   format, into dev_buf: each 16 pixels' words plane after plane), or
   standard (into std_buf: plane after plane, for vr_trnfm) */
/* chunky's h rows (words * 16 pixels each) as planes: the screen's own
   (interleaved) in dev_buf, or the standard ones in std_buf */
static void to_planes(short h, long words, int interleaved)
{
	long plane = words * h, r, g;
	short k;
	for (r = 0; r < h; r++) {
		const unsigned char *src = chunky + r * words * 16;
		unsigned short *row = interleaved ? dev_buf + r * words * planes : std_buf + r * words;
		for (g = 0; g < words; g++, src += 16) {
			const unsigned long *q = (const unsigned long *) (const void *) src;
			unsigned long a, b, c = 0, d = 0;
			unsigned short w[8];
			if (q[0] == q[1] && q[0] == q[2] && q[0] == q[3] && (q[0] >> 8) == (q[0] & 0xffffffUL)) {
				/* 16 pixels alike (most of a page is its background):
				   each plane all set or all clear */
				unsigned v = (unsigned) (q[0] & 0xff);
				for (k = 0; k < planes; k++)
					w[k] = (unsigned short) (v >> k & 1 ? 0xffff : 0);
				goto put;
			}
			a = eight(c2p_lo, src);
			b = eight(c2p_lo, src + 8);
			if (planes > 4) {
				c = eight(c2p_hi, src);
				d = eight(c2p_hi, src + 8);
			}
			for (k = 0; k < 4 && k < planes; k++)
				w[k] = (unsigned short) ((a >> (8 * (3 - k)) & 0xff) << 8 | (b >> (8 * (3 - k)) & 0xff));
			for (; k < planes; k++)
				w[k] = (unsigned short) ((c >> (8 * (7 - k)) & 0xff) << 8 | (d >> (8 * (7 - k)) & 0xff));
put:
			if (interleaved) {
				for (k = 0; k < planes; k++)
					row[g * planes + k] = w[k];
			} else {
				for (k = 0; k < planes; k++)
					row[k * plane + g] = w[k];
			}
		}
	}
}

/* dev_buf's h rows of words * 16 pixels, from chunky in the modes with
   planes, made the screen's own */
static void make(short h, long words)
{
	MFDB src, dst;
	memset(&dst, 0, sizeof dst);
	dst.fd_addr = dev_buf;
	dst.fd_w = (short) (words * 16);
	dst.fd_h = h;
	dst.fd_wdwidth = (short) words;
	dst.fd_nplanes = planes;
	if (truecolour) {
		if (swapped) {
			unsigned short *p = dev_buf;
			long n = words * 16 * h;
			while (n-- > 0) {
				*p = (unsigned short) (*p << 8 | *p >> 8);
				p++;
			}
		}
	} else if (format == 0) {
		to_planes(h, words, 1);	/* the screen's own planes: no transform */
	} else if (format == 2 && planes == 8) {
		memcpy(dev_buf, chunky, (size_t) (words * 16 * h));	/* a byte a pixel already */
	} else {
		to_planes(h, words, 0);
		src = dst;
		src.fd_addr = std_buf;
		src.fd_stand = 1;
		vr_trnfm(vdi_h, &src, &dst);
	}
}

/* h rows of words * 16 pixels in the screen's own form, at at, to the
   screen at x, y: w pixels of them, from column sx, in vro_cpyfm's mode
   (3 in their place, 4 the screen's cleared where theirs are set, 7 ORed) */
static void blit(void *at, short h, long words, short sx, short w, short x, short y, short mode)
{
	MFDB src, scr;
	short pxy[8];
	memset(&scr, 0, sizeof scr);
	memset(&src, 0, sizeof src);
	src.fd_addr = at;
	src.fd_w = (short) (words * 16);
	src.fd_h = h;
	src.fd_wdwidth = (short) words;
	src.fd_nplanes = planes;
	pxy[0] = sx;
	pxy[1] = 0;
	pxy[2] = sx + w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	vro_cpyfm(vdi_h, mode, pxy, &src, &scr);
}

/* dev_buf's h rows, made from chunky, to the screen */
static void put(short h, long words, short sx, short w, short x, short y)
{
	make(h, words);
	blit(dev_buf, h, words, sx, w, x, y, 3);
}

/* each colour's pixel value (or 5-6-5 pixel) */
static void rgb_map(unsigned short *map, const unsigned long *col, short ncol)
{
	short i;
	for (i = 0; i < ncol; i++) {
		unsigned long c = col[i];
		map[i] = truecolour ? (unsigned short) ((c >> 8 & 0xf800) | (c >> 5 & 0x07e0) | (c >> 3 & 0x001f)) :
			 pen(NULL, (uint32_t) c);
	}
}

/* n rows of pix (w wide), from row y, into chunky (or dev_buf) */
static void rgb_rows(const unsigned char *pix, const unsigned short *map, short w, long words, short y, short n)
{
	short r, x;
	for (r = 0; r < n; r++) {
		const unsigned char *s = pix + (long) (y + r) * w;
		if (truecolour) {
			unsigned short *d = dev_buf + r * words * 16;
			for (x = 0; x < w; x++)
				d[x] = map[s[x]];
		} else {
			unsigned char *d = chunky + r * words * 16;
			for (x = 0; x < w; x++)
				d[x] = (unsigned char) map[s[x]];
		}
	}
}

void scr_rgb(const unsigned char *pix, const unsigned long *col, short ncol, short w, short h, const GRECT *at,
	     const GRECT *part)
{
	unsigned short map[256];
	long words = (w + 15) / 16;
	short y0, sx = part->x - at->x, sy = part->y - at->y;
	if (part->w <= 0 || part->h <= 0 || words * 16 > band_w)
		return;
	rgb_map(map, col, ncol);
	for (y0 = 0; y0 < part->h; y0 += rows) {
		short n = part->h - y0 > rows ? rows : part->h - y0;
		rgb_rows(pix, map, w, words, sy + y0, n);
		put(n, words, sx, part->w, part->x, part->y + y0);
	}
}

long scr_dev_size(short w, short h)
{
	return (w + 15) / 16 * 2L * planes * h;
}

void *scr_dev_alloc(long n)
{
	return st_alloc(n);
}

void scr_dev_free(void *p)
{
	if (p != NULL)
		Mfree(p);
}

int scr_rgb_dev(const unsigned char *pix, const unsigned long *col, short ncol, short w, short h, void *dev)
{
	unsigned short map[256];
	long words = (w + 15) / 16, row = words * 2 * planes;
	short y0;
	if (words * 16 > band_w)
		return 0;
	rgb_map(map, col, ncol);
	for (y0 = 0; y0 < h; y0 += rows) {
		short n = h - y0 > rows ? rows : h - y0;
		rgb_rows(pix, map, w, words, y0, n);
		make(n, words);
		memcpy((char *) dev + y0 * row, dev_buf, (size_t) (n * row));
	}
	return 1;
}

void scr_blit_dev(void *dev, short w, short h, const GRECT *at, const GRECT *part)
{
	long words = (w + 15) / 16, row = words * 2 * planes;
	short sy = part->y - at->y;
	if (part->w > 0 && part->h > 0)
		blit((char *) dev + sy * row, part->h, words, part->x - at->x, part->w, part->x, part->y, 3);
}

/* a picture with holes (its colour 0: what is under it shows), as
   rgb_map: its pixel values (0 in a hole), or its mask (every bit of a
   pixel set, but in a hole). Drawn as the mask clearing the screen's
   pixels, then the picture ORed in: the same in every mode */
static void hole_map(unsigned short *map, const unsigned long *col, short n, int mask)
{
	short i;
	rgb_map(map, col, n);
	if (mask)
		for (i = 1; i < n; i++)
			map[i] = truecolour ? 0xffff : (unsigned short) (ncol - 1);
	map[0] = 0;
}

int scr_holes_dev(const unsigned char *pix, const unsigned long *col, short n, short w, short h, void *dev)
{
	unsigned short map[256];
	long words = (w + 15) / 16, row = words * 2 * planes;
	short y0, k;
	if (words * 16 > band_w)
		return 0;
	for (k = 0; k < 2; k++) {
		hole_map(map, col, n, k);
		for (y0 = 0; y0 < h; y0 += rows) {
			short m = h - y0 > rows ? rows : h - y0;
			rgb_rows(pix, map, w, words, y0, m);
			make(m, words);
			memcpy((char *) dev + (k * h + y0) * row, dev_buf, (size_t) (m * row));
		}
	}
	return 1;
}

void scr_blit_holes(void *dev, short w, short h, const GRECT *at, const GRECT *part)
{
	long words = (w + 15) / 16, row = words * 2 * planes;
	short sy = part->y - at->y, sx = part->x - at->x;
	if (part->w <= 0 || part->h <= 0)
		return;
	blit((char *) dev + (h + sy) * row, part->h, words, sx, part->w, part->x, part->y, 4);
	blit((char *) dev + sy * row, part->h, words, sx, part->w, part->x, part->y, 7);
}

void scr_holes(const unsigned char *pix, const unsigned long *col, short n, short w, short h, const GRECT *at,
	       const GRECT *part)
{
	unsigned short map[256];
	long words = (w + 15) / 16;
	short y0, k, sx = part->x - at->x, sy = part->y - at->y;
	(void) h;
	if (part->w <= 0 || part->h <= 0 || words * 16 > band_w)
		return;
	for (k = 1; k >= 0; k--) {		/* the mask, then the picture */
		hole_map(map, col, n, k);
		for (y0 = 0; y0 < part->h; y0 += rows) {
			short m = part->h - y0 > rows ? rows : part->h - y0;
			rgb_rows(pix, map, w, words, sy + y0, m);
			make(m, words);
			blit(dev_buf, m, words, sx, part->w, part->x, part->y + y0, k ? 4 : 7);
		}
	}
}

void scr_page(const page *p, const GRECT *area, int32_t px, int32_t py)
{
	short y0;
	canvas c;
	long words;

	if (area->w <= 0 || area->h <= 0)
		return;
	memset(&c, 0, sizeof c);
	c.w = area->w > band_w ? (int32_t) band_w : area->w;
	words = (c.w + 15) / 16;	/* (only as wide as the area: a strip scrolled in is narrow) */
	c.ox = px;
	c.pen = pen;
	c.shade = shade;
	c.pal = (const uint8_t (*)[3]) pal;	/* (translucent items mixed through it) */
	c.picture = picture;
	c.marked = p != NULL && win_mark(&c.mark, &c.mark_rgb);	/* (the selection, Edit > Find's words) */
	c.aes_bars = 1;		/* (the boxes' bars: win.c draws them over this, the AES's) */
	for (y0 = 0; y0 < area->h; y0 += rows) {
		short h = area->h - y0 > rows ? rows : area->h - y0;
		c.h = h;
		c.oy = py + y0;
		if (truecolour) {
			c.pix = (uint8_t *) dev_buf;
			c.stride = words * 16 * 2;
			c.bpp = 16;
		} else {
			c.pix = chunky;
			c.stride = words * 16;
			c.bpp = 8;
		}
		if (p != NULL) {
			cv_page(&c, p);
		} else if (truecolour) {
			memset(dev_buf, 0xff, words * 16 * 2 * h);
		} else {
			memset(chunky, pen(NULL, 0xffffff), words * 16 * h);
		}
		put(h, words, 0, (short) c.w, area->x, area->y + y0);
	}
}
