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
 */
#include <stdlib.h>
#include <string.h>

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
		n += (long) v->pic->w * v->pic->h * (v->pic->alpha != NULL ? 3 : 2);
	if (v->pens != NULL)
		n += (long) v->pic->w * v->pic->h;
	return n;
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
	return v->pic != NULL;
}

int img_picture(ew_img *im, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens)
{
	var *v = find(im, w, h);
	*pic = NULL;
	*pens = NULL;
	if (v == NULL || v->pic == NULL)
		return 0;
	v->used = im->used = ++tick;
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
