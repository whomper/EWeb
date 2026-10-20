/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic.c - pictures (pic.h): JPEG, PNG and GIF decoded to the size they
 * are shown at, and the screen's own colours for them.
 *
 * Each decoder gives its rows in turn, at its own size, to the scaler,
 * which makes the picture's rows as they come: a shrinking picture is
 * averaged (each pixel shown is the mean of the pixels it covers,
 * weighted by how solid they are), an enlarged one repeats its pixels.
 * So only the shown picture is kept whole, never the file's: a 1000x750
 * photo shown at 320x240 takes 230 KB, not 3 MB. A JPEG is first
 * unpacked at 1/2, 1/4 or 1/8 when it is shown that small or smaller,
 * which skips most of the work.
 *
 * The scaler's columns come from a table made once a picture, and its
 * sums are 32-bit additions: the only divisions are one a component of
 * each pixel shown.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"
#include "png.h"
#include "nsgif.h"
#ifdef PIC_LIBJPEG
#include <stdio.h>		/* jpeglib.h names FILE */
#define JPEG_INTERNALS		/* the entropy decoder, for the DSP's way */
#include "jpeglib.h"
#include "jerror.h"
#else
#include "jpegdc.h"
#include "tjpgd.h"
#endif

int (*pic_poll)(void);

/* ---------------------------------------------------------------- kinds */

int pic_kind(const unsigned char *d, long n)
{
	long i;
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
		return PIC_JPEG;
	if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
		return PIC_PNG;
	if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
		return PIC_GIF;
	if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
		return PIC_WEBP;
	if (n >= 12 && memcmp(d + 4, "ftyp", 4) == 0 && (memcmp(d + 8, "avif", 4) == 0 || memcmp(d + 8, "avis", 4) == 0))
		return PIC_AVIF;
	if (n >= 2 && d[0] == 'B' && d[1] == 'M')
		return PIC_BMP;
	if (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0)
		return PIC_ICO;
	for (i = 0; i + 4 <= n && i < 1024; i++)
		if (memcmp(d + i, "<svg", 4) == 0)
			return PIC_SVG;
	return PIC_NONE;
}

const char *pic_kind_name(int kind)
{
	switch (kind) {
	case PIC_JPEG: return "JPEG";
	case PIC_PNG: return "PNG";
	case PIC_GIF: return "GIF";
	case PIC_BMP: return "BMP";
	case PIC_WEBP: return "WebP";
	case PIC_AVIF: return "AVIF";
	case PIC_SVG: return "SVG";
	case PIC_ICO: return "icon";
	}
	return "unknown";
}

const char *pic_why_text(int why)
{
	switch (why) {
	case PIC_OK: return "decoded";
	case PIC_DAMAGED: return "damaged";
	case PIC_UNSUPPORTED: return "a kind of picture that can't be shown";
	case PIC_MEMORY: return "not enough memory";
	case PIC_TOO_BIG: return "too big";
	case PIC_STOPPED: return "stopped";
	}
	return "not decoded";
}

/* a JPEG's size, from its frame header (SOF0..SOF15 but DHT, JPG, DAC) */
static int jpeg_size(const unsigned char *d, long n, long *w, long *h)
{
	long p = 2;
	while (p + 4 <= n) {
		int m;
		long len;
		if (d[p] != 0xFF) {
			p++;
			continue;
		}
		m = d[p + 1];
		if (m == 0xFF) {
			p++;
			continue;
		}
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
			p += 2;
			continue;
		}
		len = ((long) d[p + 2] << 8) | d[p + 3];
		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			if (p + 9 > n)
				return 0;
			*h = ((long) d[p + 5] << 8) | d[p + 6];
			*w = ((long) d[p + 7] << 8) | d[p + 8];
			return *w > 0 && *h > 0;
		}
		if (m == 0xD9 || m == 0xDA || len < 2)
			return 0;
		p += 2 + len;
	}
	return 0;
}

int pic_size(const unsigned char *d, long n, long *w, long *h)
{
	switch (pic_kind(d, n)) {
	case PIC_JPEG:
		return jpeg_size(d, n, w, h);
	case PIC_PNG:
		return png_size(d, n, w, h) && *w > 0 && *h > 0;
	case PIC_GIF:
		if (n < 10)
			return 0;
		*w = d[6] | ((long) d[7] << 8);
		*h = d[8] | ((long) d[9] << 8);
		return *w > 0 && *h > 0;
	}
	return 0;
}

/* ---------------------------------------------------------------- the scaler */

typedef struct {
	PIC *pic;
	long sw, sh;			/* the rows that come */
	int down_x, down_y;		/* shrinking (else the same or enlarging) */
	short *xmap;			/* down_x: each column's pixel shown; else each shown pixel's column */
	unsigned long *sum;		/* down: dw * 5 (r, g, b weighted by alpha; alpha; pixels) */
	long dy;			/* the row shown being made */
	long y;				/* rows taken */
	int any_alpha;
	int polls;
	int solid;			/* every row came by sc_row_rgb: no alpha in the sums */
} SCALER;

/* a 16 x 16 product: a 68000's mulu.w (GCC calls its 32-bit routine for
   one followed by a shift, though both are 16 bits) */
#if defined(__mc68000__) && !defined(__mc68020__) && !defined(__mc68030__) && !defined(__mc68040__) && \
    !defined(__mc68060__)
static unsigned long mul16(unsigned short a, unsigned short b)
{
	unsigned long r = a;
	__asm__("mulu.w %1,%0" : "+d" (r) : "dmi" (b));
	return r;
}
#else
#define mul16(a, b) ((unsigned long) (unsigned short) (a) * (unsigned short) (b))
#endif

/* n source pixels to m shown, by their centres: down, each source
   pixel's pixel shown; else each shown pixel's source */
static long centre(long i, long from, long to)
{
	return ((2 * i + 1) * to) / (2 * from);
}

static short *map(long n, long m, int down)
{
	long i, cnt = down ? n : m;
	short *t = malloc((size_t) cnt * sizeof *t);
	if (t == NULL)
		return NULL;
	for (i = 0; i < cnt; i++)
		t[i] = (short) (down ? centre(i, n, m) : centre(i, m, n));
	return t;
}

static int sc_init(SCALER *s, PIC *p, long sw, long sh)
{
	memset(s, 0, sizeof *s);
	s->pic = p;
	s->sw = sw;
	s->sh = sh;
	s->down_x = sw > p->w;
	s->down_y = sh > p->h;
	s->xmap = map(sw, p->w, s->down_x);
	s->sum = calloc((size_t) p->w * 5, sizeof *s->sum);
	return s->xmap != NULL && s->sum != NULL;
}

static void sc_free(SCALER *s)
{
	free(s->xmap);
	free(s->sum);
}

/* pixel i of the picture shown */
static void put(SCALER *s, long i, int r, int g, int b, int a)
{
	s->pic->px[i] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s->pic->alpha[i] = (unsigned char) a;
	if (a != 255)
		s->any_alpha = 1;
}


/* 65536 / n, rounded, for averaging n pixels with a 16 x 16 multiply
   (a 68000 divides 32 bits in software: three to a pixel were most of
   the scaler's time); made the first time it is needed */
static unsigned short recip[258];

