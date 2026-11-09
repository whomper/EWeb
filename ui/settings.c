/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.c - the family's Settings dialog (settings.h; the guidelines, 6.1): EMail 2.14's settings_sidebar,
 * show_page and settings_run, with the pages left to the app. Built with dlgbuild.c, run by dialog.c's dlg_do.
 */
#include <stdlib.h>
#include <string.h>
#include "gemui.h"
#include "dialog.h"
#include "popup.h"
#include "alert.h"
#include "settings.h"

#define SET_WMAX 16		/* pop-ups and buttons the app may add */
#define SET_FMAX 24		/* fields: their text is kept for Cancel */
#define SET_POPMAX 16		/* choices of a pop-up */
#define SET_NAME 31		/* characters of a choice shown */
#define SET_LAB 48		/* a pop-up button's text, w at most 46 */

enum { W_POPUP = 1, W_ACT, W_VALUE };

typedef struct {
	short obj, kind, w, n, was;
	short *sel;
	const char *const *ch;
	char *label;		/* what the button shows (w characters): lab for a pop-up, the app's for a value */
	SET_ACT act;
	void *user;
	char lab[SET_LAB];
} SET_W;

typedef struct {
	char *buf;
	short cap;		/* its bytes, the end not counted */
} SET_F;

struct SETTINGS {
	DLGB b;
	const SET_DEF *d;
	char band_text[40];		/* " Settings" */
	short sw, sx, pw, w, h, gap;
	short npages, cur, adding;
	short row0, rgap;		/* set_y: the page's first row, the pixels a row more */
	short ok, cancel, pages_obj;
	short tab[SET_MAXPAGES + 1];	/* from 1 */
	char tabname[SET_MAXPAGES][16];
	unsigned char *page;		/* the page of each object, 0: on every page */
	SET_W *wk;
	short nw;
	SET_F *fl;
	short nf;
	char *pool;			/* the fields' text when the dialog opened */
};

void (*settings_drawn)(OBJECT *tree, short band);
void (*settings_uncover)(short x, short y, short w, short h);

/* ---------------- what a page's builder calls ---------------- */

short set_y(const SETTINGS *s, short n)
{
	short px = (short)(2 + n * s->rgap);
	if (px > 127)
		px = 127;		/* the high byte is a signed number of pixels */
	return (short)((s->row0 + n) | (px << 8));
}

short set_text(SETTINGS *s, short x, short y, const char *text)
{
	return db_text(&s->b, x, y, text);
}

short set_tick(SETTINGS *s, short x, short y, const char *label, short *var)
{
	return db_tick(&s->b, x, y, label, var);
}

void set_tick_group(SETTINGS *s, short tick, short group)
{
	db_tick_group(&s->b, tick, group);
}

void set_tick_enable(SETTINGS *s, short tick, int on)
{
	db_tick_enable(&s->b, tick, on);
}

static void keep_field(SETTINGS *s, char *buf, short cap)
{
	if (s->nf >= SET_FMAX) {
		s->b.lost++;
		return;
	}
	s->fl[s->nf].buf = buf;
	s->fl[s->nf].cap = cap;
	s->nf++;
}

short set_field(SETTINGS *s, short x, short y, char *buf, short len, char kind)
{
	keep_field(s, buf, len);
	return db_field(&s->b, x, y, buf, len, kind);
}

short set_long(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask)
{
	keep_field(s, buf, cap);
	return db_framed(&s->b, db_long(&s->b, x, y, buf, len, cap, mask));
}

short set_long_bare(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask)
{
	keep_field(s, buf, cap);
	return db_long(&s->b, x, y, buf, len, cap, mask);
}

short set_frame(SETTINGS *s, short x, short y, short w, short rows)
{
	return db_add(&s->b, G_BOX, 0, 0, 0x00FF1100L, x, y, w, rows);
}

