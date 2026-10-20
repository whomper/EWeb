/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * img.c - the pictures pages show (img.h).
 *
 * Each address is kept once, however many pages and places show it,
 * with the file as it came and each size it is decoded at (a page shows
 * most pictures at one size; another window width or page size adds
 * another). The memory they take is counted: past img_max the oldest
 * that no page shows go, then the decoded pixels of pictures only kept
 * pages show (their files stay: decoding again is quicker than fetching).
 *
 * A picture decoded is kept in the disk cache too (dcache.h, as EWeb's
 * own data), as the screen draws it: its pens for the screen's palette
 * (and which pixels are solid), or in true colour its pixels. Its key is
 * made from the file's contents, the size and the palette, so it is never
 * out of date: the next time a page shows that file at that size, on
 * that screen, it is read, not decoded and dithered again (seconds on a
 * 68030 for a photo).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcache.h"
#include "img.h"
#include "util.h"

typedef struct var var;
struct var {
	var *next;
	int32_t w, h;
	PIC *pic;			/* NULL: it couldn't be decoded */
	uint8_t *pens;			/* for img_pal, made when first drawn */
	long serial;			/* img_pal's when the pens were made */
	unsigned long used;
};

struct ew_img {
	ew_img *next;
	char *url;
	int state;
	char *data;			/* the file */
	long len;
	int32_t iw, ih;			/* its size, CSS px (0: not known) */
	int why;			/* PIC_* of the last decode that failed */
	int kind;			/* PIC_*: what the file is */
	var *vars;
	int refs;			/* pages showing it */
	int cur;			/* the page shown shows it */
	unsigned long used;		/* when last drawn or decoded */
	unsigned long sum[2];		/* the file's contents, for the disk cache's key */
	int cached;			/* its last decode came from the disk cache */
};

static ew_img *all;
static unsigned long tick;
static long pal_serial = 1;

PIC_PAL *img_pal;
long img_max = 8L << 20;
long img_used;
/* a file of more pixels than this isn't decoded (four bytes a pixel for
   a GIF or an interlaced PNG while it is; a JPEG streams, and may have
   twelve times as many) */
long img_max_px = 4L << 20;

static long var_bytes(const var *v)
{
	long n = (long) sizeof *v;
	if (v->pic != NULL)
		n += (long) v->pic->w * v->pic->h * ((v->pic->px != NULL ? 2 : 0) + (v->pic->alpha != NULL ? 1 : 0));
	if (v->pens != NULL)
		n += (long) v->pic->w * v->pic->h;
	return n;
}

/* ---------------------------------------------------------------- the disk cache */

/* pictures smaller than this are decoded each time (quicker than a look
   in the cache, and many small entries would crowd its index) */
#define PIX_MIN (64L * 64)

/* "EWPX", 1, flags (PX_*), the width and height as two bytes each, then
   the pixels (2 bytes each, the machine's order), the alpha (a byte
   each), the pens (a byte each) */
enum { PX_PIXELS = 1, PX_ALPHA = 2, PX_PENS = 4 };
#define PIX_HEAD 10

static void sums(const char *d, long n, unsigned long *s)
{
	unsigned long a = 2166136261UL, b = 5381;
	long i;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char) d[i];
		a = ((a ^ c) * 16777619UL) & 0xFFFFFFFFUL;
		b = (b * 33 + c) & 0xFFFFFFFFUL;
	}
	s[0] = a;
	s[1] = b;
}

/* the key: the file (its two sums and length), the size, and the screen:
   true colour, or its palette (each pen's colour summed) */
static void pix_key(const ew_img *im, int32_t w, int32_t h, char *out, int size)
{
	unsigned long p[2] = { 0, 0 };
	if (img_pal != NULL)
		sums((const char *) img_pal->rgb, (long) img_pal->n * 3, p);
	snprintf(out, (size_t) size, "pix:%08lx%08lx:%ld:%ldx%ld:%s%08lx", im->sum[0], im->sum[1], im->len, (long) w,
		 (long) h, img_pal != NULL ? "p" : "tc", p[0] ^ p[1]);
}