/* the sums made into row dy, and cleared */
static void flush(SCALER *s, long dy)
{
	long x, i = dy * s->pic->w;
	unsigned long *q = s->sum;
	if (s->solid) {
		/* solid rows (a JPEG's): sums of at most 257 pixels fit 16 bits,
		   the mean rounded */
		unsigned short *o = s->pic->px + i;
		if (recip[2] == 0)
			for (x = 2; x < 258; x++)
				recip[x] = (unsigned short) ((65536L + x / 2) / x);
		for (x = 0; x < s->pic->w; x++, q += 5) {
			unsigned n = (unsigned) q[4], r, g, b;
			if (n == 0) {
				r = g = b = 0;
			} else if (n == 1) {
				r = (unsigned) q[0];
				g = (unsigned) q[1];
				b = (unsigned) q[2];
			} else if (n < 258) {
				unsigned short k = recip[n];
				r = (unsigned) ((mul16((unsigned short) q[0], k) + 32768UL) >> 16);
				g = (unsigned) ((mul16((unsigned short) q[1], k) + 32768UL) >> 16);
				b = (unsigned) ((mul16((unsigned short) q[2], k) + 32768UL) >> 16);
			} else {
				r = (unsigned) (q[0] / n);
				g = (unsigned) (q[1] / n);
				b = (unsigned) (q[2] / n);
			}
			o[x] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
			q[0] = q[1] = q[2] = q[3] = q[4] = 0;
		}
		memset(s->pic->alpha + i, 255, (size_t) s->pic->w);
		return;
	}
	for (x = 0; x < s->pic->w; x++, q += 5, i++) {
		unsigned long n = q[4], a = q[3];
		if (n == 0) {
			put(s, i, 0, 0, 0, 0);
		} else if (n == 1) {
			/* one pixel (enlarging): as it is, no divide (a 68000's are
			   in software) */
			if (a == 255)
				put(s, i, (int) q[0], (int) q[1], (int) q[2], 255);
			else if (a == 0)
				put(s, i, 0, 0, 0, 0);
			else {
				unsigned long r = q[0] * 255 / a, g = q[1] * 255 / a, b = q[2] * 255 / a;
				put(s, i, r > 255 ? 255 : (int) r, g > 255 ? 255 : (int) g, b > 255 ? 255 : (int) b, (int) a);
			}
		} else if (a == 255 * n) {
			put(s, i, (int) (q[0] / n), (int) (q[1] / n), (int) (q[2] / n), 255);
		} else if (a == 0) {
			put(s, i, 0, 0, 0, 0);
		} else {
			/* the colour as solid, weighted by alpha: sum (c * (a + 1) >> 8) / sum a */
			unsigned long r = q[0] * 255 / a, g = q[1] * 255 / a, b = q[2] * 255 / a;
			put(s, i, r > 255 ? 255 : (int) r, g > 255 ? 255 : (int) g, b > 255 ? 255 : (int) b,
			    (int) (a / n));
		}
		q[0] = q[1] = q[2] = q[3] = q[4] = 0;
	}
}

/* enlarging n rows to m: the first row shown whose source is row i or
   later, the least d with centre(d, m, n) >= i */
static long first_shown(long i, long n, long m)
{
	long d = (2 * i * m - m + 2 * n - 1) / (2 * n);
	if (d < 0)
		d = 0;
	while (d > 0 && centre(d - 1, m, n) >= i)
		d--;
	while (d < m && centre(d, m, n) < i)
		d++;
	return d;
}

static void sc_rows(SCALER *s);

/* a row of a solid picture, R G B (a JPEG's): no alpha to weigh */
static int sc_row_rgb(SCALER *s, const unsigned char *rgb)
{
	long x, w = s->pic->w;
	unsigned long *q;
	if (pic_poll != NULL && ++s->polls >= 16) {
		s->polls = 0;
		if (!pic_poll())
			return 0;
	}
	if (s->y >= s->sh)
		return 1;
	if (s->y == 0)
		s->solid = 1;
	if (s->sw == w && s->sh == s->pic->h) {
		unsigned short *o = s->pic->px + mul16((unsigned short) s->y, (unsigned short) w);
		for (x = 0; x < w; x++, rgb += 3)
			o[x] = (unsigned short) (((rgb[0] & 0xF8) << 8) | ((rgb[1] & 0xFC) << 3) | (rgb[2] >> 3));
		/* solid, for when clear rows follow (a JPEG cut short) */
		memset(s->pic->alpha + mul16((unsigned short) s->y, (unsigned short) w), 255, (size_t) w);
		s->y++;
		return 1;
	}
	if (s->down_x) {
		const short *m = s->xmap;
		for (x = 0; x < s->sw; x++, rgb += 3) {
			q = s->sum + m[x] * 5;
			q[0] += rgb[0];
			q[1] += rgb[1];
			q[2] += rgb[2];
			q[3] += 255;
			q[4]++;
		}
	} else {
		for (x = 0, q = s->sum; x < w; x++, q += 5) {
			const unsigned char *c = rgb + s->xmap[x] * 3;
			q[0] += c[0];
			q[1] += c[1];
			q[2] += c[2];
			q[3] += 255;
			q[4]++;
		}
	}
	sc_rows(s);
	return 1;
}