static SET_W *new_w(SETTINGS *s, short kind)
{
	SET_W *p;
	if (s->nw >= SET_WMAX) {
		s->b.lost++;
		return 0;
	}
	p = &s->wk[s->nw++];
	memset(p, 0, sizeof(*p));
	p->kind = kind;
	return p;
}

/* a widget that found no room (its object is the spare, which is not in the tree) is not kept: nothing may
   find it, a click on it or Cancel's object being the spare too would open it; 0 */
static short kept(SETTINGS *s, SET_W *p)
{
	if (db_spare(&s->b, p->obj)) {
		s->nw--;
		return 0;
	}
	return p->obj;
}

/* a pop-up button's text: a space, the choice (cut to leave one before the arrow), the system font's
   down arrow as the last character (5.7) */
static void pop_label(SET_W *p)
{
	short sel = *p->sel < 0 || *p->sel >= p->n ? 0 : *p->sel, n;
	const char *c = p->ch[sel];
	n = (short)strlen(c);
	if (n > p->w - 3)
		n = (short)(p->w - 3);
	memset(p->lab, ' ', (size_t)p->w);
	p->lab[p->w] = 0;
	memcpy(p->lab + 1, c, (size_t)n);
	p->lab[p->w - 1] = 2;
}

short set_popup(SETTINGS *s, short x, short y, short w, const char *const *choices, short n, short *sel)
{
	SET_W *p = new_w(s, W_POPUP);
	if (!p)
		return 0;
	if (w > SET_LAB - 2)
		w = SET_LAB - 2;
	if (n > SET_POPMAX)
		n = SET_POPMAX;
	p->w = w;
	p->n = n;
	p->sel = sel;
	p->was = *sel;
	p->ch = choices;
	pop_label(p);
	p->label = p->lab;
	p->obj = db_add(&s->b, G_BOXTEXT, TOUCHEXIT, 0, (long)db_ted(&s->b, p->lab, 0, 0x1180), x, y, w, 1);
	((TEDINFO *)s->b.tree[p->obj].ob_spec)->te_thickness = -1;	/* an outline (5.7) */
	return kept(s, p);
}

short set_button(SETTINGS *s, short x, short y, short w, const char *label, SET_ACT act, void *user)
{
	SET_W *p = new_w(s, W_ACT);
	if (!p)
		return 0;
	p->act = act;
	p->user = user;
	p->obj = db_button(&s->b, x, y, w, label, TOUCHEXIT);
	return kept(s, p);
}

short set_value(SETTINGS *s, short x, short y, short w, char *label, SET_ACT act, void *user)
{
	SET_W *p = new_w(s, W_VALUE);
	TEDINFO *t;
	if (!p)
		return 0;
	p->act = act;
	p->user = user;
	p->label = label;
	p->obj = db_add(&s->b, G_BOXTEXT, TOUCHEXIT, 0, (long)(t = db_ted(&s->b, label, 0, 0x1180)), x, y, w, 1);
	t->te_thickness = -1;
	if (t->te_txtlen < w + 1)
		t->te_txtlen = w + 1;
	return kept(s, p);
}

DLGB *set_db(SETTINGS *s)
{
	return &s->b;
}

OBJECT *set_tree(const SETTINGS *s)
{
	return s->b.tree;
}

short set_obj_tab(const SETTINGS *s, short k)
{
	return k >= 0 && k < s->npages ? s->tab[k + 1] : 0;
}

short set_obj_ok(const SETTINGS *s)
{
	return s->ok;
}

short set_obj_cancel(const SETTINGS *s)
{
	return s->cancel;
}

short set_obj_pages(const SETTINGS *s)
{
	return s->pages_obj;
}

short set_page_now(const SETTINGS *s)
{
	return (short)(s->cur - 1);
}

/* ---------------- the pages ---------------- */

/* page p (from 1): its objects shown, the others hidden (and not editable, so Tab skips them), the
   chosen name in a black bar, white words (never SELECTED: the 3D look draws that pressed in, 5.6) */
