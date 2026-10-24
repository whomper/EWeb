/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * drawbench - a page laid out and drawn as EWEB.PRG does it (src/layout's
 * display list, then src/gfx's canvas, the window's 400 rows at a time as
 * ui/screen.c draws them), for counting the 68030 instructions that takes
 * (tools/count68k.sh, tools/prof68k.py):
 *
 *   drawbench [-P 1|2|3] [-b 8|16] [-f EWEB.FNT] [-V N [-M]] [-F WORDS] PAGE.HTM [SHEET.CSS...]
 *
 * -P stops after a phase: 1 the document styled (its <style>s and the
 * sheets named), 2 laid out (608 px wide, a CSS px 0.8 of the screen's),
 * 3 (the default) drawn, the whole page (at most 8000 rows), at 8 bits
 * in the 16 standard colours (each colour's pen found as ui/screen.c
 * finds it) or at 16 bits, true colour (-b 16). Pictures aren't fetched.
 * -V N: a history of N pages (src/browse/history.c, read as HISTORY.TXT
 * is), and -M the page's links marked visited from it before it is
 * styled, as at https://example.com/news/index.html (visited.c).
 * -F WORDS: once laid out, the words looked for on it (Edit > Find,
 * find.c), how many printed.
 * Prints the page's height and items.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "doc.h"
#include "font.h"
#include "history.h"
#include "visited.h"

#define W 608
#define ROWS 400

char http_user_agent[128] = "EWeb (Atari Falcon030)";

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	char *d;
	long len;
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc((size_t) len + 1);
	if (d != NULL && fread(d, 1, (size_t) len, f) != (size_t) len) {
		free(d);
		d = NULL;
	}
	fclose(f);
	*n = (size_t) len;
	return d;
}

static void sheet(void *ud, int is_link, const char *s, size_t len)
{
	if (!is_link)
		ec_sheet(((doc *) ud)->css, s, len, EC_ORIGIN_AUTHOR);
}

/* the Falcon's 16 standard colours, and the nearest of them as
   ui/screen.c finds it (cached by 12-bit colour) */
static const uint8_t pal[16][3] = {
	{ 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
	{ 0, 255, 255 }, { 170, 170, 170 }, { 85, 85, 85 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 170, 0 },
	{ 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 0, 0, 0 }
};
static uint8_t nearest_of[4096];

static uint8_t pen(void *ud, uint32_t rgb)
{
	unsigned key = (unsigned) ((rgb >> 12 & 0xf00) | (rgb >> 8 & 0xf0) | (rgb >> 4 & 0xf));
	long best = 0x7fffffffL;
	int i, pick = 0, r = (int) (rgb >> 16 & 255), g = (int) (rgb >> 8 & 255), b = (int) (rgb & 255);
	(void) ud;
	if (nearest_of[key])
		return (uint8_t) (nearest_of[key] - 1);
	for (i = 0; i < 16; i++) {
		long dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
		long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
		if (d < best) {
			best = d;
			pick = i;
		}
	}
	nearest_of[key] = (uint8_t) (pick + 1);
	return (uint8_t) pick;
}

int main(int argc, char **argv)
{
	ec_media m = { W, ROWS, 16 };
	const char *fnt = "binary/EWEB.FNT";
	int i = 1, phase = 3, bpp = 8, hist = 0, mark = 0;
	const char *find = NULL;
	size_t n;
	char *data;
	doc *d;
	page *p;
	LAYOUT_OPT o;

	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-P") && i + 1 < argc)
			phase = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-b") && i + 1 < argc)
			bpp = atoi(argv[++i]) == 16 ? 16 : 8;
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			fnt = argv[++i];
		else if (!strcmp(argv[i], "-V") && i + 1 < argc)
			hist = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-M"))
			mark = 1;
		else if (!strcmp(argv[i], "-F") && i + 1 < argc)
			find = argv[++i];
	}
	if (i >= argc) {
		fprintf(stderr, "drawbench [-P 1|2|3] [-b 8|16] [-f EWEB.FNT] [-V N [-M]] [-F WORDS] PAGE.HTM [SHEET.CSS...]\n");
		return 2;
	}
	if (!font_init(fnt)) {
		fprintf(stderr, "drawbench: can't read %s\n", fnt);
		return 1;
	}
	data = slurp(argv[i], &n);
	if (data == NULL || (d = doc_parse(data, n, NULL, &m)) == NULL) {
		fprintf(stderr, "drawbench: can't read %s\n", argv[i]);
		return 1;
	}
	doc_add_ua(d);
	doc_sheets(d, sheet, d);
	for (i++; i < argc; i++) {
		size_t sn;
		char *s = slurp(argv[i], &sn);
		if (s != NULL)
			ec_sheet(d->css, s, sn, EC_ORIGIN_AUTHOR);
		free(s);
	}
	if (hist > 0) {
		/* as HISTORY.TXT is read: a line a page */
		char *t = malloc((size_t) hist * 64 + 1), *q = t;
		long k;
		for (k = 0; t != NULL && k < hist; k++)
			q += sprintf(q, "1792000000\thttps://example.com/news/story%ld.html\tA story\n", k);
		if (t == NULL || !hg_parse(t, q - t))
			return 1;
		free(t);
		if (mark)
			printf("%ld links visited\n", vis_mark(d, "https://example.com/news/index.html", NULL, NULL, 0));
	}
	doc_select(d);
	if (phase < 2) {
		printf("%lu elements\n", (unsigned long) d->elements);
		return 0;
	}
	memset(&o, 0, sizeof o);
	o.width = W;
	o.zoom = 819;
	p = layout_page(d, &o);
	if (p == NULL)
		return 1;
	printf("page %d x %d, %ld items\n", (int) p->width, (int) p->height, p->nitems);
	if (find != NULL) {
		pg_pos a, b;
		long nth;
		printf("%ld found\n", page_find(p, find, NULL, 0, 0, &a, &b, &nth));
	}
	if (phase >= 3) {
		canvas c;
		int32_t y;
		memset(&c, 0, sizeof c);
		c.w = W;
		c.h = ROWS;
		c.bpp = bpp;
		c.stride = (long) W * bpp / 8;
		c.pen = pen;
		c.pal = pal;
		c.pix = malloc((size_t) c.stride * ROWS);
		if (c.pix == NULL)
			return 1;
		for (y = 0; y < p->height && y < 8000; y += ROWS) {
			c.oy = y;
			cv_page(&c, p);
		}
	}
	return 0;
}