/* v (w x h) from the disk cache: 1 when it was there and whole */
static int pix_load(ew_img *im, var *v)
{
	char key[120];
	DC_GOT g;
	const unsigned char *d;
	long n = (long) v->w * v->h, want;
	int fl, ok = 0;
	PIC *pic;
	pix_key(im, v->w, v->h, key, sizeof key);
	if (!dc_get(key, &g))
		return 0;
	d = (const unsigned char *) g.data;
	fl = g.len >= PIX_HEAD ? d[5] : 0;
	want = PIX_HEAD + (fl & PX_PIXELS ? 2 * n : 0) + (fl & PX_ALPHA ? n : 0) + (fl & PX_PENS ? n : 0);
	if (g.len == want && memcmp(d, "EWPX\1", 5) == 0 && (d[6] << 8 | d[7]) == v->w && (d[8] << 8 | d[9]) == v->h &&
	    ((fl & PX_PIXELS) || ((fl & PX_PENS) && img_pal != NULL)) && (pic = calloc(1, sizeof *pic)) != NULL) {
		const unsigned char *s = d + PIX_HEAD;
		pic->w = (int) v->w;
		pic->h = (int) v->h;
		pic->how = im->kind;
		ok = 1;
		if (fl & PX_PIXELS) {
			if ((pic->px = malloc((size_t) n * 2)) == NULL)
				ok = 0;
			else
				memcpy(pic->px, s, (size_t) n * 2);
			s += 2 * n;
		}
		if (ok && (fl & PX_ALPHA)) {
			if ((pic->alpha = malloc((size_t) n)) == NULL)
				ok = 0;
			else
				memcpy(pic->alpha, s, (size_t) n);
			s += n;
		}
		if (ok && (fl & PX_PENS) && img_pal != NULL) {
			if ((v->pens = malloc((size_t) n)) == NULL)
				ok = 0;
			else {
				memcpy(v->pens, s, (size_t) n);
				v->serial = pal_serial;
			}
		}
		if (ok)
			v->pic = pic;
		else {
			pic_free(pic);
			free(v->pens);
			v->pens = NULL;
		}
	}
	free(g.data);
	return ok;
}

/* v into the disk cache, as the screen draws it */
static void pix_save(ew_img *im, const var *v)
{
	char key[120];
	long n = (long) v->w * v->h, len;
	int fl;
	unsigned char *b, *s;
	if (img_pal != NULL && v->pens != NULL)
		fl = PX_PENS | (v->pic->alpha != NULL ? PX_ALPHA : 0);
	else if (v->pic->px != NULL)
		fl = PX_PIXELS | (v->pic->alpha != NULL ? PX_ALPHA : 0);
	else
		return;
	len = PIX_HEAD + (fl & PX_PIXELS ? 2 * n : 0) + (fl & PX_ALPHA ? n : 0) + (fl & PX_PENS ? n : 0);
	if (len > dc_own_max() || (b = malloc((size_t) len)) == NULL)	/* too big to keep: none made */
		return;
	memcpy(b, "EWPX\1", 5);
	b[5] = (unsigned char) fl;
	b[6] = (unsigned char) (v->w >> 8);
	b[7] = (unsigned char) v->w;
	b[8] = (unsigned char) (v->h >> 8);
	b[9] = (unsigned char) v->h;
	s = b + PIX_HEAD;
	if (fl & PX_PIXELS) {
		memcpy(s, v->pic->px, (size_t) n * 2);
		s += 2 * n;
	}
	if (fl & PX_ALPHA) {
		memcpy(s, v->pic->alpha, (size_t) n);
		s += n;
	}
	if (fl & PX_PENS)
		memcpy(s, v->pens, (size_t) n);
	pix_key(im, v->w, v->h, key, sizeof key);
	dc_put_own(key, im->url, (const char *) b, len);
	free(b);
}

static void var_free(ew_img *im, var *v)
{
	var **pp;
	for (pp = &im->vars; *pp != NULL; pp = &(*pp)->next)
		if (*pp == v) {
			*pp = v->next;
			break;
		}
	img_used -= var_bytes(v);
	pic_free(v->pic);
	free(v->pens);
	free(v);
}

ew_img *img_get(const char *url)
{
	ew_img *im;
	for (im = all; im != NULL; im = im->next)
		if (strcmp(im->url, url) == 0)
			return im;
	im = calloc(1, sizeof *im);
	if (im == NULL)
		return NULL;
	im->url = str_ndup(url, (long) strlen(url));
	if (im->url == NULL) {
		free(im);
		return NULL;
	}
	im->next = all;
	all = im;
	img_used += (long) sizeof *im + (long) strlen(url);
	return im;
}

const char *img_url(const ew_img *im)
{
	return im->url;
}

int img_state(const ew_img *im)
{
	return im->state;
}

void img_size(const ew_img *im, int32_t *w, int32_t *h)
{
	*w = im->state == IMG_HERE ? im->iw : 0;
	*h = im->state == IMG_HERE ? im->ih : 0;
}

void img_ref(ew_img *im, int d)
{
	im->refs += d;
	if (im->refs < 0)
		im->refs = 0;
}

int img_set_file(ew_img *im, char *data, long len)
{
	long w, h;
	free(im->data);
	img_used -= im->len;
	im->data = NULL;
	im->len = 0;
	im->kind = pic_kind((const unsigned char *) data, len);
	if ((im->kind != PIC_JPEG && im->kind != PIC_PNG && im->kind != PIC_GIF) ||
	    !pic_size((const unsigned char *) data, len, &w, &h) || w > 0x7fff || h > 0x7fff) {
		free(data);
		im->state = IMG_FAILED;
		im->why = im->kind == PIC_NONE ? PIC_DAMAGED : PIC_UNSUPPORTED;
		return 0;
	}
	im->data = data;
	im->len = len;
	sums(data, len, im->sum);
	im->iw = (int32_t) w;
	im->ih = (int32_t) h;
	im->state = IMG_HERE;
	img_used += len;
	return 1;
}

void img_failed(ew_img *im)
{
	im->state = IMG_FAILED;
}

void img_waiting(ew_img *im)
{
	if (im->state != IMG_HERE)
		im->state = IMG_WAITING;
}

