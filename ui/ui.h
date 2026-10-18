/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - EWeb's GEM layer: one window, as a browser has it, with a
 * toolbar (Back, Forward, Reload or Stop, Home and the address) over the
 * page, the status on the window's info line, and the family's menu bar.
 * The settings say too whether the DSP may do the handshake's arithmetic
 * (common/ui/dsp.c and dsp_ec.c, which EWGET.TTP uses as well).
 */
#ifndef UI_H
#define UI_H

typedef struct {
	int falcon;			/* TLS on the Atari itself (always, in EWeb) */
	int dsp;			/* RSA and elliptic curves on the DSP when there is one */
	/* EWEB.INF (the program's): */
	char home[512];			/* HOME=: the start page; "" EWeb's own */
	char search[256];		/* SEARCH=: an address with %s for the words typed */
	short zoom;			/* ZOOM=: the page's size, per cent of 80 (the Falcon's pixels to CSS's) */
	short win_x, win_y, win_w, win_h;	/* WINDOW=: where the window was */
	char workdir[200];		/* where EWEB.PRG is: EWEB.FNT, EWEB.INF, the certificates */
} EWOPT;
extern EWOPT opt;

/* common/ui/dsp.c, common/ui/dsp_ec.c */
void falcon_dsp_init(void);
const char *falcon_dsp_state(void);	/* "DSP" or "68030" */
void falcon_dsp_ec_init(void);
const char *falcon_dsp_ec_state(void);

#ifdef EW_GEM
/* ---------------------------------------------------------------- the GEM program, EWEB.PRG */

#include "tos.h"
#include "gem.h"
#include "browse.h"

typedef struct { short x, y, w, h; } GRECT;

/* the screen and the system font */
extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar;
extern short ap_id;
extern browser br;		/* the page shown, its history */
extern int loading;		/* a page is on its way: Esc and Stop stop it */

/* drawing (draw.c): the window's parts, menus and dialogs, in the system
   font (what common/ui's About box, alerts and pop-ups take, gemui.h) */
#define TX_BOLD     1
#define TX_INVERSE  2		/* white text */
#define TX_RIGHT    4		/* right-to-left text against the right edge */
#define TX_LTR     16		/* left to right even when it starts in Hebrew */
#define TX_DIM    128		/* what can't be used: grey, else light */
void fill(GRECT *r, short color);
void frame(GRECT *r, short color);
void hline(short x1, short x2, short y);
void line_col(short x1, short y1, short x2, short y2, short color);
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void clip_on(GRECT *r);
void clip_off(void);
void font_sys_push(void);
void font_sys_pop(void);
void *st_ram(long n);		/* a bitmap for the blitter (the About box's icon), n <= 512 */
int rect_and(GRECT *a, const GRECT *b);	/* a = a & b; 0 when nothing is left */
int rect_hit(const GRECT *r, short x, short y);
short grey_pen(void);		/* light grey where there is one, else 0 */
/* UTF-8 to the Atari character set, Hebrew laid out for the screen: for
   the window's title and info line, which GEM draws as they are */
void atari_text(const char *utf8, char *out, int size);

/* the page on the screen (screen.c): the display list drawn into memory
   (src/gfx) and copied to the screen through ST RAM, in every mode from
   monochrome to the Falcon's true colour */
int scr_init(void);			/* 0: a screen EWeb can't draw on */
const char *scr_mode(void);		/* "16 colours", "true colour", for the log */
/* the page's part that falls on area (on the screen), the page's point
   (px, py) at area's top left; area must be within the clip set */
void scr_page(const page *p, const GRECT *area, int32_t px, int32_t py);

/* the window (win.c) */
extern short win_h;			/* AES handle, -1 when closed */
extern short win_iconified;
void win_open(void);
void win_close(void);
void win_message(const short *msg);	/* WM_* */
void win_redraw(const GRECT *area);	/* NULL: all of it */
void win_redraw_page(void);
void win_page_changed(void);		/* a new page: title, scroll, toolbar, everything drawn */
void win_title(void);
void win_status(const char *s);		/* the info line; "" for what the page says */
void win_toolbar(void);			/* the buttons again (loading started or ended) */
int  win_mouse(short mx, short my, short button, short clicks, short kstate);	/* 1: ours */
void win_hover(short mx, short my);	/* a link's address on the info line, the pointing hand */
int  win_key(short kstate, short key);	/* 1: taken (the address field, scrolling) */
void win_scroll_to(int32_t y);
void win_scroll_by(int32_t dy);
int32_t win_top(void);
int32_t win_page_w(void);		/* the page's width, for the layout */
void win_page_area(short *x, short *y, short *w, short *h);	/* where the page is on the screen */
int32_t win_page_h(void);
void win_address(const char *url);	/* the address field shows this */
void win_address_focus(int all);	/* ^L: the keyboard to the address, all of it chosen */
int  win_address_has_focus(void);
int  win_stop_hit(short mx, short my);	/* on Stop, while loading */
void win_clip(int what);		/* CLIP_COPY...: the address field, or the page's text */

/* main.c */
enum {
	C_NONE, C_ABOUT,
	C_OPENURL, C_OPENFILE, C_RELOAD, C_STOP, C_QUIT,
	C_CUT, C_COPY, C_PASTE, C_SELALL,
	C_BACK, C_FORWARD, C_HOME, C_LARGER, C_SMALLER, C_ACTUAL,
	C_SETHOME,
	C_HELP, C_KEYS
};
void command(int cmd);
void go(const char *typed);		/* an address typed, a file dropped */
void follow(int link);			/* a link clicked */
void menu_update(void);
void status(const char *s);
int  settings_save(void);

/* about.c */
void dlg_about(void);
void dlg_keys(void);
void icon_window(const GRECT *area);	/* the iconified window (area NULL: all) */

#include "alert.h"
#include "popup.h"

#define KEY_SCAN(k)  (((unsigned short) (k)) >> 8)
#define KEY_ASCII(k) ((unsigned char) ((k) & 0xff))
#endif

#endif