static void show_page(SETTINGS *s, short p, int draw)
{
	OBJECT *tree = s->b.tree;
	short i, old = s->cur;
	s->cur = p;
	for (i = 1; i < s->b.nobj; i++) {
		if (!s->page[i])
			continue;
		if (s->page[i] == p) {
			tree[i].ob_flags &= ~HIDETREE;
			if (tree[i].ob_type == G_FTEXT || tree[i].ob_type == G_FBOXTEXT)
				tree[i].ob_flags |= EDITABLE;
		} else {
			tree[i].ob_flags |= HIDETREE;
			tree[i].ob_flags &= ~EDITABLE;
		}
	}
	for (i = 1; i <= s->npages; i++) {
		tree[s->tab[i]].ob_state = 0;
		((TEDINFO *)tree[s->tab[i]].ob_spec)->te_color = i == p ? 0x1071 : 0x1180;
	}
	if (draw) {
		/* the two names that changed (the bar's old place and new), and the page: the other names are as they were */
		if (old >= 1 && old <= s->npages && old != p)
			dlg_draw(tree, s->tab[old]);
		dlg_draw(tree, s->tab[p]);
		dlg_draw(tree, s->pages_obj);
	}
}

void set_show(SETTINGS *s, short n)
{
	if (n >= 0 && n < s->npages && n + 1 != s->cur)
		show_page(s, (short)(n + 1), 1);
}

/* the tree: the band, the list, the pages, Cancel and OK (the guidelines' 6.1) */
static void build(SETTINGS *s)
{
	const SET_DEF *d = s->d;
	DLGB *b = &s->b;
	short k, from;
	b->hc = b->hc ? b->hc : 16;
	db_begin(b, s->w, s->h);
	s->gap = (short)(b->hc / 2);
	s->rgap = (short)(b->hc * 3 / 8);
	s->npages = d->npages;
	s->adding = 0;
	db_band(b, s->w, s->band_text);
	/* the list: plain words on white, the chosen one in a black bar, a line between it and the settings */
	db_add(b, G_BOX, 0, 0, 0x00001171L, s->sw, 1, (short)(0 | (1 << 8)), (short)(s->h - 1));
	for (k = 0; k < d->npages; k++) {
		short n = (short)strlen(d->pages[k].name);
		if (n > 11)
			n = 11;
		memcpy(s->tabname[k], "  ", 2);
		memcpy(s->tabname[k] + 2, d->pages[k].name, (size_t)n);
		s->tabname[k][2 + n] = 0;
		s->tab[k + 1] = db_add(b, G_BOXTEXT, TOUCHEXIT, 0, (long)db_ted(b, s->tabname[k], 0, 0x1180),
				       (short)(0 | (2 << 8)), (short)((2 + k * 2) | ((k * s->gap) << 8)),
				       (short)((s->sw - 1) | (6 << 8)), 1);
	}
	db_keep(b, 2);			/* Cancel and OK always have room */
	s->pages_obj = db_add(b, G_IBOX, 0, 0, 0, (short)(s->sx - 1), 2, s->pw, (short)(s->h - 4));
	for (k = 1; k <= d->npages; k++) {
		from = b->nobj;
		s->adding = k;
		db_text(b, s->sx, 2, d->pages[k - 1].name);
		db_add(b, G_BOX, 0, 0, 0x00001171L, s->sx, (short)(3 | (2 << 8)), (short)(s->pw - 2), (short)(0 | (1 << 8)));
		s->row0 = 4;
		if (d->pages[k - 1].build)
			d->pages[k - 1].build(s, s->sx, 4, d->user);
		for (; from < b->nobj; from++)
			s->page[from] = (unsigned char)k;
	}
	s->adding = 0;
	db_keep(b, 0);
	s->cancel = db_button(b, (short)(s->w - 24), (short)(s->h - 2), 10, "Cancel", EXIT);
	s->ok = db_button(b, (short)(s->w - 12), (short)(s->h - 2), 10, "OK", EXIT | DEFAULT);
	db_end(b);
}