static var *find(const ew_img *im, int32_t w, int32_t h)
{
	var *v;
	for (v = im->vars; v != NULL; v = v->next)
		if (v->w == w && v->h == h)
			return v;
	return NULL;
}

int img_tried(const ew_img *im, int32_t w, int32_t h)
{
	return im->state == IMG_FAILED || find(im, w, h) != NULL;
}

int img_decode(ew_img *im, int32_t w, int32_t h)
{
	var *v = find(im, w, h);
	long max;
	if (v != NULL)
		return v->pic != NULL;
	if (im->state != IMG_HERE || w <= 0 || h <= 0 || w > 4096 || h > 4096)
		return 0;
	v = calloc(1, sizeof *v);
	if (v == NULL)
		return 0;
	v->w = w;
	v->h = h;
	im->cached = 0;
	if ((long) w * h >= PIX_MIN && pix_load(im, v)) {
		im->cached = 1;
		v->used = im->used = ++tick;
		v->next = im->vars;
		im->vars = v;
		img_used += var_bytes(v);
		return 1;
	}
	max = im->kind == PIC_JPEG ? img_max_px * 12 : img_max_px;
	v->pic = pic_decode((const unsigned char *) im->data, im->len, (int) w, (int) h, max, &im->why);
	if (v->pic == NULL && im->why == PIC_STOPPED) {
		free(v);	/* stopped: tried again later */
		return 0;
	}
	if (v->pic != NULL && img_pal != NULL) {
		/* the screen's pens now, so that drawing has nothing to work out */
		v->pens = malloc((size_t) w * h);
		if (v->pens != NULL) {
			pic_dither(v->pic, img_pal, v->pens);
			v->serial = pal_serial;
		}
	}
	v->used = im->used = ++tick;
	v->next = im->vars;
	im->vars = v;
	img_used += var_bytes(v);
	if (v->pic != NULL && (long) w * h >= PIX_MIN)
		pix_save(im, v);
	return v->pic != NULL;
}

int img_from_cache(const ew_img *im)
{
	return im->cached;
}

int img_picture(ew_img *im, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens)
{
	var *v = find(im, w, h);
	*pic = NULL;
	*pens = NULL;
	if (v == NULL || v->pic == NULL)
		return 0;
	v->used = im->used = ++tick;
	if (img_pal != NULL && (v->pens == NULL || v->serial != pal_serial) && v->pic->px == NULL) {
		var_free(im, v);	/* from the disk cache for another palette: decoded again */
		return 0;
	}
	if (img_pal != NULL && (v->pens == NULL || v->serial != pal_serial)) {
		if (v->pens == NULL) {
			v->pens = malloc((size_t) w * h);
			if (v->pens != NULL)
				img_used += (long) w * h;
		}
		if (v->pens != NULL) {
			pic_dither(v->pic, img_pal, v->pens);
			v->serial = pal_serial;
		}
	}
	*pic = v->pic;
	*pens = v->pens;
	return 1;
}

const char *img_why(const ew_img *im)
{
	if (im->state == IMG_FAILED && im->kind != PIC_NONE && im->why == PIC_UNSUPPORTED)
		return pic_kind_name(im->kind);
	if (im->state == IMG_FAILED && im->why == PIC_OK)
		return "it didn't come";
	return pic_why_text(im->why);
}

void img_pal_changed(void)
{
	pal_serial++;
}

static void img_free(ew_img *im)
{
	while (im->vars != NULL)
		var_free(im, im->vars);
	img_used -= im->len + (long) sizeof *im + (long) strlen(im->url);
	free(im->data);
	free(im->url);
	free(im);
}

void img_trim(void)
{
	/* first: the oldest no page shows, whole */
	while (img_used > img_max) {
		ew_img *im, *old = NULL, **pp;
		for (im = all; im != NULL; im = im->next)
			if (im->refs == 0 && (old == NULL || im->used < old->used))
				old = im;
		if (old == NULL)
			break;
		for (pp = &all; *pp != old; pp = &(*pp)->next)
			;
		*pp = old->next;
		img_free(old);
	}
	/* then: decoded pixels of pictures the page shown doesn't show, or
	   at a size it doesn't, oldest first */
	while (img_used > img_max) {
		ew_img *im, *oi = NULL;
		var *v, *ov = NULL;
		for (im = all; im != NULL; im = im->next)
			for (v = im->vars; v != NULL; v = v->next)
				if (v->pic != NULL && (!im->cur || v->used < im->used) && (ov == NULL || v->used < ov->used)) {
					ov = v;
					oi = im;
				}
		if (ov == NULL)
			break;
		var_free(oi, ov);
	}
}

void img_current(ew_img *im)
{
	if (im == NULL) {
		for (im = all; im != NULL; im = im->next)
			im->cur = 0;
	} else {
		im->cur = 1;
	}
}

void img_clear(void)
{
	ew_img **pp = &all;
	while (*pp != NULL) {
		ew_img *im = *pp;
		if (im->refs == 0) {
			*pp = im->next;
			img_free(im);
		} else {
			pp = &im->next;
		}
	}
}
