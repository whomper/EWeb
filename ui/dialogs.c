/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialogs.c - EWeb's Settings dialog (^,), built in code with EMail's and
 * EFTP's builder: a black title band, the pages in a list on the left (the
 * chosen one in a black bar), that page's settings on the right: ticks,
 * framed fields. The Secure page is the family's: a Direct connection (TLS
 * on this Atari) or the Pi gateway's address and port (the guidelines, 6.1
 * and 7.4).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "charset.h"
#include "dialog.h"
#include "field.h"
#include "http.h"
#include "tls.h"
#include "util.h"

#define DMAX 96		/* objects: Settings' three pages take about 60 */
#define TMAX 16

static OBJECT tree[DMAX];
static TEDINFO ted[TMAX];
static char tmpl[TMAX][64], valid[TMAX][64];
static short nobj, nted, last_child;
/* the long fields (d_lfield), put on once the tree is fixed */
static LFIELD *lf_pend[3];
static char *lf_text[3];
static short lf_obj[3], lf_cap[3], nlf;

/* ---------------------------------------------------------------- the builder */

static void d_begin(short w, short h)
{
	lf_forget(tree);	/* the last dialog's long fields */
	memset(tree, 0, sizeof tree);
	nlf = 0;
	nobj = 1;
	nted = 0;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

static short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i;
	OBJECT *o;
	if (nobj >= DMAX) {
		/* (a dialog that has grown past the table: what is left out is
		   missing, nothing else spoilt; the rigs see the log) */
		log_event("dialog too big");
		return 0;
	}
	i = nobj++;
	o = &tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		tree[last_child].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static short d_text(short x, short y, const char *s)
{
	return d_add(G_STRING, 0, 0, (long) s, x, y, (short) strlen(s), 1);
}

static TEDINFO *d_ted(const char *text, short just, short color)
{
	TEDINFO *t = &ted[nted < TMAX ? nted++ : TMAX - 1];
	t->te_ptext = (char *) text;
	t->te_ptmplt = "";
	t->te_pvalid = "";
	t->te_font = 3;
	t->te_just = just;
	t->te_color = color;
	t->te_thickness = 0;
	t->te_txtlen = (short) strlen(text) + 1;
	t->te_tmplen = 1;
	return t;
}

/* a field in a thin frame showing len characters of buf (buf holds len+1);
   kind: 'X' any character, '9' digits */
static short d_field(short x, short y, char *buf, short len, char kind)
{
	TEDINFO *t = &ted[nted];
	short i;
	if (nted >= TMAX) {
		log_event("dialog too big");
		return 0;
	}
	if (len > 60)
		len = 60;
	for (i = 0; i < len; i++) {
		tmpl[nted][i] = '_';
		valid[nted][i] = kind;
	}
	tmpl[nted][len] = valid[nted][len] = 0;
	buf[len] = 0;
	t->te_ptext = buf;
	t->te_ptmplt = tmpl[nted];
	t->te_pvalid = valid[nted];
	t->te_font = 3;
	t->te_just = 0;
	t->te_color = 0x1180;
	t->te_thickness = -1;
	t->te_txtlen = len + 1;
	t->te_tmplen = len + 1;
	nted++;
	return d_add(G_FBOXTEXT, EDITABLE, 0, (long) t, x, y, len, 1);
}

/* a field that holds more than it shows (the guidelines' 5.4): text
   (cap + 1 bytes) scrolls in a window of len characters */
static short d_lfield(short x, short y, LFIELD *f, char *text, short len, short cap)
{
	static char view[3][LF_VIEW + 1];
	short obj = d_field(x, y, view[nlf], len, 'X');
	lf_pend[nlf] = f;
	lf_text[nlf] = text;
	lf_obj[nlf] = obj;
	lf_cap[nlf++] = cap;
	return obj;
}

static short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long) s, x, y, w, 1);
}

/* black band across the top, white title: its object (in a window the
   window's title bar names the dialog instead: dlg_band, 3.5) */
static short d_band(short w, const char *title)
{
	return d_add(G_BOXTEXT, 0, 0, (long) d_ted(title, 0, 0x1071), 0, 0, w, 1);
}

