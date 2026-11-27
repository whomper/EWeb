/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * acc.c - EWEB.ACC's start and its waits (DESIGN.md, The accessory:
 * EWEB.ACC; the guidelines' section 2): the heap and the ST RAM taken
 * before the first event call, the Desk entry's alert without TT RAM,
 * the wait for AC_OPEN or VA_START between sessions, EWeb's folder asked
 * for. A file of its own, linked last, so that EWEB.PRG's code stays at
 * 1.27's places (the speed rig's).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "plat.h"
#include "util.h"
#include "av.h"
#include "alert.h"
#include "accmem.h"
#include "twin.h"
#include "stpool.h"
#include "dcache.h"
#include "browse.h"
#include "mbar.h"
#include "popup.h"

extern void *(*pf_st_alloc)(long n);	/* atari/plat_tos.c */

const MBAR_TITLE *acc_bar;	/* main.c's menu bar's table and tree, for the toolbar's menu */
MBAR *acc_mbar;

/* EWEB.INF's lines the accessory needs before its heap is taken (TTLEAVE=,
   HEAPKB=, FOLDER=): the file read once into a buffer of its own, nothing
   malloc'd (the guidelines' 2.1) */
static char acc_inf[8192];
static int acc_inf_read;

static void acc_peek(const char *key, char *out, int size)
{
	char path[64];
	const char *t, *k;
	int kl = (int) strlen(key);
	out[0] = 0;
	if (!acc_inf_read) {
		int h;
		acc_inf_read = 1;
		snprintf(path, sizeof path, "%c:\\eweb.inf", 'A' + boot_drive());	/* (main.c's inf_path) */
		if ((h = pf_open(path, PF_READ)) >= 0) {
			long n = pf_read(h, acc_inf, sizeof acc_inf - 1);
			pf_close(h);
			acc_inf[n > 0 ? n : 0] = 0;
		}
	}
	for (t = acc_inf; (k = strstr(t, key)) != NULL; t = k + kl)
		if (k == acc_inf || k[-1] == '\n') {
			int n = (int) strcspn(k + kl, "\r\n");
			str_copy(out, k + kl, n + 1 < size ? n + 1 : size);
			return;
		}
}

/* no TT RAM: nothing taken, nothing read; the Desk menu's entry says so
   (decided 27 Nov 2026; common's acc_tt_alert). An accessory never ends */
static void acc_no_room(void)
{
	for (;;) {
		short msg[8];
		AEVENT ev;
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == AC_OPEN)
			acc_tt_alert("EWeb", "EWEB.ACC");
	}
}

/* before the first event call: the Desk menu's entry, the heap from TT
   RAM (all but TTLEAVE= per cent of it, as EWEB.PRG keeps pages and
   pictures in; HEAPKB= the user's; at most half: common's accmem.c) and
   the ST RAM the screen needs (ui/stpool.c) */
void acc_boot(short *work_out)
{
	char v[200];
	long tt, st, kb, pool, screen;
	int leave = 50;
	menu_register(ap_id, "  EWeb");
	acc_peek("NATFEATS=", v, sizeof v);
	nf_on = atoi(v) != 0;	/* (the rigs': settings_load reads it again) */
	if (acc_no_ttram()) {
		log_event("accessory: no TT RAM, nothing taken");
		acc_no_room();
	}
	acc_peek("TTLEAVE=", v, sizeof v);
	if (v[0])
		leave = atoi(v) < 0 ? 0 : atoi(v) > 90 ? 90 : atoi(v);
	acc_peek("HEAPKB=", v, sizeof v);
	kb = atol(v);
	tt = (long) Mxalloc(-1L, 1);
	if (kb <= 0)
		kb = tt / 100 * (100 - leave) >> 10;
	acc_heap_take(kb << 10, kb << 10, kb, 0);
	/* ST RAM: the screen's height of bands (and the standard form's
	   copy where the screen has its own), a window's page kept off the
	   screen, the toolbar's pictures: four screens and 256 KB, a quarter
	   of the free ST RAM at most */
	st = (long) Mxalloc(-1L, 0);
	screen = (long) (work_out[0] + 1) * (work_out[1] + 1) / 8 * (planes > 8 ? 16 : planes);
	pool = 4 * screen + (256L << 10);
	if (pool > st / 4)
		pool = st / 4;
	if (st_pool_init(pool))
		pf_st_alloc = st_pool_alloc;
	/* no file left open across an event call (2.2) */
	dc_keep_shut = 1;
	br_save_shut = 1;
	acc_peek("FOLDER=", opt.workdir, sizeof opt.workdir);
	str_trim(opt.workdir);
	{
		int n = (int) strlen(opt.workdir);
		if (n > 3 && opt.workdir[n - 1] == '\\')
			opt.workdir[n - 1] = 0;
	}
	log_event("accessory: heap %ld KB in %s RAM, ST RAM %ld KB of its own, ST RAM free %ld KB, folder %s",
		  acc_heap_bytes >> 10, acc_heap_tt ? "TT" : "ST", st_pool_size() >> 10, (long) Mxalloc(-1L, 0) >> 10,
		  opt.workdir[0] ? opt.workdir : "(none)");
}