/* ---------------- the dialog ---------------- */

/* where a pop-up goes to have its row `sel` over the button at y, as the AES's own pop-ups do (5.7): its
   rows are a line and 2 pixels high, from 2 pixels down, their text a pixel in */
static short over_button(const SETTINGS *s, short y, short sel)
{
	return (short)(y - 3 - sel * (s->b.hc + 2));
}

/* an object of the dialog drawn again, or all of it where the screen under a pop-up wasn't kept */
static void drawn_again(SETTINGS *s)
{
	if (settings_drawn)
		settings_drawn(s->b.tree, s->b.band);
}

static void pop_run(SETTINGS *s, SET_W *p)
{
	char name[SET_POPMAX][SET_NAME + 3];
	const char *lab[SET_POPMAX];
	short i, k, x, y, r, sel = *p->sel < 0 || *p->sel >= p->n ? 0 : *p->sel;
	OBJECT *tree = s->b.tree;
	for (i = 0; i < p->n; i++) {
		short n = (short)strlen(p->ch[i]);
		if (n > SET_NAME)
			n = SET_NAME;
		/* popup_m's labels: the words, the ticked one with the check mark in front (popup.h) */
		k = 0;
		if (i == *p->sel)
			name[i][k++] = '\x08';
		memcpy(name[i] + k, p->ch[i], (size_t)n);
		name[i][k + n] = 0;
		lab[i] = name[i];
	}
	objc_offset(tree, p->obj, &x, &y);
	r = popup_m(x, over_button(s, y, sel), lab, p->n);
	tree[p->obj].ob_state &= ~SELECTED;
	if (!popup_put_back && settings_uncover)
		settings_uncover(popup_area.x, popup_area.y, popup_area.w, popup_area.h);
	if (r >= 0 && r < p->n) {
		*p->sel = r;
		pop_label(p);
	}
	if (dlg_in_window() || popup_put_back) {
		dlg_draw(tree, p->obj);		/* the screen under the pop-up is back, or the AES draws it */
	} else {
		dlg_draw(tree, 0);		/* it wasn't kept (no memory): the dialog again */
		drawn_again(s);
	}
}

/* a button's work: the app's dialog or list over ours, then ours again over what it covered */
static void act_run(SETTINGS *s, SET_W *p)
{
	OBJECT *tree = s->b.tree;
	short x, y, w, h;
	tree[p->obj].ob_state &= ~SELECTED;
	if (p->act)
		p->act(s, p->user);
	tree[p->obj].ob_state &= ~SELECTED;
	if (!dlg_in_window() && settings_uncover) {
		/* what the app's file selector or list covered, whatever its size: the app's windows over the whole
		   desktop (EMail's ui_redraws), then ours on top */
		wind_get(0, WF_WORKXYWH, &x, &y, &w, &h);
		settings_uncover(x, y, w, h);
	}
	dlg_draw(tree, 0);
	drawn_again(s);
}

static SET_W *widget_of(SETTINGS *s, short obj)
{
	short i;
	for (i = 0; i < s->nw; i++)
		if (s->wk[i].obj == obj)
			return &s->wk[i];
	return 0;
}

/* Cancel: everything as it was when the dialog opened */
static void revert(SETTINGS *s)
{
	short i;
	char *q = s->pool;
	for (i = 0; i < s->b.nticks; i++)
		*s->b.tick[i].var = s->b.tick[i].was;
	for (i = 0; i < s->nw; i++)
		if (s->wk[i].kind == W_POPUP)
			*s->wk[i].sel = s->wk[i].was;
	for (i = 0; i < s->nf; i++) {
		memcpy(s->fl[i].buf, q, (size_t)s->fl[i].cap + 1);
		q += s->fl[i].cap + 1;
	}
}

