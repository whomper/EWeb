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
#include "util.h"
#include "inflate.h"
#include "plat.h"
#include "tbicons.h"
#include "throbber.h"

short win_h = -1;
short win_iconified;

#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE | LFARROW | RTARROW | HSLIDE)
#define POINT_HAND 3
#define TEXT_CRSR  1

static GRECT work, r_tool, r_page, r_addr, r_thr, r_stat;
static int32_t top;			/* the page's row at r_page's top */
static int32_t left;			/* its column at r_page's left (a page wider than
					   the window: page.right) */
static void redraw(const GRECT *area, int parts);
static char info[160];			/* the status bar's text as set (Atari text) */
static char title[160];

/* ---------------------------------------------------------------- the toolbar */

/* the buttons, in the order of tbicons.h's pictures */
enum { B_BACK, B_FORWARD, B_RELOAD, B_HOME, B_SEARCH, B_STOP, NBUT };
static const char *const names[NBUT] = { "Back", "Forward", "Reload", "Home", "Search", "Stop" };
static GRECT but[NBUT];
static int hot = -1;			/* the button under the mouse: framed */
static int sunk = -1;			/* the one held down: framed, sunk */
static short tall;			/* the 20-row pictures (else the 10-row ones) */

static int enabled(int i)
{
	switch (i) {
	case B_BACK: return !loading && br_can_back(&br);
	case B_FORWARD: return !loading && br_can_forward(&br);
	case B_RELOAD: return !loading && br.cur >= 0;
	case B_HOME: return !loading;
	case B_SEARCH: return !loading && opt.search[0];
	case B_STOP: return loading || br_refresh_due(&br) >= 0;	/* or the page's refresh */
	}
	return 0;
}

/* the address field: Atari text (a URL is ASCII), the cursor, the first
   character shown, and whether it is all chosen (typing replaces it) */
static char addr[1024];
static int a_focus, a_cur, a_first, a_all;

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
	r_tool = work;
	r_tool.h = th;
	x = work.x + 4;
	for (i = 0; i < NBUT; i++) {
		but[i].x = x;
		but[i].y = work.y + 2;
		but[i].w = TB_W + 6;
		but[i].h = bh;
		x += but[i].w + 2;
	}
	r_addr.x = x + 6;
	r_addr.h = gl_hchar + 4;
	r_addr.y = work.y + (th - 1 - r_addr.h) / 2;
	r_addr.w = work.x + work.w - 4 - r_addr.x;
	r_thr.w = THR_W;
	r_thr.h = tall ? THR_TALL : THR_SHORT;
	r_thr.x = work.x + work.w - 4 - THR_W;
	r_thr.y = work.y + (th - 1 - r_thr.h) / 2;
	if (r_thr.x - 8 < r_addr.x + 8 * gl_wchar)
		r_thr.w = 0;
	else
		r_addr.w = r_thr.x - 8 - r_addr.x;
	if (r_addr.w < 4 * gl_wchar)
		r_addr.w = 0;		/* no room for it */
	/* the status bar: its text, the ribbon along its foot */
	r_stat = work;
	r_stat.h = gl_hchar + (tall ? 7 : 5);
	r_stat.y = work.y + work.h - r_stat.h;
	r_page = work;
	r_page.y += th;
	r_page.h -= th + r_stat.h;
	if (r_page.h < 0)
		r_page.h = 0;
}

static short paper(void)
{
	return planes >= 4 ? grey_pen() : 0;
}

/* the pen a picture's layer takes on this screen; -1: not drawn */
static short layer_pen(const TB_LAYER *l)
{
	if (planes == 1)
		return l->mono;
	if (planes == 2 && l->pen > 3)		/* 4 colours: white, black, red, green */
		return l->pen == 6 ? 0 : 1;
	return l->pen;
}

/* a button's picture at x, y, within clip: each layer a mask copied in its
   pen (from ST RAM, where the blitter reaches); one that can't be used,
   only its outline, grey (on a monochrome screen every other dot) */
static void draw_icon(short x, short y, int i, int on, const GRECT *clip)
{
	const TB_ICON *ic = tall ? &tb_tall[i] : &tb_short[i];
	short rows = tall ? TB_TALL : TB_SHORT, k;
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = TB_W;
	r.h = rows;
	if (!rect_and(&r, clip))
		return;
	for (k = 0; k < ic->n; k++) {
		const TB_LAYER *l = &ic->layer[k];
		unsigned short *buf = st_ram(2L * 2 * rows);
		short pen = layer_pen(l), colours[2], pxy[8], j;
		MFDB src, dst;
		if (!on) {
			if (l->pen != 1)
				continue;	/* the outline alone */
			pen = planes >= 4 ? 9 : 1;
		}
		if (pen < 0 || buf == NULL)
			continue;
		for (j = 0; j < 2 * rows; j++) {
			unsigned short v = l->bits[j];
			if (!on && planes < 4)
				v &= (j >> 1) & 1 ? 0xAAAA : 0x5555;
			buf[j] = v;
		}
		src.fd_addr = buf;
		src.fd_w = 32;
		src.fd_h = rows;
		src.fd_wdwidth = 2;
		src.fd_stand = 0;
		src.fd_nplanes = 1;
		src.fd_r1 = src.fd_r2 = src.fd_r3 = 0;
		dst.fd_addr = NULL;
		pxy[0] = r.x - x;
		pxy[1] = r.y - y;
		pxy[2] = pxy[0] + r.w - 1;
		pxy[3] = pxy[1] + r.h - 1;
		pxy[4] = r.x;
		pxy[5] = r.y;
		pxy[6] = r.x + r.w - 1;
		pxy[7] = r.y + r.h - 1;
		colours[0] = pen;
		colours[1] = 0;
		vrt_cpyfm(vdi_h, MD_TRANS, pxy, &src, &dst, colours);
	}
}