static void d_end(void)
{
	short i;
	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(tree, i);
	for (i = 0; i < nlf; i++)
		lf_init(lf_pend[i], tree, lf_obj[i], lf_text[i], lf_cap[i], 0);
	nlf = 0;
	dlg_3d(tree);		/* the AES's 3D look where it has one (5.6) */
}

/* ---------------------------------------------------------------- ticks */

/* a tick box, 16 pixels square, and the tick in it */
static const char *const tick_art[16] = {
	"................",
	".##############.",
	".#............#.",
	".#..........#.#.",
	".#.........##.#.",
	".#........##..#.",
	".#.......##...#.",
	".#.##...##....#.",
	".#.###.##.....#.",
	".#..####......#.",
	".#...##.......#.",
	".#............#.",
	".#............#.",
	".#............#.",
	".##############.",
	"................",
};
static unsigned short tick_on[16], tick_off[16];
static BITBLK tick_on_blk, tick_off_blk;

static void make_ticks(void)
{
	/* half height on 8-pixel screens: every other line, and the frame */
	short tall = gl_hchar >= 16, rows = tall ? 16 : 8, r, c;
	for (r = 0; r < rows; r++) {
		const char *src = tick_art[tall ? r : (r == rows - 1 ? 14 : r * 2 + (r ? 0 : 1))];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (unsigned short) ((v << 1) | (src[c] == '#'));
		tick_on[r] = v;
		tick_off[r] = tall ? (r == 1 || r == 14 ? v : (r == 0 || r == 15 ? 0 : 0x4002)) :
				     (r == 0 || r == rows - 1 ? v : 0x4002);
	}
	tick_on_blk.bi_pdata = (short *) tick_on;
	tick_off_blk.bi_pdata = (short *) tick_off;
	tick_on_blk.bi_wb = tick_off_blk.bi_wb = 2;
	tick_on_blk.bi_hl = tick_off_blk.bi_hl = rows;
	tick_on_blk.bi_color = tick_off_blk.bi_color = 1;
}

#define NTICK 8
static short s_on[NTICK];
static short tick_box[NTICK], tick_lbl[NTICK], tick_var[NTICK], nticks;
enum { T_PICTURES, T_FALCON, T_DSP, T_PICDSP, T_PALETTE, T_SCRIPTS, T_SCRIPTSALL };

static short d_tick(short x, short y, const char *label, short var)
{
	short k = nticks++;
	tick_var[k] = var;
	if (!tick_on_blk.bi_pdata)
		make_ticks();
	tick_box[k] = d_add(G_IMAGE, TOUCHEXIT, 0, (long) (s_on[var] ? &tick_on_blk : &tick_off_blk), x, y, 2, 1);
	tick_lbl[k] = d_add(G_STRING, TOUCHEXIT, 0, (long) label, x + 3, y, (short) strlen(label), 1);
	return k;
}

static void tick_draw(short k)
{
	OBJECT *o = &tree[tick_box[k]];
	o->ob_spec = (long) (s_on[tick_var[k]] ? &tick_on_blk : &tick_off_blk);
	dlg_draw(tree, tick_box[k]);
}

/* ---------------------------------------------------------------- pages */

static short page_of[DMAX], npages, cur_page, page_tab[4], adding_page;
static short pages_obj;		/* the box the pages are in */
static short settings_band;	/* its title band */

/* rows of a page, with some air between them: row n of a page starting
   at line y is n lines and n gaps down */
static short row_y0, row_gap;
#define ROW(n) ((short) ((row_y0 + (n)) | ((2 + (n) * row_gap) << 8)))

static void rows_at(short y)
{
	row_y0 = y;
	row_gap = (short) (gl_hchar * 3 / 8);
}

static void page_mark(short from)
{
	short i;
	for (i = from; i < nobj; i++)
		page_of[i] = adding_page;
}

