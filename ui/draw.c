/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * draw.c - the window's parts in the system font, through the VDI (from
 * EFTP's and EMail's): fills, frames, lines and text, Hebrew laid out
 * right to left first (common/bidi.c). The page itself is drawn by
 * screen.c in EWeb's own fonts.
 */
#include <string.h>
#include <stdlib.h>

#include "ui.h"
#include "bidi.h"
#include "charset.h"
#include "util.h"

void clip_on(GRECT *r)
{
	short pxy[4];
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vs_clip(vdi_h, 1, pxy);
}

/* no clipping: the whole screen (the shared pop-up and About box) */
void clip_off(void)
{
	short pxy[4] = { 0, 0, 0, 0 };
	pxy[2] = scr_w - 1;
	pxy[3] = scr_h - 1;
	vs_clip(vdi_h, 0, pxy);
}

/* EWeb draws its own parts in the system font only, at its own size:
   set again, as a dialog may have left another (the About box's fine
   print, last drawn when it is a window: 3.5) */
void font_sys_push(void)
{
	short d;
	vst_height(vdi_h, ch >= 16 ? 13 : 6, &d, &d);
}

void font_sys_pop(void)
{
}

/* Bitmaps the VDI copies to the screen must be in ST RAM: the blitter
   can't reach TT RAM, where EWeb runs when there is some. One small
   block for the About box's icon; the page has its own (screen.c). */
static unsigned short st_fallback[256];
static unsigned short *st_buf;

void *st_ram(long n)
{
	if (!st_buf) {
		void *p = Mxalloc(sizeof(st_fallback), 0);
		st_buf = (long) p > 0 ? p : st_fallback;
	}
	return n <= (long) sizeof(st_fallback) ? st_buf : 0;
}

int rect_and(GRECT *a, const GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x;
	short y1 = a->y > b->y ? a->y : b->y;
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	a->x = x1;
	a->y = y1;
	a->w = x2 - x1;
	a->h = y2 - y1;
	return a->w > 0 && a->h > 0;
}

int rect_hit(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* VDI's light grey (pen 8) with 16 colours or more, else none */
short grey_pen(void)
{
	return planes >= 4 ? 8 : 0;
}

void fill(GRECT *r, short color)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vsf_interior(vdi_h, FIS_SOLID);
	vsf_color(vdi_h, color);
	vsf_perimeter(vdi_h, 0);
	vswr_mode(vdi_h, MD_REPLACE);
	vr_recfl(vdi_h, pxy);
}

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_type(vdi_h, 1);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 2, p);
}

void hline(short x1, short x2, short y)
{
	line_col(x1, y, x2, y, 1);
}

void frame(GRECT *r, short color)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_type(vdi_h, 1);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 5, p);
}

/* text in the Atari character set at most cols characters wide, its top
   at y (vst_alignment top), Hebrew right to left */
void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	char vis[BIDI_MAX];
	const char *out = s;
	short i, grey = (flags & TX_DIM) && planes >= 4;
	if (cols <= 0)
		return;
	if (n > cols)
		n = cols;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (n <= 0)
		return;
	if (bidi_has_rtl(s, (short) n)) {
		short rtl = (flags & TX_LTR) ? 0 : bidi_is_rtl(s, (short) n);
		bidi_visual(s, (short) n, rtl, vis);
		out = vis;
		if (rtl && (flags & TX_RIGHT))
			x += (cols - (short) n) * cw;
	}
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : grey ? 9 : 1);
	vst_effects(vdi_h, ((flags & TX_BOLD) ? 1 : 0) | ((flags & TX_DIM) && !grey ? 2 : 0));
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short) (n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
	if (flags & (TX_BOLD | TX_DIM))
		vst_effects(vdi_h, 0);
}

void atari_text(const char *utf8, char *out, int size)
{
	long n;
	char *a = cs_to_atari(utf8, (long) strlen(utf8), CS_UTF8, &n);
	if (a == NULL) {
		str_copy(out, utf8, size);
		return;
	}
	if (n >= size)
		n = size - 1;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (bidi_has_rtl(a, (short) n)) {
		bidi_visual(a, (short) n, 0, out);
		out[n] = 0;
	} else {
		memcpy(out, a, n);
		out[n] = 0;
	}
	free(a);
}