/* a button's frame: raised under the mouse, sunk while held down */
static void button_frame(const GRECT *r, int down)
{
	short x1 = r->x, y1 = r->y, x2 = r->x + r->w - 1, y2 = r->y + r->h - 1;
	if (planes >= 4) {
		short lt = down ? 9 : 0, rb = down ? 0 : 9;
		line_col(x1, y1, x2, y1, lt);
		line_col(x1, y1, x1, y2, lt);
		line_col(x1, y2, x2, y2, rb);
		line_col(x2, y1, x2, y2, rb);
		if (down) {
			line_col(x1 + 1, y1 + 1, x2 - 1, y1 + 1, 1);
			line_col(x1 + 1, y1 + 1, x1 + 1, y2 - 1, 1);
		}
		return;
	}
	{
		GRECT f = *r;
		f.w--;
		f.h--;
		frame(&f, 1);
		if (down) {
			line_col(x1 + 1, y1 + 1, x2 - 2, y1 + 1, 1);
			line_col(x1 + 1, y1 + 1, x1 + 1, y2 - 2, 1);
		} else {
			line_col(x1 + 1, y2, x2, y2, 1);
			line_col(x2, y1 + 1, x2, y2, 1);
		}
	}
}

static void draw_button(int i, const GRECT *clip)
{
	GRECT r = but[i];
	short on = enabled(i), down = i == sunk, d = down ? 1 : 0;
	fill(&r, paper());
	if (down || (i == hot && on))
		button_frame(&r, down);
	draw_icon(r.x + (r.w - TB_W) / 2 + d, r.y + 3 + d, i, on, clip);
}

static short addr_cols(void)
{
	return (r_addr.w - gl_wchar) / gl_wchar;
}

static void draw_addr(void)
{
	GRECT r = r_addr, in;
	short cols = addr_cols(), n = (short) strlen(addr) - a_first;
	if (r.w <= 0)
		return;
	fill(&r, 0);
	frame(&r, a_focus ? 1 : (planes >= 4 ? 9 : 1));
	if (n > cols)
		n = cols;
	if (a_focus && a_all && addr[0]) {
		in.x = r.x + gl_wchar / 2;
		in.y = r.y + 2;
		in.w = n * gl_wchar;
		in.h = gl_hchar;
		fill(&in, 1);
		text_at(in.x, in.y, addr + a_first, n, cols, TX_LTR | TX_INVERSE);
	} else if (n > 0) {
		text_at(r.x + gl_wchar / 2, r.y + 2, addr + a_first, n, cols, TX_LTR);
	}
	if (a_focus && !a_all) {
		short cx = r.x + gl_wchar / 2 + (a_cur - a_first) * gl_wchar;
		line_col(cx, r.y + 2, cx, r.y + 1 + gl_hchar, 1);
	}
}

/* ---------------------------------------------------------------- the throbber */

static const THR_ED *thr_ed;		/* the edition for this screen, NULL: none */
static char *thr_dev;			/* its frames in the screen's own form (ST RAM), each made when first shown */
static unsigned thr_made;		/* which of them are made, a bit each */
static int thr_nodev;			/* no memory for them: each drawn as it comes */
static int thr_frame;			/* the frame shown: 0 at rest */
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
	GRECT r = r_thr;
	SBUF b;
	const THR_ED *e = thr_ed != NULL ? thr_ed : (thr_ed = edition());
	long n = (long) THR_W * e->h, size = scr_dev_size(THR_W, e->h);
	char *f;
	if (r.w <= 0 || !rect_and(&r, clip))
		return;
	r_thr.h = e->h;
	if (thr_dev == NULL && !thr_nodev && (thr_dev = scr_dev_alloc(size * THR_FRAMES)) == NULL)
		thr_nodev = 1;
	f = thr_dev != NULL ? thr_dev + size * thr_frame : NULL;
	if (f != NULL && (thr_made & (1u << thr_frame))) {
		scr_blit_dev(f, THR_W, e->h, &r_thr, &r);
		return;
	}
	sb_init(&b);
	if (inflate_raw(e->data + e->off[thr_frame], e->off[thr_frame + 1] - e->off[thr_frame], &b, n) == 1 &&
	    b.len == n) {
		if (f != NULL && scr_rgb_dev((const unsigned char *) b.s, e->col, e->ncol, THR_W, e->h, f)) {
			thr_made |= 1u << thr_frame;
			scr_blit_dev(f, THR_W, e->h, &r_thr, &r);
		} else {
			scr_rgb((const unsigned char *) b.s, e->col, e->ncol, THR_W, e->h, &r_thr, &r);
		}
	}
	sb_free(&b);
}