static void show_page(short p, int draw)
{
	short i;
	cur_page = p;
	for (i = 1; i < nobj; i++) {
		if (!page_of[i])
			continue;
		if (page_of[i] == p) {
			tree[i].ob_flags &= ~HIDETREE;
			if (tree[i].ob_type == G_FBOXTEXT)
				tree[i].ob_flags |= EDITABLE;
		} else {
			tree[i].ob_flags |= HIDETREE;
			tree[i].ob_flags &= ~EDITABLE;
		}
	}
	for (i = 1; i <= npages; i++)
		/* the chosen page in a black bar, white words (its colours, not
		   SELECTED, which the 3D look draws pressed in: 5.6) */
		((TEDINFO *) tree[page_tab[i]].ob_spec)->te_color = i == p ? 0x1071 : 0x1180;
	if (draw) {
		/* (dlg_draw: where the dialog is now, with a framed field's
		   frame a pixel or two outside it) */
		short k;
		for (k = 1; k <= npages; k++)
			dlg_draw(tree, page_tab[k]);
		dlg_draw(tree, pages_obj);
	}
}

static short first_edit(void)
{
	short i;
	for (i = 1; i < nobj; i++)
		if ((tree[i].ob_flags & EDITABLE) && !(tree[i].ob_flags & HIDETREE))
			return i;
	return 0;
}

/* ---------------------------------------------------------------- the settings */

/* the search address and the gateway hold as much as their settings,
   more than their fields show */
static char s_zoom[4], s_search[sizeof opt.search], s_gate[sizeof opt.gate], s_gport[6], s_cache[5];
static LFIELD f_search, f_gate;

static void part_general(short x, short y)
{
	rows_at(y);
	d_tick(x, ROW(0), "Show pages' pictures", T_PICTURES);
	d_text(x + 3, ROW(1), "(off: only their words, for a slow line)");
	d_text(x, ROW(2), "Text size");
	d_field(x + 12, ROW(2), s_zoom, 3, '9');
	d_text(x + 16, ROW(2), "%  (50 to 200)");
	d_text(x, ROW(3), "Words typed in the address go to:");
	d_lfield(x, ROW(4), &f_search, s_search, 40, sizeof s_search - 1);
	d_text(x, ROW(5), "(%s for the words; nothing: none)");
	d_text(x, ROW(6), "Disk cache");
	d_field(x + 12, ROW(6), s_cache, 3, '9');
	d_text(x + 16, ROW(6), "MB  (0: none, 512 at most)");
#ifndef EW_ST
	d_tick(x, ROW(7), "Pictures on the DSP (a Falcon's)", T_PICDSP);
	d_tick(x, ROW(8), "EWeb's own colours in 256 colours", T_PALETTE);
#else
	d_tick(x, ROW(7), "EWeb's own colours in 256 colours", T_PALETTE);
#endif
}

/* the family's Secure page: TLS on this Atari, or the Pi gateway */
static void part_secure(short x, short y)
{
	int can = tls_available && get_cookie(0x5F435055L) >= 20;	/* _CPU */
	const char *note = can ? "Needs CACERT.PEM next to EWEB.PRG" :
			   !tls_available ? "Not in the ST edition: EWEB.PRG has it" :
			   "Needs a 68020 or better (TT, Falcon)";
	rows_at(y);
	d_tick(x, ROW(0), "Direct connection: TLS on this Atari", T_FALCON);
	d_tick(x + 3, ROW(1), "Check certificates on the DSP", T_DSP);
	d_text(x + 3, ROW(2), note);
	d_text(x, ROW(3), "Pi gateway");
	d_lfield(x + 11, ROW(3), &f_gate, s_gate, 18, sizeof s_gate - 1);
	d_text(x + 31, ROW(3), "Port");
	d_field(x + 36, ROW(3), s_gport, 5, '9');
	d_text(x + 3, ROW(4), "Pages go through the Pi gateway without");
	d_text(x + 3, ROW(5), "a Direct connection (the ST: always).");
	d_text(x + 3, ROW(6), "On the Pi: aera-gateway install web");
}

#ifndef EW_ST
/* EWeb's Scripts page (EWEB.PRG): on no site, the sites chosen, every site */
static void part_scripts(short x, short y)
{
	rows_at(y);
	d_tick(x, ROW(0), "Run pages' scripts (JavaScript)", T_SCRIPTS);
	d_tick(x + 3, ROW(1), "On every site, not only those chosen", T_SCRIPTSALL);
	d_text(x + 3, ROW(2), "Options > Run this site's scripts");
	d_text(x + 3, ROW(3), "chooses a site, or leaves one out.");
	d_text(x + 3, ROW(4), "On every site, one whose scripts are");
	d_text(x + 3, ROW(5), "too much is left out until EWeb quits.");
	if (!br.scripts) {
		d_text(x, ROW(7), "This Atari has no FPU (68881, 68882):");
		d_text(x, ROW(8), "its pages are shown without scripts.");
	}
}
#define NPAGES 3
#else
#define NPAGES 2
#endif

