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
#include "text.h"
#include "dialog.h"

void clip_on(GRECT *r)
{
	short pxy[4];
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	text_clip(vdi_h, pxy);	/* (text_n then knows it: common's text.c) */
}

/* no clipping: the whole screen (the shared pop-up and About box) */
void clip_off(void)
{
	text_noclip(vdi_h);
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

/* the UI's own colours (the guidelines' 5.2): each the pen of the first
   16 nearest its colour in TOS's standard palette, as they are now
   (vq_color: NVDI's 16 are not TOS's, a TT's 9 to 14 are light), never a
   fixed pen number; found once, at start (EWeb's own 256 colours leave
   pens 0 to 15 as they are) */
static short pen_grey, pen_dark, pen_rib[5];

void ui_pens_init(void)
{
	/* the ribbon's: blue, then pale cyan, magenta, yellow and green */
	static const short rib[5][3] = { { 0, 0, 1000 }, { 500, 1000, 1000 }, { 1000, 500, 1000 },
					 { 1000, 1000, 500 }, { 500, 1000, 500 } };
	int i;
	if (planes < 4) {
		pen_grey = 0;
		pen_dark = 1;
		for (i = 0; i < 5; i++)
			pen_rib[i] = 1;
		return;
	}
	pen_grey = pen_nearest(vdi_h, 16, 750, 750, 750, 0);	/* TOS's light grey, pen 8 */
	pen_dark = pen_nearest(vdi_h, 16, 450, 450, 450, 1);	/* its dark grey, pen 9: for text */
	for (i = 0; i < 5; i++)
		pen_rib[i] = pen_nearest(vdi_h, 16, rib[i][0], rib[i][1], rib[i][2], 0);
}

/* light grey with 16 colours or more, else none (white) */
short grey_pen(void)
{
	return pen_grey;
}

/* dark grey with 16 colours or more, else black */
short dark_pen(void)
{
	return pen_dark;
}

/* the progress ribbon's colour for a phase (0 to 4) */
short ribbon_pen(int phase)
{
	return phase >= 0 && phase < 5 ? pen_rib[phase] : 1;
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
	short colour;
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
	colour = (flags & TX_INVERSE) ? 0 : (flags & TX_DIM) && planes >= 4 ? dark_pen() : 1;
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, colour);
	if ((flags & TX_DIM) && planes < 4)
		vst_effects(vdi_h, 2);	/* light, in mono: a pop-up's few words */
	text_n(vdi_h, x, y, out, n);	/* TOS's quick way where the clip allows (Speed) */
	if ((flags & TX_DIM) && planes < 4)
		vst_effects(vdi_h, 0);
	if (flags & TX_BOLD) {
		/* bold on the plain text, never vst_effects' (the guidelines'
		   Speed: TOS's bold costs an ST far more; NVDI's widens each
		   letter over the next) */
		if (text_fx_fast(1, MD_TRANS))
			text_fx(vdi_h, x, y, out, n, 1, colour);
		else
			text_bold_strike(vdi_h, x, y, out, n);
	}
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
