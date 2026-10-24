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
	int falcon;			/* FALCON=: a Direct connection, TLS on this Atari (never in the ST edition) */
	int dsp;			/* RSA and elliptic curves on the DSP when there is one */
	/* EWEB.INF (the program's): */
	char home[512];			/* HOME=: the start page; "" EWeb's own */
	char search[256];		/* SEARCH=: an address with %s for the words typed */
	short zoom;			/* ZOOM=: the page's size, per cent of 80 (the Falcon's pixels to CSS's) */
	short win_x, win_y, win_w, win_h;	/* WINDOW=: where the window was */
	char workdir[200];		/* where EWEB.PRG is: EWEB.FNT, EWEB.INF, the certificates */
	short pictures;			/* PICTURES=: shown (1), or only their alt text (0) */
	char gate[64];			/* GATE=: the Pi gateway (AERA-Gateway's webgate), "" none */
	unsigned short gateport;	/* GATEPORT=: its port, 8280 */
	short snapshot;			/* SNAPSHOT=: pages through the Pi's browser (webgate's snapshots) */
	char scripts[512];		/* SCRIPTS=: the sites whose scripts run, host names ("" none) */
	short cache;			/* CACHE=: the disk cache's size, MB (0 none; -1: 8 with TT RAM, else 2) */
	short picdsp;			/* PICDSP=: JPEGs and the dither on the DSP (EWEB.PRG) */
	short pens;			/* PENS=: pictures as the Pi's pens: 1 always (as it starts), -1 when quicker, 0 never */
	short palette;			/* PALETTE=: in 256 colours, EWeb's own (1) or the system's (0) */
	short runscripts;		/* RUNSCRIPTS=: pages' scripts run on no site (0), the sites in
					   SCRIPTS= (1), every site but those in NOSCRIPTS= (2) */
	char noscripts[512];		/* NOSCRIPTS=: the sites left out when every site's run */
} EWOPT;
extern EWOPT opt;
#define CACHE_MAX_MB	64	/* the most CACHE= may ask for */
long cache_mb(void);		/* the disk cache's size now, MB */

/* common/ui/dsp.c, common/ui/dsp_ec.c */
void falcon_dsp_init(void);
const char *falcon_dsp_state(void);	/* "DSP" or "68030" */
void falcon_dsp_ec_init(void);
const char *falcon_dsp_ec_state(void);
void pictures_dsp_init(void);		/* main.c: pic_dsp, from PICDSP= */

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
/* what happened, for the Hatari rigs (main.c): an "EWEB ..." line through NatFeats */
void log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

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
void scr_palette(void);			/* PALETTE= changed: EWeb's colours set or TOS's back */
int scr_exit(void);			/* TOS's colours back (at the end): 1 if EWeb's were on */
const char *scr_mode(void);		/* "16 colours", "true colour", for the log */
int scr_rows(void);			/* rows drawn at a time: the screen's, else 32 */
/* the page's part that falls on area (on the screen), the page's point
   (px, py) at area's top left; area must be within the clip set */
void scr_page(const page *p, const GRECT *area, int32_t px, int32_t py);
/* a picture of w x h, each pixel a byte into col (0xRRGGBB, ncol of them),
   at at; the part of it on part (within at and the clip set) */
void scr_rgb(const unsigned char *pix, const unsigned long *col, short ncol, short w, short h, const GRECT *at,
	     const GRECT *part);
/* the same made once into the screen's own form (dev: scr_dev_size bytes
   of ST RAM from scr_dev_alloc), then put on the screen from there */
long scr_dev_size(short w, short h);
void *scr_dev_alloc(long n);
void scr_dev_free(void *p);
int scr_rgb_dev(const unsigned char *pix, const unsigned long *col, short ncol, short w, short h, void *dev);
void scr_blit_dev(void *dev, short w, short h, const GRECT *at, const GRECT *part);
/* a picture with holes (its colour 0: what is under it shows), drawn the
   same ways: scr_holes, or made once with its mask (dev: twice
   scr_dev_size) and drawn from there */
void scr_holes(const unsigned char *pix, const unsigned long *col, short n, short w, short h, const GRECT *at,
	       const GRECT *part);
int scr_holes_dev(const unsigned char *pix, const unsigned long *col, short n, short w, short h, void *dev);
void scr_blit_holes(void *dev, short w, short h, const GRECT *at, const GRECT *part);