#define SW 12			/* the list's width */
#define SX (SW + 2)		/* where the settings start */
#define PW 44			/* their width */

static short settings_tree(short *ok)
{
	static const char *const names[] = { "  General", "  Secure", "  Scripts" };
	short k, box, from, gap = (short) (gl_hchar / 2);
	d_begin(SX + PW, 20);
	nticks = 0;
	npages = 0;
	adding_page = 0;
	memset(page_of, 0, sizeof page_of);
	settings_band = d_band(SX + PW, " Settings");
	/* the list: plain words on white, the chosen one in a black bar, a
	   line between it and the settings */
	d_add(G_BOX, 0, 0, 0x00001171L, SW, 1, (short) (0 | (1 << 8)), 19);
	for (k = 0; k < NPAGES; k++) {
		npages++;
		page_tab[npages] = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long) d_ted(names[k], 0, 0x1180),
					 (short) (0 | (2 << 8)), (short) ((2 + k * 2) | ((k * gap) << 8)),
					 (short) ((SW - 1) | (6 << 8)), 1);
	}
	box = d_add(G_IBOX, 0, 0, 0, SX - 1, 2, PW, 16);
	for (k = 1; k <= NPAGES; k++) {
		from = nobj;
		adding_page = k;
		d_add(G_STRING, 0, 0, (long) (names[k - 1] + 2), SX, 2, (short) strlen(names[k - 1] + 2), 1);
		d_add(G_BOX, 0, 0, 0x00001171L, SX, (short) (3 | (2 << 8)), PW - 2, (short) (0 | (1 << 8)));
		if (k == 1)
			part_general(SX, 4);
		else if (k == 2)
			part_secure(SX, 4);
#ifndef EW_ST
		else
			part_scripts(SX, 4);
#endif
		page_mark(from);
	}
	adding_page = 0;
	d_button(SX + PW - 26, 18, 10, "Cancel", EXIT);
	*ok = d_button(SX + PW - 13, 18, 10, "OK", EXIT | DEFAULT);
	d_end();
	show_page(1, 0);
	return box;
}

/* the dialog stays open while ticks and pages change */
static short settings_run(short pages_box, short ok)
{
	short x, y, w, h, r, k;
	pages_obj = pages_box;
	dlg_band(settings_band);	/* (in a window: its title bar, 3.5) */
	dlg_open(tree, &x, &y, &w, &h);
	log_event("dialog at %d %d", x, y);	/* (for the rigs: where its pages' list is) */
	for (;;) {
		r = dlg_do(tree, first_edit(), (short) (ok - 1));
		if (r < 0)
			break;
		r &= 0x7fff;
		for (k = 0; k < nticks; k++)
			if (r == tick_box[k] || r == tick_lbl[k]) {
				s_on[tick_var[k]] = !s_on[tick_var[k]];
				tick_draw(k);
				evnt_timer_(120);
				break;
			}
		if (k < nticks)
			continue;
		for (k = 1; k <= npages; k++)
			if (r == page_tab[k])
				break;
		if (k <= npages) {
			if (k != cur_page)
				show_page(k, 1);
			continue;
		}
		break;
	}
	dlg_close();
	if (r >= 0)
		tree[r].ob_state &= ~SELECTED;
	return r;
}

