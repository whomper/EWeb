/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * win.c - EWeb's window, as a browser's: a toolbar along the top (Back,
 * Forward, Reload, Home, Search and Stop as pictures at the left, flat
 * until the mouse is over one, its name then on the info line; the
 * address beside them, which can be typed into; at the right the
 * throbber, the AERA A with a star going round it while a page or its
 * pictures come), the page under it with the window's own
 * slider, and the status bar at the foot (a link's address under the
 * mouse, what a load is doing, a button's name; under it a thin line
 * that fills from left to right as a load comes, in each phase's colour:
 * Erez's choice of 21 Oct 2026, design C). GEM's info line takes only
 * text, so the window has none and draws its own. The page is drawn by screen.c; scrolling
 * moves what is on the screen and draws only the new rows when it can.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "canvas.h"
#include "charset.h"
#include "clip.h"
#include "form.h"
#include "iconify.h"
#include "panebar.h"
#include "util.h"
#include "inflate.h"
#include "plat.h"
#include "tbicons.h"
#include "throbber.h"

ewin *wins;			/* every window, the newest first */
ewin *cur;			/* the one worked on */
int win_max = 2;

#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE | LFARROW | RTARROW | HSLIDE)
#define POINT_HAND 3
#define TEXT_CRSR  1

static void redraw(const GRECT *area, int parts);

/* ---------------------------------------------------------------- the toolbar */

/* the buttons, in the order of tbicons.h's pictures */
enum { B_BACK, B_FORWARD, B_RELOAD, B_HOME, B_SEARCH, B_STOP, NBUT };
typedef char nbut_is_win_buttons[NBUT == WIN_BUTTONS ? 1 : -1];
static const char *const names[NBUT] = { "Back", "Forward", "Reload", "Home", "Search", "Stop" };
static short tall;			/* the 20-row pictures (else the 10-row ones) */

static int enabled(int i)
{
	switch (i) {
	case B_BACK: return !loading && br_can_back(&br);
	case B_FORWARD: return !loading && br_can_forward(&br);
	case B_RELOAD: return !loading && br.cur >= 0;
	case B_HOME: return !loading;
	case B_SEARCH: return !loading && opt.search[0];
	case B_STOP: return loading || cur->stopper || br_refresh_due(&br) >= 0;	/* or the page's refresh */
	}
	return 0;
}


/* one row: the buttons at the left, the address beside them, the
   throbber at the right (where the window is wide enough for it and
   eight letters of the address), each in the middle of the row */
static void layout(void)
{
	short x, i, ih, bh, th;
	tall = gl_hchar >= 16;
	ih = tall ? TB_TALL : TB_SHORT;
	bh = 3 + ih + 3;		/* a button: its picture in its frame */
	th = 2 + bh + 3;		/* and the line under the toolbar */
	cur->r_tool = cur->work;
	cur->r_tool.h = th;
	x = cur->work.x + 4;
	for (i = 0; i < NBUT; i++) {
		cur->but[i].x = x;
		cur->but[i].y = cur->work.y + 2;
		cur->but[i].w = TB_W + 6;
		cur->but[i].h = bh;
		x += cur->but[i].w + 2;
	}
	/* the address's text on a byte, 4 pixels into its frame (the
	   guidelines' Speed: TOS 1.04 draws text off a byte seven times
	   slower; the work area is on one, wind_x8) */
	cur->r_addr.x = cur->work.x + ((x + 6 + 4 - cur->work.x + 7) & ~7) - 4;
	cur->r_addr.h = gl_hchar + 4;
	cur->r_addr.y = cur->work.y + (th - 1 - cur->r_addr.h) / 2;
	cur->r_addr.w = cur->work.x + cur->work.w - 4 - cur->r_addr.x;
	cur->r_thr.w = THR_W;
	cur->r_thr.h = tall ? THR_TALL : THR_SHORT;
	cur->r_thr.x = cur->work.x + cur->work.w - 4 - THR_W;
	cur->r_thr.y = cur->work.y + (th - 1 - cur->r_thr.h) / 2;
	if (cur->r_thr.x - 8 < cur->r_addr.x + 8 * gl_wchar)
		cur->r_thr.w = 0;
	else
		cur->r_addr.w = cur->r_thr.x - 8 - cur->r_addr.x;
	if (cur->r_addr.w < 4 * gl_wchar)
		cur->r_addr.w = 0;		/* no room for it */
	/* the status bar: its text, the ribbon along its foot */
	cur->r_stat = cur->work;
	cur->r_stat.h = gl_hchar + (tall ? 7 : 5);
	cur->r_stat.y = cur->work.y + cur->work.h - cur->r_stat.h;
	cur->r_page = cur->work;
	cur->r_page.y += th;
	cur->r_page.h -= th + cur->r_stat.h;
	if (cur->r_page.h < 0)
		cur->r_page.h = 0;
}

static short paper(void)
{
	return planes >= 4 ? grey_pen() : 0;
}

/* an edition's pictures (the throbber's frames, the toolbar's icons), all
   unpacked at once: they are deflated together (tools/mkthrobber.py,
   mktbicons.py), one Huffman table for all. NULL out of memory */
static unsigned char *unpack_all(const unsigned char *data, long len, long n)
{
	SBUF b;
	sb_init(&b);
	if (sb_reserve(&b, n) && inflate_raw(data, len, &b, n) == 1 && b.len == n)
		return (unsigned char *) b.s;
	sb_free(&b);
	return NULL;
}

/* the icons' edition for this screen, as the throbber's */
static const TB_ED *tb_ed;
static unsigned char *tb_raw;		/* its pictures unpacked (kept: a look is made when first shown) */
static char *tb_dev;			/* the buttons whole, each look of each, in the screen's own form (ST RAM), each made when first shown */
static unsigned tb_made;		/* which of them are made, a bit each */
static int tb_nodev;			/* no memory to keep them: each made as it is drawn */
static short tb_w, tb_h;		/* the buttons' size they were made at */

static const TB_ED *icons(void)
{
#ifndef EW_ST
	if (planes >= 8)
		return tall ? &tb_256_tall : &tb_256_short;
	if (planes == 4)
		return tall ? &tb_16_tall : &tb_16_short;
#else
	if (planes == 4 && !tall)
		return &tb_16_short;
#endif
	return tall ? &tb_mono_tall : &tb_mono_short;
}

/* a button's looks: as it is, greyed when it can't be used, raised and
   lit under the mouse, sunk while held down; its icon's pictures: as it
   is, greyed, lit (it wakes) */
enum { TB_ON, TB_GREYED, TB_LIT };
enum { LOOK_ON, LOOK_GREYED, LOOK_HOT, LOOK_DOWN, LOOKS };

/* a look's frame, as lines across and down from the button's corner: in
   colour where there are 16 or more, light at the top and left when it
   is raised and dark when sunk; else black, with a shadow when raised */
static short look_frame(int look, short w, short h, SCR_LINE *l)
{
	short n = 0, down = look == LOOK_DOWN;
#define LN(a, b, c, d, k) (l[n].x0 = (a), l[n].y0 = (b), l[n].x1 = (c), l[n].y1 = (d), l[n].col = (k), n++)
	if (look != LOOK_HOT && look != LOOK_DOWN)
		return 0;
	if (planes >= 4) {
		short lt = down ? 9 : 0, rb = down ? 0 : 9;
		LN(0, 0, w - 1, 0, lt);
		LN(0, 0, 0, h - 1, lt);
		LN(0, h - 1, w - 1, h - 1, rb);
		LN(w - 1, 0, w - 1, h - 1, rb);
		if (down) {
			LN(1, 1, w - 2, 1, 1);
			LN(1, 1, 1, h - 2, 1);
		}
	} else {
		LN(0, 0, w - 2, 0, 1);
		LN(0, h - 2, w - 2, h - 2, 1);
		LN(0, 0, 0, h - 2, 1);
		LN(w - 2, 0, w - 2, h - 2, 1);
		if (down) {
			LN(1, 1, w - 3, 1, 1);
			LN(1, 1, 1, h - 3, 1);
		} else {
			LN(1, h - 1, w - 1, h - 1, 1);
			LN(w - 1, 1, w - 1, h - 1, 1);
		}
	}
#undef LN
	return n;
}

/* button i in its look, made whole in memory (its ground, its frame, its
   icon over them) and put on the screen in one copy, within clip: none
   of the steps is seen (on a stock Falcon the mouse going over a button
   showed it cleared, its frame, a hole and then its icon, each made
   again at every draw; Erez, 26 Oct 2026). The looks are kept once made
   (4 to 30 KB of ST RAM) */
static void draw_look(int i, int look, const GRECT *clip)
{
	const TB_ED *e = tb_ed != NULL ? tb_ed : (tb_ed = icons());
	GRECT at = cur->but[i], r = at;
	short w = at.w, h = at.h, d = look == LOOK_DOWN, nl;
	int k = look * NBUT + i, pic = look == LOOK_GREYED ? TB_GREYED : look == LOOK_ON ? TB_ON : TB_LIT;
	long size = scr_dev_size(w, h), n = (long) TB_W * e->h;
	char *f, *made = NULL;
	SCR_LINE ln[8];
	if (!rect_and(&r, clip))
		return;
	if (tb_dev != NULL && (w != tb_w || h != tb_h)) {	/* (another size of the system font) */
		scr_dev_free(tb_dev);
		tb_dev = NULL;
		tb_made = 0;
	}
	if (tb_dev == NULL && !tb_nodev) {
		if ((tb_dev = scr_dev_alloc(size * LOOKS * NBUT)) == NULL)
			tb_nodev = 1;
		tb_w = w;
		tb_h = h;
		tb_made = 0;
	}
	f = tb_dev != NULL ? tb_dev + size * k : NULL;
	if (f != NULL && (tb_made & (1u << k))) {
		scr_blit_dev(f, w, h, &at, &r);
		return;
	}
	if (tb_raw == NULL && (tb_raw = unpack_all(e->data, e->off[1], n * TB_PICS)) == NULL)
		return;
	if (f == NULL && (f = made = scr_dev_alloc(size)) == NULL)
		return;
	nl = look_frame(look, w, h, ln);
	if (scr_button_dev(w, h, paper(), ln, nl, tb_raw + n * (pic * NBUT + i), e->col, e->ncol, TB_W, e->h,
			   (w - TB_W) / 2 + d, 3 + d, f)) {
		if (made == NULL)
			tb_made |= 1u << k;
		scr_blit_dev(f, w, h, &at, &r);
	}
	scr_dev_free(made);
}

static void draw_button(int i, const GRECT *clip)
{
	short on = enabled(i);
	draw_look(i, !on ? LOOK_GREYED : i == cur->sunk ? LOOK_DOWN : i == cur->hot ? LOOK_HOT : LOOK_ON, clip);
}

static short addr_cols(void)
{
	return (cur->r_addr.w - gl_wchar) / gl_wchar;
}

static void draw_addr(void)
{
	GRECT r = cur->r_addr, in;
	short cols = addr_cols(), n = (short) strlen(cur->addr) - cur->a_first;
	if (r.w <= 0)
		return;
	fill(&r, 0);
	frame(&r, cur->a_focus ? 1 : (planes >= 4 ? 9 : 1));
	if (n > cols)
		n = cols;
	if (cur->a_focus && cur->a_all && cur->addr[0]) {
		in.x = r.x + gl_wchar / 2;
		in.y = r.y + 2;
		in.w = n * gl_wchar;
		in.h = gl_hchar;
		fill(&in, 1);
		text_at(in.x, in.y, cur->addr + cur->a_first, n, cols, TX_LTR | TX_INVERSE);
	} else if (n > 0) {
		text_at(r.x + gl_wchar / 2, r.y + 2, cur->addr + cur->a_first, n, cols, TX_LTR);
	}
	if (cur->a_focus && !cur->a_all) {
		short cx = r.x + gl_wchar / 2 + (cur->a_cur - cur->a_first) * gl_wchar;
		line_col(cx, r.y + 2, cx, r.y + 1 + gl_hchar, 1);
	}
}

/* ---------------------------------------------------------------- the throbber */

static const THR_ED *thr_ed;		/* the edition for this screen, NULL: none */
static char *thr_dev;			/* its frames in the screen's own form (ST RAM), each made when first shown */
static unsigned thr_made;		/* which of them are made, a bit each */
static unsigned char *thr_raw;		/* the frames unpacked, until all are made */
static int thr_nodev;			/* no memory for them: each drawn as it comes */
static int thr_drawn;			/* frames drawn, for the log */
static unsigned long thr_at;		/* when the frame was last moved on (ms) */

static const THR_ED *edition(void)
{
#ifndef EW_ST
	if (planes >= 8)
		return tall ? &thr_256_tall : &thr_256_short;
	if (planes == 4)
		return tall ? &thr_16_tall : &thr_16_short;
#else
	if (planes == 4 && !tall)
		return &thr_16_short;
#endif
	return tall ? &thr_mono_tall : &thr_mono_short;
}

/* the frame shown, within clip. Unpacking a frame and making it the
   screen's took 150 ms on an 8 MHz ST, longer than a frame lasts: each is
   made once, into the screen's own form (16 frames: 3.8 KB in
   monochrome, 30 KB in 256 colours), and copied from there after */
static void draw_throbber(const GRECT *clip)
{
	GRECT r = cur->r_thr;
	const THR_ED *e = thr_ed != NULL ? thr_ed : (thr_ed = edition());
	long n = (long) THR_W * e->h, size = scr_dev_size(THR_W, e->h);
	char *f;
	if (r.w <= 0 || !rect_and(&r, clip))
		return;
	cur->r_thr.h = e->h;
	if (thr_dev == NULL && !thr_nodev && (thr_dev = scr_dev_alloc(size * THR_FRAMES)) == NULL)
		thr_nodev = 1;
	f = thr_dev != NULL ? thr_dev + size * cur->thr_frame : NULL;
	if (f != NULL && (thr_made & (1u << cur->thr_frame))) {
		scr_blit_dev(f, THR_W, e->h, &cur->r_thr, &r);
		return;
	}
	if (thr_raw == NULL && (thr_raw = unpack_all(e->data, e->off[1], n * THR_FRAMES)) == NULL)
		return;
	if (f != NULL && scr_rgb_dev(thr_raw + n * cur->thr_frame, e->col, e->ncol, THR_W, e->h, f)) {
		thr_made |= 1u << cur->thr_frame;
		scr_blit_dev(f, THR_W, e->h, &cur->r_thr, &r);
		if (thr_made == (1u << THR_FRAMES) - 1) {
			free(thr_raw);	/* (unpacked again if they are to be made again) */
			thr_raw = NULL;
		}
	} else {
		scr_rgb(thr_raw + n * cur->thr_frame, e->col, e->ncol, THR_W, e->h, &cur->r_thr, &r);
	}
}

void win_throb(int busy)
{
	GRECT r, a;
	short mx, my, d, hide;
	unsigned long now = pf_ms();
	if (busy != cur->stopper) {
		/* Stop lit while what Esc stops comes, the pictures after the
		   page too (Erez, 27 Oct 2026: it was only the page's); the
		   button alone drawn */
		cur->stopper = busy;
		redraw(&cur->but[B_STOP], 1);
	}
	if (cur->h <= 0 || cur->iconified || cur->r_thr.w <= 0)
		return;
	if (busy ? now - thr_at < 110 : cur->thr_frame == 0)
		return;
	cur->thr_frame = busy ? (cur->thr_frame + 1) % THR_FRAMES : 0;
	thr_at = now;
	thr_drawn++;
	/* the mouse off only when it is near: the pointer doesn't flicker
	   eight times a second */
	graf_mkstate(&mx, &my, &d, &d);
	hide = mx > cur->r_thr.x - 20 && mx < cur->r_thr.x + cur->r_thr.w + 4 && my > cur->r_thr.y - 20 && my < cur->r_thr.y + cur->r_thr.h + 4;
	wind_update(BEG_UPDATE);
	if (hide)
		graf_mouse(M_OFF, 0);
	wind_get(cur->h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		a = cur->r_thr;
		if (rect_and(&a, &r)) {
			clip_on(&a);
			draw_throbber(&a);
		}
		wind_get(cur->h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	clip_off();
	if (hide)
		graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

int win_throb_frames(void)
{
	int n = thr_drawn;
	thr_drawn = 0;
	return n;
}

/* ---------------------------------------------------------------- the status bar */

/* the ribbon: how far a load has come, 0..1024 of the bar's width, in
   parts each the colour of the phase it came in */

/* each phase's pen (connecting, the page, its stylesheets, building it,
   its pictures): the darker colours of the system 16, which read on the
   grey; black where there are only 4 colours or 2 */
static short rib_pen(int phase)
{
	static const short pens[] = { 4, 13, 15, 14, 11 };
	if (planes < 4 || phase < 0 || phase > 4)
		return 1;
	return pens[phase];
}

static short rib_rows(void)
{
	return tall ? 3 : 2;
}

static void draw_ribbon(void)
{
	GRECT r;
	short x0 = cur->r_stat.x + 1, w = cur->r_stat.w - 2, from = 0;
	int i;
	r.y = cur->r_stat.y + cur->r_stat.h - rib_rows();
	r.h = rib_rows();
	for (i = 0; i < cur->nrib; i++) {
		r.x = x0 + (short) ((long) w * from / 1024);
		r.w = (short) ((long) w * cur->rib[i].end / 1024) - (r.x - x0);
		if (r.w > 0)
			fill(&r, cur->rib[i].pen);
		from = cur->rib[i].end;
	}
	r.x = x0 + (short) ((long) w * from / 1024);
	r.w = x0 + w - r.x;
	if (r.w > 0)
		fill(&r, paper());
}

static void draw_status(GRECT *clip)
{
	GRECT r = cur->r_stat;
	short cols = (cur->r_stat.w - 16) / gl_wchar;	/* (8 pixels each side) */
	if (!rect_and(&r, clip))
		return;
	fill(&cur->r_stat, paper());
	hline(cur->r_stat.x, cur->r_stat.x + cur->r_stat.w - 1, cur->r_stat.y);
	if (cur->info[0] && cols > 0)
		text_at(cur->r_stat.x + 8, cur->r_stat.y + 2, cur->info, (long) strlen(cur->info), cols, 0);	/* (on a byte) */
	draw_ribbon();
}

static void redraw_ribbon(void)
{
	GRECT r = cur->r_stat;
	r.y = cur->r_stat.y + cur->r_stat.h - rib_rows();
	r.h = rib_rows();
	redraw(&r, 1);
}

void win_progress(int phase, int at)
{
	if (phase < 0) {
		if (cur->nrib == 0)
			return;
		cur->nrib = 0;
		cur->rib_at = 0;
		cur->rib_full = 0;
	} else {
		if (at > 1024)
			at = 1024;
		if (at <= cur->rib_at && cur->nrib > 0)
			return;		/* it only goes on */
		if (cur->nrib > 0 && cur->rib[cur->nrib - 1].pen == rib_pen(phase))
			cur->rib[cur->nrib - 1].end = (short) at;
		else if (cur->nrib < RIB_PARTS) {
			cur->rib[cur->nrib].pen = rib_pen(phase);
			cur->rib[cur->nrib++].end = (short) at;
		} else {
			cur->rib[cur->nrib - 1].end = (short) at;
		}
		cur->rib_at = (short) at;
		cur->rib_full = at == 1024 ? pf_ms() | 1 : 0;
	}
	if (cur->h > 0 && !cur->iconified)
		redraw_ribbon();
}

int win_progress_shown(void)
{
	ewin *was = cur;
	int shown = 0;
	/* full for a moment, then gone */
	for (cur = wins; cur != NULL; cur = cur->next) {
		if (cur->rib_full && pf_ms() - cur->rib_full >= 800)
			win_progress(-1, 0);
		shown |= cur->nrib > 0;
	}
	cur = was;
	return shown;
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = cur->r_tool;
	int i;
	if (!rect_and(&r, clip))
		return;
	{
		/* the ground around the buttons (each button draws its own:
		   nothing under one is drawn twice) */
		GRECT g = cur->r_tool, *b0 = &cur->but[0], *bn = &cur->but[NBUT - 1];
		g.h = b0->y - cur->r_tool.y;
		fill(&g, paper());
		g.y = b0->y + b0->h;
		g.h = cur->r_tool.y + cur->r_tool.h - g.y;
		fill(&g, paper());
		g.y = b0->y;
		g.h = b0->h;
		g.w = b0->x - cur->r_tool.x;
		fill(&g, paper());
		for (i = 0; i + 1 < NBUT; i++) {
			g.x = cur->but[i].x + cur->but[i].w;
			g.w = cur->but[i + 1].x - g.x;
			fill(&g, paper());
		}
		g.x = bn->x + bn->w;
		g.w = cur->r_tool.x + cur->r_tool.w - g.x;
		fill(&g, paper());
	}
	for (i = 0; i < NBUT; i++)
		draw_button(i, &r);
	draw_addr();
	draw_throbber(&r);
	hline(cur->r_tool.x, cur->r_tool.x + cur->r_tool.w - 1, cur->r_tool.y + cur->r_tool.h - 1);
}

static void draw_bars(GRECT *clip)
{
	draw_toolbar(clip);
	draw_status(clip);
}

/* ---------------------------------------------------------------- drawing */

/* the page in the window: the one shown, or while one loads its preview
   (only drawn and scrolled: what the mouse and keys do waits for the page) */
static const page *shown(void)
{
	return br.pv_p != NULL ? br.pv_p : br.p;
}

/* a box's scroll bar (overflow auto or scroll) as panebar's (the AES's
   objects: the guidelines' 5.1), in pixels, and where it is seen on the
   screen (the boxes hiding what overflows them that it is in, the page's
   area): 0 if nowhere */
static int box_panebar(const page *p, const dl_item *d, PANEBAR *b, GRECT *seen)
{
	const struct pg_scroll *sc = &p->scr[d->len - 1];
	int32_t x0 = d->x, y0 = d->y, x1 = d->x + d->w, y1 = d->y + d->h;
	int32_t shown = d->base ? p->items[sc->clip].w : p->items[sc->clip].h;
	b->r.x = (short) (cur->r_page.x + d->x - cur->left);
	b->r.y = (short) (cur->r_page.y + d->y - cur->top);
	b->r.w = (short) d->w;
	b->r.h = (short) d->h;
	b->shown = shown;
	b->total = shown + (d->base ? sc->mx : sc->my);
	b->top = d->base ? sc->sx : sc->sy;
	b->across = d->base;
	if (!page_clip(p, d->clip, &x0, &y0, &x1, &y1))
		return 0;
	seen->x = (short) (cur->r_page.x + x0 - cur->left);
	seen->y = (short) (cur->r_page.y + y0 - cur->top);
	seen->w = (short) (x1 - x0);
	seen->h = (short) (y1 - y0);
	return rect_and(seen, &cur->r_page);
}

/* the boxes' bars over the page's copy, within clip, and the square
   where a box's two meet */
static void draw_box_bars(const page *p, const GRECT *clip)
{
	int k, a;
	if (p == NULL)
		return;
	for (k = 0; k < p->nscr; k++) {
		const struct pg_scroll *sc = &p->scr[k];
		PANEBAR b[2];
		GRECT s;
		for (a = 0; a < 2; a++)
			if (sc->bar[a] >= 0 && box_panebar(p, &p->items[sc->bar[a]], &b[a], &s) && rect_and(&s, clip))
				pb_draw(&b[a], &s);
		if (sc->bar[0] >= 0 && sc->bar[1] >= 0) {
			const dl_item *dn = &p->items[sc->bar[0]], *ac = &p->items[sc->bar[1]];
			int32_t x0 = dn->x, y0 = ac->y, x1 = dn->x + dn->w, y1 = ac->y + ac->h;
			GRECT r;
			r.x = (short) (cur->r_page.x + x0 - cur->left);
			r.y = (short) (cur->r_page.y + y0 - cur->top);
			r.w = (short) (x1 - x0);
			r.h = (short) (y1 - y0);
			if (page_clip(p, dn->clip, &x0, &y0, &x1, &y1)) {
				s.x = (short) (cur->r_page.x + x0 - cur->left);
				s.y = (short) (cur->r_page.y + y0 - cur->top);
				s.w = (short) (x1 - x0);
				s.h = (short) (y1 - y0);
				if (rect_and(&s, &cur->r_page) && rect_and(&s, clip))
					pb_corner(&r, &s);
			}
		}
	}
}

static void draw_page(GRECT *clip)
{
	GRECT r = cur->r_page;
	if (!rect_and(&r, clip))
		return;
	if (shown() == NULL) {
		fill(&r, 0);	/* no page yet: white, at once (not made in memory a band at a time) */
		return;
	}
	scr_page(shown(), &r, cur->left + (r.x - cur->r_page.x), cur->top + (r.y - cur->r_page.y));
	draw_box_bars(shown(), &r);
}

/* area of the window, through its rectangle list */
static void redraw(const GRECT *area, int parts)
{
	GRECT r, a;
	if (cur->h <= 0)
		return;
	if (cur->iconified) {
		icon_window(area);
		return;
	}
	a = area ? *area : cur->work;
	if (!rect_and(&a, &cur->work))
		return;
	gap_tag(parts & 2 ? "draw" : "draw bars");
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	font_sys_push();	/* (the system font at its size: a dialog may have left another) */
	wind_get(cur->h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		GRECT c = r;
		if (rect_and(&c, &a)) {
			clip_on(&c);
			if (parts & 1)
				draw_bars(&c);
			if (parts & 2)
				draw_page(&c);
		}
		wind_get(cur->h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	clip_off();
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

void win_redraw(const GRECT *area)
{
	redraw(area, 3);
}

void win_redraw_page(void)
{
	redraw(&cur->r_page, 2);
}

void win_toolbar(void)
{
	redraw(&cur->r_tool, 1);
}

static void redraw_addr(void)
{
	redraw(&cur->r_addr, 1);
}

/* ---------------------------------------------------------------- scrolling */

int32_t win_page_w(void)
{
	return cur->r_page.w > 0 ? cur->r_page.w : 1;
}

int32_t win_page_h(void)
{
	return cur->r_page.h;
}

int32_t win_top(void)
{
	return cur->top;
}

void win_page_area(short *x, short *y, short *w, short *h)
{
	*x = cur->r_page.x;
	*y = cur->r_page.y;
	*w = cur->r_page.w;
	*h = cur->r_page.h;
}

static const page *shown(void);

static int32_t max_top(void)
{
	int32_t h = shown() ? shown()->height : 0, m = h - cur->r_page.h;
	return m > 0 ? m : 0;
}

/* how far it scrolls sideways */
static int32_t max_left(void)
{
	int32_t w = shown() ? shown()->right : 0, m = w - cur->r_page.w;
	return m > 0 ? m : 0;
}

static void sliders(void)
{
	const page *p = shown();
	int32_t h = p ? p->height : 0, m = max_top(), w = p ? p->right : 0, mw = max_left();
	short size = (short) (h > cur->r_page.h ? (int32_t) ((int64_t) 1000 * cur->r_page.h / h) : 1000);
	short wsize = (short) (w > cur->r_page.w ? (int32_t) ((int64_t) 1000 * cur->r_page.w / w) : 1000);
	if (cur->h <= 0 || cur->iconified)
		return;
	wind_set(cur->h, WF_VSLSIZE, size < 30 ? 30 : size, 0, 0, 0);
	wind_set(cur->h, WF_VSLIDE, (short) (m > 0 ? (int32_t) ((int64_t) 1000 * cur->top / m) : 0), 0, 0, 0);
	wind_set(cur->h, WF_HSLSIZE, wsize < 30 ? 30 : wsize, 0, 0, 0);
	wind_set(cur->h, WF_HSLIDE, (short) (mw > 0 ? (int32_t) ((int64_t) 1000 * cur->left / mw) : 0), 0, 0, 0);
}

/* the window is in front and nothing covers it: one rectangle */
static int all_visible(void)
{
	GRECT r;
	short t, d;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (t != cur->h)
		return 0;
	wind_get(cur->h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	return r.x == cur->work.x && r.y == cur->work.y && r.w == cur->work.w && r.h == cur->work.h;
}

void win_scroll_to(int32_t y)
{
	int32_t m = max_top(), dy;
	if (y > m)
		y = m;
	if (y < 0)
		y = 0;
	dy = y - cur->top;
	if (dy == 0)
		return;
	cur->top = y;
	if (br.pv_p == NULL)
		br_scrolled(&br, cur->top);	/* (a preview's place isn't the page's) */
	sliders();
	if (cur->iconified || cur->h <= 0)
		return;
	if ((dy < 0 ? -dy : dy) < cur->r_page.h && all_visible() &&
	    cur->r_page.y + cur->r_page.h <= scr_h && cur->r_page.x + cur->r_page.w <= scr_w) {
		/* what stays on the screen moves; only the new rows are drawn */
		MFDB s;
		short pxy[8], d = (short) (dy < 0 ? -dy : dy);
		GRECT fresh = cur->r_page;
		memset(&s, 0, sizeof s);
		wind_update(BEG_UPDATE);
		graf_mouse(M_OFF, 0);
		clip_on(&cur->r_page);
		pxy[0] = pxy[4] = cur->r_page.x;
		pxy[2] = pxy[6] = cur->r_page.x + cur->r_page.w - 1;
		if (dy > 0) {
			pxy[1] = cur->r_page.y + d;
			pxy[3] = cur->r_page.y + cur->r_page.h - 1;
			pxy[5] = cur->r_page.y;
			pxy[7] = cur->r_page.y + cur->r_page.h - 1 - d;
			fresh.y = cur->r_page.y + cur->r_page.h - d;
		} else {
			pxy[1] = cur->r_page.y;
			pxy[3] = cur->r_page.y + cur->r_page.h - 1 - d;
			pxy[5] = cur->r_page.y + d;
			pxy[7] = cur->r_page.y + cur->r_page.h - 1;
		}
		fresh.h = d;
		vro_cpyfm(vdi_h, 3, pxy, &s, &s);
		draw_page(&fresh);
		clip_off();
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	win_redraw_page();
}

void win_scroll_by(int32_t dy)
{
	win_scroll_to(cur->top + dy);
}

/* sideways, to column x: what stays on the screen moves, as up and down */
static void scroll_x_to(int32_t x)
{
	int32_t m = max_left(), dx;
	if (x > m)
		x = m;
	if (x < 0)
		x = 0;
	dx = x - cur->left;
	if (dx == 0)
		return;
	cur->left = x;
	sliders();
	log_event("sideways %ld of %ld", (long) cur->left, (long) m);
	if (cur->iconified || cur->h <= 0)
		return;
	if ((dx < 0 ? -dx : dx) < cur->r_page.w && all_visible() &&
	    cur->r_page.y + cur->r_page.h <= scr_h && cur->r_page.x + cur->r_page.w <= scr_w) {
		MFDB s;
		short pxy[8], d = (short) (dx < 0 ? -dx : dx);
		GRECT fresh = cur->r_page;
		memset(&s, 0, sizeof s);
		wind_update(BEG_UPDATE);
		graf_mouse(M_OFF, 0);
		clip_on(&cur->r_page);
		pxy[1] = pxy[5] = cur->r_page.y;
		pxy[3] = pxy[7] = cur->r_page.y + cur->r_page.h - 1;
		if (dx > 0) {
			pxy[0] = cur->r_page.x + d;
			pxy[2] = cur->r_page.x + cur->r_page.w - 1;
			pxy[4] = cur->r_page.x;
			pxy[6] = cur->r_page.x + cur->r_page.w - 1 - d;
			fresh.x = cur->r_page.x + cur->r_page.w - d;
		} else {
			pxy[0] = cur->r_page.x;
			pxy[2] = cur->r_page.x + cur->r_page.w - 1 - d;
			pxy[4] = cur->r_page.x + d;
			pxy[6] = cur->r_page.x + cur->r_page.w - 1;
		}
		fresh.w = d;
		vro_cpyfm(vdi_h, 3, pxy, &s, &s);
		draw_page(&fresh);
		clip_off();
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	win_redraw_page();
}

static int32_t col_step(void)
{
	return gl_wchar * 4;
}

/* a picture came: the part of the page from y0 to y1 drawn again, where
   it is in the window */
void win_page_drawn(int32_t y0, int32_t y1)
{
	GRECT r = cur->r_page;
	int32_t a = y0 - cur->top, b = y1 - cur->top;
	if (cur->h <= 0 || cur->iconified || b <= 0 || a >= cur->r_page.h)
		return;
	if (a < 0)
		a = 0;
	if (b > cur->r_page.h)
		b = cur->r_page.h;
	r.y = (short) (cur->r_page.y + a);
	r.h = (short) (b - a);
	redraw(&r, 2);
}

/* the page laid out again (its pictures' sizes came): the same place on
   it, all drawn */
void win_page_relaid(void)
{
	win_page_relaid_at(cur->top);
}

/* the page laid out again where it is (another text size): its place
   kept, within its new height and width; drawn by the caller */
void win_page_clamp(void)
{
	if (cur->top > max_top())
		cur->top = max_top();
	if (cur->left > max_left())
		cur->left = max_left();
	br_scrolled(&br, cur->top);
	sliders();
}

void win_page_relaid_at(int32_t y)
{
	cur->top = y;
	if (cur->top > max_top())
		cur->top = max_top();
	if (cur->top < 0)
		cur->top = 0;
	if (cur->left > max_left())
		cur->left = max_left();
	br_scrolled(&br, cur->top);
	sliders();
	win_redraw_page();
}

static int32_t line_step(void)
{
	return gl_hchar * 3;
}

/* ---------------------------------------------------------------- titles and status */

void win_title(void)
{
	char t[sizeof cur->title], a[140];
	if (br.title[0]) {
		atari_text(br.title, a, sizeof a);
		snprintf(t, sizeof t, " EWeb - %s ", a);
	} else {
		snprintf(t, sizeof t, " EWeb ");
	}
	if (strcmp(t, cur->title) == 0)
		return;
	str_copy(cur->title, t, sizeof cur->title);
	if (cur->h > 0)
		wind_set_str(cur->h, WF_NAME, cur->title);
}


void win_status(const char *s)
{
	char t[sizeof cur->info];
	cur->at_rest = !*s;
	if (*s)
		snprintf(t, sizeof t, " %s", s);
	else if (br_refresh_note(&br, t + 1, (int) sizeof t - 1))
		t[0] = ' ';	/* "Going on to example.com in 5 s: Esc stays here." */
	else if (br.security[0])
		snprintf(t, sizeof t, " Secure: %s", br.security);
	else
		t[0] = 0;
	if (strcmp(t, cur->info) == 0)
		return;
	str_copy(cur->info, t, sizeof cur->info);
	/* what the bar says now, for the rigs (a link's address without its
	   query, 7.3) */
	log_event("status %.*s", (int) strcspn(t + (t[0] == ' '), "?"), t + (t[0] == ' '));
	if (cur->h > 0 && !cur->iconified) {
		GRECT r = cur->r_stat;
		r.h -= rib_rows();
		redraw(&r, 1);
	}
}

void win_status_again(void)
{
	if (cur->at_rest)
		win_status("");
}

/* ---------------------------------------------------------------- the address field */

static void addr_show_cursor(void)
{
	short cols = addr_cols(), n = (short) strlen(cur->addr);
	if (cur->a_cur > n)
		cur->a_cur = n;
	if (cur->a_cur < cur->a_first)
		cur->a_first = cur->a_cur;
	if (cur->a_cur > cur->a_first + cols - 1)
		cur->a_first = cur->a_cur - cols + 1;
	if (cur->a_first < 0)
		cur->a_first = 0;
}

void win_address(const char *url)
{
	char a[sizeof cur->addr];
	atari_text(url, a, sizeof a);
	str_copy(cur->addr, a, sizeof cur->addr);
	cur->a_cur = (int) strlen(cur->addr);
	cur->a_first = 0;
	cur->a_all = 0;
	cur->a_focus = 0;
	if (cur->h > 0)
		redraw_addr();
}

static void ctl_blur(void);

void win_address_focus(int all)
{
	ctl_blur();
	cur->a_focus = 1;
	cur->a_all = all;
	cur->a_cur = (int) strlen(cur->addr);
	addr_show_cursor();
	if (all)
		cur->a_first = 0;
	redraw_addr();
}

int win_address_has_focus(void)
{
	return cur->a_focus;
}

static void addr_blur(void)
{
	if (!cur->a_focus)
		return;
	cur->a_focus = 0;
	cur->a_all = 0;
	cur->a_first = 0;
	redraw_addr();
}

static void addr_insert(const char *s, long n)
{
	long len = (long) strlen(cur->addr);
	if (cur->a_all) {
		cur->addr[0] = 0;
		len = 0;
		cur->a_cur = 0;
		cur->a_all = 0;
	}
	if (len + n >= (long) sizeof cur->addr)
		n = (long) sizeof cur->addr - 1 - len;
	if (n <= 0)
		return;
	memmove(cur->addr + cur->a_cur + n, cur->addr + cur->a_cur, len - cur->a_cur + 1);
	memcpy(cur->addr + cur->a_cur, s, n);
	cur->a_cur += (int) n;
}

/* a key for the address field: 1 when taken */
static int addr_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char c = KEY_ASCII(key);
	int len = (int) strlen(cur->addr), shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	if (key_ctrl(kstate, key)) {
		switch (scan) {
		case 0x1e:		/* ^A: all of it */
			cur->a_all = 1;
			break;
		case 0x2e:		/* ^C */
			win_clip(CLIP_COPY);
			return 1;
		case 0x2d:		/* ^X */
			win_clip(CLIP_CUT);
			return 1;
		case 0x2f:		/* ^V */
			win_clip(CLIP_PASTE);
			return 1;
		default:
			return 0;
		}
	} else if (scan == 0x1c || scan == 0x72) {	/* Return, Enter */
		char t[sizeof cur->addr];
		long n;
		char *u = atari_to_utf8(cur->addr, (long) strlen(cur->addr), &n);
		str_copy(t, u ? u : cur->addr, sizeof t);
		free(u);
		addr_blur();
		go(t);
		return 1;
	} else if (scan == 0x0f) {			/* Tab: the keyboard to the page */
		addr_blur();
		return 1;
	} else if (scan == 0x01) {			/* Esc: the page's address back, as every browser's (3.5) */
		win_address(br.url);
		win_address_focus(1);
		return 1;
	} else if (scan == 0x4b) {			/* Left */
		if (cur->a_all)
			cur->a_cur = 0;
		else if (cur->a_cur > 0)
			cur->a_cur--;
		cur->a_all = 0;
	} else if (scan == 0x4d) {			/* Right */
		if (cur->a_all)
			cur->a_cur = len;
		else if (cur->a_cur < len)
			cur->a_cur++;
		cur->a_all = 0;
	} else if (scan == 0x47) {			/* ClrHome: the start, with Shift the end */
		cur->a_cur = shift ? len : 0;
		cur->a_all = 0;
	} else if (scan == 0x0e) {			/* Backspace */
		if (cur->a_all) {
			cur->addr[0] = 0;
			cur->a_cur = 0;
			cur->a_all = 0;
		} else if (cur->a_cur > 0) {
			memmove(cur->addr + cur->a_cur - 1, cur->addr + cur->a_cur, len - cur->a_cur + 1);
			cur->a_cur--;
		}
	} else if (scan == 0x53) {			/* Delete */
		if (cur->a_all) {
			cur->addr[0] = 0;
			cur->a_cur = 0;
			cur->a_all = 0;
		} else if (cur->a_cur < len) {
			memmove(cur->addr + cur->a_cur, cur->addr + cur->a_cur + 1, len - cur->a_cur);
		}
	} else if (c >= 0x20 && c != 0x7f) {
		char s = (char) c;
		addr_insert(&s, 1);
	} else {
		return 0;
	}
	addr_show_cursor();
	redraw_addr();
	return 1;
}

/* ---------------------------------------------------------------- the page's forms */


/* a control's place on the screen, its frame too */
static int ctl_rect(const fctl *c, GRECT *r)
{
	const dl_item *it;
	if (br.p == NULL || (it = page_ctl_item(br.p, c)) == NULL)
		return 0;
	r->x = (short) (cur->r_page.x + it->x - cur->left - 1);
	r->y = (short) (cur->r_page.y + it->y - cur->top - 1);
	r->w = (short) (it->w + 2);
	r->h = (short) (it->h + 2);
	return 1;
}

static void ctl_redraw(const fctl *c)
{
	GRECT r;
	if (ctl_rect(c, &r) && rect_and(&r, &cur->r_page))
		redraw(&r, 2);
}

/* the window scrolled so that the control is in it */
static void ctl_show(const fctl *c)
{
	const dl_item *it;
	if (br.p == NULL || (it = page_ctl_item(br.p, c)) == NULL)
		return;
	if (page_reveal(br.p, c->n, it->y, it->h, 0))
		win_redraw_page();	/* (a box that scrolls it is in brought it into view) */
	if (it->y < cur->top)
		win_scroll_to(it->y - 8);
	else if (it->y + it->h > cur->top + cur->r_page.h)
		win_scroll_to(it->y + it->h - cur->r_page.h + 8);
	if (it->x < cur->left)
		scroll_x_to(it->x - 8);
	else if (it->x + it->w > cur->left + cur->r_page.w)
		scroll_x_to(it->x + it->w - cur->r_page.w + 8);
}


/* is c still one of the page's controls (a script may have taken it away) */
static int ctl_alive(const fctl *c)
{
	const fctl *k;
	const doc *d;
	int i;
	for (i = 0; (d = br_doc(&br, i)) != NULL; i++)	/* (the page's, its frames') */
		for (k = d->ctls; k != NULL; k = k->next)
			if (k == c)
				return 1;
	return 0;
}

void win_ctl_check(void)
{
	if (cur->f_focus != NULL && !ctl_alive(cur->f_focus)) {
		cur->f_focus = NULL;
		cur->typed = 0;
	}
}

/* a box ticked or an option chosen: input, then change */
static void ctl_events(fctl *c)
{
	ctl_event(c, "input");
	if (ctl_alive(c))
		ctl_event(c, "change");
}

static void ctl_blur(void)
{
	fctl *c = cur->f_focus;
	if (c == NULL)
		return;
	c->focus = 0;
	cur->f_focus = NULL;
	ctl_redraw(c);
	if (cur->typed) {
		cur->typed = 0;
		ctl_event(c, "change");
	}
}

static void addr_blur(void);

static void ctl_focus(fctl *c)
{
	if (cur->f_focus == c)
		return;
	ctl_blur();
	addr_blur();
	cur->f_focus = c;
	cur->typed = 0;
	c->focus = 1;
	ctl_show(c);
	ctl_redraw(c);
}

/* a new page: nothing of it has the keyboard (the page before may be
   gone; a page come back from Back may have been left with it) */
static void ctl_forget(void)
{
	fctl *c;
	const doc *d;
	int i;
	cur->f_focus = NULL;
	for (i = 0; (d = br_doc(&br, i)) != NULL; i++)
		for (c = d->ctls; c != NULL; c = c->next)
			c->focus = 0;
}

/* a form sent: the page it goes to replaces this one */
static void ctl_submit(fctl *from, fctl *by, int32_t x, int32_t y)
{
	if (cur->f_focus != NULL)
		cur->f_focus->focus = 0;
	cur->f_focus = NULL;
	submit(from, by, x, y);
}

/* a list's options in a pop-up, a screenful at a time */
static void choose(fctl *c, short mx, short my)
{
	short rows = (short) ((desk_h - 8) / (ch + 2)) - 2, n, k, r;
	int first = 0, i;
	char (*lab)[64];
	const char *items[64];
	if (rows > 60)
		rows = 60;
	if (rows < 4)
		rows = 4;
	if (c->nopt == 0 || (lab = malloc(64 * sizeof *lab)) == NULL)
		return;
	for (i = 0; i < c->nopt; i++)
		if (c->sel[i]) {
			first = i - rows / 2;
			break;
		}
	for (;;) {
		if (first > c->nopt - rows)
			first = c->nopt - rows;
		if (first < 0)
			first = 0;
		n = 0;
		if (first > 0) {
			items[n++] = "  \x01 Earlier";
		}
		for (i = first; i < c->nopt && n < rows + (first > 0); i++) {
			char t[200], a[60];
			form_option_text(c->opt[i], t, sizeof t);
			atari_text(t, a, sizeof a);
			snprintf(lab[n], sizeof lab[n], "%s%s%s", dn_attr(c->opt[i], "disabled") ? "~" : "",
				 c->sel[i] ? "\x08 " : "  ", a[0] ? a : " ");
			items[n] = lab[n];
			n++;
		}
		k = n;
		if (i < c->nopt)
			items[n++] = "  \x02 Later";
		r = popup(mx, my, items, n);
		if (r < 0)
			break;
		if (first > 0 && r == 0) {
			first -= rows;
			continue;
		}
		if (r == k) {
			first = i;
			continue;
		}
		form_choose(c, first + r - (first > 0));
		break;
	}
	free(lab);
	ctl_redraw(c);
}

/* a button, a box or a list clicked (or Space or Return on it) */
static void ctl_press(fctl *c, const dl_item *it, short mx, short my)
{
	if (c->disabled)
		return;
	switch (c->kind) {
	case FC_CHECKBOX:
		form_check(br_ctl_doc(&br, c), c);
		ctl_redraw(c);
		ctl_events(c);
		break;
	case FC_RADIO:
		form_check(br_ctl_doc(&br, c), c);
		win_redraw_page();	/* the rest of its group */
		ctl_events(c);
		break;
	case FC_SELECT:
		choose(c, mx, my);
		ctl_events(c);
		break;
	case FC_SUBMIT:
	case FC_IMAGE: {
		/* where on a picture, in its own pixels */
		int32_t x = 0, y = 0;
		if (c->kind == FC_IMAGE && it != NULL && br.zoom > 0) {
			x = (int32_t) (mx - cur->r_page.x + cur->left - it->x) * 1024 / br.zoom;
			y = (int32_t) (my - cur->r_page.y - (it->y - cur->top)) * 1024 / br.zoom;
			if (x < 0)
				x = 0;
			if (y < 0)
				y = 0;
		}
		ctl_submit(NULL, c, x, y);
		break;
	}
	case FC_RESET:
		form_reset(br_ctl_doc(&br, c), c->form);
		win_redraw_page();
		break;
	case FC_FILE:
		win_status("EWeb doesn't send files yet.");
		break;
	}
}

/* the text of the clipboard's first line (all of it into a textarea) */
static void ctl_paste(fctl *c)
{
	char *buf = malloc(4096), *u;
	long n;
	if (buf == NULL)
		return;
	n = clip_get(buf, 4096);
	if (n > 0) {
		if (c->kind != FC_TEXTAREA)
			clip_line(buf);
		u = atari_to_utf8(buf, (long) strlen(buf), &n);
		if (u != NULL) {
			form_insert(c, u, n);
			free(u);
		}
	}
	free(buf);
}

/* a key for the control with the keyboard: 1 when taken */
static int ctl_key(short kstate, short key)
{
	fctl *c = cur->f_focus;
	const dl_item *it = br.p ? page_ctl_item(br.p, c) : NULL;
	short scan = KEY_SCAN(key);
	unsigned char ch = KEY_ASCII(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0, typing = form_typed(c), changed = 0;

	if (it == NULL) {		/* no longer shown */
		ctl_blur();
		return 0;
	}
	if (key_ctrl(kstate, key)) {
		switch (scan) {
		case 0x2e: win_clip(CLIP_COPY); return 1;	/* ^C */
		case 0x2d: win_clip(CLIP_CUT); return 1;	/* ^X */
		case 0x2f: win_clip(CLIP_PASTE); return 1;	/* ^V */
		case 0x1e: return 1;				/* ^A: nothing to choose yet */
		}
		return 0;
	}
	switch (scan) {
	case 0x0f: {		/* Tab: the next control, with Shift the one before */
		fctl *n = c;
		do
			n = form_next(br_ctl_doc(&br, c), n, shift ? -1 : 1);	/* (in its own document) */
		while (n != NULL && page_ctl_item(br.p, n) == NULL);
		if (n != NULL) {
			ctl_focus(n);
		} else {
			ctl_blur();
			win_address_focus(1);
		}
		return 1;
	}
	case 0x01:		/* Esc: the keyboard back to the page */
		ctl_blur();
		return 1;
	case 0x1c:
	case 0x72:		/* Return, Enter */
		if (c->kind == FC_TEXTAREA) {
			changed = form_insert(c, "\n", 1);
			break;
		}
		if (typing || c->kind == FC_CHECKBOX || c->kind == FC_RADIO) {
			/* the form sent, as by its first button */
			int send;
			fctl *by = form_default(br_ctl_doc(&br, c), c, &send);
			if (send)
				ctl_submit(c, by, 0, 0);
			return 1;
		}
		ctl_press(c, it, (short) (cur->r_page.x + it->x - cur->left + 4), (short) (cur->r_page.y + it->y - cur->top + it->h));
		return 1;
	case 0x39:		/* Space: a button pressed, a box ticked */
		if (!typing) {
			ctl_press(c, it, (short) (cur->r_page.x + it->x - cur->left + 4), (short) (cur->r_page.y + it->y - cur->top + it->h));
			return 1;
		}
		break;
	}
	if (!typing)
		return 0;
	switch (scan) {
	case 0x4b:		/* Left */
		if (c->cur > 0) {
			c->cur--;
			while (c->cur > 0 && ((unsigned char) c->v[c->cur] & 0xc0) == 0x80)
				c->cur--;
		}
		changed = 1;
		break;
	case 0x4d:		/* Right */
		if (c->cur < c->len) {
			c->cur++;
			while (c->cur < c->len && ((unsigned char) c->v[c->cur] & 0xc0) == 0x80)
				c->cur++;
		}
		changed = 1;
		break;
	case 0x47:		/* ClrHome: the line's start, with Shift its end */
		c->cur = cv_ctl_line(it, c, shift ? CV_LINE_END : CV_LINE_START);
		changed = 1;
		break;
	case 0x48:		/* Up, Down: a textarea's lines */
	case 0x50:
		if (c->kind != FC_TEXTAREA)
			return 0;
		c->cur = cv_ctl_line(it, c, scan == 0x48 ? CV_UP : CV_DOWN);
		changed = 1;
		break;
	case 0x0e:		/* Backspace */
		changed = form_backspace(c);
		break;
	case 0x53:		/* Delete */
		changed = form_delete(c);
		break;
	default:
		if (ch >= 0x20 && ch != 0x7f) {
			/* the Atari's letter (Hebrew too) as UTF-8 */
			unsigned long u = atari_to_uni(ch);
			char b[4];
			int n;
			if (u < 0x80) {
				b[0] = (char) u;
				n = 1;
			} else if (u < 0x800) {
				b[0] = (char) (0xc0 | (u >> 6));
				b[1] = (char) (0x80 | (u & 0x3f));
				n = 2;
			} else {
				b[0] = (char) (0xe0 | (u >> 12));
				b[1] = (char) (0x80 | ((u >> 6) & 0x3f));
				b[2] = (char) (0x80 | (u & 0x3f));
				n = 3;
			}
			changed = form_insert(c, b, n);
			break;
		}
		return 0;
	}
	if (changed) {
		ctl_redraw(c);
		cur->typed = 1;
		ctl_event(c, "input");
	}
	return 1;
}

/* ---------------------------------------------------------------- the clipboard */

static int sel_now(void);

/* the selection's text, else the page's (page_copy_text), in the Atari's
   characters */
static char *page_text(long *n)
{
	pg_range r;
	uint32_t rgb;
	long k;
	char *u, *a;
	if (br.p == NULL)
		return NULL;
	u = page_copy_text(br.p, win_mark(&r, &rgb) ? &r : NULL, &k);
	if (u == NULL)
		return NULL;
	a = cs_to_atari(u, k, CS_UTF8, n);
	free(u);
	return a;
}

void win_clip(int what)
{
	if (cur->f_focus != NULL && form_typed(cur->f_focus)) {
		fctl *c = cur->f_focus;
		if (what == CLIP_COPY || what == CLIP_CUT) {
			long n;
			char *a;
			if (c->kind == FC_PASSWORD)
				return;		/* a password stays where it is */
			a = cs_to_atari(c->v, c->len, CS_UTF8, &n);
			if (a != NULL) {
				clip_put(a, n);
				free(a);
			}
			if (what == CLIP_CUT)
				form_set(c, "", 0);
		} else if (what == CLIP_PASTE) {
			ctl_paste(c);
		}
		ctl_redraw(c);
		return;
	}
	if (cur->a_focus) {
		long len = (long) strlen(cur->addr);
		if (what == CLIP_COPY || what == CLIP_CUT) {
			clip_put(cur->addr, len);
			if (what == CLIP_CUT) {
				cur->addr[0] = 0;
				cur->a_cur = 0;
				cur->a_all = 0;
				redraw_addr();
			}
		} else if (what == CLIP_PASTE) {
			char buf[sizeof cur->addr];
			long n = clip_get(buf, sizeof buf);
			if (n > 0) {
				clip_line(buf);	/* the first line */
				addr_insert(buf, (long) strlen(buf));
				addr_show_cursor();
				redraw_addr();
			}
		}
		return;
	}
	if (what == CLIP_COPY) {
		long n;
		char *t = page_text(&n);
		if (t != NULL) {
			if (clip_put(t, n))
				status(sel_now() ? "The text selected is on the clipboard." : "The page's text is on the clipboard.");
			log_event("copied %ld bytes", n);
			free(t);
		}
	}
}

/* ---------------------------------------------------------------- find in page */

/* the text selected (dragged over, a word double-clicked, all of it, the
   words found): its two ends, where it was begun and where it goes to,
   kept as places in the page's text by the characters before them
   (page_text_index), so that it is the same text when the page has been
   laid out again (a picture came, the window was sized); on the page
   shown when it was made only */

#define SEL_RGB 0x99ccff		/* the selection's colour (Edit > Find's: yellow) */

static void sel_set(const pg_pos *from, const pg_pos *to, int found)
{
	cur->sel.at[0] = *from;
	cur->sel.at[1] = *to;
	cur->sel.on = from->item != to->item || from->off != to->off;
	cur->sel.found = found;
	cur->sel.ix[0] = cur->sel.ix[1] = -1;
	cur->sel.shows = br.shows;
	cur->sel.serial = br.p->serial;
	page_range(br.p, from, to, &cur->sel.r);
}

/* its ends as places in the text, once it is made */
static void sel_keep(void)
{
	if (cur->sel.on && cur->sel.ix[0] < 0) {
		cur->sel.ix[0] = page_text_index(br.p, &cur->sel.at[0]);
		cur->sel.ix[1] = page_text_index(br.p, &cur->sel.at[1]);
	}
}

/* the selection on the page shown, in its layout now: 0 none */
static int sel_now(void)
{
	if (!cur->sel.on || br.p == NULL || br.shows != cur->sel.shows)
		return 0;
	if (br.p->serial != cur->sel.serial) {
		int back = cur->sel.ix[0] > cur->sel.ix[1];
		if (cur->sel.ix[0] < 0 || !page_text_place(br.p, cur->sel.ix[0], back, &cur->sel.at[0]) ||
		    !page_text_place(br.p, cur->sel.ix[1], !back, &cur->sel.at[1])) {
			cur->sel.on = 0;
			return 0;
		}
		cur->sel.serial = br.p->serial;
		page_range(br.p, &cur->sel.at[0], &cur->sel.at[1], &cur->sel.r);
	}
	return 1;
}

/* the selection gone (a click on the page) */
static void sel_clear(void)
{
	if (sel_now()) {
		cur->sel.on = 0;
		win_redraw_page();
	}
	cur->sel.on = 0;
}

/* the selection marked on the screen (screen.c): 1, the stretch and its
   colour */
int win_mark(pg_range *r, uint32_t *rgb)
{
	if (br.pv_p != NULL || !sel_now())	/* (a preview: the selection is the page's) */
		return 0;
	*r = cur->sel.r;
	*rgb = cur->sel.found ? 0xffff00 : SEL_RGB;
	return 1;
}

/* Edit > Select all, on the page: all its text */
void win_select_all(void)
{
	pg_pos a, b;
	long n;
	if (br.p == NULL)
		return;
	ctl_blur();		/* (^C then copies it, not a field's text; first: its
				   change event may lay the page out again) */
	if (br.p == NULL || !page_text_place(br.p, 0, 0, &a) || !page_text_place(br.p, LONG_MAX, 1, &b))
		return;
	sel_set(&a, &b, 0);
	sel_keep();
	n = cur->sel.ix[1] - cur->sel.ix[0];
	log_event("selected all: %ld", n);
	win_redraw_page();
}

static char f_words[256];		/* Edit > Find's words (UTF-8) */

/* Edit > Find (how 0: the words asked for, then the first from the
   window's top), Find next (1), Find previous (-1): marked, and the
   window (and a box that scrolls, if it is in one) brought to them */
void win_find(int how)
{
	pg_pos from, a, b;
	long n, nth, i;
	const dl_item *it;
	char line[80];
	if (br.p == NULL)
		return;
	if ((how == 0 || f_words[0] == 0) && (!dlg_find(f_words, sizeof f_words) || f_words[0] == 0))
		return;
	if (how != 0 && sel_now()) {
		/* on from the selection (the words found last): the next after
		   its start, or the one before it */
		from = cur->sel.r.a;
		if (how > 0)
			from.off++;
	} else {
		/* the first run at or below the window's top */
		from.item = br.p->nitems;
		from.off = 0;
		for (i = 0; i < br.p->nitems; i++)
			if (br.p->items[i].type == DL_TEXT && br.p->items[i].y >= cur->top) {
				from.item = i;
				break;
			}
	}
	n = page_find(br.p, f_words, &from, how < 0, &a, &b, &nth);
	if (n <= 0) {
		log_event("found none");
		status(n < 0 ? "No memory to look through the page." : "Not on this page.");
		return;
	}
	sel_set(&a, &b, 1);
	sel_keep();
	page_reveal_item(br.p, a.item, 0);	/* (a box that scrolls brought to it: drawn below) */
	it = &br.p->items[a.item];
	if (it->y < cur->top || it->y + it->h > cur->top + cur->r_page.h)
		win_scroll_to(it->y - cur->r_page.h / 3);
	if (it->x < cur->left || it->x + it->w > cur->left + cur->r_page.w)
		scroll_x_to(it->x - 8);
	win_redraw_page();
	snprintf(line, sizeof line, "%ld of %ld on this page.", nth, n);
	log_event("found %ld of %ld", nth, n);
	status(line);
}

/* ---------------------------------------------------------------- the mouse */

static int link_at(short mx, short my)
{
	if (br.p == NULL || !rect_hit(&cur->r_page, mx, my))
		return -1;
	return page_link_at(br.p, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top);
}

int win_stop_hit(short mx, short my)
{
	return enabled(B_STOP) && !cur->iconified && rect_hit(&cur->but[B_STOP], mx, my) &&
	       wind_find(mx, my) == cur->h;
}

static short pointer = ARROW;

/* the button under the mouse, framed (the others flat), its name on the
   info line (while nothing loads: then the line is the load's) */
static void hot_button(int b)
{
	int old = cur->hot;
	if (b == cur->hot)
		return;
	cur->hot = b;
	if (!loading)
		win_status(b >= 0 ? names[b] : "");
	if (old >= 0)
		redraw(&cur->but[old], 1);
	if (b >= 0)
		redraw(&cur->but[b], 1);
}

void win_hover(short mx, short my)
{
	short h = wind_find(mx, my), t, d, want = ARROW;
	int l = -1, b = -1, i;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (h == cur->h && t == cur->h && !cur->iconified)
		for (i = 0; i < NBUT; i++)
			if (rect_hit(&cur->but[i], mx, my) && enabled(i))
				b = i;
	hot_button(b);
	if (h == cur->h && t == cur->h && !cur->iconified && !loading) {
		fctl *c = br.p && rect_hit(&cur->r_page, mx, my) ? page_ctl_at(br.p, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top, NULL)
							     : NULL;
		l = c == NULL ? link_at(mx, my) : -1;
		if (l >= 0)
			want = POINT_HAND;
		else if (rect_hit(&cur->r_addr, mx, my) || (c != NULL && form_typed(c) && !c->disabled))
			want = TEXT_CRSR;
	}
	if (want != pointer) {
		graf_mouse(want, 0);
		pointer = want;
	}
	if (!loading) {
		if (l != cur->link_shown) {
			char u[512], a[160];
			cur->link_shown = l;
			if (l >= 0) {
				br_link_url(&br, l, u, sizeof u);
				atari_text(u, a, sizeof a);
				win_status(a);
			} else {
				win_status("");
			}
		}
	}
	evnt_set_m1(1, mx, my, 1, 1);
}

/* a button held down: sunk while the mouse is on it, raised when it
   leaves; 1 when let go on it (a click), 0 when let go elsewhere */
static int press(int i)
{
	short mx, my, mb, ks, in = 1;
	cur->sunk = i;
	redraw(&cur->but[i], 1);
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 1))
			break;
		if (rect_hit(&cur->but[i], mx, my) != in) {
			in = !in;
			cur->sunk = in ? i : -1;
			cur->hot = in ? -1 : i;
			redraw(&cur->but[i], 1);
		}
		evnt_timer_(20);
	}
	cur->sunk = -1;
	cur->hot = rect_hit(&cur->but[i], mx, my) ? i : -1;
	redraw(&cur->but[i], 1);
	if (!in)
		hot_button(-1);
	return in;
}

/* box k of the page (one that scrolls) scrolled to v along its axis, and
   drawn again where it is on the screen */
static void box_to(int k, int axis, int32_t v)
{
	const struct pg_scroll *sc = &br.p->scr[k];
	int32_t x, y, w, h, x0, y0, x1, y1;
	GRECT r;
	if (!page_scroll(br.p, k, axis ? v : sc->sx, axis ? sc->sy : v, &x, &y, &w, &h))
		return;
	log_event("box %d scrolled to %ld,%ld", k + 1, (long) sc->sx, (long) sc->sy);
	x0 = cur->r_page.x + x - cur->left;
	y0 = cur->r_page.y + y - cur->top;
	x1 = x0 + w;
	y1 = y0 + h;
	x0 = x0 < cur->r_page.x ? cur->r_page.x : x0;
	y0 = y0 < cur->r_page.y ? cur->r_page.y : y0;
	x1 = x1 > cur->r_page.x + cur->r_page.w ? cur->r_page.x + cur->r_page.w : x1;
	y1 = y1 > cur->r_page.y + cur->r_page.h ? cur->r_page.y + cur->r_page.h : y1;
	if (x0 >= x1 || y0 >= y1)
		return;
	r.x = (short) x0;
	r.y = (short) y0;
	r.w = (short) (x1 - x0);
	r.h = (short) (y1 - y0);
	redraw(&r, 2);
}

/* the box whose bar (down) is held, for panebar's pb_click */
static int held_k;
static PANEBAR held;

/* n rows: an arrow's are lines (gl_hchar), a press on the track's a
   boxful less a pixel (panebar's page) */
static int held_scroll(long n)
{
	const struct pg_scroll *sc = &br.p->scr[held_k];
	int32_t was = held.across ? sc->sx : sc->sy;
	long step = held.shown > 1 ? held.shown - 1 : 1;
	if (n != step && n != -step)
		n *= gl_hchar;
	box_to(held_k, held.across, was + (int32_t) n);
	held.top = held.across ? sc->sx : sc->sy;
	return held.top != was;
}

static int held_goto(long t)
{
	const struct pg_scroll *sc = &br.p->scr[held_k];
	box_to(held_k, held.across, (int32_t) t);
	held.top = held.across ? sc->sx : sc->sy;
	return 1;
}

/* a click on a box's scroll bar (a box with overflow auto or scroll):
   panebar's (its arrows, track and slider: the guidelines' 5.1) */
static int box_bar(short mx, short my)
{
	const dl_item *bar;
	int32_t px = mx - cur->r_page.x + cur->left, py = my - cur->r_page.y + cur->top;
	GRECT seen;
	if (br.p == NULL || (bar = page_bar_at(br.p, px, py)) == NULL)
		return 0;
	if (box_panebar(br.p, bar, &held, &seen)) {
		held_k = (int) bar->len - 1;
		pb_click(&held, mx, my, held_scroll, held_goto);
	}
	return 1;
}

/* right-click: the link's commands, or the page's (Back first, as
   EHypView's) */
static void context_menu(short mx, short my)
{
	int l = link_at(mx, my);
	if (l >= 0) {
		static const char *const items[] = { "Open the link", "Open it in a new window  Ctrl-click",
						     "Save the link's file...  Alt-click", "Copy the link's address" };
		short r = popup(mx, my, items, 4);
		if (r == 0) {
			follow(l);
		} else if (r == 1) {
			follow_window(l);
		} else if (r == 2) {
			save_link(l);
		} else if (r == 3) {
			char u[2048], a[2048];
			br_link_url(&br, l, u, sizeof u);
			atari_text(u, a, sizeof a);
			clip_put(a, (long) strlen(a));
		}
	} else if (rect_hit(&cur->r_page, mx, my)) {
		const char *items[6];
		static char back[40], fwd[40];
		short r;
		snprintf(back, sizeof back, "%sBack           Undo", br_can_back(&br) ? "" : "\x08");
		snprintf(fwd, sizeof fwd, "%sForward      \x01^Z", br_can_forward(&br) ? "" : "\x08");
		items[0] = back;
		items[1] = fwd;
		items[2] = "Reload           ^R";
		items[3] = "-";
		items[4] = sel_now() ? "Copy             ^C" : "Copy the text    ^C";
		items[5] = "Select all       ^A";
		r = popup(mx, my, items, 6);
		switch (r) {
		case 0: command(C_BACK); break;
		case 1: command(C_FORWARD); break;
		case 2: command(C_RELOAD); break;
		case 4: win_clip(CLIP_COPY); break;
		case 5: win_select_all(); break;
		}
	}
}

/* the rows of the items that may be marked otherwise when the
   selection's end moves from one place to another (those between the
   two in the list, which has the text in the order it is read), drawn
   again */
static void sel_redraw(const pg_pos *was, const pg_pos *now)
{
	long lo = was->item < now->item ? was->item : now->item, hi = was->item < now->item ? now->item : was->item, i;
	int32_t y0 = INT32_MAX, y1 = INT32_MIN;
	GRECT g;
	for (i = lo; i <= hi && i < br.p->nitems; i++) {
		const dl_item *d = &br.p->items[i];
		if (d->type != DL_TEXT)
			continue;
		if (d->y < y0)
			y0 = d->y;
		if (d->y + d->h > y1)
			y1 = d->y + d->h;
	}
	y0 -= cur->top;
	y1 -= cur->top;
	if (y0 < 0)
		y0 = 0;
	if (y1 > cur->r_page.h)
		y1 = cur->r_page.h;
	if (y1 <= y0)
		return;
	g.x = cur->r_page.x;
	g.w = cur->r_page.w;
	g.y = (short) (cur->r_page.y + y0);
	g.h = (short) (y1 - y0);
	redraw(&g, 2);
}

/* the left button pressed on the page at (mx, my), on no control: text
   selected as the mouse is dragged over it (once it has moved by more
   than 3 pixels), the window scrolled while it is above or below it; a
   double click on a word selects it (the page's scripts told of the
   click first; on a link or a summary it is theirs), a click with Shift
   takes the selection on to there. 0: a click (the selection gone), to
   go on as one */
static int select_text(short mx, short my, short clicks, short kstate)
{
	pg_pos at, from, to;
	short x = mx, y = my, mb = 1, ks, lx = -1, ly = -1;
	int32_t px = mx - cur->r_page.x + cur->left, py = my - cur->r_page.y + cur->top;
	int whole;
	if (br.p == NULL || !page_pos_at(br.p, px, py, &at))
		return 0;
	if (clicks >= 2) {
		pg_pos a, b;
		if (link_at(mx, my) >= 0 || page_summary_at(br.p, px, py) != NULL)
			return 0;
		if (page_click(px, py) || br.p == NULL || page_pos_at(br.p, px, py, &at) != 2)
			return 1;	/* (a script said no, or there is no word there) */
		ctl_blur();
		page_word(br.p, &at, &a, &b);
		sel_set(&a, &b, 0);
		sel_keep();
		log_event("selected a word: %ld", cur->sel.ix[1] - cur->sel.ix[0]);
		win_redraw_page();
		return 1;
	}
	whole = sel_now();	/* (an old selection shown: the page drawn again at the first move) */
	if ((kstate & (K_LSHIFT | K_RSHIFT)) && whole) {
		from = cur->sel.at[0];
		to = cur->sel.at[1];
		whole = cur->sel.found;	/* (Find's yellow becomes the selection's colour) */
	} else {
		for (;;) {
			graf_mkstate(&x, &y, &mb, &ks);
			if (!(mb & 1)) {
				sel_clear();
				return 0;
			}
			if (x < mx - 3 || x > mx + 3 || y < my - 3 || y > my + 3)
				break;
			evnt_timer_(10);
		}
		cur->sel.on = 0;
		from = to = at;
	}
	ctl_blur();		/* (^C then copies the selection, not a field's text) */
	for (;;) {
		int32_t step = 0;
		if (y < cur->r_page.y)
			step = -line_step();
		else if (y >= cur->r_page.y + cur->r_page.h)
			step = line_step();
		if (step != 0)
			win_scroll_to(cur->top + step);	/* (drawn again, the selection too) */
		if (x != lx || y != ly || step != 0) {
			lx = x;
			ly = y;
			px = (x < cur->r_page.x ? cur->r_page.x : x >= cur->r_page.x + cur->r_page.w ? cur->r_page.x + cur->r_page.w - 1 : x) - cur->r_page.x + cur->left;
			py = (y < cur->r_page.y ? cur->r_page.y : y >= cur->r_page.y + cur->r_page.h ? cur->r_page.y + cur->r_page.h - 1 : y) - cur->r_page.y + cur->top;
			if (page_pos_at(br.p, px, py, &at) && (whole || at.item != to.item || at.off != to.off)) {
				sel_set(&from, &at, 0);
				if (whole)
					win_redraw_page();
				else
					sel_redraw(&to, &at);
				whole = 0;
				to = at;
			}
		}
		if (!(mb & 1))
			break;
		evnt_timer_(20);
		graf_mkstate(&x, &y, &mb, &ks);
	}
	sel_keep();
	if (cur->sel.on)
		log_event("selected: %ld", cur->sel.ix[1] > cur->sel.ix[0] ? cur->sel.ix[1] - cur->sel.ix[0] : cur->sel.ix[0] - cur->sel.ix[1]);
	return 1;
}

int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	int i, l;
	if (cur->h <= 0 || wind_find(mx, my) != cur->h || cur->iconified)
		return 0;
	for (i = 0; i < NBUT; i++) {
		if (!rect_hit(&cur->but[i], mx, my))
			continue;
		if (!enabled(i) || !press(i))
			return 1;
		switch (i) {
		case B_BACK: command(C_BACK); break;
		case B_FORWARD: command(C_FORWARD); break;
		case B_RELOAD: command(C_RELOAD); break;
		case B_HOME: command(C_HOME); break;
		case B_SEARCH: command(C_SEARCH); break;
		case B_STOP: command(C_STOP); break;
		}
		return 1;
	}
	if (rect_hit(&cur->r_addr, mx, my)) {
		if (button & 2) {
			static const char *const items[] = { "Copy        ^C", "Cut         ^X", "Paste       ^V",
							     "-", "Select all  ^A" };
			short r;
			if (!cur->a_focus)
				win_address_focus(1);
			r = popup(mx, my, items, 5);
			if (r >= 0 && r <= 2)
				win_clip(r == 0 ? CLIP_COPY : r == 1 ? CLIP_CUT : CLIP_PASTE);
			else if (r == 4)
				win_address_focus(1);
			return 1;
		}
		if (!cur->a_focus || clicks >= 2) {
			win_address_focus(1);
		} else {
			cur->a_all = 0;
			cur->a_cur = cur->a_first + (mx - cur->r_addr.x - gl_wchar / 2 + gl_wchar / 2) / gl_wchar;
			addr_show_cursor();
			redraw_addr();
		}
		return 1;
	}
	if (rect_hit(&cur->r_page, mx, my)) {
		const dl_item *it = NULL;
		fctl *c;
		dnode *det;
		addr_blur();
		if (button & 2) {
			context_menu(mx, my);
			return 1;
		}
		if (box_bar(mx, my))
			return 1;	/* a box's scroll bar */
		if (br.p != NULL && page_ctl_at(br.p, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top, &it) == NULL &&
		    select_text(mx, my, clicks, kstate))
			return 1;	/* text selected */
		/* the page's scripts first: a click event, which may say no to
		   what the click does, or change the page */
		if (!(kstate & 0x08) && page_click(mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top))
			return 1;
		c = br.p ? page_ctl_at(br.p, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top, &it) : NULL;
		if (c != NULL && !loading) {
			/* a label's text stands for its control */
			int label = it->type == DL_TEXT;
			if (c->disabled || (label && (it = page_ctl_item(br.p, c)) == NULL))
				return 1;
			if (form_typed(c)) {
				ctl_focus(c);
				c->cur = label ? c->len : cv_ctl_hit(it, c, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top);
				ctl_redraw(c);
			} else {
				ctl_focus(c);
				if (label && c->kind == FC_SELECT)
					ctl_press(c, it, (short) (cur->r_page.x + it->x - cur->left + 4), (short) (cur->r_page.y + it->y - cur->top + it->h));
				else
					ctl_press(c, it, label ? (short) (cur->r_page.x + it->x - cur->left) : mx, label ? (short) (cur->r_page.y + it->y - cur->top) : my);
			}
			return 1;
		}
		ctl_blur();
		l = link_at(mx, my);
		if (l >= 0 && (kstate & 0x08))
			save_link(l);	/* Alternate-click (0x08): its file saved, as Safari's Option-click */
		else if (l >= 0 && (kstate & 0x04))
			follow_window(l);	/* Control-click: in a new window, as Safari's Command-click */
		else if (l >= 0)
			follow(l);
		else if (br.p != NULL && (det = page_summary_at(br.p, mx - cur->r_page.x + cur->left, my - cur->r_page.y + cur->top)) != NULL)
			details(det);	/* a details' summary: it opens or closes */
		return 1;
	}
	return 1;
}

/* ---------------------------------------------------------------- keys */

/* the box that scrolls down under the mouse (the smallest there: a frame,
   a box in it), -1 none */
static int box_under_mouse(void)
{
	short mx, my, mb, ks;
	int32_t px, py;
	long ba = 0;
	int k, best = -1;
	if (br.p == NULL || br.p->nscr == 0)
		return -1;
	graf_mkstate(&mx, &my, &mb, &ks);
	px = mx - cur->r_page.x + cur->left;
	py = my - cur->r_page.y + cur->top;
	for (k = 0; k < br.p->nscr; k++) {
		const struct pg_scroll *sc = &br.p->scr[k];
		const dl_item *c = &br.p->items[sc->clip];
		int32_t x0 = c->x, y0 = c->y, x1 = c->x + c->w, y1 = c->y + c->h;
		if (sc->my == 0 || (c->clip && !page_clip(br.p, c->clip, &x0, &y0, &x1, &y1)))
			continue;
		if (px < x0 || px >= x1 || py < y0 || py >= y1)
			continue;
		if (best < 0 || (long) c->w * c->h < ba) {
			best = k;
			ba = (long) c->w * c->h;
		}
	}
	return best;
}

/* Up, Down, Space, ClrHome on a page that doesn't scroll down itself (a
   frameset): the box under the mouse scrolls (by a line, a boxful, to its
   top or end). 1 if one did */
static int box_key(short scan, int shift)
{
	int k;
	const struct pg_scroll *sc;
	int32_t boxful;
	if (max_top() > 0 || (k = box_under_mouse()) < 0)
		return 0;
	sc = &br.p->scr[k];
	boxful = br.p->items[sc->clip].h - line_step();
	switch (scan) {
	case 0x48: box_to(k, 1, sc->sy - (shift ? boxful : line_step())); return 1;
	case 0x50: box_to(k, 1, sc->sy + (shift ? boxful : line_step())); return 1;
	case 0x39: box_to(k, 1, sc->sy + (shift ? -boxful : boxful)); return 1;
	case 0x47: box_to(k, 1, shift ? sc->my : 0); return 1;
	}
	return 0;
}

int win_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	int32_t page = cur->r_page.h - line_step();
	if (cur->h <= 0 || cur->iconified)
		return 0;
	if (cur->f_focus != NULL && cur->f_focus->kind == FC_TEXTAREA && (scan == 0x48 || scan == 0x50) && !shift)
		return ctl_key(kstate, key);	/* a textarea's lines */
	if ((scan == 0x48 || scan == 0x50 || ((scan == 0x39 || scan == 0x47) && !cur->a_focus && cur->f_focus == NULL &&
	     !key_ctrl(kstate, key))) && box_key(scan, shift))
		return 1;
	/* up and down scroll the page wherever the keyboard is */
	switch (scan) {
	case 0x48:		/* Up */
		win_scroll_by(shift ? -page : -line_step());
		return 1;
	case 0x50:		/* Down */
		win_scroll_by(shift ? page : line_step());
		return 1;
	}
	if (cur->a_focus)
		return addr_key(kstate, key);
	if (cur->f_focus != NULL)
		return ctl_key(kstate, key);
	switch (scan) {
	case 0x4b:		/* Left, Right: a page wider than the window sideways */
	case 0x4d:
		scroll_x_to(cur->left + (scan == 0x4b ? -1 : 1) * (shift ? cur->r_page.w - col_step() : col_step()));
		return 1;
	case 0x39:		/* Space: a page down, with Shift up */
		if (key_ctrl(kstate, key))
			return 0;
		win_scroll_by(shift ? -page : page);
		return 1;
	case 0x47:		/* ClrHome: the top, with Shift the end */
		win_scroll_to(shift ? max_top() : 0);
		return 1;
	case 0x0f: {		/* Tab: the keyboard to the page's first control, else the address */
		fctl *n = NULL;
		if (br.d != NULL && br.p != NULL) {
			n = form_next(br.d, NULL, shift ? -1 : 1);
			while (n != NULL && page_ctl_item(br.p, n) == NULL)
				n = form_next(br.d, n, shift ? -1 : 1);
		}
		if (n != NULL)
			ctl_focus(n);
		else
			win_address_focus(1);
		return 1;
	}
	}
	return 0;
}

/* ---------------------------------------------------------------- a new page */

/* the page loading's preview: drawn from its top in place of the page,
   whose place is kept for when the load is stopped */

void win_preview(void)
{
	cur->pv_top = cur->top;
	cur->pv_left = cur->left;
	cur->top = 0;
	cur->left = 0;
	sliders();
	win_redraw_page();
}

void win_preview_gone(void)
{
	cur->top = cur->pv_top;
	cur->left = cur->pv_left;
	if (cur->top > max_top())
		cur->top = max_top();
	if (cur->left > max_left())
		cur->left = max_left();
	sliders();
	win_redraw_page();
}

void win_page_changed(void)
{
	int32_t y = br.scroll_to >= 0 ? br.scroll_to : 0;
	ctl_forget();
	win_title();
	win_address(br.url);
	win_status("");
	cur->top = 0;
	cur->left = 0;
	if (y > max_top())
		y = max_top();
	cur->top = y;
	br_scrolled(&br, cur->top);
	sliders();
	win_redraw(NULL);
}

/* ---------------------------------------------------------------- the window */

static void get_work(void)
{
	wind_get(cur->h, WF_WORKXYWH, &cur->work.x, &cur->work.y, &cur->work.w, &cur->work.h);
	layout();
}

static void save_place(void)
{
	wind_get(cur->h, WF_CURRXYWH, &opt.win_x, &opt.win_y, &opt.win_w, &opt.win_h);
}

/* the window's size changed: the page laid out for its width again */
static void resized(void)
{
	int32_t old_w = br.width, old_h = br.height;
	get_work();
	save_place();
	br.height = win_page_h();
	if (br.p != NULL && (win_page_w() != old_w || (br.p->frameset && win_page_h() != old_h))) {
		/* (a frameset is as tall as the window: its frames laid out again) */
		/* the same place in the text, as near as it can be; the busy
		   bee, as the page's sheets may be fetched for the new width */
		int32_t h0 = br.p->height;
		if (!loading)
			graf_mouse(BUSYBEE, 0);
		br_relayout(&br, win_page_w());
		if (!loading)
			graf_mouse(ARROW, 0);
		if (h0 > 0)
			cur->top = (int32_t) ((int64_t) cur->top * br.p->height / h0);
	}
	br.width = win_page_w();
	br_live_size(br.width, br.height);
	if (cur->top > max_top())
		cur->top = max_top();
	if (cur->left > max_left())
		cur->left = max_left();
	sliders();
}

/* the window's border x for its work area on a multiple of 8 across
   (wind_x8: the guidelines' Speed, text on bytes), and its width less
   what that moved it where it reached the desk's right edge (a window at
   the left edge goes 7 pixels right) */
static void on_grid(short *x, short y, short *w, short h)
{
	short nx = wind_x8(icon_kind(KIND), *x, y, *w, h);
	if (nx > *x && *x + *w <= desk_x + desk_w && nx + *w > desk_x + desk_w)
		*w -= nx + *w - (desk_x + desk_w);
	*x = nx;
}

int win_open(void)
{
	short x = opt.win_x, y = opt.win_y, w = opt.win_w, h = opt.win_h;
	const ewin *o;
	if (cur->h > 0) {
		wind_set(cur->h, WF_TOP, 0, 0, 0, 0);
		return 1;
	}
	/* another window's place, a little down and to the right */
	for (o = wins; o != NULL && (o == cur || o->h <= 0 || o->iconified); o = o->next)
		;
	if (o != NULL) {
		short dx = 2 * gl_wchar, dy = gl_hchar + 3;
		wind_get(o->h, WF_CURRXYWH, &x, &y, &w, &h);
		x += dx;
		y += dy;
		if (x + w > desk_x + desk_w && w - dx >= 40 * gl_wchar)
			w -= dx;	/* (smaller, where it would run off the desk) */
		if (y + h > desk_y + desk_h && h - dy >= 12 * gl_hchar)
			h -= dy;
		if (x + w > desk_x + desk_w || y + h > desk_y + desk_h) {
			x = desk_x;
			y = desk_y;
		}
	}
	if (w < 40 * gl_wchar || h < 12 * gl_hchar || x < 0 || y < desk_y ||
	    x + w > desk_x + desk_w + 4 || y + h > desk_y + desk_h + 4) {
		x = desk_x;
		y = desk_y;
		w = desk_w;
		h = desk_h;
	}
	cur->h = wind_create(icon_kind(KIND), desk_x, desk_y, desk_w, desk_h);
	cur->iconified = 0;
	if (cur->h < 0)
		return 0;
	if (!cur->title[0])
		str_copy(cur->title, " EWeb ", sizeof cur->title);
	wind_set_str(cur->h, WF_NAME, cur->title);
	on_grid(&x, y, &w, h);		/* the work area's text on bytes */
	wind_open(cur->h, x, y, w, h);
	get_work();
	save_place();
	br.width = win_page_w();
	br.height = win_page_h();
	br_live_size(br.width, br.height);
	sliders();
	return 1;
}

void win_close(void)
{
	const ewin *o;
	if (cur->h <= 0)
		return;
	if (!cur->iconified)
		save_place();
	wind_close(cur->h);
	wind_delete(cur->h);
	cur->h = -1;
	for (o = wins; o != NULL && o->h <= 0; o = o->next)
		;
	if (o != NULL)
		return;		/* (the pictures below are every window's) */
	scr_dev_free(thr_dev);		/* the throbber's frames */
	thr_dev = NULL;
	thr_made = 0;
	scr_dev_free(tb_dev);		/* the toolbar's pictures */
	tb_dev = NULL;
	tb_made = 0;
}

ewin *win_new(void)
{
	ewin *w = malloc(sizeof *w);
	if (w == NULL)
		return NULL;
	memset(w, 0, sizeof *w);
	w->h = -1;
	w->hot = w->sunk = -1;
	w->link_shown = -2;
	w->next = wins;
	wins = w;
	cur = w;
	return w;
}

void win_free(ewin *w)
{
	ewin **pp;
	ewin *was = cur;
	cur = w;
	win_close();
	br_free(&w->b);
	for (pp = &wins; *pp != NULL && *pp != w; pp = &(*pp)->next)
		;
	if (*pp != NULL)
		*pp = w->next;
	free(w);
	cur = was != w ? was : wins;
}

void win_use(ewin *w)
{
	if (w != NULL)
		cur = w;
}

ewin *win_of(short h)
{
	ewin *w;
	for (w = wins; w != NULL && (h <= 0 || w->h != h); w = w->next)
		;
	return w;
}

int win_front(void)
{
	short t, d;
	ewin *w;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	w = win_of(t);
	if (w == NULL || w == cur)
		return 0;
	if (cur != NULL && cur->hot >= 0 && cur->h > 0) {
		int b = cur->hot;	/* (the button the mouse was over, flat again) */
		cur->hot = -1;
		redraw(&cur->but[b], 1);
	}
	cur = w;
	return 1;
}

void win_next(void)
{
	ewin *w;
	if (cur == NULL)
		return;
	for (w = cur->next != NULL ? cur->next : wins; w != cur && w->h <= 0; w = w->next != NULL ? w->next : wins)
		;
	if (w == cur)
		return;		/* (one window: nothing) */
	log_event("next window");
	wind_set(w->h, WF_TOP, 0, 0, 0, 0);
	win_front();
}

int win_count(void)
{
	const ewin *w;
	int n = 0;
	for (w = wins; w != NULL; w = w->next)
		n++;
	return n;
}

/* (the pictures were made in the screen's pens of the time) */
void win_colours(void)
{
	thr_made = 0;
	tb_made = 0;
}

static void message(const short *msg);

/* to the window it names, cur while it is done (the one in front is cur
   again at the next event, main.c's win_front) */
void win_message(const short *msg)
{
	ewin *was = cur, *w = win_of(msg[3]), *k;
	if (w == NULL)
		return;
	cur = w;
	message(msg);
	for (k = wins; k != NULL && k != was; k = k->next)
		;
	if (k != NULL)
		cur = was;	/* (unless it was closed) */
}

static void message(const short *msg)
{
	GRECT r;
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		win_redraw(&r);
		break;
	case WM_TOPPED:
	case WM_NEWTOP:
		wind_set(cur->h, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_BOTTOMED:
		wind_set(cur->h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (!loading)
			command(C_CLOSE);
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (cur->iconified)
			break;
		save_place();
		cur->iconified = 1;
		wind_set(cur->h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_UNICONIFY:
		if (!cur->iconified)
			break;
		wind_set(cur->h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		cur->iconified = 0;
		resized();
		win_redraw(NULL);
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		if (cur->iconified || loading)
			break;
		wind_get(cur->h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(cur->h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		on_grid(&f.x, f.y, &f.w, f.h);	/* (fulled: within the desk, on a byte) */
		if (c.x == f.x && c.y == f.y && c.w == f.w && c.h == f.h) {
			wind_get(cur->h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			on_grid(&p.x, p.y, &p.w, p.h);
			wind_set(cur->h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
			wind_set(cur->h, WF_CURRXYWH, f.x, f.y, f.w, f.h);
		}
		resized();
		win_redraw(NULL);
		break;
	}
	case WM_SIZED:
	case WM_MOVED: {
		short w = msg[6], h = msg[7];
		if (cur->iconified) {
			wind_set(cur->h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		if (loading && msg[0] == WM_SIZED)
			break;		/* the page being laid out keeps its width */
		if (w < 40 * gl_wchar)
			w = 40 * gl_wchar;
		if (h < 12 * gl_hchar)
			h = 12 * gl_hchar;
		/* its work area on a byte across (the guidelines' Speed:
		   text on bytes), up to 7 pixels from where it was put */
		{
			short x = msg[4];
			on_grid(&x, msg[5], &w, h);
			wind_set(cur->h, WF_CURRXYWH, x, msg[5], w, h);
		}
		resized();
		if (msg[0] == WM_SIZED)
			win_redraw(NULL);
		break;
	}
	case WM_ARROWED:
		if (loading)
			break;
		switch (msg[4]) {
		case WA_UPPAGE: win_scroll_by(-(cur->r_page.h - line_step())); break;
		case WA_DNPAGE: win_scroll_by(cur->r_page.h - line_step()); break;
		case WA_UPLINE: win_scroll_by(-line_step()); break;
		case WA_DNLINE: win_scroll_by(line_step()); break;
		case WA_LFPAGE: scroll_x_to(cur->left - (cur->r_page.w - col_step())); break;
		case WA_RTPAGE: scroll_x_to(cur->left + (cur->r_page.w - col_step())); break;
		case WA_LFLINE: scroll_x_to(cur->left - col_step()); break;
		case WA_RTLINE: scroll_x_to(cur->left + col_step()); break;
		}
		/* (one click, one step: the guidelines' 5.1, common's evnt_multi_
		   dropping TOS's echo; the rigs count these) */
		log_event("arrowed %d top %ld left %ld", msg[4], (long) cur->top, (long) cur->left);
		break;
	case WM_VSLID:
		if (loading)
			break;
		win_scroll_to((int32_t) ((int64_t) max_top() * msg[4] / 1000));
		break;
	case WM_HSLID:
		if (loading)
			break;
		scroll_x_to((int32_t) ((int64_t) max_left() * msg[4] / 1000));
		break;
	}
}