/* the window (win.c) */
extern short win_h;			/* AES handle, -1 when closed */
extern short win_iconified;
void win_open(void);
void win_close(void);
void win_message(const short *msg);	/* WM_* */
void win_redraw(const GRECT *area);	/* NULL: all of it */
void win_redraw_page(void);
void win_preview(void);			/* the page loading's preview (br.pv_p): drawn from its top */
void win_preview_gone(void);		/* and gone, the load stopped: the page as it was */
void win_page_changed(void);		/* a new page: title, scroll, toolbar, everything drawn */
void win_page_drawn(int32_t y0, int32_t y1);	/* a picture came: that part of the page drawn */
void win_page_relaid(void);		/* laid out again for its pictures: the same place, drawn */
void win_page_relaid_at(int32_t y);	/* laid out again: at y on it, drawn */
void win_page_clamp(void);		/* laid out again where it is: the place kept within it */
void win_title(void);
/* the status bar's text; "" for what the page says: its refresh while
   that waits (the time left), or that it stopped refreshes at once,
   else its security. Each change is logged ("status ...") */
void win_status(const char *s);
void win_status_again(void);		/* what the page says, again if the bar says it (the time left) */
/* the status bar's ribbon: a load has come so far (0..1024) in a phase
   (BRPH_*: its colour); phase -1 clears it */
void win_progress(int phase, int at);
int win_progress_shown(void);		/* it is shown (and gone a moment after it filled) */
void win_toolbar(void);			/* the buttons again (loading started or ended) */
void win_colours(void);			/* the screen's colours changed: the toolbar's pictures made again */
void win_throb(int busy);		/* the throbber's next frame while busy (each 110 ms), else at rest */
int  win_throb_frames(void);		/* frames drawn since the last call, for the log */
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
void win_find(int how);			/* Edit > Find (0), Find next (1), Find previous (-1) */
int  win_mark(pg_range *r, uint32_t *rgb);	/* the selection, to mark (screen.c): 1, where and its colour */
void win_select_all(void);		/* Edit > Select all, on the page */

/* main.c */
enum {
	C_NONE, C_ABOUT,
	C_OPENURL, C_OPENFILE, C_SAVE, C_RELOAD, C_STOP, C_QUIT,
	C_CUT, C_COPY, C_PASTE, C_SELALL,
	C_BACK, C_FORWARD, C_HOME, C_SEARCH, C_SNAPSHOT, C_LIVE, C_LARGER, C_SMALLER, C_ACTUAL, C_ENCODING,
	C_PICTURES,
	C_SETTINGS, C_FALCON, C_SCRIPTS, C_SETHOME, C_COOKIES, C_CACHE, C_HISTORY, C_FORGETHG,
	C_HELP, C_KEYS,
	C_BMADD, C_BMSHOW, C_FIND, C_FINDNEXT, C_FINDPREV,
	C_BM			/* C_BM + i: the Bookmarks menu's ith bookmark (BM_MENU of them) */
};
void command(int cmd);
int  falcon_mode_set(int on);		/* 0 (and an alert why) when it can't */

/* dialogs.c */
int  dlg_settings(void);		/* 1: changed (in opt) */
/* a page's prompt(): its question (UTF-8), the answer in answer (UTF-8,
   the page's suggestion first); 1 OK, 0 Cancel */
int  dlg_prompt(const char *text, char *answer, int size);
/* Edit > Find: the words to find (UTF-8, the last ones first); 1 OK */
int  dlg_find(char *words, int size);
void go(const char *typed);		/* an address typed, a file dropped */
void follow(int link);			/* a link clicked */
void save_link(int link);		/* its file saved (Alternate-click, the link's menu) */
struct fctl;
/* a form sent (form.h): by the button by, or from the field from with
   Return; (x, y) on a picture button */
void submit(struct fctl *from, struct fctl *by, int32_t x, int32_t y);
void details(dnode *det);		/* a details' summary clicked: opened or closed */
void menu_update(void);
void status(const char *s);
/* the page's scripts (src/js): a click at (x, y) on the page, before
   its default (1: done, a script said no or loaded a page); an event at
   a form control ("input", "change") */
int  page_click(int32_t x, int32_t y);
void win_ctl_check(void);		/* after a script: the control with the keyboard may be gone */
void ctl_event(struct fctl *c, const char *type);
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
