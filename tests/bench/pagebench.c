/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pagebench - a page made and drawn as EWEBST.PRG makes and draws it on the
 * ST's monochrome screen (PAGEST.TTP, 68000 code as the ST edition's; DESIGN.md,
 * The ST edition's speed): parsed, styled, laid out WIDTH pixels wide, then
 * drawn as ui/screen.c draws it, a band of 16 rows 640 pixels wide at a
 * time, straight into one plane where canvas.c's cv_page_planes takes the
 * band, else at a byte a pixel. Each step timed on the 200 Hz clock;
 * tests/bench/pagest.py runs it on the emulated 8 MHz ST, cycle-exact, and
 * with --profile names where the cycles go.
 *
 *   pagebench [-f EWEB.FNT] [-w WIDTH] [-r ROWS] PAGE.HTM [SHEET.CSS...]
 *
 * the sheets named are the page's linked ones (EWeb fetches them; here
 * they are read from the disk, as the news page's NEWS.CSS).
 * -r draws only the page's first ROWS rows (all of it, 8000 at most, by
 * default); -n draws it N times. Just before it draws, it asks GEMDOS the
 * time (Tgettime), where pagest.py --draw turns Hatari's profiler on.
 */
#include <stdlib.h>
#include <string.h>

#include <tos.h>

#include "plat.h"
#include "canvas.h"
#include "doc.h"
#include "html.h"
#include "font.h"

#define BAND 16
#define BAND_W 640

char http_user_agent[128] = "EWeb (Atari ST)";

static unsigned long ms(unsigned long t)
{
	return t * 1000UL / TICKS_PER_SEC;
}

static void sheet(void *ud, int is_link, const char *s, size_t len)
{
	if (!is_link)
		ec_sheet(((doc *) ud)->css, s, len, EC_ORIGIN_AUTHOR);
}

/* the monochrome screen's pens and shades, as ewrender -p 2 has them,
   each colour's level kept as ui/screen.c's shade keeps it */
static const uint8_t mono[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };

static long luma(uint32_t rgb)
{
	return 299L * (long) (rgb >> 16 & 255) + 587L * (long) (rgb >> 8 & 255) + 114L * (long) (rgb & 255);
}

static uint8_t pen(void *ud, uint32_t rgb)
{
	(void) ud;
	return luma(rgb) >= 127500L ? 0 : 1;
}

static int shade(void *ud, uint32_t rgb, uint8_t *dark, uint8_t *light)
{
	static uint32_t key[8] = { 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL,
				   0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL };
	static int8_t lev[8];
	unsigned k;
	(void) ud;
	*light = 0;
	*dark = 1;
	rgb &= 0xffffff;
	k = (unsigned) (rgb ^ rgb >> 9 ^ rgb >> 17) & 7;
	if (key[k] != rgb) {
		key[k] = rgb;
		lev[k] = (int8_t) ((luma(rgb) * 16 + 127500L) / 255000L);
	}
	return lev[k];
}

int pagebench(int argc, char **argv)
{
	ec_media m = { 640, 400, 2 };
	const char *fnt = "EWEB.FNT";
	int i = 1;
	long width = 608, rows = 8000, y, planar = 0, bands = 0, times = 1, k;
	unsigned long t0, t_parse, t_style, t_layout, t_draw;
	size_t n;
	char *data;
	doc *d;
	page *p;
	LAYOUT_OPT o;
	html_opts ho;
	canvas c;
	uint8_t *chunky, *plane;

	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-f") && i + 1 < argc)
			fnt = argv[++i];
		else if (!strcmp(argv[i], "-w") && i + 1 < argc)
			width = atol(argv[++i]);
		else if (!strcmp(argv[i], "-r") && i + 1 < argc)
			rows = atol(argv[++i]);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			times = atol(argv[++i]);
	}
	if (i >= argc) {
		out("pagebench [-f EWEB.FNT] [-w WIDTH] [-r ROWS] PAGE.HTM [SHEET.CSS...]\n");
		return 2;
	}
	if (!font_init(fnt)) {
		out("pagebench: can't read %s\n", fnt);
		return 1;
	}
	data = load(argv[i], &n);
	if (data == NULL) {
		out("pagebench: can't read %s\n", argv[i]);
		return 1;
	}
	memset(&ho, 0, sizeof ho);
	ho.drop_scripts = 1;		/* (as EWEBST.PRG: it runs no scripts) */
	t0 = ticks();
	d = doc_parse_opts(data, n, &ho, &m);
	t_parse = ticks() - t0;
	if (d == NULL) {
		out("pagebench: can't parse %s\n", argv[i]);
		return 1;
	}
	out("parsed: %lu elements in %lu ms\n", (unsigned long) d->elements, ms(t_parse));
	t0 = ticks();
	doc_add_ua(d);
	ec_prune(d->css, 1);
	doc_sheets(d, sheet, d);
	for (i++; i < argc; i++) {
		size_t sn;
		char *s = load(argv[i], &sn);
		if (s == NULL) {
			out("pagebench: can't read %s\n", argv[i]);
			return 1;
		}
		ec_sheet(d->css, s, sn, EC_ORIGIN_AUTHOR);
		free(s);
	}
	doc_select(d);
	t_style = ticks() - t0;
	out("styled: %lu ms\n", ms(t_style));
	memset(&o, 0, sizeof o);
	o.width = (int32_t) width;
	o.zoom = 1024;
	o.share_text = 1;
	t0 = ticks();
	p = layout_page(d, &o);
	t_layout = ticks() - t0;
	if (p == NULL) {
		out("pagebench: no room to lay it out\n");
		return 1;
	}
	out("laid out: %d x %d, %ld items in %lu ms\n", (int) p->width, (int) p->height, p->nitems, ms(t_layout));

	chunky = malloc((size_t) BAND_W * BAND);
	plane = malloc((size_t) BAND_W / 8 * BAND);
	if (chunky == NULL || plane == NULL)
		return 1;
	if (rows > p->height)
		rows = p->height;
	(void) Tgettime();	/* (pagest.py --draw: the profiler from here) */
	t0 = ticks();
	for (k = 0; k < times; k++)
	for (y = 0; y < rows; y += BAND) {
		canvas b;
		memset(&c, 0, sizeof c);
		c.w = BAND_W;
		c.h = BAND;
		c.oy = (int32_t) y;
		c.pen = pen;
		c.shade = shade;
		c.pal = mono;
		b = c;
		b.pix = plane;
		b.stride = BAND_W / 8;
		b.bpp = 1;
		bands++;
		if (cv_page_planes(&b, p)) {
			cv_page(&b, p);
			planar++;
			continue;
		}
		c.pix = chunky;
		c.stride = BAND_W;
		c.bpp = 8;
		cv_page(&c, p);
	}
	t_draw = ticks() - t0;
	out("drawn: %ld rows %ld times in %lu ms (%lu us a row), %ld of %ld bands in one plane\n", rows, times,
	    ms(t_draw), rows > 0 ? ms(t_draw) * 1000UL / (unsigned long) (rows * times) : 0UL, planar, bands);
	out("in all: %lu ms\n", ms(t_parse + t_style + t_layout + t_draw));
	return 0;
}