/* waiting, the window closed: for the Desk menu's entry (AC_OPEN) or an
   address from another program (VA_START, 4.1). *first the address ("":
   the home page) */
void acc_wait(char *first, int size)
{
	first[0] = 0;
	for (;;) {
		short msg[8];
		AEVENT ev;
		const char *va;
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == AC_CLOSE) {
			twin_reset();	/* (the program in front changed) */
			continue;
		}
		if (msg[0] == AC_OPEN) {
			if (twin_pass(""))
				continue;	/* EWEB.PRG runs: its window to the front (2.3) */
			return;
		}
		if (av_message(msg, &va) == AV_START) {
			if (twin_pass(va))
				continue;	/* EWEB.PRG opens it */
			if (av_arg(va, first, size) == 0)
				first[0] = 0;	/* (none: the home page) */
			return;
		}
	}
}

/* EWeb's folder unknown (no FOLDER= in EWEB.INF, or it is gone): where
   EWEB.FNT is, chosen in the file selector (as ECal's locate_folder); 0:
   cancelled */
int acc_locate(void)
{
	char path[256], name[20] = "", f[256];
	short button = 0;
	for (;;) {
		char *e;
		if (alert(1, "[2][Where is EWeb's folder, with|EWEB.PRG and EWEB.FNT?|(Start EWEB.PRG once and|EWEB.ACC finds it.)][Locate...|Cancel]") != 1)
			return 0;
		snprintf(path, sizeof path, "%c:\\*.*", 'A' + boot_drive());
		if (!fsel_exinput(path, name, &button, "EWeb's folder: pick EWEB.FNT") || !button)
			continue;
		if ((e = strrchr(path, '\\')) != NULL)
			*e = 0;
		path_join(f, sizeof f, path, "eweb.fnt");
		if (pf_exists(f)) {
			str_copy(opt.workdir, path, sizeof opt.workdir);
			return 1;
		}
	}
}

/* the toolbar's menu (the accessory has no menu bar: the guidelines' 3.1;
   decided 27 Nov 2026: the menu bar's titles first, About EWeb... at their
   head, then the title's items in the same place, with the bar's words,
   keys, ticks and greyed items). Quit is left out: Close window (^U), or
   ^Q, closes the accessory's windows */
void acc_menu(short x, short y)
{
	const char *lab[16];
	char text[40][48];
	const char *items[40];
	int cmds[40], i, n = 0, k;
	char titles[12][16];
	lab[n++] = "About EWeb...";
	for (i = 1; acc_bar[i].title != NULL && n < 16; i++) {
		const char *t = acc_bar[i].title;
		while (*t == ' ')
			t++;
		str_copy(titles[n], t, sizeof titles[n]);
		str_trim(titles[n]);
		lab[n] = titles[n];
		n++;
	}
	k = popup_m(x, y, lab, (short) n);
	if (k == 0) {
		command(C_ABOUT);
		return;
	}
	if (k < 1)
		return;
	{
		const MBAR_ITEM *m = acc_bar[k].items;
		int keyw = 0, w = 0;
		for (i = 0; m[i].text != NULL; i++) {
			if ((int) strlen(m[i].text) > w)
				w = (int) strlen(m[i].text);
			if (m[i].key && (int) strlen(m[i].key) > keyw)
				keyw = (int) strlen(m[i].key);
		}
		n = 0;
		for (i = 0; m[i].text != NULL && n < 40; i++) {
			short o, st = 0;
			char key[12];
			if (m[i].cmd == C_QUIT)
				continue;
			if (!strcmp(m[i].text, "-")) {
				items[n] = "-";
				cmds[n++] = 0;
				continue;
			}
			o = mbar_object(acc_mbar, m[i].cmd);
			if (o > 0)
				st = acc_mbar->tree[o].ob_state;
			str_copy(key, m[i].key ? m[i].key : "", sizeof key);	/* (\x01: Shift, as the bar) */
			snprintf(text[n], sizeof text[n], "%s%-*s%s%s", (st & DISABLED) ? "~" : (st & CHECKED) ? "\x08" : "",
				 w, m[i].text, keyw ? "  " : "", key);
			items[n] = text[n];
			cmds[n++] = m[i].cmd;
		}
		k = popup_m(x, y, items, (short) n);
		if (k >= 0 && k < n && cmds[k])
			command(cmds[k]);
	}
}