/* a row of the decoded picture, R G B A */
static int sc_row(SCALER *s, const unsigned char *rgba)
{
	long x, w = s->pic->w;
	s->solid = 0;		/* the sums weigh alpha from now on */
	if (pic_poll != NULL && ++s->polls >= 16) {
		s->polls = 0;
		if (!pic_poll())
			return 0;
	}
	if (s->y >= s->sh)
		return 1;
	if (s->sw == w && s->sh == s->pic->h) {
		/* the size it is: as it is */
		const unsigned char *q = rgba;
		unsigned short *o = s->pic->px + s->y * w;
		unsigned char *a = s->pic->alpha + s->y * w;
		unsigned all = 255;
		for (x = 0; x < w; x++, q += 4) {
			o[x] = (unsigned short) (((q[0] & 0xF8) << 8) | ((q[1] & 0xFC) << 3) | (q[2] >> 3));
			a[x] = q[3];
			all &= q[3];
		}
		if (all != 255)
			s->any_alpha = 1;
		s->y++;
		return 1;
	}
	{
		/* the row's pixels to the columns shown */
		unsigned long *q;
		if (s->down_x) {
			const unsigned char *c = rgba;
			for (x = 0; x < s->sw; x++, c += 4) {
				q = s->sum + s->xmap[x] * 5;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		} else {
			for (x = 0, q = s->sum; x < w; x++, q += 5) {
				const unsigned char *c = rgba + s->xmap[x] * 4;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		}
	}
	sc_rows(s);
	return 1;
}

/* the rows shown that the row summed finishes (shrinking: when the next
   one's first row comes; enlarging: each whose centre falls in it), and
   the next row */
static void sc_rows(SCALER *s)
{
	long w = s->pic->w;
	if (s->down_y) {
		/* rows shown are made when the next one's first row comes */
		long dy = s->y + 1 < s->sh ? centre(s->y + 1, s->sh, s->pic->h) : -1;
		if (dy != s->dy) {
			flush(s, s->dy);
			s->dy = dy;
		}
	} else {
		/* every row shown whose centre falls in this one: those from
		   dy0 to before dy1 */
		long dy0 = first_shown(s->y, s->sh, s->pic->h), dy1 = first_shown(s->y + 1, s->sh, s->pic->h), dy;
		if (dy1 > dy0) {
			flush(s, dy0);
			for (dy = dy0 + 1; dy < dy1; dy++)
				memcpy(s->pic->px + dy * w, s->pic->px + dy0 * w, (size_t) w * 2),
				memcpy(s->pic->alpha + dy * w, s->pic->alpha + dy0 * w, (size_t) w);
		} else {
			memset(s->sum, 0, (size_t) w * 5 * sizeof *s->sum);
		}
	}
	s->y++;
}

/* ---------------------------------------------------------------- JPEG */

#ifdef PIC_LIBJPEG
/* libjpeg-turbo (third_party/libjpeg-turbo), the library NetSurf takes:
   every kind of JPEG a page has (progressive ones too), unpacked by its
   IDCT straight at n/8 of the size (n from 1 to 8), the smallest still
   as big as shown; on a 68030 1.8 times as quick as TJpgDec at the same
   size, and six times on a big photo shown small */

typedef struct {
	struct jpeg_error_mgr pub;
	void *jmp[5];			/* __builtin_setjmp's */
} LJERR;

static void lj_exit(j_common_ptr c)
{
	__builtin_longjmp(((LJERR *) c->err)->jmp, 1);
}

static void lj_emit(j_common_ptr c, int level)
{
	(void) c;
	(void) level;
}

static void lj_output(j_common_ptr c)
{
	(void) c;
}

static void lj_format(j_common_ptr c, char *buf)
{
	(void) c;
	buf[0] = 0;
}

static void lj_reset(j_common_ptr c)
{
	c->err->num_warnings = 0;
	c->err->msg_code = 0;
}

/* libjpeg's error handling without its jerror.c (which prints to stderr
   and calls exit(), neither of which the Atari's runtime has): no
   messages, and a failure jumps back to decode_jpeg */
static struct jpeg_error_mgr *lj_errors(struct jpeg_error_mgr *e)
{
	memset(e, 0, sizeof *e);
	e->error_exit = lj_exit;
	e->emit_message = lj_emit;
	e->output_message = lj_output;
	e->format_message = lj_format;
	e->reset_error_mgr = lj_reset;
	return e;
}

/* the file, in memory: cut short, it ends there (libjpeg makes the rest
   grey, as browsers show it) */
static void src_init(j_decompress_ptr c)
{
	(void) c;
}

static boolean src_fill(j_decompress_ptr c)
{
	static const JOCTET eoi[2] = { 0xFF, JPEG_EOI };
	c->src->next_input_byte = eoi;
	c->src->bytes_in_buffer = 2;
	return TRUE;
}

static void src_skip(j_decompress_ptr c, long n)
{
	if (n <= 0)
		return;
	if ((size_t) n > c->src->bytes_in_buffer) {
		src_fill(c);
		return;
	}
	c->src->next_input_byte += n;
	c->src->bytes_in_buffer -= (size_t) n;
}

static void src_term(j_decompress_ptr c)
{
	(void) c;
}

/* what a decode holds, kept off the stack that __builtin_longjmp leaves */
typedef struct {
	struct jpeg_decompress_struct ci;
	LJERR err;
	struct jpeg_source_mgr src;
	unsigned char *row;
	int stopped;
	/* the DSP's way */
	int dsp_on;			/* the DSP is held (let go if libjpeg gives up) */
	union {
		JBLOCK b;
		uint32_t l[32];		/* two coefficients at a time */
	} blk[C_MAX_BLOCKS_IN_MCU];	/* an MCU's coefficients, 0 where not written */
	JBLOCKROW rows[C_MAX_BLOCKS_IN_MCU];
	unsigned long w[7 * 3 + 13 + 4 * 8 + 64 + 8 * 8 * 3 + 8 * 8 + 3 * 64 + 7];	/* words for the DSP */
} LJ;

/* ---------------------------------------------------------------- JPEG on the DSP */

/* libjpeg unpacks each MCU's coefficients (the Huffman codes), and
   dsp/jpeg.a56 does the rest: dequantising, the transform straight to
   n/8 of the size (each pixel the mean of those it covers), the colours,
   the chroma over the luma it covers; it sends the pixels back an MCU
   behind, so it works out one MCU while the 68030 unpacks the next */

const PIC_DSP *pic_dsp;
int pic_dsp_failed;
static int dsp_state;			/* 0: not checked yet, 1: checked, -1: the 68030 does it */
static int dsp_testing;			/* the check: 1 its DSP decodes, 2 its 68030 ones */

/* jpeg.a56's commands and memory */
#define D_LOADX 1
#define D_MCU   2
#define D_END   3
#define D_LOADY 4
#define D_BT    0x20			/* x: the block table */
#define D_BLK   0x40			/* x: the block */
#define D_TRM   0x600			/* x: the colour terms */
#define D_QT    0x40			/* y: the quantisers, 64 a component */
#define D_YPL   0x200			/* y: the MCU's luma */
#define D_CBP   0x300			/* y: its Cb, Cr */
#define D_CRP   0x340
#define D_CONST 0x500			/* y: the format's constants */

/* the 8-point inverse DCT: B[x][u] = C(u) / 2 * cos((2x + 1) u pi / 16),
   times 2^23 */
static const long dsp_basis[8][8] = {
	{  2965821,  4113712,  3875032,  3487436,  2965821,  2330230,  1605091,   818268 },
	{  2965821,  3487436,  1605091,  -818268, -2965821, -4113712, -3875032, -2330230 },
	{  2965821,  2330230, -1605091, -4113712, -2965821,   818268,  3875032,  3487436 },
	{  2965821,   818268, -3875032, -2330230,  2965821,  3487436, -1605091, -4113712 },
	{  2965821,  -818268, -3875032,  2330230,  2965821, -3487436, -1605091,  4113712 },
	{  2965821, -2330230, -1605091,  4113712, -2965821,  -818268,  3875032, -3487436 },
	{  2965821, -3487436,  1605091,   818268, -2965821,  4113712, -3875032,  2330230 },
	{  2965821, -4113712,  3875032, -3487436,  2965821, -2330230,  1605091,  -818268 }
};

/* each format's 7 constants (jpeg.a56): for R, G and B its mask and how
   far it moves down (R G B's R stays: $7FFFFF, rounded), then what puts
   the top bits right */
static const unsigned long dsp_565[7] = { 0xF80000, 0x008000, 0xFC0000, 0x000400, 0xF80000, 0x000010, 0x008410 };
static const unsigned long dsp_rgb[7] = { 0xFF0000, 0x7FFFFF, 0xFF0000, 0x008000, 0xFF0000, 0x000080, 0x808080 };

/* the JPEGs it takes: 8-bit, one scan of grey or of Y Cb Cr with the
   chroma at most halved each way, shown at 2/8 of the size or more (at
   1/8 there is no transform to speak of) */
static int dsp_takes(j_decompress_ptr c, int k)
{
	jpeg_component_info *ci = c->comp_info;
	if (k < 2 || c->data_precision != 8 || c->progressive_mode || c->master->lossless ||
	    c->inputctl->has_multiple_scans || c->blocks_in_MCU > 8)
		return 0;
	if (c->num_components == 1)
		return c->jpeg_color_space == JCS_GRAYSCALE;
	return c->num_components == 3 && c->jpeg_color_space == JCS_YCbCr && c->comps_in_scan == 3 &&
	       ci[0].h_samp_factor <= 2 && ci[0].v_samp_factor <= 2 && ci[1].h_samp_factor == 1 &&
	       ci[1].v_samp_factor == 1 && ci[2].h_samp_factor == 1 && ci[2].v_samp_factor == 1;
}

/* the M x 8 transform to M pixels a block: pixel i the mean of the
   8/M pixels it covers (parts of them at the ends), twice the true
   one (so each entry is below 1, as the DSP's fractions are) */
static void dsp_matrix(unsigned long *w, int m)
{
	int i, u, x;
	for (i = 0; i < m; i++)
		for (u = 0; u < 8; u++) {
			long sum = 0;
			for (x = 0; x < 8; x++) {
				/* how much of source pixel x is in pixel i, in M-ths:
				   [mx, mx + m) against [8i, 8i + 8) */
				int lo = m * x > 8 * i ? m * x : 8 * i, hi = m * x + m < 8 * i + 8 ? m * x + m : 8 * i + 8;
				if (hi > lo)
					sum += (hi - lo) * dsp_basis[x][u];
			}
			/* weights (hi - lo) / 8, times 2 */
			sum = sum >= 0 ? (sum + 2) >> 2 : -((-sum + 2) >> 2);
			*w++ = (unsigned long) sum & 0xFFFFFFUL;
		}
}

/* an MCU's pixels from the DSP: into the picture (5-6-5), or into the
   band of R G B rows */
static void dsp_read(LJ *j, PIC *p, unsigned char *band, long x, long y, int cols, int rows)
{
	if (band == NULL)
		pic_dsp->get16(p->px + y * p->w + x, cols, rows, p->w);
	else
		pic_dsp->get24(band + x * 3, cols, rows, (long) j->ci.output_width * 3);
}

/* 1 decoded, 0 not (why), -1 the DSP stopped answering */
static int decode_dsp(LJ *j, PIC *p, SCALER *sc, int k, int *why)
{
	j_decompress_ptr c = &j->ci;
	int nb = c->blocks_in_MCU, grey = c->num_components == 1, direct = c->out_color_space == JCS_RGB565;
	int hy = grey ? 1 : c->comp_info[0].h_samp_factor, vy = grey ? 1 : c->comp_info[0].v_samp_factor;
	int mw = hy * k, mh = vy * k, b, i, ci, seen[3] = { 0, 0, 0 }, pcols = 0, prows = 0, ok = 0;
	long ow = (long) c->output_width, oh = (long) c->output_height, mx, my, px = 0, py = -1, n;
	unsigned long *w = j->w;
	unsigned char *band = NULL;

	if (!direct) {
		band = malloc((size_t) ow * mh * 3);
		if (band == NULL || !sc_init(sc, p, ow, oh)) {
			free(band);
			*why = PIC_MEMORY;
			return 0;
		}
	}
	for (b = 0; b < nb; b++)
		j->rows[b] = &j->blk[b].b;

	/* the setup, written into the DSP's memory: the variables */
	n = 0;
	w[n++] = D_LOADX;
	w[n++] = 0;
	w[n++] = 13;
	w[n++] = (unsigned long) k;
	w[n++] = (unsigned long) nb;
	w[n++] = (unsigned long) mw;
	w[n++] = (unsigned long) hy;
	w[n++] = (unsigned long) vy;
	w[n++] = grey ? 1 : 3;
	w[n++] = direct ? 0 : 1;
	for (i = 0; i < 5; i++)
		w[n++] = 0;		/* this and the last MCU's pixels: none */
	w[n++] = (unsigned long) mw * 3;
	/* each block's quantisers, where its pixels go, the pitch */
	w[n++] = D_LOADX;
	w[n++] = D_BT;
	w[n++] = (unsigned long) nb * 4;
	for (b = 0; b < nb; b++) {
		ci = c->cur_comp_info[c->MCU_membership[b]]->component_index;
		if (ci == 0) {
			int bx = seen[0] % hy, by = seen[0] / hy;
			seen[0]++;
			w[n++] = D_QT;
			w[n++] = (unsigned long) (D_YPL + by * k * mw + bx * k);
			w[n++] = (unsigned long) (mw - k);
			w[n++] = 0x4000;	/* the DC + 4: each pixel + 1/2, rounded */
		} else {
			w[n++] = (unsigned long) (D_QT + 64 * ci);
			w[n++] = ci == 1 ? D_CBP : D_CRP;
			w[n++] = 0;
			w[n++] = 0;
		}
	}
	/* the block 0, and grey's colour terms */
	w[n++] = D_LOADX;
	w[n++] = D_BLK;
	w[n++] = 64;
	for (i = 0; i < 64; i++)
		w[n++] = 0;
	if (grey) {
		w[n++] = D_LOADX;
		w[n++] = D_TRM;
		w[n++] = (unsigned long) k * k * 3;
		for (i = 0; i < k * k * 3; i++)
			w[n++] = 0;
	}
	/* the transform, the quantisers, the format's constants */
	w[n++] = D_LOADY;
	w[n++] = 0;
	w[n++] = (unsigned long) k * 8;
	dsp_matrix(w + n, k);
	n += 8 * k;
	w[n++] = D_LOADY;
	w[n++] = D_QT;
	w[n++] = grey ? 64 : 3 * 64;
	for (ci = 0; ci < (grey ? 1 : 3); ci++) {
		const JQUANT_TBL *q = c->comp_info[ci].quant_table;
		for (i = 0; i < 64; i++) {
			unsigned long v = q != NULL ? (unsigned long) q->quantval[i] * 32 : 32;
			w[n++] = v > 0x7FFFFFUL ? 0x7FFFFFUL : v;
		}
	}
	w[n++] = D_LOADY;
	w[n++] = D_CONST;
	w[n++] = 7;
	for (i = 0; i < 7; i++)
		w[n++] = direct ? dsp_565[i] : dsp_rgb[i];
	if (!pic_dsp->begin()) {
		free(band);
		return -2;
	}
	j->dsp_on = 1;
	pic_dsp_failed = 0;
	pic_dsp->put(w, n);

	for (my = 0; my < (long) c->MCU_rows_in_scan && !pic_dsp_failed; my++) {
		for (mx = 0; mx < (long) c->MCUs_per_row && !pic_dsp_failed; mx++) {
			(*c->entropy->decode_mcu)(c, j->rows);
			/* the coefficients that are not 0, and 0 again for the next */
			n = 0;
			w[n++] = D_MCU;
			w[n++] = (unsigned long) (ow - mx * mw < mw ? ow - mx * mw : mw);
			w[n++] = (unsigned long) (oh - my * mh < mh ? oh - my * mh : mh);
			for (b = 0; b < nb; b++) {
				uint32_t *l = j->blk[b].l;
				const JCOEF *q = j->blk[b].b;
				long at = n++;
				for (i = 0; i < 32; i++)
					if (l[i] != 0) {
						if (q[2 * i] != 0)
							w[n++] = (((unsigned long) (long) q[2 * i] << 6) | (unsigned) (2 * i)) & 0xFFFFFFUL;
						if (q[2 * i + 1] != 0)
							w[n++] = (((unsigned long) (long) q[2 * i + 1] << 6) | (unsigned) (2 * i + 1)) &
								 0xFFFFFFUL;
						l[i] = 0;
					}
				w[at] = (unsigned long) (n - at - 1);
			}
			pic_dsp->put(w, n);
			/* the last one's pixels */
			if (py >= 0)
				dsp_read(j, p, band, px, py, pcols, prows);
			if (band != NULL && py >= 0 && px + pcols >= ow) {
				/* its band of rows is whole */
				for (i = 0; i < prows; i++)
					if (!sc_row_rgb(sc, band + (long) i * ow * 3)) {
						*why = PIC_STOPPED;
						goto out;
					}
			}
			px = mx * mw;
			py = my * mh;
			pcols = (int) w[1];
			prows = (int) w[2];
		}
		if (direct && pic_poll != NULL && !pic_poll()) {
			*why = PIC_STOPPED;
			goto out;
		}
	}
	/* the last MCU's pixels */
	w[0] = D_END;
	pic_dsp->put(w, 1);
	dsp_read(j, p, band, px, py, pcols, prows);
	if (band != NULL && !pic_dsp_failed) {
		for (i = 0; i < prows; i++)
			if (!sc_row_rgb(sc, band + (long) i * ow * 3)) {
				*why = PIC_STOPPED;
				break;
			}
		ok = i == prows;
	} else {
		ok = 1;
	}
out:
	pic_dsp->end(ok && !pic_dsp_failed);
	j->dsp_on = 0;
	free(band);
	return pic_dsp_failed ? -1 : ok;
}

/* the check, the first time: a known picture (32 x 24, smooth colours,
   4:2:0) decoded on the DSP and on the 68030, at its size (5-6-5 from the
   DSP) and at 10 x 7 (from 3/8 by the scaler): the same within a step or
   two of 5-6-5 (the DSP repeats the chroma where libjpeg's 68030 way
   blends it, and its transform is exact) */
static const unsigned char dsp_test_jpg[] = {
	0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03,
	0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
	0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d, 0x0e, 0x12, 0x10, 0x0d,
	0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f,
	0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x03, 0x04,
	0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0d, 0x0b, 0x0d, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0xc0,
	0x00, 0x11, 0x08, 0x00, 0x18, 0x00, 0x20, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
	0x01, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
	0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
	0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23,
	0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
	0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
	0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
	0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
	0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00,
	0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00,
	0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
	0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15,
	0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
	0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
	0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
	0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
	0xfa, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xfa,
	0x97, 0x47, 0xb5, 0x8e, 0xe6, 0x35, 0xe0, 0x1c, 0xd6, 0xac, 0xde, 0x19, 0x8e, 0x78, 0xf8, 0x41,
	0xd3, 0xd2, 0xb8, 0x7f, 0x08, 0x78, 0x95, 0x24, 0x54, 0xcb, 0x57, 0xa8, 0xe9, 0x5a, 0x94, 0x73,
	0xc6, 0x39, 0x07, 0x22, 0xbf, 0x9b, 0xe1, 0x83, 0xad, 0x87, 0x9e, 0x88, 0xf4, 0x70, 0xf9, 0x4d,
	0x4c, 0xb3, 0x5b, 0x6c, 0x79, 0xd6, 0xbf, 0xe0, 0x85, 0x65, 0x6c, 0x47, 0xfa, 0x57, 0x92, 0xf8,
	0xb3, 0xc1, 0x7e, 0x5e, 0xfc, 0x27, 0xe9, 0x5f, 0x54, 0x5e, 0xdb, 0xc5, 0x71, 0x19, 0xe0, 0x1a,
	0xf3, 0xff, 0x00, 0x14, 0xf8, 0x7d, 0x25, 0x57, 0xc2, 0xd7, 0xeb, 0x5c, 0x3b, 0x8c, 0xa9, 0x4e,
	0x51, 0x52, 0x3e, 0x9e, 0x8f, 0x15, 0xcb, 0x00, 0xb9, 0x5c, 0x8f, 0x9e, 0x7c, 0x27, 0xe3, 0x13,
	0x0e, 0xc0, 0x5f, 0x1f, 0x8d, 0x7a, 0xff, 0x00, 0x87, 0x3c, 0x74, 0xbb, 0x53, 0x32, 0x7e, 0xb4,
	0x51, 0x5e, 0xbd, 0x6c, 0xbf, 0x0e, 0xf5, 0x71, 0x3f, 0x58, 0xe2, 0x2c, 0x0d, 0x08, 0x53, 0x6d,
	0x44, 0xee, 0xac, 0x7c, 0x5d, 0x1c, 0xe8, 0x32, 0xe0, 0xfe, 0x34, 0xb7, 0xba, 0x8c, 0x77, 0x48,
	0x79, 0x06, 0x8a, 0x2b, 0x7c, 0x1e, 0x0e, 0x95, 0x39, 0xae, 0x54, 0x7f, 0x1b, 0xf1, 0x65, 0x49,
	0x51, 0x94, 0xb9, 0x1d, 0x8f, 0xff, 0xd9
};

static int dsp_close(const PIC *a, const PIC *b)
{
	long i, n = (long) a->w * a->h, sum = 0;
	if (a->w != b->w || a->h != b->h)
		return 0;
	for (i = 0; i < n; i++) {
		int d1 = ((a->px[i] >> 11) & 31) - ((b->px[i] >> 11) & 31);
		int d2 = ((a->px[i] >> 5) & 63) - ((b->px[i] >> 5) & 63);
		int d3 = (a->px[i] & 31) - (b->px[i] & 31);
		d1 = d1 < 0 ? -d1 : d1;
		d2 = d2 < 0 ? -d2 : d2;
		d3 = d3 < 0 ? -d3 : d3;
		if (d1 > 3 || d2 > 6 || d3 > 3)
			return 0;
		sum += d1 + d2 / 2 + d3;
	}
	return sum <= n * 3;		/* a step in each component at most, on the whole */
}

static int dsp_check(void)
{
	static const short size[2][2] = { { 32, 24 }, { 10, 7 } };
	int s, ok = 1, why;
	for (s = 0; s < 2 && ok; s++) {
		PIC *a, *b;
		dsp_testing = 1;
		a = pic_decode(dsp_test_jpg, (long) sizeof dsp_test_jpg, size[s][0], size[s][1], 1024, &why);
		dsp_testing = 2;
		b = pic_decode(dsp_test_jpg, (long) sizeof dsp_test_jpg, size[s][0], size[s][1], 1024, &why);
		ok = a != NULL && b != NULL && dsp_close(a, b);
		pic_free(a);
		pic_free(b);
	}
	dsp_testing = 0;
	return ok;
}

static int dsp_usable(void)
{
	if (pic_dsp == NULL || dsp_state < 0 || dsp_testing == 2)
		return 0;
	if (dsp_testing == 1 || dsp_state > 0)
		return 1;
	dsp_state = dsp_check() ? 1 : -1;
	return dsp_state > 0;
}

const char *pic_dsp_state(void)
{
	return pic_dsp == NULL || dsp_state < 0 ? "68030" : dsp_state > 0 ? "DSP" : "-";
}

static int decode_lj(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	LJ *j;
	long y, x;
	int k, ok = 0, dsp = dsp_usable();
	j = calloc(1, sizeof *j);
	if (j == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	j->ci.err = lj_errors(&j->err.pub);
	if (__builtin_setjmp(j->err.jmp)) {
		/* libjpeg gave up: damaged, or out of memory */
		if (j->dsp_on)
			pic_dsp->end(0);
		*why = j->stopped ? PIC_STOPPED : j->err.pub.msg_code == JERR_OUT_OF_MEMORY ? PIC_MEMORY :
		       (j->err.pub.msg_code == JERR_BAD_PRECISION || j->err.pub.msg_code == JERR_NOT_COMPILED) ?
		       PIC_UNSUPPORTED : PIC_DAMAGED;
		goto done;
	}
	jpeg_create_decompress(&j->ci);
	j->src.init_source = src_init;
	j->src.fill_input_buffer = src_fill;
	j->src.skip_input_data = src_skip;
	j->src.resync_to_restart = jpeg_resync_to_restart;
	j->src.term_source = src_term;
	j->src.next_input_byte = d;
	j->src.bytes_in_buffer = (size_t) n;
	j->ci.src = &j->src;
	jpeg_read_header(&j->ci, TRUE);
	/* CMYK (Adobe's, inverted) is made RGB below; the rest by libjpeg */
	j->ci.out_color_space = j->ci.jpeg_color_space == JCS_CMYK || j->ci.jpeg_color_space == JCS_YCCK ?
				JCS_CMYK : JCS_RGB;
	/* n/8 of the size: the least n still as big as shown */
	for (k = 1; k < 8; k++)
		if (((long) j->ci.image_width * k + 7) / 8 >= p->w && ((long) j->ci.image_height * k + 7) / 8 >= p->h)
			break;
	j->ci.scale_num = (unsigned) k;
	j->ci.scale_denom = 8;
	/* the integer transform made of fewer multiplies (13% fewer
	   instructions for the whole picture on the 68030, a quality step
	   no one sees: 38.2 dB against the exact one's 39.2, both against a
	   decode in floating point, the 5-6-5 pixels' own loss included) */
	j->ci.dct_method = JDCT_IFAST;
	jpeg_calc_output_dimensions(&j->ci);
	if (j->ci.out_color_space == JCS_RGB && (long) j->ci.output_width == p->w &&
	    (long) j->ci.output_height == p->h) {
		/* at the size shown: libjpeg writes the 5-6-5 pixels into the
		   picture, as put() makes them (cut, not dithered) */
		j->ci.out_color_space = JCS_RGB565;
		j->ci.dither_mode = JDITHER_NONE;
	}
	jpeg_start_decompress(&j->ci);
	p->how = PIC_JPEG;
	p->scale = k;
	if (dsp && dsp_takes(&j->ci, k)) {
		ok = decode_dsp(j, p, sc, k, why);
		if (ok != -2) {
			/* -1: the DSP stopped answering; from now on the 68030
			   does them, this one again (decode_jpeg) */
			if (ok < 0 && dsp_testing == 0)
				dsp_state = -1;
			jpeg_abort_decompress(&j->ci);
			goto done;
		}
		ok = 0;			/* the DSP couldn't be had: the 68030 this time */
	}
	if (j->ci.out_color_space == JCS_RGB565) {
		for (y = 0; y < (long) j->ci.output_height; y++) {
			JSAMPROW r = (JSAMPROW) (p->px + y * p->w);
			if (pic_poll != NULL && (y & 15) == 15 && !pic_poll()) {
				*why = PIC_STOPPED;
				jpeg_abort_decompress(&j->ci);
				goto done;
			}
			jpeg_read_scanlines(&j->ci, &r, 1);
		}
		jpeg_finish_decompress(&j->ci);
		ok = 1;
		goto done;
	}
	j->row = malloc((size_t) j->ci.output_width * (size_t) j->ci.output_components);
	if (j->row == NULL || !sc_init(sc, p, j->ci.output_width, j->ci.output_height)) {
		*why = PIC_MEMORY;
		jpeg_abort_decompress(&j->ci);
		goto done;
	}
	for (y = 0; y < (long) j->ci.output_height; y++) {
		JSAMPROW r = j->row;
		jpeg_read_scanlines(&j->ci, &r, 1);
		if (j->ci.out_color_space == JCS_CMYK) {
			/* Adobe writes CMYK inverted: each component times K,
			   made R G B in place */
			const unsigned char *s = j->row;
			unsigned char *o = j->row;
			for (x = 0; x < (long) j->ci.output_width; x++, s += 4, o += 3) {
				unsigned k4 = s[3];
				o[0] = (unsigned char) (s[0] * k4 / 255);
				o[1] = (unsigned char) (s[1] * k4 / 255);
				o[2] = (unsigned char) (s[2] * k4 / 255);
			}
		}
		if (!sc_row_rgb(sc, j->row)) {
			*why = PIC_STOPPED;
			jpeg_abort_decompress(&j->ci);
			goto done;
		}
	}
	jpeg_finish_decompress(&j->ci);
	ok = 1;
done:
	jpeg_destroy_decompress(&j->ci);
	free(j->row);
	free(j);
	return ok;
}

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	int ok = decode_lj(d, n, p, sc, why);
	if (ok < 0) {
		/* the DSP stopped answering: again on the 68030 */
		sc_free(sc);
		memset(sc, 0, sizeof *sc);
		*why = PIC_OK;
		ok = decode_lj(d, n, p, sc, why);
	}
	return ok;
}

#else

const PIC_DSP *pic_dsp;			/* (no DSP for TJpgDec's builds) */
int pic_dsp_failed;

const char *pic_dsp_state(void)
{
	return "68000";
}

/* TJpgDec (third_party/tjpgd), for the family's programs on a 68000:
   small, and quick at 1/8 (no IDCT); the kinds it won't take from their
   blocks' averages (jpegdc.c) */

typedef struct {
	const unsigned char *p;
	long left;
	SCALER *sc;
	unsigned char *strip;		/* an MCU row of the unpacked picture, R G B */
	long sw;
	int rows;			/* in the strip */
	int stopped;
} JPG;

static size_t jin(JDEC *jd, uint8_t *buf, size_t n)
{
	JPG *j = jd->device;
	if ((long) n > j->left)
		n = (size_t) j->left;
	if (buf != NULL)
		memcpy(buf, j->p, n);
	j->p += n;
	j->left -= (long) n;
	return n;
}

static int jout(JDEC *jd, void *bitmap, JRECT *r)
{
	JPG *j = jd->device;
	const unsigned char *px = bitmap;
	long y, w = r->right - r->left + 1;
	for (y = 0; y <= r->bottom - r->top; y++, px += w * 3)
		memcpy(j->strip + (mul16((unsigned short) y, (unsigned short) j->sw) + r->left) * 3, px, (size_t) w * 3);
	j->rows = r->bottom - r->top + 1;
	if (r->right + 1 >= j->sw) {
		/* the row of blocks is whole */
		for (y = 0; y < j->rows; y++)
			if (!sc_row_rgb(j->sc, j->strip + mul16((unsigned short) y, (unsigned short) j->sw) * 3)) {
				j->stopped = 1;
				return 0;
			}
	}
	return 1;
}

/* the unpacked size at a scale, as TJpgDec makes it (each block row and
   column rounded down) */
static long scaled(long size, int mcu, int scale)
{
	long whole = size / mcu;
	return whole * (mcu >> scale) + ((size - whole * mcu) >> scale);
}

/* jpegdc's polls, through pic_poll: whether it stopped */
static int jdc_stopped;
static int jdc_pic_poll(void)
{
	if (pic_poll != NULL && !pic_poll()) {
		jdc_stopped = 1;
		return 0;
	}
	return 1;
}

#define POOL 11512L			/* TJpgDec's workspace with its Huffman tables, and the
					   folding multipliers of 1/2 and 1/4 (4 x 128 bytes at most) */

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	JDEC jd;
	JPG j;
	void *pool = malloc(POOL);
	int scale = 0, rc;
	long sw, sh;
	memset(&j, 0, sizeof j);
	j.p = d;
	j.left = n;
	if (pool == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	rc = jd_prepare(&jd, jin, pool, (size_t) POOL, &j);
	if (rc == JDR_OK) {
		/* as small as it can be unpacked and still be as big as shown */
		while (scale < 3 && (jd.width >> (scale + 1)) >= p->w && (jd.height >> (scale + 1)) >= p->h)
			scale++;
		sw = scaled(jd.width, jd.msx * 8, scale);
		sh = scaled(jd.height, jd.msy * 8, scale);
		j.sw = sw;
		j.strip = malloc((size_t) sw * 16 * 3);
		if (j.strip == NULL || !sc_init(sc, p, sw, sh)) {
			free(j.strip);
			free(pool);
			*why = PIC_MEMORY;
			return 0;
		}
		j.sc = sc;
		p->how = PIC_JPEG;
		p->scale = scale;
		rc = jd_decomp(&jd, jout, (uint8_t) scale);
		free(j.strip);
		free(pool);
		if (rc == JDR_OK || (rc == JDR_INP && sc->y > 0)) {
			/* a picture cut short: the rest stays clear */
			unsigned char *row = sc->y < sh ? calloc((size_t) sw, 4) : NULL;
			if (row != NULL) {
				while (sc->y < sh)
					if (!sc_row(sc, row))
						break;
				free(row);
			}
			return 1;
		}
		if (j.stopped) {
			*why = PIC_STOPPED;
			return 0;
		}
		sc_free(sc);
		memset(sc, 0, sizeof *sc);
		/* damaged as TJpgDec sees it: the averages may still be read */
	} else {
		free(pool);
	}
	{
		/* progressive, SOF1, other samplings, CMYK: each 8x8 block's
		   average, an eighth of the size */
		int jw, jh, jwhy;
		int (*was)(void) = jdc_poll;
		long bw, bh, bx, by;
		unsigned char *row;
		JDC *c = jdc_open(d, n, &jw, &jh, &jwhy);
		if (c == NULL) {
			*why = jwhy == JW_MEMORY ? PIC_MEMORY : jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_stopped = 0;
		jdc_poll = jdc_pic_poll;
		if (!jdc_read(c)) {
			jdc_poll = was;
			jwhy = jdc_why(c);
			jdc_free(c);
			*why = jdc_stopped ? PIC_STOPPED : jwhy == JW_MEMORY ? PIC_MEMORY :
			       jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_poll = was;
		bw = (jw + 7) / 8;
		bh = (jh + 7) / 8;
		row = malloc((size_t) bw * 3);
		if (row == NULL || !sc_init(sc, p, bw, bh)) {
			free(row);
			jdc_free(c);
			*why = PIC_MEMORY;
			return 0;
		}
		p->how = PIC_JPEG_AVG;
		p->scale = 3;
		for (by = 0; by < bh; by++) {
			for (bx = 0; bx < bw; bx++) {
				int r, g, b;
				jdc_rgb(c, (int) bx, (int) by, &r, &g, &b);
				row[bx * 3] = (unsigned char) r;
				row[bx * 3 + 1] = (unsigned char) g;
				row[bx * 3 + 2] = (unsigned char) b;
			}
			if (!sc_row_rgb(sc, row)) {
				free(row);
				jdc_free(c);
				*why = PIC_STOPPED;
				return 0;
			}
		}
		free(row);
		jdc_free(c);
		return 1;
	}
}

#endif

/* ---------------------------------------------------------------- PNG */

static int png_row(void *ud, long y, const unsigned char *rgba)
{
	(void) y;
	return sc_row(ud, rgba);
}

static int decode_png(const unsigned char *d, long n, PIC *p, SCALER *sc, long max_px, int *why)
{
	long w, h;
	int r;
	if (!png_size(d, n, &w, &h) || w <= 0 || h <= 0) {
		*why = PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, w, h)) {
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_PNG;
	r = png_decode(d, n, max_px, png_row, sc);
	switch (r) {
	case PNG_OK: return 1;
	case PNG_MEMORY: *why = PIC_MEMORY; break;
	case PNG_TOO_BIG: *why = PIC_TOO_BIG; break;
	case PNG_STOPPED: *why = PIC_STOPPED; break;
	case PNG_UNSUPPORTED: *why = PIC_UNSUPPORTED; break;
	default: *why = PIC_DAMAGED;
	}
	return 0;
}

/* ---------------------------------------------------------------- GIF */

typedef struct {
	long w, h;
	unsigned char px[1];		/* w * h * 4 */
} GBMP;

static nsgif_bitmap_t *gb_create(int w, int h)
{
	GBMP *b = calloc(1, sizeof *b + (size_t) w * (size_t) h * 4);
	if (b != NULL) {
		b->w = w;
		b->h = h;
	}
	return b;
}

static void gb_destroy(nsgif_bitmap_t *b)
{
	free(b);
}

static uint8_t *gb_buffer(nsgif_bitmap_t *b)
{
	/* nsgif wants it on a 4-byte boundary: px follows two longs */
	return ((GBMP *) b)->px;
}

static int decode_gif(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	static const nsgif_bitmap_cb_vt vt = { gb_create, gb_destroy, gb_buffer, NULL, NULL, NULL, NULL };
	nsgif_t *g;
	nsgif_bitmap_t *bmp;
	const nsgif_info_t *info;
	nsgif_error e;
	long y;
	if (nsgif_create(&vt, NSGIF_BITMAP_FMT_R8G8B8A8, &g) != NSGIF_OK) {
		*why = PIC_MEMORY;
		return 0;
	}
	e = nsgif_data_scan(g, (size_t) n, d);
	nsgif_data_complete(g);
	info = nsgif_get_info(g);
	if (info == NULL || info->frame_count == 0) {
		nsgif_destroy(g);
		*why = e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	e = nsgif_frame_decode(g, 0, &bmp);
	if (e != NSGIF_OK || bmp == NULL) {
		nsgif_destroy(g);
		*why = e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, ((GBMP *) bmp)->w, ((GBMP *) bmp)->h)) {
		nsgif_destroy(g);
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_GIF;
	for (y = 0; y < ((GBMP *) bmp)->h; y++)
		if (!sc_row(sc, ((GBMP *) bmp)->px + y * ((GBMP *) bmp)->w * 4)) {
			nsgif_destroy(g);
			*why = PIC_STOPPED;
			return 0;
		}
	nsgif_destroy(g);
	return 1;
}

/* ---------------------------------------------------------------- decoding */

void pic_free(PIC *p)
{
	if (p == NULL)
		return;
	free(p->px);
	free(p->alpha);
	free(p);
}

PIC *pic_decode(const unsigned char *d, long n, int w, int h, long max_px, int *why)
{
	PIC *p;
	SCALER sc;
	long sw, sh;
	int kind = pic_kind(d, n), ok = 0, dummy;
	if (why == NULL)
		why = &dummy;
	*why = PIC_OK;
	if (kind != PIC_JPEG && kind != PIC_PNG && kind != PIC_GIF) {
		*why = PIC_UNSUPPORTED;
		return NULL;
	}
	if (!pic_size(d, n, &sw, &sh)) {
		*why = PIC_DAMAGED;
		return NULL;
	}
	if (sw > max_px / sh) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	if (w <= 0 || h <= 0) {
		w = (int) sw;
		h = (int) sh;
	}
	if (w > 0x7fff || h > 0x7fff || (long) w * h > max_px) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	p = calloc(1, sizeof *p);
	if (p == NULL) {
		*why = PIC_MEMORY;
		return NULL;
	}
	p->w = w;
	p->h = h;
	p->px = malloc((size_t) w * h * 2);
	p->alpha = malloc((size_t) w * h);
	memset(&sc, 0, sizeof sc);
	if (p->px == NULL || p->alpha == NULL) {
		*why = PIC_MEMORY;
	} else {
		memset(p->alpha, 0, (size_t) w * h);
		memset(p->px, 0, (size_t) w * h * 2);
		switch (kind) {
		case PIC_JPEG: ok = decode_jpeg(d, n, p, &sc, why); break;
		case PIC_PNG: ok = decode_png(d, n, p, &sc, max_px, why); break;
		default: ok = decode_gif(d, n, p, &sc, why); break;
		}
	}
	if (ok && sc.down_y && sc.y < sc.sh && sc.dy >= 0) {
		/* fewer rows came than the header said: the last one made */
		flush(&sc, sc.dy);
	}
	if (ok && !sc.any_alpha) {
		free(p->alpha);
		p->alpha = NULL;
	}
	sc_free(&sc);
	if (!ok) {
		pic_free(p);
		return NULL;
	}
	return p;
}

/* ---------------------------------------------------------------- the screen's colours */

void pic_pal_init(PIC_PAL *pal, int n, const unsigned char rgb[][3])
{
	int i, j;
	pal->n = n < 2 ? 2 : n > 256 ? 256 : n;
	memcpy(pal->rgb, rgb, (size_t) pal->n * 3);
	memset(pal->near, 0, sizeof pal->near);
	/* the pens by their red, for nearest_far */
	for (i = 0; i < pal->n; i++) {
		for (j = i; j > 0 && pal->rgb[pal->order[j - 1]][0] > pal->rgb[i][0]; j--)
			pal->order[j] = pal->order[j - 1];
		pal->order[j] = (unsigned char) i;
	}
}


/* a colour (each component -128..383) to its index in near[] */
#define NEAR_AT(r, g, b) ((((r) + 128) >> 4) * 1024 + (((g) + 128) >> 4) * 32 + (((b) + 128) >> 4))

/* a square: each side at most 640 apart, so a 16 x 16 multiply (a
   68000's muls.w; a long one was a call to GCC's 32-bit routine) */
#define SQ(v) ((long) (short) (v) * (short) (v))

static int nearest_far(PIC_PAL *pal, int r, int g, int b)
{
	int k = NEAR_AT(r, g, b), lo, hi, best = 0;
	long bd = 0x7fffffffL;
	if (pal->near[k])
		return pal->near[k] - 1;
	/* the step's middle */
	r = (r & ~15) + 8;
	g = (g & ~15) + 8;
	b = (b & ~15) + 8;
	/* outwards from the pens of about this red (they are sorted by it),
	   until red alone puts a pen farther than the best: a few pens of
	   256 instead of all (the 256-colour modes spent most of a picture's
	   time here) */
	lo = 0;
	hi = pal->n;
	while (lo < hi) {
		int mid = (lo + hi) >> 1;
		if (pal->rgb[pal->order[mid]][0] < r)
			lo = mid + 1;
		else
			hi = mid;
	}
	hi = lo;
	lo = lo - 1;
	while (lo >= 0 || hi < pal->n) {
		int i, side, dr;
		long dd;
		if (hi < pal->n && (lo < 0 || pal->rgb[pal->order[hi]][0] - r <= r - pal->rgb[pal->order[lo]][0]))
			i = pal->order[hi++], side = 1;
		else
			i = pal->order[lo--], side = 0;
		dr = r - pal->rgb[i][0];
		if (3 * SQ(dr) >= bd) {
			/* every pen past this one on its side is farther still */
			if (side)
				hi = pal->n;
			else
				lo = -1;
			continue;
		}
		dd = 3 * SQ(dr) + 4 * SQ(g - pal->rgb[i][1]) + 2 * SQ(b - pal->rgb[i][2]);
		if (dd < bd) {
			bd = dd;
			best = i;
		}
	}
	pal->near[k] = (unsigned char) (best + 1);
	return best;
}

int pic_pal_nearest(PIC_PAL *pal, int r, int g, int b)
{
	return nearest_far(pal, r, g, b);
}

/* two pens (a monochrome screen): Floyd-Steinberg on the brightness alone,
   from tables, in 16-bit arithmetic. The three colours' dither took 250 us
   a pixel on an 8 MHz ST (5.7 s for a 192 x 128 photo); brightness is all
   two pens can show */
static void dither_two(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	long w = p->w, y;
	short lr[32], lg[64], lb[32], y0, y1, mid, i;
	short *rows = calloc((size_t) (w + 2) * 2, sizeof *rows), *cur, *nxt;
	int light;
	/* each pen's brightness (0.299, 0.587, 0.114 in 256ths), and each
	   5-6-5 component's share of a pixel's */
	y0 = (short) ((77 * pal->rgb[0][0] + 150 * pal->rgb[0][1] + 29 * pal->rgb[0][2]) >> 8);
	y1 = (short) ((77 * pal->rgb[1][0] + 150 * pal->rgb[1][1] + 29 * pal->rgb[1][2]) >> 8);
	light = y0 >= y1 ? 0 : 1;
	if (light) {
		short t = y0;
		y0 = y1;
		y1 = t;
	}
	mid = (short) ((y0 + y1) >> 1);		/* y0 the light pen's, y1 the dark one's */
	for (i = 0; i < 32; i++) {
		int c = (i << 3) | (i >> 2);
		lr[i] = (short) ((77 * c) >> 8);
		lb[i] = (short) ((29 * c) >> 8);
	}
	for (i = 0; i < 64; i++)
		lg[i] = (short) ((150 * ((i << 2) | (i >> 4))) >> 8);
	if (rows == NULL) {
		long k;
		for (k = 0; k < (long) p->h * w; k++) {
			unsigned v = p->px[k];
			out[k] = (unsigned char) (lr[v >> 11] + lg[(v >> 5) & 63] + lb[v & 31] >= mid ? light : !light);
		}
		return;
	}
	cur = rows + 1;
	nxt = rows + w + 3;
	for (y = 0; y < p->h; y++) {
		const unsigned short *px = p->px + y * w;
		const unsigned char *al = p->alpha != NULL ? p->alpha + y * w : NULL;
		unsigned char *o = out + y * w;
		short e = 0, *c, *d;	/* e: 7/16 of the last pixel's error, in 16ths */
		long n;
		/* a pixel at s (its alpha a, or none), its pen to o; c its row's
		   errors, d the next row's, STEP the way along (serpentine): a
		   loop each way, so that the pointers step by addressing (a
		   68000's (a0)+ and -(a0)); GCC makes the weights shifts and adds */
#define TWO(STEP) do { \
			unsigned v = *s; \
			short want, q; \
			if (a != NULL && *a < 128) { \
				*o = 0; \
				e = 0; \
				break; \
			} \
			want = (short) (lr[v >> 11] + lg[(v >> 5) & 63] + lb[v & 31] + ((e + *c) >> 4)); \
			if (want >= mid) { \
				*o = (unsigned char) light; \
				q = want - y0; \
			} else { \
				*o = (unsigned char) !light; \
				q = want - y1; \
			} \
			e = (short) (q * 7); \
			d[-(STEP)] += (short) (q * 3); \
			d[0] += (short) (q * 5); \
			d[STEP] = q; \
		} while (0)
		if (!(y & 1)) {
			const unsigned short *s = px;
			const unsigned char *a = al;
			c = cur;
			d = nxt;
			for (n = w; n > 0; n--, s++, o++, c++, d++, a = a != NULL ? a + 1 : NULL)
				TWO(1);
		} else {
			const unsigned short *s = px + w - 1;
			const unsigned char *a = al != NULL ? al + w - 1 : NULL;
			o += w - 1;
			c = cur + w - 1;
			d = nxt + w - 1;
			for (n = w; n > 0; n--, s--, o--, c--, d--, a = a != NULL ? a - 1 : NULL)
				TWO(-1);
		}
#undef TWO
		{
			short *t = cur;
			cur = nxt;
			nxt = t;
			memset(nxt - 1, 0, (size_t) (w + 2) * sizeof *nxt);
		}
	}
	free(rows);
}

static void dither_many(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	/* Floyd-Steinberg: each pixel's error (what it wanted less its pen)
	   carried 7/16 on, and 3/16, 5/16 and 1/16 to the row below,
	   serpentine. The colour wanted may lie past the cube's edge (light
	   blue on 16 colours wants more blue than there is): its pen is the
	   nearest to that, so the error is paid back by pens that lie that
	   way instead of being lost to a clamp (the colour came out grey) */
	long w = p->w, y;
	short *rows = calloc((size_t) (w + 2) * 6, sizeof *rows), *cur, *nxt;
	if (rows == NULL) {
		long i;
		for (i = 0; i < (long) p->h * w; i++) {
			unsigned v = p->px[i];
			out[i] = (unsigned char) nearest_far(pal, (v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8);
		}
		return;
	}
	cur = rows;
	nxt = rows + (w + 2) * 3;
	for (y = 0; y < p->h; y++) {
		const unsigned short *px = p->px + y * w;
		const unsigned char *al = p->alpha != NULL ? p->alpha + y * w : NULL;
		unsigned char *o = out + y * w;
		int dir = (y & 1) ? -1 : 1, step = 3 * dir;
		long x = dir > 0 ? 0 : w - 1, n;
		short *c = cur + (x + 1) * 3, *d = nxt + (x + 1) * 3;
		int er = 0, eg = 0, eb = 0;	/* 7/16 of the last pixel's, in 16ths */
		for (n = 0; n < w; n++, x += dir, c += step, d += step) {
			unsigned v = px[x];
			int r, g, b, k, qr, qg, qb;
			if (al != NULL && al[x] < 128) {
				o[x] = 0;
				er = eg = eb = 0;
				continue;
			}
			r = (int) ((v >> 8) & 0xF8) + ((er + c[0]) >> 4);
			g = (int) ((v >> 3) & 0xFC) + ((eg + c[1]) >> 4);
			b = (int) ((v << 3) & 0xF8) + ((eb + c[2]) >> 4);
			r = r < -128 ? -128 : r > 383 ? 383 : r;
			g = g < -128 ? -128 : g > 383 ? 383 : g;
			b = b < -128 ? -128 : b > 383 ? 383 : b;
			k = pal->near[NEAR_AT(r, g, b)];
			k = k ? k - 1 : nearest_far(pal, r, g, b);
			o[x] = (unsigned char) k;
			qr = r - pal->rgb[k][0];
			qg = g - pal->rgb[k][1];
			qb = b - pal->rgb[k][2];
			er = qr * 7;
			eg = qg * 7;
			eb = qb * 7;
			d[-step] += (short) (qr * 3);
			d[-step + 1] += (short) (qg * 3);
			d[-step + 2] += (short) (qb * 3);
			d[0] += (short) (qr * 5);
			d[1] += (short) (qg * 5);
			d[2] += (short) (qb * 5);
			d[step] = (short) qr;
			d[step + 1] = (short) qg;
			d[step + 2] = (short) qb;
		}
		{
			short *t = cur;
			cur = nxt;
			nxt = t;
			memset(nxt, 0, (size_t) (w + 2) * 6);
		}
	}
	free(rows);
}

void pic_dither(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	if (pal->n == 2)
		dither_two(p, pal, out);
	else
		dither_many(p, pal, out);
}
