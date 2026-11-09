/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialogs.c - EWeb's dialogs: Settings (^,) on the family's Settings
 * dialog (common's settings.c, the guidelines' 6.1: a black title band, the
 * pages in a list on the left, the chosen one in a black bar, that page's
 * settings on the right; the Secure page is the family's, a Direct
 * connection or the Gateway's address and port, 7.4), a question and its
 * answer (a page's prompt(), Find) and the box quitting shows, these two on
 * common's builder (dlgbuild.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "charset.h"
#include "dialog.h"
#include "dlgbuild.h"
#include "settings.h"
#include "field.h"
#include "http.h"
#include "tls.h"
#include "util.h"
#include "alert.h"
#include "plat.h"
#include "progbar.h"
#ifndef EW_ST
#include "langs.h"
#include "popup.h"
#include "translate.h"
#endif

/* the question's and the quit box's tree (the Settings dialog has its own,
   from the heap while it is open: settings.c) */
#define DMAX 16
static OBJECT tree[DMAX];
static TEDINFO ted[4];
static LFIELD lfs[1];
static DLGB db;

static void db_start(short w, short h)
{
	db_free(&db);	/* (the last one's long field let go) */
	db_init(&db, tree, DMAX, ted, 4, NULL, 0, lfs, 1);
	db_begin(&db, w, h);
}

/* ---------------------------------------------------------------- the Hebrew keyboard's badge */

/* while the Hebrew keyboard is on, a dialog that takes typing carries
   common's badge (hebrew_badge: ayin and bet) at its title band's right
   end, as the family's dialogs do (hebrew_badge_right, placed as the main
   window's info line has it); in a dialog window the title is the AES's,
   and the main window's info line shows it (Erez, 6 Nov 2026) */
static OBJECT *badge_tree;
static short badge_band = -1;

static void badge_show(void)
{
#ifndef EW_ST
	short bx, by, bw, bh, c[4];
	if (badge_tree == NULL || badge_band <= 0 || !hebrew_kbd || dlg_in_window())
		return;
	objc_offset(badge_tree, badge_band, &bx, &by);
	bw = badge_tree[badge_band].ob_width;
	bh = badge_tree[badge_band].ob_height;
	c[0] = bx;
	c[1] = by;
	c[2] = (short) (bx + bw - 1);
	c[3] = (short) (by + bh - 1);
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vs_clip(vdi_h, 1, c);
	hebrew_badge_right(vdi_h, bx, by, bw, bh, 0);	/* (white on the band's black; placed as the info line's) */
	vs_clip(vdi_h, 0, c);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
#endif
}

static void badge_open(OBJECT *t, short band)
{
	badge_tree = t;
	badge_band = band;
	badge_show();
}

static void badge_close(void)
{
	badge_tree = NULL;
	badge_band = -1;
}

/* the keyboard switched (F10) while a dialog is open: its band again,
   with the badge or without */
void dlg_hebrew_changed(void)
{
	if (badge_tree == NULL || badge_band <= 0 || dlg_in_window())
		return;
	dlg_draw(badge_tree, badge_band);
	badge_show();
}

/* ---------------------------------------------------------------- the settings */

/* the values as the dialog changes them: its ticks, fields and the
   language; Cancel puts them back (settings.c) */
enum { T_PICTURES, T_FALCON, T_DSP, T_PICDSP, T_PALETTE, T_SCRIPTS, T_SCRIPTSALL, T_CACHEQUIT, T_TRANSLATE, NTICK };
static short s_on[NTICK];
/* the search address and the gateway hold as much as their settings,
   more than their fields show (5.4) */
static char s_zoom[4], s_search[sizeof opt.search], s_gate[sizeof opt.gate], s_gport[6], s_cache[5];
static int set_open;	/* the dialog drawn once: its place told (for the rigs) */

/* settings.c's hook: the dialog or its band drawn, the badge over it */
static void settings_drawn_now(OBJECT *t, short band)
{
	if (!set_open) {
		short x, y;
		objc_offset(t, 0, &x, &y);
		gap_tag("dialog: drawn");
		log_event("dialog at %d %d", x, y);	/* (for the rigs: where its pages' list is) */
		set_open = 1;
	}
	badge_open(t, band);
}

static void page_general(SETTINGS *s, short x, short y, void *user)
{
	(void) y, (void) user;
	set_tick(s, x, set_y(s, 0), "Show pages' pictures", &s_on[T_PICTURES]);
	set_text(s, x + 3, set_y(s, 1), "(off: only their words, for a slow line)");
	set_text(s, x, set_y(s, 2), "Text size");
	set_field(s, x + 12, set_y(s, 2), s_zoom, 3, '9');
	set_text(s, x + 16, set_y(s, 2), "%  (50 to 200)");
	set_text(s, x, set_y(s, 3), "Words typed in the address go to:");
	set_long(s, x, set_y(s, 4), s_search, 40, sizeof s_search - 1, 0);
	set_text(s, x, set_y(s, 5), "(%s for the words; nothing: none)");
	set_text(s, x, set_y(s, 6), "Disk cache");
	set_field(s, x + 12, set_y(s, 6), s_cache, 3, '9');
	set_text(s, x + 16, set_y(s, 6), "MB  (0: none, 512 at most)");
	set_tick(s, x + 3, set_y(s, 7), "Write it to the disk only at quit", &s_on[T_CACHEQUIT]);
#ifndef EW_ST
	set_tick(s, x, set_y(s, 8), "Pictures on the DSP (a Falcon's)", &s_on[T_PICDSP]);
	set_tick(s, x, set_y(s, 9), "EWeb's own colours in 256 colours", &s_on[T_PALETTE]);
#else
	set_tick(s, x, set_y(s, 8), "EWeb's own colours in 256 colours", &s_on[T_PALETTE]);
#endif
}

/* the family's Secure page: TLS on this Atari, or the Gateway */
static void page_secure(SETTINGS *s, short x, short y, void *user)
{
	int can = tls_available && get_cookie(0x5F435055L) >= 20;	/* _CPU */
	const char *note = can ? "Needs CACERT.PEM next to EWEB.PRG" :
			   !tls_available ? "Not in the ST edition: EWEB.PRG has it" :
			   "Needs a 68020 or better (TT, Falcon)";
	(void) y, (void) user;
	set_tick(s, x, set_y(s, 0), "Direct connection: TLS on this Atari", &s_on[T_FALCON]);
	set_tick(s, x + 3, set_y(s, 1), "Check certificates on the DSP", &s_on[T_DSP]);
	set_text(s, x + 3, set_y(s, 2), note);
	set_text(s, x, set_y(s, 3), "Gateway");
	set_long(s, x + 11, set_y(s, 3), s_gate, 18, sizeof s_gate - 1, 0);
	set_text(s, x + 31, set_y(s, 3), "Port");
	set_field(s, x + 36, set_y(s, 3), s_gport, 5, '9');
	set_text(s, x + 3, set_y(s, 4), "Pages go through the Gateway without");
	set_text(s, x + 3, set_y(s, 5), "a Direct connection (the ST: always).");
	set_text(s, x + 3, set_y(s, 6), "On the Gateway: aera-gateway install web");
}

#ifndef EW_ST
/* EWeb's Scripts page (EWEB.PRG): on no site, the sites chosen, every site */
static void page_scripts(SETTINGS *s, short x, short y, void *user)
{
	(void) y, (void) user;
	set_tick(s, x, set_y(s, 0), "Run pages' scripts (JavaScript)", &s_on[T_SCRIPTS]);
	set_tick(s, x + 3, set_y(s, 1), "On every site, not only those chosen", &s_on[T_SCRIPTSALL]);
	set_text(s, x + 3, set_y(s, 2), "Options > Run this site's scripts");
	set_text(s, x + 3, set_y(s, 3), "chooses a site, or leaves one out.");
	set_text(s, x + 3, set_y(s, 4), "On every site, one whose scripts are");
	set_text(s, x + 3, set_y(s, 5), "too much is left out until EWeb quits.");
	if (!br.scripts) {
		set_text(s, x, set_y(s, 7), "This Atari has no FPU (68881, 68882):");
		set_text(s, x, set_y(s, 8), "its pages are shown without scripts.");
	}
}

/* ---------------------------------------------------------------- Translate */

/* EWeb's Translate page (EWEB.PRG; AERA-Atari's
   standards/TRANSLATION-SERVICE.md, Erez, 8 Nov 2026): on or off (as View >
   Translate), the language pages are translated into, from those the
   Gateway has installed (asked for when the button is pressed, never
   before: with Translate off nothing goes to the Gateway), and common's
   Languages dialog for what the Gateway installs */
static char s_to[TR_LANG], to_label[17];
static short to_btn;
langs_job translate_job;
int translate_job_on;

/* a language's name for the button: the Gateway's as last asked, else
   the commonest's, else its code */
static char known_code[TR_LANGS_MAX][TR_LANG], known_name[TR_LANGS_MAX][LANGS_NAME];
static int nknown;

static void lang_label(const char *code, char *out, int size)
{
	static const char *const names[][2] = {
		{ "en", "English" }, { "he", "Hebrew" }, { "de", "German" }, { "fr", "French" },
		{ "es", "Spanish" }, { "it", "Italian" }, { "pt", "Portuguese" }, { "nl", "Dutch" },
		{ "ru", "Russian" }, { "ar", "Arabic" }, { "ja", "Japanese" }, { "zh", "Chinese" },
	};
	int i;
	for (i = 0; i < nknown; i++)
		if (!strcmp(known_code[i], code)) {
			str_copy(out, known_name[i], size);
			return;
		}
	for (i = 0; i < (int) (sizeof names / sizeof names[0]); i++)
		if (!strcmp(names[i][0], code)) {
			str_copy(out, names[i][1], size);
			return;
		}
	str_copy(out, code, size);
}

/* the Gateway's address as the Secure page has it now */
static const char *gate_now(void)
{
	static char g[sizeof s_gate];
	str_copy(g, s_gate, sizeof g);
	str_trim(g);
	return g;
}

static void say_no_gateway(const char *why)
{
	alert(1, "[1][The Gateway's languages|couldn't be had:|%.100s][ OK ]", why);
}

/* the button's words: a space, the language, "..." (a choice from the
   Gateway's list, 5.7's value button), 16 characters */
static void to_text(void)
{
	char n[LANGS_NAME];
	lang_label(s_to, n, sizeof n);
	snprintf(to_label, sizeof to_label, " %.11s...", n);
	while (strlen(to_label) < sizeof to_label - 1)
		strcat(to_label, " ");
}

/* "Translate into": the Gateway's installed languages (asked now, never
   before: with Translate off nothing goes to the Gateway) in the family's
   pop-up over the button, the one now chosen ticked (5.7) */
static void choose_language(SETTINGS *s, void *user)
{
	SBUF list;
	char err[160], *p, *e;
	static char lab[TR_LANGS_MAX][LANGS_NAME + 2];
	static const char *labp[TR_LANGS_MAX];
	short bx, by, k, n = 0, sel = 0;
	OBJECT *t = set_tree(s);
	(void) user;
	if (!gate_now()[0]) {
		say_no_gateway("set its address on the Secure page");
		return;
	}
	sb_init(&list);
	graf_mouse(BUSYBEE, 0);
	k = (short) translate_languages(gate_now(), opt.translate_port, &list, err, sizeof err);
	graf_mouse(ARROW, 0);
	if (!k) {
		sb_free(&list);
		say_no_gateway(err);
		return;
	}
	nknown = 0;
	for (p = list.s; p != NULL && *p && n < TR_LANGS_MAX; p = *e ? e + 1 : e) {
		TR_LANGINFO li;
		char *tab = strchr(p, '\t');
		e = strchr(p, '\n');
		if (e == NULL)
			e = p + strlen(p);
		if (tab == NULL || tab > e || tab - p >= TR_LANG)
			continue;
		memset(&li, 0, sizeof li);
		memcpy(li.code, p, (size_t) (tab - p));
		snprintf(li.name, sizeof li.name, "%.*s", (int) (e - tab - 1), tab + 1);
		if (li.name[0] && li.name[strlen(li.name) - 1] == '\r')
			li.name[strlen(li.name) - 1] = 0;
		str_copy(known_code[nknown], li.code, TR_LANG);
		langs_name(&li, known_name[nknown]);
		if (!strcmp(li.code, s_to))
			sel = n;
		/* (popup_m's labels: the words, the chosen one with the check
		   mark in front: popup.h) */
		snprintf(lab[n], sizeof lab[n], "%s%s", strcmp(li.code, s_to) ? "" : "\x08", known_name[nknown]);
		labp[n] = lab[n];
		nknown++;
		n++;
	}
	sb_free(&list);
	if (n == 0) {
		say_no_gateway("it has no language installed: Languages...");
		return;
	}
	/* over the button, its chosen row where the button is (settings.c's
	   pop-ups open so) */
	objc_offset(t, to_btn, &bx, &by);
	k = popup_m(bx, (short) (by - 3 - sel * (gl_hchar + 2)), labp, n);
	if (k >= 0 && k < nknown) {
		str_copy(s_to, known_code[k], sizeof s_to);
		to_text();
		log_event("translate into %s", s_to);
	}
}

/* Languages...: common's dialog; an install is a job on the Gateway that
   main.c follows on the status line (translate_job) */
static void languages(SETTINGS *s, void *user)
{
	static langs l;
	int r;
	(void) s, (void) user;
	if (!gate_now()[0]) {
		say_no_gateway("set its address on the Secure page");
		return;
	}
	memset(&l, 0, sizeof l);
	l.host = gate_now();
	l.port = opt.translate_port;
	r = langs_run(&l);
	log_event("languages %d job %d", r, l.job);
	if (r == 1 && l.job) {
		langs_job_begin(&translate_job, l.host, l.port, l.job, l.add_mb, pf_ms());
		translate_job_on = 1;
	}
}


/* EWeb's Translate page (EWEB.PRG) */
static void page_translate(SETTINGS *s, short x, short y, void *user)
{
	(void) y, (void) user;
	set_tick(s, x, set_y(s, 0), "Translate pages (as View > Translate)", &s_on[T_TRANSLATE]);
	set_text(s, x, set_y(s, 2), "Translate into");
	to_text();
	to_btn = set_value(s, x + 16, set_y(s, 2), 16, to_label, choose_language, NULL);
	set_button(s, x + 16, set_y(s, 4), 16, "Languages...", languages, NULL);
	set_text(s, x, set_y(s, 6), "On the Gateway (Secure page's address):");
	set_text(s, x, set_y(s, 7), "aera-gateway install translate.");
	set_text(s, x, set_y(s, 8), "Pages are translated there and kept");
	set_text(s, x, set_y(s, 9), "in memory only.");
}
#endif

/* a number typed out of its range (the UI standards' D30, Erez's words of
   9 Nov 2026): the alert that says what to type, its page shown; OK
   refused, nothing changed silently (settings.c's check) */
static int settings_check(SETTINGS *s, void *user)
{
	const char *bad = NULL;
	short page = 0;
	long v;
	(void) user;
	v = atol(s_zoom);
	if (!s_zoom[0] || v < 50 || v > 200)
		bad = "[1][The text size is a number|from 50 to 200 (per cent).][ OK ]";
	else if (s_cache[0] && atol(s_cache) > CACHE_MAX_MB)
		bad = "[1][The disk cache can be at most|512 MB: type 512 or less.][ OK ]";
	else if (!s_gport[0] || (v = atol(s_gport)) < 1 || v > 65535) {
		bad = "[1][The Gateway's port is a|number from 1 to 65535.][ OK ]";
		page = 1;
	}
	if (bad == NULL)
		return 1;
	log_event("settings refused: page %d", page + 1);
	alert(1, bad);
	if (page != set_page_now(s))
		set_show(s, page);
	dlg_draw(set_tree(s), 0);	/* (what the alert covered) */
	badge_show();
	return 0;
}

/* 1 when the settings changed (they are in opt, and on the disk) */
int dlg_settings(void)
{
	static const SET_PAGE pages[] = {
		{ "General", page_general }, { "Secure", page_secure },
#ifndef EW_ST
		{ "Scripts", page_scripts }, { "Translate", page_translate },
#endif
	};
	SET_DEF d;
	int r;
	s_on[T_PICTURES] = opt.pictures;
	s_on[T_FALCON] = (short) opt.falcon;
	s_on[T_DSP] = (short) opt.dsp;
	s_on[T_PICDSP] = opt.picdsp;
	s_on[T_PALETTE] = opt.palette;
	s_on[T_CACHEQUIT] = opt.cachequit;
	s_on[T_SCRIPTS] = opt.runscripts != 0;
	s_on[T_SCRIPTSALL] = opt.runscripts == 2;
#ifndef EW_ST
	s_on[T_TRANSLATE] = opt.translate;
	str_copy(s_to, opt.translate_to, sizeof s_to);
#endif
	snprintf(s_zoom, sizeof s_zoom, "%d", opt.zoom);
	str_copy(s_search, opt.search, sizeof s_search);
	str_copy(s_gate, opt.gate, sizeof s_gate);
	snprintf(s_gport, sizeof s_gport, "%u", opt.gateport);
	snprintf(s_cache, sizeof s_cache, "%ld", cache_mb());
	memset(&d, 0, sizeof d);
	d.title = "Settings";
	d.pages = pages;
	d.npages = (short) (sizeof pages / sizeof pages[0]);
	d.page_w = 44;		/* (EWeb's long fields: settings.h) */
	d.height = 20;
	d.check = settings_check;
	gap_tag("dialog: made");
	set_open = 0;
	settings_drawn = settings_drawn_now;
	r = settings_dialog(&d);
	settings_drawn = NULL;
	badge_close();
	if (!r)
		return 0;
	opt.pictures = s_on[T_PICTURES];
	opt.zoom = (short) atoi(s_zoom);	/* (in range: settings_check) */
	str_copy(opt.search, s_search, sizeof opt.search);
	str_copy(opt.gate, s_gate, sizeof opt.gate);
	str_trim(opt.gate);
	opt.gateport = (unsigned short) atol(s_gport);
	/* the size as it was unless it was changed (an INF without CACHE=
	   keeps following the machine's memory) */
	if (s_cache[0] && atol(s_cache) != cache_mb())
		opt.cache = (short) atol(s_cache);
	opt.dsp = s_on[T_DSP];
	opt.picdsp = s_on[T_PICDSP];
	opt.palette = s_on[T_PALETTE];
	opt.cachequit = s_on[T_CACHEQUIT];
#ifndef EW_ST
	opt.translate = s_on[T_TRANSLATE];
	if (s_to[0])
		str_copy(opt.translate_to, s_to, sizeof opt.translate_to);
	opt.runscripts = (short) (!s_on[T_SCRIPTS] ? 0 : s_on[T_SCRIPTSALL] ? 2 : 1);
#endif
	if (s_on[T_FALCON] != opt.falcon)
		falcon_mode_set(s_on[T_FALCON]);	/* says why when it can't */
	return 1;
}

/* ---------------------------------------------------------------- a question and its answer */

static char p_lines[3][44], p_answer[256];

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
	/* (the family's geometry, 6.1: margins 2, Cancel at W - 24, OK at
	   W - 12, 10 wide) */
	db_start(44, 9);
	band = db_band(&db, 44, title);
	for (k = 0; k < 3; k++)
		if (p_lines[k][0])
			db_text(&db, 2, (short) (2 + k), p_lines[k]);
	field = db_framed(&db, db_long(&db, 2, 5, p_answer, 40, sizeof p_answer - 1, 0));
	db_button(&db, 20, 7, 10, "Cancel", EXIT);
	ok = db_button(&db, 32, 7, 10, "OK", EXIT | DEFAULT);
	db_end(&db);
	dlg_band(band);		/* (in a window: its title bar, 3.5) */
	dlg_open(tree, &x, &y, &w, &h);
	badge_open(tree, band);
	r = dlg_do(tree, field, (short) (ok - 1));
	badge_close();
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

/* ---------------------------------------------------------------- the quit's box */

/* what quitting does, said while it does it, with a bar of how far it
   is (Erez, 29 Oct 2026: on a slow disk a quit took half a minute, the
   screen as it was). No buttons: it closes when the work is done */
static char q_what[44];
static short q_open, q_text, q_frame, q_at;

/* the bar's part from..to (1024ths), the family's progress bar
   (common's progbar, the guidelines' 5.9) inside its frame */
static void q_bar(short from, short to)
{
	short x, y;
	GRECT r;
	if (to <= from)
		return;
	objc_offset(tree, q_frame, &x, &y);
	r.x = x + 1;
	r.y = y + 1;
	r.w = tree[q_frame].ob_width - 2;
	r.h = tree[q_frame].ob_height - 2;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	progbar_fill(vdi_h, r.x, r.y, r.w, r.h, from, to);
	graf_mouse(M_ON, 0);
}

static short q_part(int permille)
{
	return (short) ((long) permille * PB_FULL / 1000);
}

void quit_box(const char *what, int permille)
{
	short x, y, w, h;
	if (permille < 0)
		permille = 0;
	if (permille > 1000)
		permille = 1000;
	str_copy(q_what, what, sizeof q_what);
	if (!q_open) {
		short band;
		db_start(44, 6);
		band = db_band(&db, 44, " Closing EWeb");
		q_text = db_text(&db, 2, 2, q_what);
		tree[q_text].ob_width = 40;	/* (the longest step's words) */
		q_frame = db_add(&db, G_BOX, 0, 0, 0x00011100L, 2, 4, 40, 1);
		db_end(&db);
		dlg_band(band);		/* (in a window: its title bar, 3.5) */
		dlg_open(tree, &x, &y, &w, &h);
		q_open = 1;
		q_at = q_part(permille);
		q_bar(0, q_at);
		log_event("quit box %d %s", permille, q_what);
		return;
	}
	dlg_draw(tree, 0);	/* the box whole: the last step's words gone */
	q_at = q_part(permille);
	q_bar(0, q_at);
	log_event("quit box %d %s", permille, q_what);
}

void quit_bar(int permille)
{
	short w;
	if (!q_open)
		return;
	if (permille > 1000)
		permille = 1000;
	w = q_part(permille);
	if (w * (long) (tree[q_frame].ob_width - 2) / PB_FULL <= q_at * (long) (tree[q_frame].ob_width - 2) / PB_FULL)
		return;		/* nothing more to show (a pixel or less) */
	q_bar(q_at, w);	/* only what is new (the guidelines' Speed) */
	q_at = w;
	log_event("quit box %d %s", permille, q_what);
}

void quit_box_close(void)
{
	if (q_open)
		dlg_close();
	q_open = 0;
}

#ifndef EW_ST	/* the ST edition runs no scripts */
int dlg_prompt(const char *text, char *answer, int size)
{
	return ask(" The page asks", text, answer, size);
}
#endif

int find_dialog(char *words, int size)
{
	return ask(" Find on this page", "Words to find (Return; then ^G finds the next, Shift-^G the one before):",
		   words, size);
}