/* 1 when the settings changed (they are in opt, and on the disk) */
int dlg_settings(void)
{
	short ok, box, r;
	int z, port;
	s_on[T_PICTURES] = opt.pictures;
	s_on[T_FALCON] = (short) opt.falcon;
	s_on[T_DSP] = (short) opt.dsp;
	s_on[T_PICDSP] = opt.picdsp;
	s_on[T_PALETTE] = opt.palette;
	s_on[T_SCRIPTS] = opt.runscripts != 0;
	s_on[T_SCRIPTSALL] = opt.runscripts == 2;
	snprintf(s_zoom, sizeof s_zoom, "%d", opt.zoom);
	str_copy(s_search, opt.search, sizeof s_search);
	str_copy(s_gate, opt.gate, sizeof s_gate);
	snprintf(s_gport, sizeof s_gport, "%u", opt.gateport);
	snprintf(s_cache, sizeof s_cache, "%ld", cache_mb());
	box = settings_tree(&ok);
	r = settings_run(box, ok);
	if (r != ok)
		return 0;
	opt.pictures = s_on[T_PICTURES];
	z = atoi(s_zoom);
	if (z >= 50 && z <= 200)
		opt.zoom = (short) z;
	str_copy(opt.search, s_search, sizeof opt.search);
	str_copy(opt.gate, s_gate, sizeof opt.gate);
	str_trim(opt.gate);
	port = atoi(s_gport);
	opt.gateport = (unsigned short) (port > 0 && port < 65536 ? port : HTTP_GATEWAY_PORT);
	/* the size as it was unless it was changed (an INF without CACHE=
	   keeps following the machine's memory) */
	if (s_cache[0] && atol(s_cache) != cache_mb())
		opt.cache = (short) (atol(s_cache) <= CACHE_MAX_MB ? atol(s_cache) : CACHE_MAX_MB);
	opt.dsp = s_on[T_DSP];
	opt.picdsp = s_on[T_PICDSP];
	opt.palette = s_on[T_PALETTE];
#ifndef EW_ST
	opt.runscripts = (short) (!s_on[T_SCRIPTS] ? 0 : s_on[T_SCRIPTSALL] ? 2 : 1);
#endif
	if (s_on[T_FALCON] != opt.falcon)
		falcon_mode_set(s_on[T_FALCON]);	/* says why when it can't */
	return 1;
}

/* ---------------------------------------------------------------- a question and its answer */

static char p_lines[3][44], p_answer[256];
static LFIELD f_answer;

/* a question (UTF-8) under a title, its answer typed in a field (UTF-8,
   what it was first); 1 OK, 0 Cancel: a page's prompt(), Edit > Find */
static int ask(const char *title, const char *text, char *answer, int size)
{
	char a[200];
	const char *s;
	short field, ok, r, x, y, w, h, k, band;
	long n;
	char *u;
	/* the question in at most three lines of 40 */
	atari_text(text, a, sizeof a);
	memset(p_lines, 0, sizeof p_lines);
	for (s = a, k = 0; *s && k < 3; k++) {
		size_t len = strlen(s), cut = len > 40 ? 40 : len;
		if (len > 40)
			while (cut > 20 && s[cut] != ' ')
				cut--;
		memcpy(p_lines[k], s, cut);
		p_lines[k][cut] = 0;
		s += cut;
		while (*s == ' ' || *s == '\n')
			s++;
	}
	atari_text(answer, p_answer, sizeof p_answer);
	d_begin(44, 9);
	nticks = 0;
	memset(page_of, 0, sizeof page_of);
	band = d_band(44, title);
	for (k = 0; k < 3; k++)
		if (p_lines[k][0])
			d_text(2, (short) (2 + k), p_lines[k]);
	field = d_lfield(2, 5, &f_answer, p_answer, 40, sizeof p_answer - 1);
	d_button(18, 7, 10, "Cancel", EXIT);
	ok = d_button(31, 7, 10, "OK", EXIT | DEFAULT);
	d_end();
	dlg_band(band);		/* (in a window: its title bar, 3.5) */
	dlg_open(tree, &x, &y, &w, &h);
	r = dlg_do(tree, field, (short) (ok - 1));
	dlg_close();
	if (r >= 0)
		tree[r & 0x7fff].ob_state &= ~SELECTED;
	if (r < 0 || (r & 0x7fff) != ok)
		return 0;
	u = atari_to_utf8(p_answer, (long) strlen(p_answer), &n);
	if (u != NULL) {
		str_copy(answer, u, size);
		free(u);
	}
	return 1;
}

#ifndef EW_ST	/* the ST edition runs no scripts */
int dlg_prompt(const char *text, char *answer, int size)
{
	return ask(" The page asks", text, answer, size);
}
#endif

int dlg_find(char *words, int size)
{
	return ask(" Find on this page", "Words to find (Return; then ^G finds the next, Shift-^G the one before):",
		   words, size);
}