void win_throb(int busy)
{
	GRECT r, a;
	short mx, my, d, hide;
	unsigned long now = pf_ms();
	if (win_h <= 0 || win_iconified || r_thr.w <= 0)
		return;
	if (busy ? now - thr_at < 110 : thr_frame == 0)
		return;
	thr_frame = busy ? (thr_frame + 1) % THR_FRAMES : 0;
	thr_at = now;
	thr_drawn++;
	/* the mouse off only when it is near: the pointer doesn't flicker
	   eight times a second */
	graf_mkstate(&mx, &my, &d, &d);
	hide = mx > r_thr.x - 20 && mx < r_thr.x + r_thr.w + 4 && my > r_thr.y - 20 && my < r_thr.y + r_thr.h + 4;
	wind_update(BEG_UPDATE);
	if (hide)
		graf_mouse(M_OFF, 0);
	wind_get(win_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		a = r_thr;
		if (rect_and(&a, &r)) {
			clip_on(&a);
			draw_throbber(&a);
		}
		wind_get(win_h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
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
#define RIB_PARTS 16
static struct {
	short end, pen;
} rib[RIB_PARTS];
static int nrib;
static short rib_at;			/* how far it has come */
static unsigned long rib_full;		/* when it filled (ms), 0: not full */

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
	short x0 = r_stat.x + 1, w = r_stat.w - 2, from = 0;
	int i;
	r.y = r_stat.y + r_stat.h - rib_rows();
	r.h = rib_rows();
	for (i = 0; i < nrib; i++) {
		r.x = x0 + (short) ((long) w * from / 1024);
		r.w = (short) ((long) w * rib[i].end / 1024) - (r.x - x0);
		if (r.w > 0)
			fill(&r, rib[i].pen);
		from = rib[i].end;
	}
	r.x = x0 + (short) ((long) w * from / 1024);
	r.w = x0 + w - r.x;
	if (r.w > 0)
		fill(&r, paper());
}

static void draw_status(GRECT *clip)
{
	GRECT r = r_stat;
	short cols = (r_stat.w - gl_wchar) / gl_wchar;
	if (!rect_and(&r, clip))
		return;
	fill(&r_stat, paper());
	hline(r_stat.x, r_stat.x + r_stat.w - 1, r_stat.y);
	if (info[0] && cols > 0)
		text_at(r_stat.x + gl_wchar / 2, r_stat.y + 2, info, (long) strlen(info), cols, 0);
	draw_ribbon();
}

static void redraw_ribbon(void)
{
	GRECT r = r_stat;
	r.y = r_stat.y + r_stat.h - rib_rows();
	r.h = rib_rows();
	redraw(&r, 1);
}

void win_progress(int phase, int at)
{
	if (phase < 0) {
		if (nrib == 0)
			return;
		nrib = 0;
		rib_at = 0;
		rib_full = 0;
	} else {
		if (at > 1024)
			at = 1024;
		if (at <= rib_at && nrib > 0)
			return;		/* it only goes on */
		if (nrib > 0 && rib[nrib - 1].pen == rib_pen(phase))
			rib[nrib - 1].end = (short) at;
		else if (nrib < RIB_PARTS) {
			rib[nrib].pen = rib_pen(phase);
			rib[nrib++].end = (short) at;
		} else {
			rib[nrib - 1].end = (short) at;
		}
		rib_at = (short) at;
		rib_full = at == 1024 ? pf_ms() | 1 : 0;
	}
	if (win_h > 0 && !win_iconified)
		redraw_ribbon();
}

int win_progress_shown(void)
{
	/* full for a moment, then gone */
	if (rib_full && pf_ms() - rib_full >= 800)
		win_progress(-1, 0);
	return nrib > 0;
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	int i;
	if (!rect_and(&r, clip))
		return;
	fill(&r_tool, paper());
	for (i = 0; i < NBUT; i++)
		draw_button(i, &r);
	draw_addr();
	draw_throbber(&r);
	hline(r_tool.x, r_tool.x + r_tool.w - 1, r_tool.y + r_tool.h - 1);
}

static void draw_bars(GRECT *clip)
{
	draw_toolbar(clip);
	draw_status(clip);
}

/* ---------------------------------------------------------------- drawing */

static void draw_page(GRECT *clip)
{
	GRECT r = r_page;
	if (!rect_and(&r, clip))
		return;
	scr_page(br.p, &r, left + (r.x - r_page.x), top + (r.y - r_page.y));
}

/* area of the window, through its rectangle list */
static void redraw(const GRECT *area, int parts)
{
	GRECT r, a;
	if (win_h <= 0)
		return;
	if (win_iconified) {
		icon_window(area);
		return;
	}
	a = area ? *area : work;
	if (!rect_and(&a, &work))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(win_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		GRECT c = r;
		if (rect_and(&c, &a)) {
			clip_on(&c);
			if (parts & 1)
				draw_bars(&c);
			if (parts & 2)
				draw_page(&c);
		}
		wind_get(win_h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
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
	redraw(&r_page, 2);
}

void win_toolbar(void)
{
	redraw(&r_tool, 1);
}

static void redraw_addr(void)
{
	redraw(&r_addr, 1);
}

/* ---------------------------------------------------------------- scrolling */

int32_t win_page_w(void)
{
	return r_page.w > 0 ? r_page.w : 1;
}

int32_t win_page_h(void)
{
	return r_page.h;
}

int32_t win_top(void)
{
	return top;
}

void win_page_area(short *x, short *y, short *w, short *h)
{
	*x = r_page.x;
	*y = r_page.y;
	*w = r_page.w;
	*h = r_page.h;
}

static int32_t max_top(void)
{
	int32_t h = br.p ? br.p->height : 0, m = h - r_page.h;
	return m > 0 ? m : 0;
}

/* how far it scrolls sideways */
static int32_t max_left(void)
{
	int32_t w = br.p ? br.p->right : 0, m = w - r_page.w;
	return m > 0 ? m : 0;
}

static void sliders(void)
{
	int32_t h = br.p ? br.p->height : 0, m = max_top(), w = br.p ? br.p->right : 0, mw = max_left();
	short size = (short) (h > r_page.h ? (int32_t) ((int64_t) 1000 * r_page.h / h) : 1000);
	short wsize = (short) (w > r_page.w ? (int32_t) ((int64_t) 1000 * r_page.w / w) : 1000);
	if (win_h <= 0 || win_iconified)
		return;
	wind_set(win_h, WF_VSLSIZE, size < 30 ? 30 : size, 0, 0, 0);
	wind_set(win_h, WF_VSLIDE, (short) (m > 0 ? (int32_t) ((int64_t) 1000 * top / m) : 0), 0, 0, 0);
	wind_set(win_h, WF_HSLSIZE, wsize < 30 ? 30 : wsize, 0, 0, 0);
	wind_set(win_h, WF_HSLIDE, (short) (mw > 0 ? (int32_t) ((int64_t) 1000 * left / mw) : 0), 0, 0, 0);
}

/* the window is in front and nothing covers it: one rectangle */
static int all_visible(void)
{
	GRECT r;
	short t, d;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (t != win_h)
		return 0;
	wind_get(win_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	return r.x == work.x && r.y == work.y && r.w == work.w && r.h == work.h;
}

void win_scroll_to(int32_t y)
{
	int32_t m = max_top(), dy;
	if (y > m)
		y = m;
	if (y < 0)
		y = 0;
	dy = y - top;
	if (dy == 0)
		return;
	top = y;
	br_scrolled(&br, top);
	sliders();
	if (win_iconified || win_h <= 0)
		return;
	if ((dy < 0 ? -dy : dy) < r_page.h && all_visible() &&
	    r_page.y + r_page.h <= scr_h && r_page.x + r_page.w <= scr_w) {
		/* what stays on the screen moves; only the new rows are drawn */
		MFDB s;
		short pxy[8], d = (short) (dy < 0 ? -dy : dy);
		GRECT fresh = r_page;
		memset(&s, 0, sizeof s);
		wind_update(BEG_UPDATE);
		graf_mouse(M_OFF, 0);
		clip_on(&r_page);
		pxy[0] = pxy[4] = r_page.x;
		pxy[2] = pxy[6] = r_page.x + r_page.w - 1;
		if (dy > 0) {
			pxy[1] = r_page.y + d;
			pxy[3] = r_page.y + r_page.h - 1;
			pxy[5] = r_page.y;
			pxy[7] = r_page.y + r_page.h - 1 - d;
			fresh.y = r_page.y + r_page.h - d;
		} else {
			pxy[1] = r_page.y;
			pxy[3] = r_page.y + r_page.h - 1 - d;
			pxy[5] = r_page.y + d;
			pxy[7] = r_page.y + r_page.h - 1;
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
	win_scroll_to(top + dy);
}

/* sideways, to column x: what stays on the screen moves, as up and down */
static void scroll_x_to(int32_t x)
{
	int32_t m = max_left(), dx;
	if (x > m)
		x = m;
	if (x < 0)
		x = 0;
	dx = x - left;
	if (dx == 0)
		return;
	left = x;
	sliders();
	log_event("sideways %ld of %ld", (long) left, (long) m);
	if (win_iconified || win_h <= 0)
		return;
	if ((dx < 0 ? -dx : dx) < r_page.w && all_visible() &&
	    r_page.y + r_page.h <= scr_h && r_page.x + r_page.w <= scr_w) {
		MFDB s;
		short pxy[8], d = (short) (dx < 0 ? -dx : dx);
		GRECT fresh = r_page;
		memset(&s, 0, sizeof s);
		wind_update(BEG_UPDATE);
		graf_mouse(M_OFF, 0);
		clip_on(&r_page);
		pxy[1] = pxy[5] = r_page.y;
		pxy[3] = pxy[7] = r_page.y + r_page.h - 1;
		if (dx > 0) {
			pxy[0] = r_page.x + d;
			pxy[2] = r_page.x + r_page.w - 1;
			pxy[4] = r_page.x;
			pxy[6] = r_page.x + r_page.w - 1 - d;
			fresh.x = r_page.x + r_page.w - d;
		} else {
			pxy[0] = r_page.x;
			pxy[2] = r_page.x + r_page.w - 1 - d;
			pxy[4] = r_page.x + d;
			pxy[6] = r_page.x + r_page.w - 1;
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
	GRECT r = r_page;
	int32_t a = y0 - top, b = y1 - top;
	if (win_h <= 0 || win_iconified || b <= 0 || a >= r_page.h)
		return;
	if (a < 0)
		a = 0;
	if (b > r_page.h)
		b = r_page.h;
	r.y = (short) (r_page.y + a);
	r.h = (short) (b - a);
	redraw(&r, 2);
}

/* the page laid out again (its pictures' sizes came): the same place on
   it, all drawn */
void win_page_relaid(void)
{
	win_page_relaid_at(top);
}

/* the page laid out again where it is (another text size): its place
   kept, within its new height and width; drawn by the caller */
void win_page_clamp(void)
{
	if (top > max_top())
		top = max_top();
	if (left > max_left())
		left = max_left();
	br_scrolled(&br, top);
	sliders();
}

void win_page_relaid_at(int32_t y)
{
	top = y;
	if (top > max_top())
		top = max_top();
	if (top < 0)
		top = 0;
	if (left > max_left())
		left = max_left();
	br_scrolled(&br, top);
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
	char t[sizeof title], a[140];
	if (br.title[0]) {
		atari_text(br.title, a, sizeof a);
		snprintf(t, sizeof t, " EWeb - %s ", a);
	} else {
		snprintf(t, sizeof t, " EWeb ");
	}
	if (strcmp(t, title) == 0)
		return;
	str_copy(title, t, sizeof title);
	if (win_h > 0)
		wind_set_str(win_h, WF_NAME, title);
}

static int at_rest;	/* the status bar says what the page says (win_status("")) */

void win_status(const char *s)
{
	char t[sizeof info];
	at_rest = !*s;
	if (*s)
		snprintf(t, sizeof t, " %s", s);
	else if (br_refresh_note(&br, t + 1, (int) sizeof t - 1))
		t[0] = ' ';	/* "Going on to example.com in 5 s: Esc stays here." */
	else if (br.security[0])
		snprintf(t, sizeof t, " Secure: %s", br.security);
	else
		t[0] = 0;
	if (strcmp(t, info) == 0)
		return;
	str_copy(info, t, sizeof info);
	/* what the bar says now, for the rigs (a link's address without its
	   query, 7.3) */
	log_event("status %.*s", (int) strcspn(t + (t[0] == ' '), "?"), t + (t[0] == ' '));
	if (win_h > 0 && !win_iconified) {
		GRECT r = r_stat;
		r.h -= rib_rows();
		redraw(&r, 1);
	}
}

void win_status_again(void)
{
	if (at_rest)
		win_status("");
}

/* ---------------------------------------------------------------- the address field */

static void addr_show_cursor(void)
{
	short cols = addr_cols(), n = (short) strlen(addr);
	if (a_cur > n)
		a_cur = n;
	if (a_cur < a_first)
		a_first = a_cur;
	if (a_cur > a_first + cols - 1)
		a_first = a_cur - cols + 1;
	if (a_first < 0)
		a_first = 0;
}

void win_address(const char *url)
{
	char a[sizeof addr];
	atari_text(url, a, sizeof a);
	str_copy(addr, a, sizeof addr);
	a_cur = (int) strlen(addr);
	a_first = 0;
	a_all = 0;
	a_focus = 0;
	if (win_h > 0)
		redraw_addr();
}

static void ctl_blur(void);

void win_address_focus(int all)
{
	ctl_blur();
	a_focus = 1;
	a_all = all;
	a_cur = (int) strlen(addr);
	addr_show_cursor();
	if (all)
		a_first = 0;
	redraw_addr();
}

int win_address_has_focus(void)
{
	return a_focus;
}

static void addr_blur(void)
{
	if (!a_focus)
		return;
	a_focus = 0;
	a_all = 0;
	a_first = 0;
	redraw_addr();
}

static void addr_insert(const char *s, long n)
{
	long len = (long) strlen(addr);
	if (a_all) {
		addr[0] = 0;
		len = 0;
		a_cur = 0;
		a_all = 0;
	}
	if (len + n >= (long) sizeof addr)
		n = (long) sizeof addr - 1 - len;
	if (n <= 0)
		return;
	memmove(addr + a_cur + n, addr + a_cur, len - a_cur + 1);
	memcpy(addr + a_cur, s, n);
	a_cur += (int) n;
}

/* a key for the address field: 1 when taken */
static int addr_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char c = KEY_ASCII(key);
	int len = (int) strlen(addr), shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	if (key_ctrl(kstate, key)) {
		switch (scan) {
		case 0x1e:		/* ^A: all of it */
			a_all = 1;
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
		char t[sizeof addr];
		long n;
		char *u = atari_to_utf8(addr, (long) strlen(addr), &n);
		str_copy(t, u ? u : addr, sizeof t);
		free(u);
		addr_blur();
		go(t);
		return 1;
	} else if (scan == 0x0f) {			/* Tab: the keyboard to the page */
		addr_blur();
		return 1;
	} else if (scan == 0x01) {			/* Esc: nothing (it never clears text) */
		return 1;
	} else if (scan == 0x4b) {			/* Left */
		if (a_all)
			a_cur = 0;
		else if (a_cur > 0)
			a_cur--;
		a_all = 0;
	} else if (scan == 0x4d) {			/* Right */
		if (a_all)
			a_cur = len;
		else if (a_cur < len)
			a_cur++;
		a_all = 0;
	} else if (scan == 0x47) {			/* ClrHome: the start, with Shift the end */
		a_cur = shift ? len : 0;
		a_all = 0;
	} else if (scan == 0x0e) {			/* Backspace */
		if (a_all) {
			addr[0] = 0;
			a_cur = 0;
			a_all = 0;
		} else if (a_cur > 0) {
			memmove(addr + a_cur - 1, addr + a_cur, len - a_cur + 1);
			a_cur--;
		}
	} else if (scan == 0x53) {			/* Delete */
		if (a_all) {
			addr[0] = 0;
			a_cur = 0;
			a_all = 0;
		} else if (a_cur < len) {
			memmove(addr + a_cur, addr + a_cur + 1, len - a_cur);
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

/* the control the keyboard is in (form.h; its state is the page's
   document), NULL: none */
static fctl *f_focus;

/* a control's place on the screen, its frame too */
static int ctl_rect(const fctl *c, GRECT *r)
{
	const dl_item *it;
	if (br.p == NULL || (it = page_ctl_item(br.p, c)) == NULL)
		return 0;
	r->x = (short) (r_page.x + it->x - left - 1);
	r->y = (short) (r_page.y + it->y - top - 1);
	r->w = (short) (it->w + 2);
	r->h = (short) (it->h + 2);
	return 1;
}

static void ctl_redraw(const fctl *c)
{
	GRECT r;
	if (ctl_rect(c, &r) && rect_and(&r, &r_page))
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
	if (it->y < top)
		win_scroll_to(it->y - 8);
	else if (it->y + it->h > top + r_page.h)
		win_scroll_to(it->y + it->h - r_page.h + 8);
	if (it->x < left)
		scroll_x_to(it->x - 8);
	else if (it->x + it->w > left + r_page.w)
		scroll_x_to(it->x + it->w - r_page.w + 8);
}

static int typed;		/* the field with the keyboard was typed in: change when it goes */

/* is c still one of the page's controls (a script may have taken it away) */
static int ctl_alive(const fctl *c)
{
	const fctl *k;
	for (k = br.d ? br.d->ctls : NULL; k != NULL; k = k->next)
		if (k == c)
			return 1;
	return 0;
}

void win_ctl_check(void)
{
	if (f_focus != NULL && !ctl_alive(f_focus)) {
		f_focus = NULL;
		typed = 0;
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
	fctl *c = f_focus;
	if (c == NULL)
		return;
	c->focus = 0;
	f_focus = NULL;
	ctl_redraw(c);
	if (typed) {
		typed = 0;
		ctl_event(c, "change");
	}
}

static void addr_blur(void);

static void ctl_focus(fctl *c)
{
	if (f_focus == c)
		return;
	ctl_blur();
	addr_blur();
	f_focus = c;
	typed = 0;
	c->focus = 1;
	ctl_show(c);
	ctl_redraw(c);
}

/* a new page: nothing of it has the keyboard (the page before may be
   gone; a page come back from Back may have been left with it) */
static void ctl_forget(void)
{
	fctl *c;
	f_focus = NULL;
	if (br.d != NULL)
		for (c = br.d->ctls; c != NULL; c = c->next)
			c->focus = 0;
}

/* a form sent: the page it goes to replaces this one */
static void ctl_submit(fctl *from, fctl *by, int32_t x, int32_t y)
{
	if (f_focus != NULL)
		f_focus->focus = 0;
	f_focus = NULL;
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
		form_check(br.d, c);
		ctl_redraw(c);
		ctl_events(c);
		break;
	case FC_RADIO:
		form_check(br.d, c);
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
			x = (int32_t) (mx - r_page.x + left - it->x) * 1024 / br.zoom;
			y = (int32_t) (my - r_page.y - (it->y - top)) * 1024 / br.zoom;
			if (x < 0)
				x = 0;
			if (y < 0)
				y = 0;
		}
		ctl_submit(NULL, c, x, y);
		break;
	}
	case FC_RESET:
		form_reset(br.d, c->form);
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
	fctl *c = f_focus;
	const dl_item *it = br.p ? page_ctl_item(br.p, c) : NULL;
	short scan = KEY_SCAN(key);
	unsigned char ch = KEY_ASCII(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0, typed = form_typed(c), changed = 0;

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
			n = form_next(br.d, n, shift ? -1 : 1);
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
		if (typed || c->kind == FC_CHECKBOX || c->kind == FC_RADIO) {
			/* the form sent, as by its first button */
			int send;
			fctl *by = form_default(br.d, c, &send);
			if (send)
				ctl_submit(c, by, 0, 0);
			return 1;
		}
		ctl_press(c, it, (short) (r_page.x + it->x - left + 4), (short) (r_page.y + it->y - top + it->h));
		return 1;
	case 0x39:		/* Space: a button pressed, a box ticked */
		if (!typed) {
			ctl_press(c, it, (short) (r_page.x + it->x - left + 4), (short) (r_page.y + it->y - top + it->h));
			return 1;
		}
		break;
	}
	if (!typed)
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
		typed = 1;
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
	if (f_focus != NULL && form_typed(f_focus)) {
		fctl *c = f_focus;
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
	if (a_focus) {
		long len = (long) strlen(addr);
		if (what == CLIP_COPY || what == CLIP_CUT) {
			clip_put(addr, len);
			if (what == CLIP_CUT) {
				addr[0] = 0;
				a_cur = 0;
				a_all = 0;
				redraw_addr();
			}
		} else if (what == CLIP_PASTE) {
			char buf[sizeof addr];
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
static struct {
	int on, found;			/* found: Edit > Find's words (marked yellow) */
	long ix[2];			/* -1: not known yet (while the mouse drags it) */
	unsigned long shows, serial;	/* on which page shown, in which layout of it */
	pg_pos at[2];			/* in that layout */
	pg_range r;
} sel;

#define SEL_RGB 0x99ccff		/* the selection's colour (Edit > Find's: yellow) */

static void sel_set(const pg_pos *from, const pg_pos *to, int found)
{
	sel.at[0] = *from;
	sel.at[1] = *to;
	sel.on = from->item != to->item || from->off != to->off;
	sel.found = found;
	sel.ix[0] = sel.ix[1] = -1;
	sel.shows = br.shows;
	sel.serial = br.p->serial;
	page_range(br.p, from, to, &sel.r);
}

/* its ends as places in the text, once it is made */
static void sel_keep(void)
{
	if (sel.on && sel.ix[0] < 0) {
		sel.ix[0] = page_text_index(br.p, &sel.at[0]);
		sel.ix[1] = page_text_index(br.p, &sel.at[1]);
	}
}

/* the selection on the page shown, in its layout now: 0 none */
static int sel_now(void)
{
	if (!sel.on || br.p == NULL || br.shows != sel.shows)
		return 0;
	if (br.p->serial != sel.serial) {
		int back = sel.ix[0] > sel.ix[1];
		if (sel.ix[0] < 0 || !page_text_place(br.p, sel.ix[0], back, &sel.at[0]) ||
		    !page_text_place(br.p, sel.ix[1], !back, &sel.at[1])) {
			sel.on = 0;
			return 0;
		}
		sel.serial = br.p->serial;
		page_range(br.p, &sel.at[0], &sel.at[1], &sel.r);
	}
	return 1;
}

/* the selection gone (a click on the page) */
static void sel_clear(void)
{
	if (sel_now()) {
		sel.on = 0;
		win_redraw_page();
	}
	sel.on = 0;
}

/* the selection marked on the screen (screen.c): 1, the stretch and its
   colour */
int win_mark(pg_range *r, uint32_t *rgb)
{
	if (!sel_now())
		return 0;
	*r = sel.r;
	*rgb = sel.found ? 0xffff00 : SEL_RGB;
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
	n = sel.ix[1] - sel.ix[0];
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
		from = sel.r.a;
		if (how > 0)
			from.off++;
	} else {
		/* the first run at or below the window's top */
		from.item = br.p->nitems;
		from.off = 0;
		for (i = 0; i < br.p->nitems; i++)
			if (br.p->items[i].type == DL_TEXT && br.p->items[i].y >= top) {
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
	if (it->y < top || it->y + it->h > top + r_page.h)
		win_scroll_to(it->y - r_page.h / 3);
	if (it->x < left || it->x + it->w > left + r_page.w)
		scroll_x_to(it->x - 8);
	win_redraw_page();
	snprintf(line, sizeof line, "%ld of %ld on this page.", nth, n);
	log_event("found %ld of %ld", nth, n);
	status(line);
}

/* ---------------------------------------------------------------- the mouse */

static int link_at(short mx, short my)
{
	if (br.p == NULL || !rect_hit(&r_page, mx, my))
		return -1;
	return page_link_at(br.p, mx - r_page.x + left, my - r_page.y + top);
}

int win_stop_hit(short mx, short my)
{
	return loading && !win_iconified && rect_hit(&but[B_STOP], mx, my);
}

static short pointer = ARROW;

/* the button under the mouse, framed (the others flat), its name on the
   info line (while nothing loads: then the line is the load's) */
static void hot_button(int b)
{
	int old = hot;
	if (b == hot)
		return;
	hot = b;
	if (!loading)
		win_status(b >= 0 ? names[b] : "");
	if (old >= 0)
		redraw(&but[old], 1);
	if (b >= 0)
		redraw(&but[b], 1);
}

void win_hover(short mx, short my)
{
	short h = wind_find(mx, my), t, d, want = ARROW;
	int l = -1, b = -1, i;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (h == win_h && t == win_h && !win_iconified)
		for (i = 0; i < NBUT; i++)
			if (rect_hit(&but[i], mx, my) && enabled(i))
				b = i;
	hot_button(b);
	if (h == win_h && t == win_h && !win_iconified && !loading) {
		fctl *c = br.p && rect_hit(&r_page, mx, my) ? page_ctl_at(br.p, mx - r_page.x + left, my - r_page.y + top, NULL)
							     : NULL;
		l = c == NULL ? link_at(mx, my) : -1;
		if (l >= 0)
			want = POINT_HAND;
		else if (rect_hit(&r_addr, mx, my) || (c != NULL && form_typed(c) && !c->disabled))
			want = TEXT_CRSR;
	}
	if (want != pointer) {
		graf_mouse(want, 0);
		pointer = want;
	}
	if (!loading) {
		static int shown = -2;
		if (l != shown) {
			char u[512], a[160];
			shown = l;
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
	sunk = i;
	redraw(&but[i], 1);
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 1))
			break;
		if (rect_hit(&but[i], mx, my) != in) {
			in = !in;
			sunk = in ? i : -1;
			hot = in ? -1 : i;
			redraw(&but[i], 1);
		}
		evnt_timer_(20);
	}
	sunk = -1;
	hot = rect_hit(&but[i], mx, my) ? i : -1;
	redraw(&but[i], 1);
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
	x0 = r_page.x + x - left;
	y0 = r_page.y + y - top;
	x1 = x0 + w;
	y1 = y0 + h;
	x0 = x0 < r_page.x ? r_page.x : x0;
	y0 = y0 < r_page.y ? r_page.y : y0;
	x1 = x1 > r_page.x + r_page.w ? r_page.x + r_page.w : x1;
	y1 = y1 > r_page.y + r_page.h ? r_page.y + r_page.h : y1;
	if (x0 >= x1 || y0 >= y1)
		return;
	r.x = (short) x0;
	r.y = (short) y0;
	r.w = (short) (x1 - x0);
	r.h = (short) (y1 - y0);
	redraw(&r, 2);
}

/* a click on a box's scroll bar (a box with overflow auto or scroll): before
   or after its thumb, the box a boxful on; on it, the thumb dragged */
static int box_bar(short mx, short my)
{
	const dl_item *bar;
	const struct pg_scroll *sc;
	int32_t px = mx - r_page.x + left, py = my - r_page.y + top, t0, t1, at, len, seen, cur, step;
	int k, axis;
	if (br.p == NULL || (bar = page_bar_at(br.p, px, py)) == NULL)
		return 0;
	k = (int) bar->len - 1;
	axis = bar->base;
	sc = &br.p->scr[k];
	page_bar_thumb(br.p, bar, &t0, &t1);
	at = axis ? px - bar->x : py - bar->y;
	len = axis ? bar->w : bar->h;
	seen = axis ? br.p->items[sc->clip].w : br.p->items[sc->clip].h;
	cur = axis ? sc->sx : sc->sy;
	if (at < t0 || at >= t1) {
		step = seen - gl_hchar;	/* (a line of it still to be seen) */
		if (step < gl_hchar)
			step = gl_hchar;
		box_to(k, axis, cur + (at < t0 ? -step : step));
		return 1;
	}
	{
		/* the thumb, where it was taken, follows the mouse */
		short x, y, mb, ks;
		int32_t grab = at - t0, room = len - (t1 - t0), reach = axis ? sc->mx : sc->my;
		for (;;) {
			graf_mkstate(&x, &y, &mb, &ks);
			if (!(mb & 1))
				break;
			at = (axis ? x - r_page.x + left - bar->x : y - r_page.y + top - bar->y) - grab;
			if (room > 0)
				box_to(k, axis, (int32_t) ((int64_t) at * reach / room));
			evnt_timer_(20);
		}
	}
	return 1;
}

/* right-click: the link's commands, or the page's (Back first, as
   EHypView's) */
static void context_menu(short mx, short my)
{
	int l = link_at(mx, my);
	if (l >= 0) {
		static const char *const items[] = { "Open the link", "Save the link's file...  Alt-click",
						     "Copy the link's address" };
		short r = popup(mx, my, items, 3);
		if (r == 0) {
			follow(l);
		} else if (r == 1) {
			save_link(l);
		} else if (r == 2) {
			char u[2048], a[2048];
			br_link_url(&br, l, u, sizeof u);
			atari_text(u, a, sizeof a);
			clip_put(a, (long) strlen(a));
		}
	} else if (rect_hit(&r_page, mx, my)) {
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
	y0 -= top;
	y1 -= top;
	if (y0 < 0)
		y0 = 0;
	if (y1 > r_page.h)
		y1 = r_page.h;
	if (y1 <= y0)
		return;
	g.x = r_page.x;
	g.w = r_page.w;
	g.y = (short) (r_page.y + y0);
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
	int32_t px = mx - r_page.x + left, py = my - r_page.y + top;
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
		log_event("selected a word: %ld", sel.ix[1] - sel.ix[0]);
		win_redraw_page();
		return 1;
	}
	whole = sel_now();	/* (an old selection shown: the page drawn again at the first move) */
	if ((kstate & (K_LSHIFT | K_RSHIFT)) && whole) {
		from = sel.at[0];
		to = sel.at[1];
		whole = sel.found;	/* (Find's yellow becomes the selection's colour) */
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
		sel.on = 0;
		from = to = at;
	}
	ctl_blur();		/* (^C then copies the selection, not a field's text) */
	for (;;) {
		int32_t step = 0;
		if (y < r_page.y)
			step = -line_step();
		else if (y >= r_page.y + r_page.h)
			step = line_step();
		if (step != 0)
			win_scroll_to(top + step);	/* (drawn again, the selection too) */
		if (x != lx || y != ly || step != 0) {
			lx = x;
			ly = y;
			px = (x < r_page.x ? r_page.x : x >= r_page.x + r_page.w ? r_page.x + r_page.w - 1 : x) - r_page.x + left;
			py = (y < r_page.y ? r_page.y : y >= r_page.y + r_page.h ? r_page.y + r_page.h - 1 : y) - r_page.y + top;
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
	if (sel.on)
		log_event("selected: %ld", sel.ix[1] > sel.ix[0] ? sel.ix[1] - sel.ix[0] : sel.ix[0] - sel.ix[1]);
	return 1;
}

int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	int i, l;
	if (win_h <= 0 || wind_find(mx, my) != win_h || win_iconified)
		return 0;
	for (i = 0; i < NBUT; i++) {
		if (!rect_hit(&but[i], mx, my))
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
	if (rect_hit(&r_addr, mx, my)) {
		if (button & 2) {
			static const char *const items[] = { "Copy        ^C", "Cut         ^X", "Paste       ^V",
							     "-", "Select all  ^A" };
			short r;
			if (!a_focus)
				win_address_focus(1);
			r = popup(mx, my, items, 5);
			if (r >= 0 && r <= 2)
				win_clip(r == 0 ? CLIP_COPY : r == 1 ? CLIP_CUT : CLIP_PASTE);
			else if (r == 4)
				win_address_focus(1);
			return 1;
		}
		if (!a_focus || clicks >= 2) {
			win_address_focus(1);
		} else {
			a_all = 0;
			a_cur = a_first + (mx - r_addr.x - gl_wchar / 2 + gl_wchar / 2) / gl_wchar;
			addr_show_cursor();
			redraw_addr();
		}
		return 1;
	}
	if (rect_hit(&r_page, mx, my)) {
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
		if (br.p != NULL && page_ctl_at(br.p, mx - r_page.x + left, my - r_page.y + top, &it) == NULL &&
		    select_text(mx, my, clicks, kstate))
			return 1;	/* text selected */
		/* the page's scripts first: a click event, which may say no to
		   what the click does, or change the page */
		if (!(kstate & 0x08) && page_click(mx - r_page.x + left, my - r_page.y + top))
			return 1;
		c = br.p ? page_ctl_at(br.p, mx - r_page.x + left, my - r_page.y + top, &it) : NULL;
		if (c != NULL && !loading) {
			/* a label's text stands for its control */
			int label = it->type == DL_TEXT;
			if (c->disabled || (label && (it = page_ctl_item(br.p, c)) == NULL))
				return 1;
			if (form_typed(c)) {
				ctl_focus(c);
				c->cur = label ? c->len : cv_ctl_hit(it, c, mx - r_page.x + left, my - r_page.y + top);
				ctl_redraw(c);
			} else {
				ctl_focus(c);
				if (label && c->kind == FC_SELECT)
					ctl_press(c, it, (short) (r_page.x + it->x - left + 4), (short) (r_page.y + it->y - top + it->h));
				else
					ctl_press(c, it, label ? (short) (r_page.x + it->x - left) : mx, label ? (short) (r_page.y + it->y - top) : my);
			}
			return 1;
		}
		ctl_blur();
		l = link_at(mx, my);
		if (l >= 0 && (kstate & 0x08))
			save_link(l);	/* Alternate-click (0x08): its file saved, as Safari's Option-click */
		else if (l >= 0)
			follow(l);
		else if (br.p != NULL && (det = page_summary_at(br.p, mx - r_page.x + left, my - r_page.y + top)) != NULL)
			details(det);	/* a details' summary: it opens or closes */
		return 1;
	}
	return 1;
}

/* ---------------------------------------------------------------- keys */

int win_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	int32_t page = r_page.h - line_step();
	if (win_h <= 0 || win_iconified)
		return 0;
	if (f_focus != NULL && f_focus->kind == FC_TEXTAREA && (scan == 0x48 || scan == 0x50) && !shift)
		return ctl_key(kstate, key);	/* a textarea's lines */
	/* up and down scroll the page wherever the keyboard is */
	switch (scan) {
	case 0x48:		/* Up */
		win_scroll_by(shift ? -page : -line_step());
		return 1;
	case 0x50:		/* Down */
		win_scroll_by(shift ? page : line_step());
		return 1;
	}
	if (a_focus)
		return addr_key(kstate, key);
	if (f_focus != NULL)
		return ctl_key(kstate, key);
	switch (scan) {
	case 0x4b:		/* Left, Right: a page wider than the window sideways */
	case 0x4d:
		scroll_x_to(left + (scan == 0x4b ? -1 : 1) * (shift ? r_page.w - col_step() : col_step()));
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

void win_page_changed(void)
{
	int32_t y = br.scroll_to >= 0 ? br.scroll_to : 0;
	ctl_forget();
	win_title();
	win_address(br.url);
	win_status("");
	top = 0;
	left = 0;
	if (y > max_top())
		y = max_top();
	top = y;
	br_scrolled(&br, top);
	sliders();
	win_redraw(NULL);
}

/* ---------------------------------------------------------------- the window */

static void get_work(void)
{
	wind_get(win_h, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	layout();
}

static void save_place(void)
{
	wind_get(win_h, WF_CURRXYWH, &opt.win_x, &opt.win_y, &opt.win_w, &opt.win_h);
}

/* the window's size changed: the page laid out for its width again */
static void resized(void)
{
	int32_t old_w = br.width;
	get_work();
	save_place();
	if (br.p != NULL && win_page_w() != old_w) {
		/* the same place in the text, as near as it can be; the busy
		   bee, as the page's sheets may be fetched for the new width */
		int32_t h0 = br.p->height;
		if (!loading)
			graf_mouse(BUSYBEE, 0);
		br_relayout(&br, win_page_w());
		if (!loading)
			graf_mouse(ARROW, 0);
		if (h0 > 0)
			top = (int32_t) ((int64_t) top * br.p->height / h0);
	}
	br.width = win_page_w();
	if (top > max_top())
		top = max_top();
	if (left > max_left())
		left = max_left();
	sliders();
}

void win_open(void)
{
	short x = opt.win_x, y = opt.win_y, w = opt.win_w, h = opt.win_h;
	if (win_h > 0) {
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		return;
	}
	if (w < 40 * gl_wchar || h < 12 * gl_hchar || x < 0 || y < desk_y ||
	    x + w > desk_x + desk_w + 4 || y + h > desk_y + desk_h + 4) {
		x = desk_x;
		y = desk_y;
		w = desk_w;
		h = desk_h;
	}
	win_h = wind_create(icon_kind(KIND), desk_x, desk_y, desk_w, desk_h);
	win_iconified = 0;
	if (win_h < 0) {
		alert(1, "[3][No more windows available.][ OK ]");
		return;
	}
	if (!title[0])
		str_copy(title, " EWeb ", sizeof title);
	wind_set_str(win_h, WF_NAME, title);
	wind_open(win_h, x, y, w, h);
	get_work();
	save_place();
	br.width = win_page_w();
	sliders();
}

void win_close(void)
{
	if (win_h <= 0)
		return;
	if (!win_iconified)
		save_place();
	wind_close(win_h);
	wind_delete(win_h);
	win_h = -1;
	scr_dev_free(thr_dev);		/* the throbber's frames */
	thr_dev = NULL;
	thr_made = 0;
}

void win_message(const short *msg)
{
	GRECT r;
	if (win_h <= 0 || msg[3] != win_h)
		return;
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
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_BOTTOMED:
		wind_set(win_h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (!loading)
			command(C_QUIT);
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (win_iconified)
			break;
		save_place();
		win_iconified = 1;
		wind_set(win_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_UNICONIFY:
		if (!win_iconified)
			break;
		wind_set(win_h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		win_iconified = 0;
		resized();
		win_redraw(NULL);
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		if (win_iconified || loading)
			break;
		wind_get(win_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(win_h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		if (c.x == f.x && c.y == f.y && c.w == f.w && c.h == f.h) {
			wind_get(win_h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			wind_set(win_h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
			wind_set(win_h, WF_CURRXYWH, f.x, f.y, f.w, f.h);
		}
		resized();
		win_redraw(NULL);
		break;
	}
	case WM_SIZED:
	case WM_MOVED: {
		short w = msg[6], h = msg[7];
		if (win_iconified) {
			wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		if (loading && msg[0] == WM_SIZED)
			break;		/* the page being laid out keeps its width */
		if (w < 40 * gl_wchar)
			w = 40 * gl_wchar;
		if (h < 12 * gl_hchar)
			h = 12 * gl_hchar;
		wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], w, h);
		resized();
		if (msg[0] == WM_SIZED)
			win_redraw(NULL);
		break;
	}
	case WM_ARROWED:
		if (loading)
			break;
		switch (msg[4]) {
		case WA_UPPAGE: win_scroll_by(-(r_page.h - line_step())); break;
		case WA_DNPAGE: win_scroll_by(r_page.h - line_step()); break;
		case WA_UPLINE: win_scroll_by(-line_step()); break;
		case WA_DNLINE: win_scroll_by(line_step()); break;
		case WA_LFPAGE: scroll_x_to(left - (r_page.w - col_step())); break;
		case WA_RTPAGE: scroll_x_to(left + (r_page.w - col_step())); break;
		case WA_LFLINE: scroll_x_to(left - col_step()); break;
		case WA_RTLINE: scroll_x_to(left + col_step()); break;
		}
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