int settings_dialog(const SET_DEF *d)
{
	SETTINGS S, *s = &S;
	short omax = d->max_objects ? d->max_objects : SET_OBJECTS, k, x, y, w, h, r, nt;
	long pool = 0;
	int ok = 0;
	char *q;
	if (d->npages < 1 || d->npages > SET_MAXPAGES)
		return 0;
	memset(s, 0, sizeof(S));
	s->d = d;
	s->sw = d->list_w ? d->list_w : SET_SW;
	s->sx = (short)(s->sw + 2);
	s->pw = d->page_w ? d->page_w : SET_PW;
	s->w = (short)(s->sx + s->pw);
	s->h = d->height >= 8 ? d->height : 17;
	nt = (short)(omax / 4 + 8);
	if (!db_alloc(&s->b, omax, nt, 32, 12,
		      (long)omax + (long)SET_WMAX * sizeof(SET_W) + (long)SET_FMAX * sizeof(SET_F))) {
		alert(1, "[1][Not enough memory for|the settings.][ OK ]");
		return 0;
	}
	s->page = (unsigned char *)s->b.extra;
	s->wk = (SET_W *)(((long)s->page + omax + 7) & ~7L);
	s->fl = (SET_F *)((char *)s->wk + (long)SET_WMAX * sizeof(SET_W));
	s->band_text[0] = ' ';
	strncpy(s->band_text + 1, d->title ? d->title : "Settings", sizeof(s->band_text) - 2);
	build(s);
	/* what Cancel puts back */
	for (k = 0; k < s->nf; k++)
		pool += s->fl[k].cap + 1;
	s->pool = malloc((size_t)pool + 1);
	if (!s->pool) {
		db_free(&s->b);
		alert(1, "[1][Not enough memory for|the settings.][ OK ]");
		return 0;
	}
	for (q = s->pool, k = 0; k < s->nf; k++) {
		memcpy(q, s->fl[k].buf, (size_t)s->fl[k].cap + 1);
		q += s->fl[k].cap + 1;
	}
	show_page(s, (short)(d->first >= 0 && d->first < d->npages ? d->first + 1 : 1), 0);

	dlg_name(d->title ? d->title : "Settings");
	dlg_band(s->b.band);		/* in a window its title says it (3.5) */
	dlg_open(s->b.tree, &x, &y, &w, &h);
	drawn_again(s);
	for (;;) {
		SET_W *p;
		r = dlg_do(s->b.tree, 0, s->cancel) & 0x7fff;
		k = db_tick_hit(&s->b, r);
		if (k >= 0) {
			if (!(s->b.tree[s->b.tick[k].box].ob_state & DISABLED)) {
				db_tick_flip(&s->b, k);
				evnt_timer_(120);
			}
			continue;
		}
		for (k = 1; k <= s->npages; k++)
			if (r == s->tab[k])
				break;
		if (k <= s->npages) {
			if (k != s->cur)
				show_page(s, k, 1);
			continue;
		}
		p = widget_of(s, r);
		if (p) {
			if (p->kind == W_POPUP)
				pop_run(s, p);
			else
				act_run(s, p);
			continue;
		}
		if (r == s->ok) {
			if (d->check && !d->check(s, d->user)) {
				s->b.tree[r].ob_state &= ~SELECTED;
				dlg_draw(s->b.tree, r);
				continue;
			}
			ok = 1;
			break;
		}
		if (r == s->cancel)
			break;
		/* anything else (an object that found no room, the root): nothing to do */
		s->b.tree[r].ob_state &= ~SELECTED;
	}
	dlg_close();
	if (!ok)
		revert(s);
	free(s->pool);
	db_free(&s->b);
	return ok;
}

int settings_run(const char *title, const SET_PAGE *pages, short npages, short page_width, short height,
		 void *user)
{
	SET_DEF d;
	memset(&d, 0, sizeof(d));
	d.title = title;
	d.pages = pages;
	d.npages = npages;
	d.page_w = page_width;
	d.height = height;
	d.user = user;
	return settings_dialog(&d);
}
