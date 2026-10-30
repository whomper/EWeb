/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * main.c - EWEB.PRG: start-up, the menu bar, the event loop, the
 * commands behind the menus, keys and toolbar, the settings (EWEB.INF,
 * next to the program), and staying alive while a page loads (Esc, Undo
 * and Stop stop it). Laid out as EFTP's main.c.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "text.h"
#ifndef EW_ST
#include "tlsovl.h"
#endif
#include "aboutbox.h"
#include "av.h"
#include "bookmark.h"
#include "history.h"
#include "clip.h"
#include "conn.h"
#include "cookie.h"
#include "crash.h"
#include "dcache.h"
#include "dd.h"
#include "dialog.h"
#include "panebar.h"
#include "font.h"
#include "help.h"
#include "http.h"
#include "img.h"
#include "js.h"
#include "mbar.h"
#include "notify.h"
#include "pic.h"
#include "plat.h"
#include "secret.h"
#include "tls.h"
#include "util.h"
#ifndef EW_ST
#include "dspjpeg.h"
#include "js_ovl.h"
#endif

/* the stack's size (the Makefile's, crt0's too) and the room kept under
   the floor (DESIGN.md, The stack) */
#ifndef EW_STACK
#error "EW_STACK: the Makefile gives crt0's stack size"
#endif
#define EW_STACK_ROOM (16L * 1024)

#ifndef EWEB_VERSION
#define EWEB_VERSION ""
#endif

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;
short ap_id;
EWOPT opt = { 1, 1, "", "http://frogfind.com/?q=%s", 100, 0, 0, 0, 0, "", 1, "", HTTP_GATEWAY_PORT, 0, "", -1, 1, 1, 1,
	       1, "", 0 };
int loading;
static short quit;
static int pics;		/* the page's pictures are coming: the window scrolls meanwhile */
/* what came while a picture did and isn't the window's own (a menu, a
   click, a key that isn't scrolling): done in turn when the picture has
   stopped (keys typed meanwhile were lost after the first: demozoo.org,
   26 Oct 2026) */
#define LATER 32
typedef struct {
	short which, msg[8];
	AEVENT ev;
} LATER_EV;
static LATER_EV later[LATER];
static int nlater;

#define FROGFIND "http://frogfind.com/?q=%s"

/* ---------------------------------------------------------------- the menu bar */

static const MBAR_ITEM m_desk[] = {
	{ "About EWeb...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "New window", "^N", C_NEWWIN },
	{ "Open address...", "^L", C_OPENURL },
	{ "Open file...", "^O", C_OPENFILE },
	{ "Save page as...", "^S", C_SAVE },
	{ "-", 0, 0 },
	{ "Reload", "^R", C_RELOAD },
	{ "Stop", "Esc", C_STOP },
	{ "-", 0, 0 },
	{ "Close window", "^U", C_CLOSE },
	{ "Next window", "^W", C_NEXTWIN },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Cut", "^X", C_CUT },
	{ "Copy", "^C", C_COPY },
	{ "Paste", "^V", C_PASTE },
	{ "-", 0, 0 },
	{ "Select all", "^A", C_SELALL },
	{ "-", 0, 0 },
	{ "Find...", "^F", C_FIND },
	{ "Find next", "^G", C_FINDNEXT },
	{ "Find previous", "\x01^G", C_FINDPREV },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_view[] = {
	{ "Back", "Undo", C_BACK },
	{ "Forward", "\x01^Z", C_FORWARD },
	{ "Home", 0, C_HOME },
	{ "History", "^Y", C_HISTORY },
	{ "-", 0, 0 },
	{ "Larger text", "^+", C_LARGER },
	{ "Smaller text", "^-", C_SMALLER },
	{ "Actual size", "^0", C_ACTUAL },
	{ "Encoding...", 0, C_ENCODING },
	{ "-", 0, 0 },
	{ "Show pictures", 0, C_PICTURES },
	{ "-", 0, 0 },
	{ "Through the Pi's browser", 0, C_SNAPSHOT },
	{ "Live view", 0, C_LIVE },
	{ 0, 0, 0 }
};
/* Bookmarks: the commands, then the first BM_MENU bookmarks, made again
   when they change (bm_menu) */
#define BM_MENU 20
static MBAR_ITEM m_bm[3 + BM_MENU + 1];
static char bm_text[BM_MENU][40];
static unsigned bm_seen;
static const MBAR_ITEM m_opts[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ "Direct connection", 0, C_FALCON },
#ifndef EW_ST
	{ "Run this site's scripts", 0, C_SCRIPTS },
#endif
	{ "Write a log", 0, C_LOG },	/* EWEB.LOG, as ECal's and EContact's */
	{ "-", 0, 0 },
	{ "Use this page as home", 0, C_SETHOME },
	{ "-", 0, 0 },
	{ "Forget cookies...", 0, C_COOKIES },
	{ "Forget history...", 0, C_FORGETHG },
	{ "Empty the cache...", 0, C_CACHE },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "EWeb help", "Help", C_HELP },
	{ "Keys...", 0, C_KEYS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " EWeb ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " View ", m_view },
	{ " Bookmarks ", m_bm },
	{ " Options ", m_opts },
	{ " Help ", m_help },
	{ 0, 0 }
};
static MBAR mb;

/* the Bookmarks menu's items from the bookmarks: their titles (else
   their addresses) in the system font's characters, cut to fit */
static void bm_items(void)
{
	int i, n = 0;
	m_bm[n].text = "Add bookmark";
	m_bm[n].key = "^D";
	m_bm[n++].cmd = C_BMADD;
	m_bm[n].text = "Show bookmarks";
	m_bm[n].key = "^B";
	m_bm[n++].cmd = C_BMSHOW;
	for (i = 0; i < bm_n && i < BM_MENU; i++) {
		char t[160];
		const char *u = bm_list[i].url, *s = strstr(u, "://");
		if (i == 0) {
			m_bm[n].text = "-";
			m_bm[n].key = 0;
			m_bm[n++].cmd = 0;
		}
		atari_text(bm_list[i].title[0] ? bm_list[i].title : s ? s + 3 : u, t, sizeof t);
		if (strlen(t) > 30)
			strcpy(t + 27, "...");
		str_copy(bm_text[i], t, sizeof bm_text[i]);
		m_bm[n].text = bm_text[i];
		m_bm[n].key = 0;
		m_bm[n++].cmd = C_BM + i;
	}
	memset(&m_bm[n], 0, sizeof m_bm[n]);
	bm_seen = bm_changes;
}

/* each menu entry's state as the AES has it (1 off, 2 on; 0 not set yet):
   menu_update runs at every key and click, and asks the AES only for
   what changed (its 45 calls were most of a letter typed on an ST) */
static unsigned char menu_was[2][512];

/* the menu bar made again when the bookmarks changed */
static void bm_menu(void)
{
	MBAR old = mb;
	if (bm_seen == bm_changes)
		return;
	bm_items();
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		mb = old;		/* no memory: the menu as it was */
		return;
	}
	menu_bar(old.tree, 0);
	menu_bar(mb.tree, 1);
	mbar_free(&old);
	memset(menu_was, 0, sizeof menu_was);	/* (a new tree, as made) */
}

static void menu_set(int check, short i, int on)
{
	unsigned char v = on ? 2 : 1;
	if (i < 0)
		return;
	if (i < 512 && menu_was[check][i] == v)
		return;
	if (i < 512)
		menu_was[check][i] = v;
	if (check)
		menu_icheck(mb.tree, i, on ? 1 : 0);
	else
		menu_ienable(mb.tree, i, on ? 1 : 0);
}

static void enable(int cmd, int on)
{
	menu_set(0, mbar_object(&mb, cmd), on);
}

#ifndef EW_ST
/* the site a page is of, as SCRIPTS= names it: its host, or "file" for
   pages on the disk; 0 for none (EWeb's own pages) */
static int site_of(const char *url, char *out, int size)
{
	const char *h;
	char t[128];
	size_t n;
	if (strncmp(url, "file:", 5) == 0) {
		if (out != NULL)
			str_copy(out, "file", size);
		return 1;
	}
	if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		return 0;
	h = strstr(url, "://") + 3;
	n = strcspn(h, "/?#:");
	if (n == 0 || n >= sizeof t)
		return 0;
	memcpy(t, h, n);
	t[n] = 0;
	if (out != NULL)
		str_copy(out, t, size);
	return 1;
}

/* a list of sites (SCRIPTS=, NOSCRIPTS=) with site in it (on) or not */
static void site_list_set(char *list, int size, const char *site, int on)
{
	char out[sizeof opt.scripts];
	const char *s = list;
	size_t n = 0;
	out[0] = 0;
	while (*s) {
		const char *w;
		while (*s == ' ' || *s == ',')
			s++;
		w = s;
		while (*s && *s != ' ' && *s != ',')
			s++;
		if (s == w || ((size_t) (s - w) == strlen(site) && strncmp(w, site, (size_t) (s - w)) == 0))
			continue;
		if (n + (size_t) (s - w) + 2 >= sizeof out)
			break;
		if (n)
			out[n++] = ' ';
		memcpy(out + n, w, (size_t) (s - w));
		n += (size_t) (s - w);
		out[n] = 0;
	}
	if (on && n + strlen(site) + 2 < sizeof out) {
		if (n)
			out[n++] = ' ';
		strcpy(out + n, site);
	}
	str_copy(list, out, size);
}

/* the settings for scripts given to a window's browser (as it is made,
   after Settings) */
static void scripts_apply(browser *b)
{
	b->script_sites = opt.scripts;
	b->noscript_sites = opt.noscripts;
	b->script_mode = opt.runscripts;
}

/* this site's scripts on or off: on the sites chosen, its name added to
   SCRIPTS= or taken out; on every site, taken out of NOSCRIPTS= or added
   (and tried again if they were too much before) */
static void scripts_toggle(void)
{
	char site[128];
	int on = !br_scripts_for(&br, br.url);
	if (!site_of(br.url, site, sizeof site))
		return;
	if (opt.runscripts == 2) {
		site_list_set(opt.noscripts, sizeof opt.noscripts, site, !on);
		if (on)
			br_scripts_retry(&br, br.url);
	} else {
		site_list_set(opt.scripts, sizeof opt.scripts, site, on);
	}
	settings_save();
	log_event("scripts %s %s", on ? "on" : "off", site);
}

#endif

void menu_update(void)
{
	int addr = win_address_has_focus();
	enable(C_RELOAD, !loading && br.cur >= 0);
	enable(C_STOP, loading || br_pictures_pending(&br) || br_refresh_due(&br) >= 0);
	enable(C_BACK, !loading && br_can_back(&br));
	enable(C_FORWARD, !loading && br_can_forward(&br));
	enable(C_HOME, !loading);
	enable(C_NEWWIN, !loading && win_count() < win_max);
	enable(C_CLOSE, !loading);
	enable(C_NEXTWIN, !loading && win_count() > 1);
	enable(C_HISTORY, !loading);
	enable(C_OPENURL, !loading);
	enable(C_OPENFILE, !loading);
	enable(C_SAVE, !loading && br.src != NULL);
	enable(C_CUT, addr);
	enable(C_PASTE, addr);
	enable(C_COPY, addr || br.p != NULL);
	enable(C_SELALL, 1);
	enable(C_FIND, !loading && br.p != NULL);
	enable(C_FINDNEXT, !loading && br.p != NULL);
	enable(C_FINDPREV, !loading && br.p != NULL);
	enable(C_LARGER, !loading && opt.zoom < 200);
	enable(C_SMALLER, !loading && opt.zoom > 50);
	enable(C_ACTUAL, !loading && opt.zoom != 100);
	enable(C_SETHOME, !loading && br.cur >= 0);
	enable(C_ENCODING, !loading && br.encodable && br.src != NULL);
	enable(C_PICTURES, !loading);
	enable(C_SNAPSHOT, !loading && http_gateway[0]);
	enable(C_LIVE, !loading && http_gateway[0]);
	enable(C_BMADD, !loading && br.cur >= 0 && strncmp(br.url, "about:", 6) != 0);
	enable(C_BMSHOW, !loading);
	{
		int i;
		for (i = 0; i < bm_n && i < BM_MENU; i++)
			enable(C_BM + i, !loading);
	}
	enable(C_SETTINGS, !loading);
	enable(C_FALCON, !loading);
#ifndef EW_ST
	enable(C_SCRIPTS, !loading && site_of(br.url, NULL, 0));	/* without an FPU: it says why */
#endif
	{
		menu_set(1, mbar_object(&mb, C_PICTURES), opt.pictures);
		menu_set(1, mbar_object(&mb, C_FALCON), opt.falcon);
		menu_set(1, mbar_object(&mb, C_LOG), opt.log);
		menu_set(1, mbar_object(&mb, C_SNAPSHOT), opt.snapshot && http_gateway[0]);
#ifndef EW_ST
		menu_set(1, mbar_object(&mb, C_SCRIPTS), br.scripts && br_scripts_for(&br, br.url));
#endif
	}
}

/* ---------------------------------------------------------------- the settings */
/* EWEB.INF next to the program (a program without an accessory keeps its
   settings there, the guidelines' 6.1): NAME=value lines, a ';' note
   above each and an empty line after, other lines kept as they are. The file's text is read once,
   at start, and kept: a save puts the settings into it and writes it only
   when that changed it (the guidelines' 6.5) */

static const char inf_head[] = "; EWeb's settings (EWeb writes them; lines it doesn't know are kept)\r\n";
static char *inf_text;		/* EWEB.INF as it is on the disk; NULL: none */
static long inf_len;

static void inf_path(char *out, int size)
{
	path_join(out, size, opt.workdir, "EWEB.INF");
}

static void settings_load(void)
{
	char path[256], *data, *line, *next;
	long len;
	inf_path(path, sizeof path);
	data = pf_load(path, &len);
	if (data == NULL)
		return;
	inf_text = malloc(len + 1);
	if (inf_text != NULL) {
		memcpy(inf_text, data, len + 1);
		inf_len = len;
	}
	for (line = data; line && *line; line = next) {
		char *eq, *end;
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		end = line + strlen(line);
		while (end > line && (end[-1] == '\r' || end[-1] == ' '))
			*--end = 0;
		if (*line == ';' || (eq = strchr(line, '=')) == NULL)
			continue;
		*eq++ = 0;
		if (!strcmp(line, "HOME"))
			str_copy(opt.home, eq, sizeof opt.home);
		else if (!strcmp(line, "SEARCH"))
			str_copy(opt.search, eq, sizeof opt.search);
		else if (!strcmp(line, "ZOOM"))
			opt.zoom = (short) atoi(eq);
		else if (!strcmp(line, "DSP"))
			opt.dsp = atoi(eq) != 0;
		else if (!strcmp(line, "PICTURES"))
			opt.pictures = atoi(eq) != 0;
		else if (!strcmp(line, "PICDSP"))
			opt.picdsp = atoi(eq) != 0;
		else if (!strcmp(line, "PENS"))
			opt.pens = (short) (atoi(eq) < 0 ? -1 : atoi(eq) > 0);
		else if (!strcmp(line, "PALETTE"))
			opt.palette = atoi(eq) != 0;
		else if (!strcmp(line, "LOG"))
			opt.log = atoi(eq) != 0;
		else if (!strcmp(line, "FALCON"))
			opt.falcon = atoi(eq) != 0;
		else if (!strcmp(line, "GATE"))
			str_copy(opt.gate, eq, sizeof opt.gate);
		else if (!strcmp(line, "GATEPORT"))
			opt.gateport = (unsigned short) atoi(eq);
		else if (!strcmp(line, "SNAPSHOT"))
			opt.snapshot = atoi(eq) != 0;
		else if (!strcmp(line, "SCRIPTS"))
			str_copy(opt.scripts, eq, sizeof opt.scripts);
		else if (!strcmp(line, "RUNSCRIPTS"))
			opt.runscripts = (short) (atoi(eq) < 0 ? 0 : atoi(eq) > 2 ? 2 : atoi(eq));
		else if (!strcmp(line, "NOSCRIPTS"))
			str_copy(opt.noscripts, eq, sizeof opt.noscripts);
		else if (!strcmp(line, "CACHE"))
			opt.cache = (short) atoi(eq);
		else if (!strcmp(line, "WINDOW")) {
			short *v[4];
			char *p = eq;
			int k;
			v[0] = &opt.win_x;
			v[1] = &opt.win_y;
			v[2] = &opt.win_w;
			v[3] = &opt.win_h;
			for (k = 0; k < 4 && *p; k++) {
				*v[k] = (short) strtol(p, &p, 10);
				if (*p == ',')
					p++;
			}
		}
	}
	free(data);
	if (opt.zoom < 50 || opt.zoom > 200)
		opt.zoom = 100;
	if (opt.cache < -1 || opt.cache > CACHE_MAX_MB)
		opt.cache = -1;
	if (opt.gateport == 0)
		opt.gateport = HTTP_GATEWAY_PORT;
	if (!tls_available)
		opt.falcon = 0;		/* the ST edition: through the Pi gateway */
}

/* the disk cache: CACHE next to the program, read the first time a page
   is fetched; CACHE= MB, or 32 MB (a page of 45 photos and their decoded
   pixels takes 20 or so; 2 MB on a machine without TT RAM) */
long cache_mb(void)
{
	return opt.cache >= 0 ? opt.cache : (long) Mxalloc(-1L, 1) > 0 ? 32 : 2;
}

/* the connections' log (LOG=1, Options > Write a log): EWEB.LOG next to
   the program, held in memory for a page and its pictures and written
   once then (6.5), and at the end; off (0): what is held written, no
   more. 1 at the start, 2 from the menu */
static void log_start(int on)
{
	char line[120];
	if (on) {
		path_join(conn_logfile, sizeof conn_logfile, opt.workdir, "EWEB.LOG");
		conn_log_hold(1);
		crash_report = conn_log_append;	/* a crash's report after what is held */
		snprintf(line, sizeof line, on == 2 ? "EWeb %s: the log started" : "EWeb %s started", EWEB_VERSION);
		conn_log("EWeb", " -- ", line, (long) strlen(line));
	} else if (conn_logfile[0]) {
		conn_log("EWeb", " -- ", "the log stopped", 15);
		conn_log_hold(0);
		crash_report = NULL;
		conn_logfile[0] = 0;
	}
}

static void cache_open(void)
{
	char dir[256];
	long mb = cache_mb();
	if (mb == 0) {
		dc_dir("", 0);		/* none: nothing read or kept */
		return;
	}
	path_join(dir, sizeof dir, opt.workdir, "CACHE");
	dc_dir(dir, mb << 20);
}

/* the text ends with an empty line */
static int ends_empty(const SBUF *s)
{
	long i = s->len;
	if (i == 0 || s->s[i - 1] != '\n')
		return 0;
	if (--i > 0 && s->s[i - 1] == '\r')
		i--;
	return i == 0 || s->s[i - 1] == '\n';
}

/* one setting into the text: its line replaced, or its note and line
   added at the end; an empty line after each (the guidelines' 6.1), put
   in after a setting from an EWEB.INF before 0.27, which had none */
static void put(SBUF *s, const char *key, const char *value, const char *note)
{
	char *at = s->s, want[40];
	int n = snprintf(want, sizeof want, "%s=", key);
	while (at != NULL && *at) {
		char *eol = strchr(at, '\n');
		long ll = eol ? eol - at + 1 : (long) strlen(at);
		if (!strncmp(at, want, n)) {
			SBUF t;
			long off = at - s->s;
			sb_init(&t);
			sb_add(&t, s->s, off);
			sb_printf(&t, "%s=%s\r\n", key, value);
			if (at[ll] != '\r' && at[ll] != '\n')
				sb_adds(&t, "\r\n");
			sb_adds(&t, at + ll);
			sb_free(s);
			*s = t;
			return;
		}
		at += ll;
	}
	if (s->len > 0 && s->s[s->len - 1] != '\n')
		sb_adds(s, "\r\n");
	if (s->len > 0 && !ends_empty(s))
		sb_adds(s, "\r\n");
	sb_printf(s, "; %s\r\n%s=%s\r\n\r\n", note, key, value);
}

/* a setting EWeb has no more taken out of the INF's text: its line, the
   note above it and the empty line after (DIALOGS3D=, the guidelines'
   5.6 of 27 Oct 2026) */
static void drop(SBUF *s, const char *key)
{
	char want[40];
	int n = snprintf(want, sizeof want, "%s=", key);
	long at = 0, from = -1, note = -1;
	while (at < s->len) {
		char *eol = memchr(s->s + at, '\n', s->len - at);
		long ll = eol ? eol - (s->s + at) + 1 : s->len - at;
		if (s->s[at] == ';') {
			if (note < 0)
				note = at;
		} else if (!strncmp(s->s + at, want, n)) {
			from = note >= 0 ? note : at;
			at += ll;
			if (at < s->len && (s->s[at] == '\r' || s->s[at] == '\n'))
				at += s->s[at] == '\r' && at + 1 < s->len && s->s[at + 1] == '\n' ? 2 : 1;
			memmove(s->s + from, s->s + at, s->len - at + 1);
			s->len -= at - from;
			at = from;
			note = -1;
			continue;
		} else {
			note = -1;
		}
		at += ll;
	}
}

/* the settings to EWEB.INF, a safe save when they changed it; 0: it
   couldn't be written */
int settings_save(void)
{
	char path[256], v[40];
	SBUF s;
	int ok;
	inf_path(path, sizeof path);
	sb_init(&s);
	if (inf_text != NULL) {
		long h = sizeof inf_head - 1;
		sb_add(&s, inf_text, inf_len);
		/* before 0.27 the first setting's note came straight after the
		   heading: an empty line between them */
		if (s.len > h && !strncmp(s.s, inf_head, h) && s.s[h] != '\r' && s.s[h] != '\n') {
			SBUF t;
			sb_init(&t);
			sb_add(&t, s.s, h);
			sb_adds(&t, "\r\n");
			sb_adds(&t, s.s + h);
			sb_free(&s);
			s = t;
		}
	} else {
		sb_adds(&s, inf_head);
	}
	drop(&s, "DIALOGS3D");
	put(&s, "HOME", opt.home, "the start page: an address, or nothing for EWeb's own");
	put(&s, "SEARCH", opt.search, "words typed in the address go here, %s for them (FrogFind: " FROGFIND ")");
	snprintf(v, sizeof v, "%d", opt.zoom);
	put(&s, "ZOOM", v, "the size of the text, per cent (Larger text, Smaller text)");
	snprintf(v, sizeof v, "%d", opt.falcon);
	put(&s, "FALCON", v, "1 = Direct connection: secure pages with TLS on this Atari (68020 or better: "
	    "Falcon, TT; EWEB.PRG); 0 = through the Pi gateway");
	snprintf(v, sizeof v, "%d", opt.dsp);
	put(&s, "DSP", v, "Direct connection: 1 = the DSP does the secure connections' arithmetic, 0 = the 68030 does");
	snprintf(v, sizeof v, "%d", opt.pictures);
	put(&s, "PICTURES", v, "1: pages' pictures are shown, 0: only what they say instead (a slow line)");
	snprintf(v, sizeof v, "%d", opt.picdsp);
	put(&s, "PICDSP", v, "1 = the DSP decodes JPEGs and dithers pictures while the 68030 hands it the bytes "
	    "(a Falcon's EWEB.PRG), 0 = the 68030 does it all");
	snprintf(v, sizeof v, "%d", opt.palette);
	put(&s, "PALETTE", v, "in 256 colours: 1 = EWeb's own colours while it runs (pale tints for pages and "
	    "pictures; TOS's first 16 kept), 0 = the system's");
	snprintf(v, sizeof v, "%d", opt.log);
	put(&s, "LOG", v, "1 = EWEB.LOG next to EWEB.PRG: each connection and its secure handshake, step by step "
	    "(no address's query, cookie or form field), and where EWeb waited longest; 0 = none");
	snprintf(v, sizeof v, "%d", opt.pens);
	put(&s, "PENS", v, "through the Pi gateway, pictures made for this screen on the Pi: 1 = always (as it "
	    "starts), -1 = only when quicker than the DSP, 0 = never (the Atari decodes them)");
	put(&s, "GATE", opt.gate, "the Raspberry Pi gateway's address, for secure pages without a Direct connection "
	    "(aera-gateway install web on the Pi)");
	snprintf(v, sizeof v, "%u", opt.gateport);
	put(&s, "GATEPORT", v, "the gateway's port");
	snprintf(v, sizeof v, "%d", opt.snapshot);
	put(&s, "SNAPSHOT", v, "1: pages through the Pi's browser, after their scripts ran (the gateway's "
	    "snapshots; View menu), 0: as the servers send them");
	snprintf(v, sizeof v, "%d", opt.runscripts);
	put(&s, "RUNSCRIPTS", v, "pages' scripts (EWEB.PRG with an FPU; Settings > Scripts): 0 = on no site, "
	    "1 = on the sites in SCRIPTS=, 2 = on every site but those in NOSCRIPTS=");
	put(&s, "SCRIPTS", opt.scripts, "the sites whose scripts run, their names with spaces between "
	    "(Options > Run this site's scripts; \"file\": pages on the disk)");
	put(&s, "NOSCRIPTS", opt.noscripts, "with RUNSCRIPTS=2, the sites whose scripts don't run (Options > "
	    "Run this site's scripts, unticked)");
	if (opt.cache >= 0) {
		snprintf(v, sizeof v, "%d", opt.cache);
		put(&s, "CACHE", v, "the disk cache (CACHE next to EWEB.PRG), MB: pages, pictures as the screen "
		    "shows them; 0: none");
	}
	snprintf(v, sizeof v, "%d,%d,%d,%d", opt.win_x, opt.win_y, opt.win_w, opt.win_h);
	put(&s, "WINDOW", v, "where the window was: x, y, width, height");
	if (inf_text != NULL && s.len == inf_len && !memcmp(s.s, inf_text, s.len)) {
		sb_free(&s);
		return 1;
	}
	ok = pf_save(path, s.s, s.len) == 0;
	if (ok) {
		free(inf_text);
		inf_text = s.s;		/* what is on the disk now */
		inf_len = s.len;
	} else {
		sb_free(&s);
	}
	return ok;
}

/* ---------------------------------------------------------------- status and loading */

/* what happened, for the Hatari rigs (tests/hatari/gui_test.py): "EWEB
   ..." lines on the emulator's console through NatFeats; nothing on a
   real Atari */
void log_event(const char *fmt, ...)
{
	char buf[600];
	va_list ap;
	int n = snprintf(buf, sizeof buf, "EWEB ");
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
	va_end(ap);
	strcat(buf, "\n");
	nf_debug(buf);
}

static const char *gap_last;

void gap_event(const char *what)
{
	gap_last = NULL;
	aes_profile_tag(what);
}

void gap_tag(const char *what)
{
	aes_profile_tag(gap_last = what);
}

void gap_stage(const char *what)
{
	if (gap_last == NULL)
		aes_profile_tag(what);
}

extern long heap_tt, heap_st;	/* (common's libc.c) what the heap holds from TOS */

/* the memory, for the rigs' log: the heap's blocks in use, the most
   they were, and the RAM TOS has left (the largest block) */
static long ram_free(void)
{
	long st = (long) Mxalloc(-1L, 0), tt = (long) Mxalloc(-1L, 1);
	if (st < 0)
		return (long) Malloc(-1L);
	return st > tt ? st : tt;
}

/* a page and its pictures in: the heap's blocks all free again (the
   pages before it, their fetches and decoding) go back to TOS, the
   system's and other programs' again (Erez's 128 MB TT RAM Falcon held
   109 MB after one big page, 27 Oct 2026) */
static void trim(void)
{
	long back = heap_trim();
	log_event("trimmed %ld heap %ld taken %ld free %ld", back, heap_use, heap_tt + heap_st, ram_free());
}

/* the key for the cookies' values couldn't be made: they go into
   COOKIES.TXT as they are, and the user is told (the guidelines, 7.2) */
static void key_warn(const char *keyfile)
{
	char name[64];
	const char *b = strrchr(keyfile, '\\');
	str_copy(name, b ? b + 1 : keyfile, sizeof name);
	alert(1, "[1][EWeb can't write its key file|%s, so the cookies kept|in COOKIES.TXT aren't|encrypted.][ OK ]", name);
}

/* the page's address for the log: without its query, which may hold
   what was typed into a form or a search (7.3) */
static void log_url(char *out, int size)
{
	str_copy(out, br.url, size);
	out[strcspn(out, "?")] = 0;
}

/* the browser's own lines (a picture each) */
static void log_line(void *ud, const char *line)
{
	(void) ud;
	log_event("%s", line);
}

static char said[160];		/* what the browser said last, for the KB counter */
static long said_bytes;

/* the status bar's ribbon: each phase its stretch of the line (0..1024);
   within one, the line goes on as its bytes come (never quite to the
   end: how much will come isn't known), the pictures one by one */
static const short ph_from[] = { 0, 80, 420, 560, 660 }, ph_to[] = { 80, 420, 560, 660, 1024 };
static int ph = -1;		/* the phase now, -1 none */
static long ph_bytes;		/* conn_bytes when it began */

static void on_phase(void *ud, int p)
{
	(void) ud;
	if (p < 0 || p > BRPH_PICTURES || p < ph)
		return;
	ph = p;
	ph_bytes = conn_bytes;
	win_progress(p, ph_from[p]);
}

/* how far the bytes of this phase have taken it */
static void ph_bytes_came(void)
{
	long kb;
	if (ph == BRPH_CONNECT && conn_bytes > 0)
		on_phase(NULL, BRPH_PAGE);
	if (ph != BRPH_PAGE && ph != BRPH_STYLES)
		return;
	kb = (conn_bytes - ph_bytes) >> 10;
	win_progress(ph, ph_from[ph] + (int) ((long) (ph_to[ph] - ph_from[ph]) * kb / (kb + 48)));
}

/* the pictures: done of all */
static void ph_pictures(void)
{
	int n = br.pic_n > br.pic_done ? br.pic_n : br.pic_done;
	if (!br_pictures_pending(&br)) {
		win_progress(ph >= 0 ? ph : BRPH_PICTURES, 1024);
		ph = -1;
		return;
	}
	if (ph < BRPH_PICTURES)
		on_phase(NULL, BRPH_PICTURES);
	if (n > 0)
		win_progress(BRPH_PICTURES, ph_from[BRPH_PICTURES] +
			     (int) ((long) (ph_to[BRPH_PICTURES] - ph_from[BRPH_PICTURES]) * br.pic_done / (n + 1)));
}

void status(const char *s)
{
	crash_note(s);
	win_status(s);
}

static void say(void *ud, const char *what)
{
	char a[160];
	atari_text(what, a, sizeof a);
	str_copy(said, a, sizeof said);
	said_bytes = conn_bytes;
	snprintf(a, sizeof a, "%s   (Esc stops)", said);
	status(a);
}

static unsigned long esc_quiet_until;	/* after a stop: Esc held stops nothing more for a second */

static void keys_flush(void)
{
	short msg[8], n = 0;
	AEVENT ev;
	while (n++ < 32 && (evnt_multi_(MU_KEYBD | MU_TIMER, 1, 1, 1, 0, msg, &ev) & MU_KEYBD))
		;
	esc_quiet_until = pf_ms() + 1000;
}

/* ms until the page's refresh is due (0: now), -1 none; it waits while
   the address field has the keyboard, so that what is typed there stays */
static long refresh_due(void)
{
	return loading || cur->iconified || win_address_has_focus() ? -1 : br_refresh_due(&br);
}

/* Esc or Stop while the page's refresh waits: the page stays (said on
   the status line unless the pictures' end says what stopped) */
static void refresh_stop(int tell)
{
	if (br_refresh_due(&br) < 0)
		return;
	br_refresh_cancel(&br);
	log_event("refresh stopped");
	if (tell)
		status("Staying on this page.");
	win_toolbar();
}

/* a picture is stopped for what came: it is done after */
static void put_off(short which, const short *msg, const AEVENT *ev)
{
	if (nlater < LATER) {
		later[nlater].which = which;
		memcpy(later[nlater].msg, msg, sizeof later[nlater].msg);
		later[nlater].ev = *ev;
		nlater++;
	}
	conn_stop = 1;
}

/* while a page loads: the window redrawn, moved and topped; Esc, Undo
   and Stop stop the load; the menu bar waits. While its pictures come
   the window scrolls too, and what else comes stops the picture and is
   done after it (Esc and Stop: no more pictures for this page) */
static void idle(void)
{
	short msg[8], which;
	AEVENT ev;
	gap_stage(br_building == BRB_PARSE ? "parse" : br_building == BRB_STYLE ? "style" :
		  br_building == BRB_LAYOUT ? "layout" : br_building == BRB_SCRIPT ? "script" :
		  pics ? "pictures" : "load");
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, pf_idle_quick ? 0 : 5, msg, &ev);
	gap_event(which & MU_KEYBD ? "key" : which & MU_BUTTON ? "click" : which & MU_MESAG ? "message" : "load");
	win_throb(1);
	if ((which & MU_MESAG) && notify_message(msg))
		which &= ~MU_MESAG;	/* (BGEM.NG's) */
	if (pics) {
		/* the page's refresh to another page is due: the picture is
		   cut short (it is fetched again if the page stays; one of the
		   page itself counts from when its pictures are in) */
		if (refresh_due() == 0)
			conn_stop = 1;
		if (which & MU_MESAG) {
			if (msg[0] == MN_SELECTED) {
				if (mbar_command(&mb, msg[4]) == C_STOP) {
					conn_stop = 1;
					br_pictures_stop(&br);
					refresh_stop(0);
					menu_tnormal(mb.tree, msg[3], 1);
				} else {
					put_off(MU_MESAG, msg, &ev);
				}
			} else if (msg[0] == AP_TERM || msg[0] == WM_CLOSED) {
				put_off(MU_MESAG, msg, &ev);	/* (the window may be the pictures') */
			} else {
				win_message(msg);
			}
		}
		if (which & MU_KEYBD) {
			short scan = KEY_SCAN(ev.kreturn);
			ewin *was = cur;
			int front;
			win_front();		/* (the keys are the window in front's) */
			front = cur == was;
			if (scan == 0x01 && front) {
				conn_stop = 1;
				br_pictures_stop(&br);
				refresh_stop(0);
			} else if (scan != 0x01 && (scan == 0x48 || scan == 0x50 ||
				   (!win_address_has_focus() && (scan == 0x39 || scan == 0x47) &&
				    !key_ctrl(ev.kstate, ev.kreturn)))) {
				win_key(ev.kstate, ev.kreturn);	/* scrolling: the page stays */
			} else {
				put_off(MU_KEYBD, msg, &ev);
			}
			win_use(was);
		}
		if (which & MU_BUTTON) {
			ewin *was = cur;
			win_front();
			if (win_stop_hit(ev.mx, ev.my)) {
				/* Stop pressed: as Esc */
				conn_stop = 1;
				br_pictures_stop(&br);
				refresh_stop(0);
			} else {
				put_off(MU_BUTTON, msg, &ev);
			}
			win_use(was);
		}
		return;
	}
	if (which & MU_MESAG) {
		switch (msg[0]) {
		case MN_SELECTED:
			if (mbar_command(&mb, msg[4]) == C_STOP)
				conn_stop = 1;
			menu_tnormal(mb.tree, msg[3], 1);
			break;
		case AP_TERM:
			conn_stop = 1;
			quit = 1;
			break;
		default: {
			DROP d;
			if (dd_message(msg, 0, &d) == DD_NONE)	/* a drop now is refused */
				win_message(msg);
			dd_done(&d);
		}
		}
	}
	if (which & MU_KEYBD) {
		short scan = KEY_SCAN(ev.kreturn);
		if ((scan == 0x01 || scan == 0x61) && (long) (pf_ms() - esc_quiet_until) >= 0)
			conn_stop = 1;
	}
	if ((which & MU_BUTTON) && win_stop_hit(ev.mx, ev.my))
		conn_stop = 1;
	if (loading) {
		ph_bytes_came();
		br_preview_step(&br);	/* (once enough of the page has come) */
	}
	/* how much has come, as it comes */
	if (loading && said[0] && conn_bytes - said_bytes >= 1024) {
		char a[200];
		snprintf(a, sizeof a, "%s %ld KB   (Esc stops)", said, (conn_bytes - said_bytes) / 1024);
		status(a);
	}
}

static unsigned long load_t0;		/* when the load started, for the log */

static void loading_on(void)
{
	load_t0 = pf_ms();
	br_scrolled(&br, win_top());
	loading = 1;
	conn_stop = 0;
	conn_bytes = 0;
	said[0] = 0;
	win_progress(-1, 0);
	ph = -1;
	graf_mouse(BUSYBEE, 0);
	win_toolbar();
	menu_update();
}

/* a file to save: the file selector, in the folder last saved to, with
   name; *full the path chosen. 0: cancelled, or a file there not to be
   replaced */
static int ask_save(const char *title, const char *name, char *full, int size)
{
	static char path[256];
	char fname[64], *p;
	short button = 0;
	if (!path[0])
		snprintf(path, sizeof path, "%s\\*.*", opt.workdir);
	str_copy(fname, name, sizeof fname);
	if (!fsel_exinput(path, fname, &button, (char *) title) || !button || !fname[0])
		return 0;
	str_copy(full, path, size);
	p = strrchr(full, '\\');
	if (p != NULL)
		p[1] = 0;
	if ((long) (strlen(full) + strlen(fname)) >= (long) size)
		return 0;
	strcat(full, fname);
	return !pf_exists(full) ||
	       alert(2, "[2][There is a file of that name.|Replace it?][Replace|Cancel]") == 1;
}

/* br_save_url's: where a file goes, asked once its answer has begun (its
   server may name it) */
static int save_where(void *ud, const char *name, char *path, int size)
{
	int r;
	(void) ud;
	graf_mouse(ARROW, 0);
	r = ask_save("Save the link's file", name, path, size);
	graf_mouse(BUSYBEE, 0);
	return r;
}

static short notified_h;		/* the window a notification's Show tops */

/* BGEM.NG's Show (4.5): the window the file came in, in front */
static void notified(short id, short button)
{
	if (button == 1 && win_of(notified_h) != NULL)
		wind_set(notified_h, WF_TOP, 0, 0, 0, 0);
}

/* said on the status line, and in the log (its size, no path); where
   another program's window is in front by then, a notification too, where
   BGEM.NG shows them (the guidelines' 4.5) */
static void saved(const char *path, long len)
{
	char t[120];
	short top, d;
	const char *name = strrchr(path, '\\');
	snprintf(t, sizeof t, "Saved %.60s (%ld KB).", name != NULL ? name + 1 : path, (len + 1023) >> 10);
	status(t);
	log_event("saved %ld", len);
	wind_get(0, WF_TOP, &top, &d, &d, &d);
	if (win_of(top) == NULL && cur->h > 0) {
		notified_h = cur->h;
		if (notify_send("Download finished", t, "Show", 1))
			log_event("notified");
	}
}

static void save_file(const char *url, const char *given);
static void to_window(const char *url, const char *name);

/* what a load came to, on the screen */
/* the page loading's preview is ready: drawn in place of the page shown */
static void previewed(void *ud)
{
	(void) ud;
	if (br.pv_p == NULL) {
		win_preview_gone();	/* (gone for the page's memory) */
		log_event("preview dropped");
		return;
	}
	win_preview();
	log_event("preview height %ld items %ld", (long) br.pv_p->height, (long) br.pv_p->nitems);
}

static void loaded(int r)
{
	/* a new page: the load's own (BR_SHOWN), or one shown on the way to
	   what it came to (its script, as it loaded, clicked a download link
	   or sent the browser on): drawn first, then what the load came to
	   (the file selector for a file, EMail for a mail address) */
	int fresh = r == BR_SHOWN || br.shows != cur->drawn, pv = br.pv_p != NULL;
	int32_t pv_top = win_top();
	/* the load's preview goes: the page in its place, where it was
	   scrolled to (unless the page goes to an anchor), or the page shown
	   before again */
	br_preview_free(&br);
	loading = 0;
	graf_mouse(ARROW, 0);
	said[0] = 0;
	if (fresh || r == BR_SAME)
		ph_pictures();		/* full, or on to the pictures */
	else {
		win_progress(-1, 0);
		ph = -1;
	}
	if (fresh) {
		unsigned long ms_drawn = pf_ms();
		char shown[440];
		cur->drawn = br.shows;
		win_page_changed();
		if (pv && br.scroll_to <= 0 && pv_top > 0)
			win_scroll_to(pv_top);
		ms_drawn = pf_ms() - ms_drawn;
		log_url(shown, sizeof shown);
		{
			short x, y, w, h;
			long md, mc, mk;
			win_page_area(&x, &y, &w, &h);
			br_mem(&br, &md, &mc, &mk);
			log_event("shown %.400s height %ld links %d area %d %d %d %d top %ld ms %lu "
				  "(fetch %lu, parse %lu, style %lu, layout %lu, drawn %lu) heap %ld peak %ld free %ld "
				  "(doc %ld css %ld kept %ld) title %.200s", shown,
				  br.p ? (long) br.p->height : 0L, br.p ? br.p->nlinks : 0, x, y, w, h, (long) win_top(),
				  pf_ms() - load_t0, br.ms_fetch, br.ms_parse, br.ms_style, br.ms_layout, ms_drawn,
				  heap_use, heap_peak, ram_free(), md, mc, mk, br.title);
		}
		if (br.p && br.p->deep)
			log_event("deep %.400s: nested deeper than the stack holds, the rest left out", shown);
		if (br.note[0]) {
			char t[96];
			/* a refresh's (or that refreshes at once were stopped) is
			   what the bar says at rest (win_status("")), so it comes
			   back after the pictures' lines and a button's or a link's */
			win_status(br_refresh_note(&br, t, sizeof t) ? "" : br.note);
			log_event("note %s", br.note);
		} else if (br.p && br.p->deep) {
			win_status("Part of this page is nested too deeply to show.");
		}
	}
	if (pv && !fresh)
		win_preview_gone();
	if (fresh && !br_pictures_pending(&br))
		trim();			/* (with pictures, once they are in) */
	switch (r) {
	case BR_SHOWN:
		break;
	case BR_SAME:
		log_event("same %.*s top %ld%s%ld", (int) strcspn(br.url, "?"), br.url, (long) br.scroll_to,
			  br.relaid ? " opened height " : " height ", br.p ? (long) br.p->height : 0L);
		win_address(br.url);
		if (br.relaid) {
			/* a closed details opened to show the place: the page
			   laid out again, drawn there (or, out of memory, gone:
			   its note says so) */
			win_ctl_check();
			win_page_relaid_at(br.scroll_to >= 0 ? br.scroll_to : 0);
		} else {
			win_scroll_to(br.scroll_to >= 0 ? br.scroll_to : 0);
		}
		win_status(br.note);
		if (br.note[0])
			log_event("note %s", br.note);
		win_toolbar();
		break;
	case BR_DOWNLOAD: {
		/* a file EWeb doesn't show: saved where the user says */
		char name[16], full[320];
		long len = br.dl.len;
		win_address(br.url);
		win_status("");
		win_toolbar();
		log_event("download %.60s %ld", br.dl.mime, len);
		br_file_name(br.dl.url, br.dl.name, br.dl.mime, name, sizeof name);
		if (!ask_save("Save the file", name, full, sizeof full))
			br_dl_free(&br);
		else if (br_dl_save(&br, full))
			saved(full, len);
		else
			alert(1, "[3][EWeb can't write the file:|is the disk locked or full?][ OK ]");
		break;
	}
	case BR_SAVE: {
		/* a link to save (<a download>): as it comes, as Alternate-click
		   saves one */
		char url[1024], given[256];
		str_copy(url, br.dl.url, sizeof url);
		str_copy(given, br.dl.name, sizeof given);
		win_address(br.url);
		win_status("");
		log_event("save link");
		save_file(url, given);
		break;
	}
	case BR_WINDOW:
		/* a link or a form for another window: opened below */
		win_address(br.url);
		win_status("");
		win_toolbar();
		break;
	case BR_ELSEWHERE:
		/* mailto: to EMail, as the family hands each other work */
		if (!av_send("EMAIL", br.elsewhere))
			alert(1, "[1][A mail address: EWeb hands|these to EMail, which isn't|running.][ OK ]");
		win_address(br.url);
		win_status("");
		win_toolbar();
		break;
	default:
		log_event("stopped heap %ld peak %ld free %ld", heap_use, heap_peak, ram_free());
		keys_flush();
		win_address(br.url);
		win_status(conn_stop || r == BR_STOPPED ? "Stopped." : "");
		win_toolbar();
		break;
	}
	conn_stop = 0;
	bm_menu();		/* a bookmark removed on about:bookmarks */
	menu_update();
	if (r == BR_WINDOW)
		to_window(br.elsewhere, br.window);
}

#ifndef EW_ST
/* ---------------------------------------------------------------- the page's scripts */

/* a script's alert, confirm or prompt: the family's alert (its text
   wrapped, in the system font's characters), or the prompt's dialog */
static int js_ask(void *ud, int kind, const char *text, char *out, int size)
{
	char a[200], *p;
	(void) ud;
	graf_mouse(ARROW, 0);
	if (kind == JS_PROMPT)
		return dlg_prompt(text, out, size);
	atari_text(text, a, sizeof a);
	for (p = a; *p; p++)
		if (*p == '\n')
			*p = '|';
		else if (*p == '[')
			*p = '(';
		else if (*p == ']')
			*p = ')';
	log_event("script %s", kind == JS_ALERT ? "alert" : "confirm");
	if (kind == JS_ALERT) {
		alert(1, "[1][%s][ OK ]", a);
		return 1;
	}
	return alert(1, "[2][%s][ OK |Cancel]", a) == 1;
}

/* what a script did, on the screen */
static void scripted(int r, int load)
{
	if (r == BRJ_LOAD) {
		loaded(load);
	} else if (r == BRJ_DRAWN) {
		log_event("scripted %.*s height %ld title %.100s", (int) strcspn(br.url, "?"), br.url,
			  br.p ? (long) br.p->height : 0L, br.title);
		win_ctl_check();
		win_title();
		win_page_relaid();
		menu_update();
	}
}

int page_click(int32_t x, int32_t y)
{
	int cancelled = 0, load = 0, r;
	if (loading || br.d == NULL || br.d->js == NULL)
		return 0;
	r = br_js_click(&br, x, y, &cancelled, &load);
	scripted(r, load);
	return cancelled || r == BRJ_LOAD;
}

void ctl_event(struct fctl *c, const char *type)
{
	int cancelled = 0, load = 0;
	if (loading || br.d == NULL || br.d->js == NULL)
		return;
	scripted(br_js_event(&br, c, type, &cancelled, &load), load);
}

/* the page's timers that are due */
static void js_timers(void)
{
	int load = 0, r = br_js_step(&br, &load);
	scripted(r, load);
}

#else	/* the ST edition: no scripts */
int page_click(int32_t x, int32_t y)
{
	(void) x, (void) y;
	return 0;
}

void ctl_event(struct fctl *c, const char *type)
{
	(void) c, (void) type;
}
#endif

void go(const char *typed)
{
	if (loading || !*typed)
		return;
	loading_on();
	loaded(br_go(&br, typed));
}

void follow(int link)
{
	if (loading)
		return;
	loading_on();
	loaded(br_follow(&br, link));
}

/* ---------------------------------------------------------------- windows */

#ifndef EW_ST
static int scripts_ok;		/* scripts may run here at all (an FPU, EWEBJS.OVL) */
#endif
static int can_open(void *ud, const char *url);

/* a window's browser: EWeb's settings and the front end's hooks */
static void br_setup(browser *b)
{
	b->sbar = pb_width();		/* a box's bars as wide and high as the AES's (5.1) */
	b->sbar_h = pb_height();
	b->text = (int32_t) (1024L * opt.zoom / 100);
	b->search = opt.search;
	b->pictures = opt.pictures;
	b->pens = opt.pens;
	b->say = say;
	b->phase = on_phase;
	b->previewed = previewed;
	b->log = log_line;
	b->can_open = can_open;
	b->save_as = save_where;
	b->snapshot = opt.snapshot;
#ifndef EW_ST
	b->scripts = scripts_ok;
	scripts_apply(b);
	b->ask = js_ask;
#endif
}

/* a new window, opened in front and made cur; NULL when there may be no
   more (win_max, by the memory), or the AES has none (cur as it was) */
static ewin *new_window(void)
{
	ewin *was = cur, *w;
	if (win_count() >= win_max || (w = win_new()) == NULL)
		return NULL;
	br_init(&w->b, 600, 819);	/* a CSS pixel is 0.8 of the screen's */
	br_setup(&w->b);
	if (!win_open()) {
		win_free(w);
		win_use(was);
		return NULL;
	}
	log_event("window %d of %d", win_count(), win_max);
	return w;
}

/* a link's or a form's page for another window (its target): the window
   of that name if there is one, else a new one (named so), else, with no
   more to be had, this one */
static void to_window(const char *url, const char *name)
{
	char u[1024], n[64];
	ewin *w = NULL;
	str_copy(u, url, sizeof u);	/* (they are the browser's, which the load changes) */
	str_copy(n, name, sizeof n);
	if (n[0])
		for (w = wins; w != NULL && strcmp(w->name, n) != 0; w = w->next)
			;
	if (w == NULL && (w = new_window()) == NULL) {
		log_event("no window for it: here");
		go(u);
		return;
	}
	str_copy(w->name, n, sizeof w->name);
	win_use(w);
	if (w->iconified) {
		short m[8] = { WM_UNICONIFY, 0, 0, 0, 0, 0, 0, 0 };
		m[3] = w->h;
		wind_get(w->h, WF_PREVXYWH, &m[4], &m[5], &m[6], &m[7]);
		win_message(m);
	}
	win_open();		/* (in front) */
	menu_update();
	go(u);
}

void follow_window(int link)
{
	char u[1024];
	if (loading)
		return;
	br_link_url(&br, link, u, sizeof u);
	if (strncmp(u, "javascript:", 11) == 0 || strncmp(u, "mailto:", 7) == 0)
		follow(link);	/* (the page's own, or EMail's) */
	else if (u[0])
		to_window(u, "");
}

/* a setting changed: each window's browser told, its page laid out
   again (relayout) or drawn again (redraw) */
static void each_window(int relayout, int redraw)
{
	ewin *was = cur, *w;
	for (w = wins; w != NULL; w = w->next) {
		win_use(w);
		br.pictures = opt.pictures;
		br.text = (int32_t) (1024L * opt.zoom / 100);
		br.snapshot = opt.snapshot;
#ifndef EW_ST
		scripts_apply(&br);
#endif
		if (relayout && br.d != NULL) {
			graf_mouse(BUSYBEE, 0);
			br_relayout(&br, win_page_w());
			graf_mouse(ARROW, 0);
			if (!redraw)
				win_page_relaid();
			else
				win_page_clamp();
		}
		if (redraw)
			win_redraw(NULL);
	}
	win_use(was);
}

void submit(struct fctl *from, struct fctl *by, int32_t x, int32_t y)
{
	if (loading)
		return;
	loading_on();
	loaded(br_submit(&br, from, by, x, y));
}

/* where it is: the page laid out again, the window's place kept (the
   page's scripts get its toggle event after, with their timers) */
void details(dnode *det)
{
	unsigned long t = pf_ms();
	int ok;
	if (loading)
		return;
	graf_mouse(BUSYBEE, 0);		/* (1.7 s on a 1040 ST) */
	ok = br_details(&br, det);
	graf_mouse(ARROW, 0);
	if (!ok) {
		/* (the page shown went to make room: the window empty) */
		if (br.p == NULL) {
			win_ctl_check();
			win_page_relaid();
		}
		log_event("details failed");
		alert(1, "[3][EWeb hasn't the memory to lay|the page out again.][ OK ]");
		return;
	}
	t = pf_ms() - t;
	win_ctl_check();
	win_page_relaid();
	log_event("details %s height %ld top %ld ms %lu", dn_attr(det, "open") ? "opened" : "closed",
		  br.p ? (long) br.p->height : 0L, (long) win_top(), t);
}

static void home(void)
{
	go(opt.home[0] ? opt.home : "about:start");
}

/* live view: the Pi's browser on the screen, showing the page shown here
   (webgate's own page, http://webgate/live) */
static void live_view(void)
{
	char u[4200];
	br_live_size(br.width, br.height, br.zoom);
	br_live_url(br.cur >= 0 ? br.url : NULL, u, sizeof u);
	go(u);
}

/* the search engine's own page: the search address up to its question
   (FrogFind's http://frogfind.com/?q=%s: http://frogfind.com/) */
static void search_home(void)
{
	char u[256];
	char *q;
	str_copy(u, opt.search, sizeof u);
	q = strchr(u, '?');
	if (q == NULL)
		q = strstr(u, "%s");
	if (q != NULL)
		*q = 0;
	if (u[0])
		go(u);
}

/* every page through the Pi gateway (webgate) when one is set and Falcon
   mode is off: always so in the ST edition */
static void gate_apply(void)
{
	if (opt.gate[0] && !opt.falcon)
		snprintf(http_gateway, sizeof http_gateway, "%.100s:%u", opt.gate, opt.gateport);
	else
		http_gateway[0] = 0;
}

/* JPEGs decoded and pictures dithered on a Falcon's DSP
   (common/atari/dspjpeg.c) unless PICDSP=0; the 68030 does them if the DSP
   can't be had, or gets pic.c's checks wrong */
void pictures_dsp_init(void)
{
#ifndef EW_ST
	pic_dsp = opt.picdsp && dspjpeg_present() ? &dspjpeg_port : NULL;
#endif
}

int falcon_mode_set(int on)
{
	if (on) {
		if (!tls_available) {
			alert(1, "[3][This is the ST edition: a|Direct connection is in|EWEB.PRG.][ OK ]");
			return 0;
		}
		if (get_cookie(0x5F435055L) < 20) {	/* _CPU */
			alert(1, "[3][A Direct connection needs|a 68020 or better (Falcon,|TT): this Atari is too slow|for TLS.][ OK ]");
			return 0;
		}
		if (!pf_exists(conn_cacert)) {
			alert(1, "[3][A Direct connection needs the|root certificates: put|CACERT.PEM and ROOTS.DAT|next to EWEB.PRG.][ OK ]");
			return 0;
		}
	}
	opt.falcon = on;
	falcon_dsp_init();
	falcon_dsp_ec_init();
	gate_apply();
	return 1;
}

/* a secure address with neither a Direct connection nor the Pi gateway: said
   plainly, and the page stays (EFTP's words) */
static int can_open(void *ud, const char *url)
{
	(void) ud;
	if (strncmp(url, "https:", 6) != 0 || http_gateway[0] || opt.falcon)
		return 1;
	log_event("secure page: needs the gateway");
	if (!tls_available)
		alert(1, "[1][Secure pages need the|Raspberry Pi gateway: on the|Pi, aera-gateway install web,|then its address in|Settings > Secure.][ OK ]");
	else
		alert(1, "[1][Secure pages need a Direct|connection (Options menu) or|the Raspberry Pi gateway|(Settings > Secure).][ OK ]");
	return 0;
}

/* View > Encoding: the encodings EWeb offers (br_encodings) in a popup
   at the mouse, by the menu item (TOS's AES has no submenus), the one
   the page is read in ticked (its tick in place of the first of the two
   spaces the others start with: popup()'s way); the page read again
   from what came in the one chosen, until another page is loaded
   (br_encoding) */
static void encoding_menu(void)
{
	char lab[1 + 20][44];	/* (on the stack while the popup is up: not kept) */
	const char *items[2 + 20];
	short mx, my, mb, ks, r, n = 0;
	int i;
	if (br.d == NULL)
		return;
	if (br.enc[0])
		snprintf(lab[0], sizeof lab[0], "  Auto");
	else
		snprintf(lab[0], sizeof lab[0], "\x08 Auto: %.35s", br_encoding_label(br.d->charset));
	items[n++] = lab[0];
	items[n++] = "-";
	for (i = 0; i < br_nencodings && i < 20; i++) {
		snprintf(lab[1 + i], sizeof lab[1 + i], "%s%s", strcmp(br.enc, br_encodings[i].name) == 0 ? "\x08 " : "  ",
			 br_encodings[i].label);
		items[n++] = lab[1 + i];
	}
	graf_mkstate(&mx, &my, &mb, &ks);
	r = popup(mx, my, items, n);
	if (r < 0 || r == 1)
		return;
	{
		const char *name = r == 0 ? "" : br_encodings[r - 2].name;
		if (strcmp(name, br.enc) == 0)
			return;		/* as it is */
		log_event("encoding %s", name[0] ? name : "auto");
		loading_on();
		loaded(br_encoding(&br, name));
	}
}

/* the text's size, per cent: only text grows or shrinks, the page's
   pictures keep their size (decoded once) and the page its width in CSS
   px (the same pictures chosen, the same media queries) */
static void set_zoom(int z)
{
	if (z < 50 || z > 200 || z == opt.zoom)
		return;
	opt.zoom = (short) z;
	settings_save();	/* a setting: on the disk at once (6.5) */
	each_window(1, 0);	/* (each window's) */
}

/* ---------------------------------------------------------------- commands */

static void open_file(void)
{
	static char path[256], name[64];
	short button = 0;
	char full[320], *p;
	if (!path[0]) {
		snprintf(path, sizeof path, "%s\\*.HTM", opt.workdir);
	}
	if (!fsel_exinput(path, name, &button, "Open a page") || !button || !name[0])
		return;
	str_copy(full, path, sizeof full);
	p = strrchr(full, '\\');
	if (p != NULL)
		p[1] = 0;
	if ((long) (strlen(full) + strlen(name)) < (long) sizeof full)
		strcat(full, name);
	go(full);
}

/* File > Save page as: the page shown as it came */
static void save_page(void)
{
	char name[16], full[320];
	if (br.src == NULL)
		return;
	br_file_name(br.url, NULL, br.mime, name, sizeof name);
	if (!ask_save("Save the page", name, full, sizeof full))
		return;
	if (br_save_page(&br, full))
		saved(full, br.src_len);
	else
		alert(1, "[3][EWeb can't write the file:|is the disk locked or full?][ OK ]");
}

/* url's file saved as it comes (br_save_url), where the user says once
   the answer has begun; given: the name its link gives (<a download>),
   or NULL */
static void save_file(const char *url, const char *given)
{
	char full[320], err[200];
	int r;
	full[0] = 0;
	loading_on();
	r = br_save_url(&br, url, given, full, sizeof full, err, sizeof err);
	loading = 0;
	graf_mouse(ARROW, 0);
	said[0] = 0;
	win_status("");
	if (r == BRS_SAVED) {
		saved(full, br_saved);
	} else if (r == BRS_STOPPED) {
		keys_flush();
		log_event("stopped heap %ld peak %ld free %ld", heap_use, heap_peak, ram_free());
		win_status("Stopped.");
	} else if (r == BRS_FAILED) {
		char t[220];
		char *c;
		atari_text(err, t, sizeof t);
		for (c = t; *c; c++)		/* the alert's own marks */
			if (*c == '[' || *c == ']' || *c == '|')
				*c = ' ';
		log_event("not saved");
		alert(1, "[3][EWeb couldn't save the file. %.120s][ OK ]", t);
	}
	conn_stop = 0;
	win_toolbar();
	menu_update();
}

void save_link(int link)
{
	char url[2048], given[256];
	const char *g;
	if (loading)
		return;
	br_link_url(&br, link, url, sizeof url);
	if (url[0] == 0)
		return;
	/* copied: a window sized while the file comes lays the page out anew */
	g = br_link_download(&br, link);
	if (g != NULL)
		str_copy(given, g, sizeof given);
	save_file(url, g != NULL ? given : NULL);
}

void command(int cmd)
{
	if (loading && cmd != C_STOP && cmd != C_ABOUT && cmd != C_KEYS && cmd != C_HELP && cmd != C_COPY)
		return;
	switch (cmd) {
	case C_ABOUT: dlg_about(); break;
	case C_NEWWIN:
		/* the home page in a new window */
		if (new_window() != NULL) {
			menu_update();
			home();
		} else if (win_count() >= win_max) {
			alert(1, "[1][EWeb has as many windows|as this Atari's memory|takes (%d): close one|first.][ OK ]", win_max);
		} else {
			alert(1, "[3][No more windows available.][ OK ]");
		}
		break;
	case C_CLOSE:
		/* the window in front; the last: EWeb quits (the guidelines' ^U) */
		if (win_count() <= 1) {
			quit = 1;
			break;
		}
		log_event("window closed");
		win_free(cur);
		win_front();
		break;
	case C_NEXTWIN: win_next(); break;
	case C_OPENURL: win_address_focus(1); break;
	case C_OPENFILE: open_file(); break;
	case C_SAVE: save_page(); break;
	case C_RELOAD:
		loading_on();
		loaded(br_reload(&br));
		break;
	case C_STOP:
		/* a load, or a picture as it comes, idle() stops; here, between
		   them, what is still to come: the pictures, the page's refresh.
		   conn_stop stays clear, as the page's scripts' requests need it */
		if (loading)
			conn_stop = 1;
		if (br_pictures_pending(&br))
			br_pictures_stop(&br);
		if (br_refresh_due(&br) >= 0)
			refresh_stop(1);
		else
			win_status("");
		break;
	case C_QUIT: quit = 1; break;
	case C_CUT: win_clip(CLIP_CUT); break;
	case C_COPY: win_clip(CLIP_COPY); break;
	case C_PASTE: win_clip(CLIP_PASTE); break;
	case C_SELALL:
		/* (in the address: all of it; else the page's text) */
		if (win_address_has_focus() || br.p == NULL)
			win_address_focus(1);
		else
			win_select_all();
		break;
	case C_FIND: win_find(0); break;
	case C_FINDNEXT: win_find(1); break;
	case C_FINDPREV: win_find(-1); break;
	case C_BACK:
		if (br_can_back(&br)) {
			loading_on();
			loaded(br_back(&br));
		}
		break;
	case C_FORWARD:
		if (br_can_forward(&br)) {
			loading_on();
			loaded(br_forward(&br));
		}
		break;
	case C_HOME: home(); break;
	case C_SEARCH: search_home(); break;
	case C_LARGER: set_zoom(opt.zoom + 10); break;
	case C_SMALLER: set_zoom(opt.zoom - 10); break;
	case C_ACTUAL: set_zoom(100); break;
	case C_ENCODING: encoding_menu(); break;
	case C_PICTURES:
		/* a setting: on the disk at once (6.5); the page laid out again
		   with its pictures or without */
		opt.pictures = (short) !opt.pictures;
		settings_save();
		each_window(1, 0);
		break;
	case C_SNAPSHOT:
		/* a setting: on the disk at once (6.5); the page shown comes again
		   the other way */
		opt.snapshot = (short) !opt.snapshot;
		each_window(0, 0);
		settings_save();
		log_event("snapshots %s", opt.snapshot ? "on" : "off");
		if (br.cur >= 0 && strncmp(br.url, "about:", 6) != 0 && strncmp(br.url, "file:", 5) != 0)
			command(C_RELOAD);
		break;
	case C_LIVE:
		live_view();
		break;
#ifndef EW_ST
	case C_SCRIPTS:
		/* a setting: on the disk at once (6.5); the page comes again
		   with its scripts or without */
		if (!br.scripts) {
			alert(1, "[1][JavaScript needs an FPU|(a 68881 or 68882): on|this Atari pages are|shown without their|scripts.][ OK ]");
			break;
		}
		if (opt.runscripts == 0) {
			alert(1, "[1][Pages' scripts are off|on every site: Settings >|Scripts turns them on.][ OK ]");
			break;
		}
		scripts_toggle();
		if (br.cur >= 0)
			command(C_RELOAD);
		break;
#endif
	case C_SETTINGS: {
		int pics = opt.pictures, zoom = opt.zoom, cache = opt.cache, palette = opt.palette, changed;
#ifndef EW_ST
		int scripted = br_scripts_for(&br, br.url);
#endif
		log_event("settings shown");
		changed = dlg_settings();
		log_event("settings %s", changed ? "changed" : "kept");
		if (changed) {
			gate_apply();
			if (palette != opt.palette) {
				/* the pages drawn again in the colours now on the
				   screen, their pictures dithered to them */
				scr_palette();
				win_colours();
			}
			falcon_dsp_init();
			falcon_dsp_ec_init();
			pictures_dsp_init();
			settings_save();
			if (cache != opt.cache) {
				dc_close();	/* what it holds kept; made smaller at the next quit */
				cache_open();
			}
			/* each window's page laid out again or drawn again */
			each_window(pics != opt.pictures || zoom != opt.zoom, palette != opt.palette);
#ifndef EW_ST
			if (br.cur >= 0 && br_scripts_for(&br, br.url) != scripted) {
				/* the page again, with its scripts or without */
				command(C_RELOAD);
				break;
			}
#endif
		}
		break;
	}
	case C_FALCON:
		if (falcon_mode_set(!opt.falcon))
			settings_save();
		break;
	case C_LOG:
		/* on or off at once, and kept (LOG=; the family's Write a log) */
		opt.log = !opt.log;
		log_start(opt.log ? 2 : 0);
		settings_save();
		break;
	case C_SETHOME:
		if (br.cur >= 0 && strncmp(br.url, "about:", 6) != 0) {
			str_copy(opt.home, br.url, sizeof opt.home);
			if (settings_save())
				status("This page is EWeb's home page now.");
			else
				alert(1, "[3][EWeb can't write EWEB.INF:|is the disk locked or full?][ OK ]");
		}
		break;
	case C_COOKIES:
		/* what sites know of you: gone from memory and the disk at once */
		if (alert(2, "[2][Forget every site's cookies?|You will be logged out of|the sites you were logged in to.][Forget|Cancel]") == 1) {
			ck_clear();
			status("EWeb has forgotten every site's cookies.");
		}
		break;
	case C_FORGETHG:
		/* the pages shown: gone from memory at once, and from the disk at
		   quit (their links not visited from the next page on) */
		if (alert(2, "[2][Forget the history?|The pages you have opened|are no longer listed, and|their links not marked.][Forget|Cancel]") == 1) {
			hg_clear();
			status("EWeb has forgotten the pages you opened.");
			if (strncmp(br.url, "about:history", 13) == 0)
				command(C_RELOAD);	/* (the list shown, empty) */
		}
		break;
	case C_HISTORY: go("about:history"); break;
	case C_CACHE: {
		/* the pages and pictures kept on the disk: gone at once */
		long bytes, n = dc_count(&bytes);
		if (alert(2, "[2][Empty the cache?|%ld pages and pictures,|%ld KB, kept on the disk|are forgotten.][Empty|Cancel]",
			  n, (bytes + 1023) >> 10) == 1) {
			dc_clear();
			br_cache_clear();
			status("EWeb's cache is empty.");
		}
		break;
	}
	case C_HELP:
		if (help_key(opt.workdir, "EWEB.HYP", 0, "Keys"))
			dlg_keys();
		break;
	case C_KEYS: dlg_keys(); break;
	case C_BMADD: {
		int r = br_bookmark(&br);
		log_event("bookmarked %d %d", r, bm_n);
		switch (r) {
		case 1:
			status("Bookmarked: it is in the Bookmarks menu.");
			bm_menu();
			break;
		case -1:
			status("This page is in your bookmarks already.");
			break;
		default:
			alert(1, "[3][EWeb can't write BOOKMARK.HTM:|is the disk locked or full?][ OK ]");
		}
		break;
	}
	case C_BMSHOW: go("about:bookmarks"); break;
	default:
		if (cmd >= C_BM && cmd < C_BM + BM_MENU && cmd - C_BM < bm_n)
			go(bm_list[cmd - C_BM].url);
	}
	menu_update();
}

/* ---------------------------------------------------------------- keys */

static int shortcut(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	unsigned char c = KEY_ASCII(key);
	if (crash_test(scan))		/* test builds only: F1-F4 crash on purpose */
		return 1;
	if (scan == 0x62) {		/* Help */
		command(C_HELP);
		return 1;
	}
	if (scan == 0x61) {		/* Undo: Back, with Shift Forward */
		command(shift ? C_FORWARD : C_BACK);
		return 1;
	}
	if (key_ctrl(kstate, key)) {
		int cmd = C_NONE;
		switch (scan) {
		case 0x26: cmd = C_OPENURL; break;		/* L */
		case 0x33: cmd = C_SETTINGS; break;		/* , */
		case 0x18: cmd = C_OPENFILE; break;		/* O */
		case 0x1f: cmd = C_SAVE; break;			/* S */
		case 0x20: cmd = C_BMADD; break;		/* D */
		case 0x30: cmd = C_BMSHOW; break;		/* B */
		case 0x21: cmd = C_FIND; break;			/* F */
		case 0x22: cmd = shift ? C_FINDPREV : C_FINDNEXT; break;	/* G */
		case 0x15: cmd = C_HISTORY; break;		/* Y */
		case 0x13: cmd = C_RELOAD; break;		/* R */
		case 0x10: cmd = C_QUIT; break;			/* Q */
		case 0x16: cmd = C_CLOSE; break;		/* U: the window, the last one quits */
		case 0x11: cmd = C_NEXTWIN; break;		/* W: the next window */
		case 0x31: cmd = C_NEWWIN; break;		/* N */
		case 0x2c: cmd = shift ? C_FORWARD : C_BACK; break;	/* Z */
		case 0x0b: cmd = C_ACTUAL; break;		/* 0 */
		case 0x70: cmd = C_ACTUAL; break;		/* 0 on the keypad */
		case 0x0c: case 0x4a: cmd = C_SMALLER; break;	/* - */
		case 0x0d: case 0x4e: cmd = C_LARGER; break;	/* = + */
		case 0x2e:					/* C: the page's text, with nothing chosen */
			if (!win_address_has_focus()) {
				cmd = C_COPY;
				break;
			}
			return 0;
		case 0x1e:					/* A */
			if (!win_address_has_focus()) {
				cmd = C_SELALL;
				break;
			}
			return 0;
		}
		if (c == '+')
			cmd = C_LARGER;
		if (cmd != C_NONE) {
			command(cmd);
			return 1;
		}
	}
	return 0;
}

static void av_start(const char *s);

/* while a dialog is a window (MiNT, MagiC: 3.5), the AES's requests to
   draw EWeb's own windows (WM_REDRAW), to the window each names */
static void dialog_redraw(const short *msg)
{
	win_message(msg);
}

/* an event of the main loop (or one put off while a picture came) */
static void handle(short which, short *msg, AEVENT *pev, short *hover_x, short *hover_y)
{
	AEVENT ev = *pev;
	if (win_front())
		menu_update();	/* (another window in front: its page's commands) */
	if (which & MU_M1) {
		short k;
		graf_mkstate(hover_x, hover_y, &k, &k);
		win_hover(*hover_x, *hover_y);
	}
	if ((which & MU_MESAG) && notify_message(msg))
		which &= ~MU_MESAG;	/* (BGEM.NG's: its answer, a button) */
	if (which & MU_MESAG) {
		const char *va;
		switch (msg[0]) {
		case MN_SELECTED:
			command(mbar_command(&mb, msg[4]));
			menu_tnormal(mb.tree, msg[3], 1);
			break;
		case AP_TERM:
			quit = 1;
			break;
		default: {
			DROP dr;
			/* a file dropped is opened, as VA_START's */
			switch (dd_message(msg, DD_WANT_FILES, &dr)) {
			case DD_FILES: av_start(dr.data); break;
			case DD_NONE:
				if (av_message(msg, &va) == AV_START)
					av_start(va);
				else
					win_message(msg);
			}
			dd_done(&dr);
		}
		}
		if (!quit && win_front())
			menu_update();
	}
	if (quit)
		return;
	if (cur->iconified)	/* an icon takes no keys or clicks */
		which &= ~(MU_KEYBD | MU_BUTTON);
	if (which & MU_KEYBD) {
		if (KEY_SCAN(ev.kreturn) == 0x01 && br_refresh_due(&br) >= 0)
			refresh_stop(1);	/* Esc: the page's refresh first */
		else if (!shortcut(ev.kstate, ev.kreturn))
			win_key(ev.kstate, ev.kreturn);
		menu_update();
	}
	if (which & MU_BUTTON) {
		short h = wind_find(ev.mx, ev.my), t, dd, mx, my, mbt, ks;
		wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
		if (h > 0 && h != t) {	/* a click in a window behind: bring it up too */
			wind_set(h, WF_TOP, 0, 0, 0, 0);
			if (win_of(h) != NULL) {
				win_use(win_of(h));
				menu_update();
			}
		}
		if (win_mouse(ev.mx, ev.my, ev.mbutton, ev.breturn >= 2 ? 2 : 1, ev.kstate))
			menu_update();
		do {			/* the button let go */
			graf_mkstate(&mx, &my, &mbt, &ks);
			if (mbt & 3)
				evnt_timer_(10);
		} while (mbt & 3);
	}
}

/* the page's refresh is due: on to where it goes, in its place in the
   history (its address in the log without a query, 7.3) */
static void refreshed(void)
{
	log_event("refresh %lu %.*s", br.refresh_s, (int) strcspn(br.refresh, "?"), br.refresh);
	loading_on();
	loaded(br_refresh_go(&br));
}

/* the next of the page's pictures: fetched and decoded, then drawn */
static void pictures(void)
{
	int32_t y0, y1;
	int r;
	unsigned long t = pf_ms();
	static const ewin *ph_of;	/* the window the ribbon's phase is of */
	if (ph_of != cur) {
		ph_of = cur;
		ph = -1;
	}
	pics = 1;
	conn_stop = 0;
	conn_bytes = 0;
	said[0] = 0;
	r = br_pictures_step(&br, win_top(), win_page_h(), &y0, &y1);
	pics = 0;
	switch (r) {
	case BRP_DRAWN:
		if (y1 > y0)
			win_page_drawn(y0, y1);
		break;
	case BRP_LAYOUT:
		log_event("relaid %.*s height %ld ms %lu (layout %lu)", (int) strcspn(br.url, "?"), br.url,
			  br.p ? (long) br.p->height : 0L, pf_ms() - t, br.pic_lay_ms);
		win_page_relaid();
		break;
	case BRP_STOPPED:
		break;
	}
	conn_stop = 0;
	said[0] = 0;
	ph_pictures();
	if (!br_pictures_pending(&br)) {
		win_status(br.pic_stopped ? "Pictures stopped." : "");
		log_event("pictures %.*s %d", (int) strcspn(br.url, "?"), br.url, br.pic_done);
		log_event("throbber %d", win_throb_frames());
		trim();
		if (opt.log) {
			conn_log_hold(0);	/* the page's and its pictures' lines, at once */
			conn_log_hold(1);
		}
		menu_update();
	}
}

/* the window whose pictures come next: the one in front's first, then
   the others' (not an icon's); NULL none */
static ewin *pictures_window(void)
{
	ewin *w;
	if (!cur->iconified && br_pictures_pending(&br))
		return cur;
	for (w = wins; w != NULL; w = w->next)
		if (!w->iconified && w->h > 0 && br_pictures_pending(&w->b))
			return w;
	return NULL;
}

/* each window's throbber: moving on while its pictures come, at rest
   once they have */
static void throb_all(void)
{
	ewin *was = cur, *w;
	for (w = wins; w != NULL; w = w->next) {
		win_use(w);
		win_throb(!quit && br_pictures_pending(&br));
	}
	win_use(was);
}

/* ---------------------------------------------------------------- start and end */

/* EWeb's own folder, where its files are (EWEB.FNT, EWEB.INF, the cache
   and the rest): the folder of the command the AES started it with
   (common's app_dir, the guidelines' 6.1), not the current folder, which
   TOS 1.04 and 4.04 leave as the desktop's when another program starts
   EWeb (Erez, 28 Oct 2026: EWeb didn't find its fonts). Without its last
   backslash, as path_join takes it */
static void work_dir(char *out, int size)
{
	snprintf(out, size, "%s", app_dir());
	if (out[0] && out[strlen(out) - 1] == '\\')
		out[strlen(out) - 1] = 0;
}

/* EWeb crashed and CRASH.LOG is written (common/atari/crash.c): say so
   and leave GEM tidy, with the AES and VDI alone (the heap may be what
   went wrong) */
static void crash_quit(const char *what)
{
	char a[200];
	const ewin *w;
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof a, "[3][EWeb has crashed|(%s).|What happened is in|CRASH.LOG, in EWeb's folder.][ Quit ]", what);
	form_alert(1, a);
	for (w = wins; w != NULL; w = w->next)
		if (w->h > 0) {
			wind_close(w->h);
			wind_delete(w->h);
		}
	menu_bar(mb.tree, 0);
	scr_exit();
	v_clsvwk(vdi_h);
	appl_exit();
}

/* the command line: an address or a file to open first, the basepage's
   or the ARGV way's (MiNT, MagiC: common's cmd_line, the guidelines' 4.3),
   without the quotes cmd_line puts round a name with a space */
static void command_line(char *out, int size)
{
	const char *s = cmd_line();
	int len;
	snprintf(out, size, "%s", s);
	str_trim(out);
	len = (int) strlen(out);
	if (len >= 2 && out[0] == '"' && out[len - 1] == '"') {
		memmove(out, out + 1, len - 2);
		out[len - 2] = 0;
	}
}

/* VA_START (common/ui/av.c) from the desktop or another program: an
   address or a file to show, or nothing (the window to the front) */
static void av_start(const char *s)
{
	char what[512];
	if (cur->h > 0 && cur->iconified) {
		short m[8] = { WM_UNICONIFY, 0, 0, 0, 0, 0, 0, 0 };
		m[3] = cur->h;
		wind_get(cur->h, WF_PREVXYWH, &m[4], &m[5], &m[6], &m[7]);
		win_message(m);
	}
	win_open();
	if (av_arg(s, what, sizeof what) != 0 && what[0])
		go(what);
}

#ifdef EW_ST
int main(void)
#else
/* EWEB.PRG's main() is common's cpucheck.c, 68000 code: on an ST it shows
   this and goes, where the 68030 code from appl_init on would die */
const long cpu_need = 30;
const char cpu_alert[] = "[3][EWEB.PRG needs a Falcon or|a TT. On an ST or STE, run|EWEBST.PRG, the ST edition.][ Quit ]";

int app_main(void)
#endif
{
	short work_out[57], msg[8], d;
	char path[256], first[256];
	AEVENT ev;
	short hover_x = -1, hover_y = -1;

	/* the stack (crt0's, EW_STACK bytes) ends that far below main's frame:
	   the cascade and the layout go no deeper into a page than EW_STACK_ROOM
	   above its end, room for the deepest call under them (the layout's
	   1.5 KB, and 5 KB for a picture's address resolved in it; GCC's
	   -fcallgraph-info=su) with some to spare; the cascade stops sooner
	   (src/css/select.c) */
	ec_stack_floor = (char *)__builtin_frame_address(0) - EW_STACK + EW_STACK_ROOM;
	{
		extern int mem_unaligned_ok;
		mem_unaligned_ok = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	}
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	if (vdi_h <= 0) {
		/* no workstation (the VDI out of memory, 2.1): said by the AES,
		   never drawn with */
		form_alert(1, "[3][EWeb couldn't open a VDI|workstation: too little|memory is free for it.|Quit a program or accessory|and start EWeb again.][ OK ]");
		appl_exit();
		return 1;
	}
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	if (!screen_check("EWeb", scr_w)) {	/* not in ST low */
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	vst_alignment(vdi_h, 0, 5);
	text_font(vdi_h, 1, cw, ch);	/* the system font: text_n's quick way (common's text.c) */
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(BUSYBEE, 0);

	pf_size_open = pf_open_size;	/* a file read: opened, not looked up first (6.5) */
	work_dir(opt.workdir, sizeof opt.workdir);
	crash_install(opt.workdir, "EWeb", EWEB_VERSION);
	crash_cleanup = crash_quit;
	help_init();
	av_init("EWEB");
	notify_init("EWEB    ");	/* notifications through BGEM.NG (4.5) */
	notify_clicked = notified;
	notify_ask();
	dlg_title = "EWeb";		/* dialogs' windows where the AES has them (3.5) */
	dlg_redraw_hook = dialog_redraw;
	settings_load();
	if (!scr_init()) {
		graf_mouse(ARROW, 0);
		alert(1, "[3][EWeb can't draw pages on this|screen: it needs 2 to 256|colours, or the Falcon's true|colour.][ Quit ]");
		scr_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	path_join(path, sizeof path, opt.workdir, "EWEB.FNT");
	if (!font_init(path)) {
		graf_mouse(ARROW, 0);
		alert(1, "[3][EWeb's fonts are missing:|put EWEB.FNT next to|EWEB.PRG.][ Quit ]");
		scr_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	/* secure connections: the certificates, the seed and the signatures
	   kept next to the program, as EWGET.TTP keeps them */
	net_init();
	if (opt.log)
		log_start(1);
	path_join(conn_cacert, sizeof conn_cacert, opt.workdir, "CACERT.PEM");
#ifndef EW_ST
	/* the TLS code, the family's TLS.OVL beside it: read at the first
	   Direct connection (never through the Pi) */
	path_join(tls_ovl_path, sizeof tls_ovl_path, opt.workdir, "TLS.OVL");
#endif
	{
		/* the cookies that last: read when the first page needs them */
		char ck[256], key[256];
		path_join(ck, sizeof ck, opt.workdir, "COOKIES.TXT");
		path_join(key, sizeof key, opt.workdir, "EWEB.KEY");
		ck_file(ck, key);
		secret_warn = key_warn;
	}
	cache_open();
	path_join(tls_roots_cache, sizeof tls_roots_cache, opt.workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof tls_seed_path, opt.workdir, "SEED.DAT");
	path_join(tls_sigs_path, sizeof tls_sigs_path, opt.workdir, "SIGS.DAT");
	tls_file_stamp = pf_stamp;
	tls_file_info = pf_info;	/* CACERT.PEM looked up once (6.5) */
	falcon_dsp_init();
	falcon_dsp_ec_init();
	pictures_dsp_init();
	pf_idle = idle;
#ifdef EW_ST
	snprintf(http_user_agent, sizeof http_user_agent, "EWeb/%s (Atari ST)", EWEB_VERSION);
#else
	snprintf(http_user_agent, sizeof http_user_agent, "EWeb/%s (Atari Falcon030)", EWEB_VERSION);
#endif
	gate_apply();

	{
		/* the bookmarks: read once a start (6.5), for their menu */
		char bm[256];
		path_join(bm, sizeof bm, opt.workdir, "BOOKMARK.HTM");
		bm_file(bm);
		bm_items();
		/* the pages shown: read when a page's links are first marked */
		path_join(bm, sizeof bm, opt.workdir, "HISTORY.TXT");
		hg_file(bm);
	}
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EWeb: not enough memory.][ Quit ]");
		scr_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);

	{
		/* pages kept for Back and Forward (the heap takes TT RAM first):
		   a quarter of the free TT RAM up to 24 MB, else an eighth of
		   the free ST RAM up to 2 MB, shared by the windows */
		long tt = (long) Mxalloc(-1L, 1), st = (long) Mxalloc(-1L, 0);
		if (st < 0) {		/* TOS 1.x has no Mxalloc: all is ST RAM */
			tt = 0;
			st = (long) Malloc(-1L);
		}
		if (tt > 0)
			br_keep_max = tt / 4 < (24L << 20) ? tt / 4 : (24L << 20);
		else if (st > 0)
			br_keep_max = st / 8 < (2L << 20) ? st / 8 : (2L << 20);
		/* and pictures (their files and decoded pixels): an eighth of
		   the TT RAM up to 16 MB, else a sixteenth of the ST RAM up to
		   1 MB, a file of a million pixels at most (GIF and interlaced
		   PNG decode whole, four bytes a pixel) */
		if (tt > 0) {
			img_max = tt / 8 < (16L << 20) ? tt / 8 : (16L << 20);
		} else if (st > 0) {
			img_max = st / 16 < (1L << 20) ? st / 16 : (1L << 20);
			img_max_px = 1L << 20;
		}
		/* windows, each with a page laid out: eight beside TT RAM, four
		   in 6 MB of ST RAM, else two (a 4 MB Falcon, a 1040 ST) */
		win_max = tt > 0 ? 8 : st >= (6L << 20) ? 4 : 2;
	}
	/* scripts: EWEB.PRG with an FPU (the _FPU cookie), on the sites chosen;
	   QuickJS is EWEBJS.OVL next to the program, loaded when first needed */
#ifndef EW_ST
	path_join(path, sizeof path, opt.workdir, "EWEBJS.OVL");
	js_ovl_at(path);
	scripts_ok = js_built && (get_cookie(0x5F465055L) >> 16) != 0;
#endif
	if (new_window() == NULL) {
		graf_mouse(ARROW, 0);
		alert(1, "[3][EWeb can't open its window:|the AES has no more, or|memory is short.][ Quit ]");
		menu_bar(mb.tree, 0);
		scr_exit();
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	menu_update();
	crash_offer("EWeb", "in EWeb's folder");
	if (strcmp(net_stack(), "none") == 0)
		status("No network: load STinG to reach web sites (files can be opened).");

	log_event("ready %s %dx%d buf %d heap %ld free %ld", scr_mode(), scr_w, scr_h, scr_rows(), heap_use,
		  ram_free());
	log_event("box bars %d wide, arrows %d, across %d high", pb_width(), pb_arrow_h(), pb_height());	/* (for the rigs) */
	{
		/* the window drawn whole before the first page is read: its
		   redraw handled now, not when the page's making first lets
		   GEM in (on a stock Falcon the desktop showed in it for 8 s,
		   Erez, 26 Oct 2026) */
		short msg[8];
		AEVENT ev;
		int n = 0;
		while (n++ < 8 && (evnt_multi_(MU_MESAG | MU_TIMER, 1, 1, 1, 0, msg, &ev) & MU_MESAG))
			handle(MU_MESAG, msg, &ev, &hover_x, &hover_y);
	}
	command_line(first, sizeof first);
	if (first[0])
		go(first);
	else
		home();
	evnt_set_m1(1, 0, 0, 1, 1);

	while (!quit) {
		short which;
		int more = !loading && pictures_window() != NULL;
		long due = loading || cur->iconified ? -1 : br_js_due(&br);
		long rdue = refresh_due();
		int rib;
		short wait;
		if (rdue >= 0 && (due < 0 || rdue < due))
			due = rdue;	/* the page's refresh comes before its scripts' next timer */
		/* the ribbon, full: gone a moment later, whatever else is waited for */
		rib = !more && win_progress_shown();
		wait = more ? 0 : due >= 0 && due < 1000 ? (short) due : 1000;
		if (rib && wait > 200)
			wait = 200;
		which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1 | (more || due >= 0 || rib ? MU_TIMER : 0),
				    0x102, 3, 0, wait, msg, &ev);
		gap_event(which & MU_KEYBD ? "key" : which & MU_BUTTON ? "click" : which & MU_MESAG ? "message" :
			  which & MU_M1 ? "mouse" : "timer");
		handle(which & ~MU_TIMER, msg, &ev, &hover_x, &hover_y);
		if ((which & MU_TIMER) && !quit && rdue >= 0) {
			if (refresh_due() == 0) {
				refreshed();	/* before the page's pictures: it is going */
				continue;
			}
			win_status_again();	/* the time left, on the status bar at rest */
		}
#ifndef EW_ST
		if ((which & MU_TIMER) && !quit && !loading && br_js_due(&br) == 0)
			js_timers();
#endif
		if ((which & MU_TIMER) && !quit && more && pictures_window() != NULL) {
			ewin *was = cur;
			win_use(pictures_window());
			pictures();
			win_use(was);
			if (nlater > 0) {
				/* (copied first: what one does may put off more) */
				int n = nlater, i;
				static LATER_EV now[LATER];
				memcpy(now, later, n * sizeof later[0]);
				nlater = 0;
				for (i = 0; i < n && !quit; i++)
					handle(now[i].which, now[i].msg, &now[i].ev, &hover_x, &hover_y);
			}
		}
		/* moving on while the pictures come; at rest once they have */
		throb_all();
	}

	log_event("quit");
	{
		/* where this machine waited longest (gem.c keeps the four
		   longest stretches between event calls; the guidelines' Speed) */
		const char *slow = aes_slowest();
		char line[200];
		snprintf(line, sizeof line, "the slowest: %s", slow[0] ? slow : "none");
		conn_log("EWeb", " -- ", line, (long) strlen(line));
		log_event("%s", line);
	}
	graf_mouse(BUSYBEE, 0);
	{
		/* each step timed, for the log (Erez's quit was slow on his
		   real disk, 27 Oct 2026) */
		unsigned long t0 = pf_ms(), tw, ts, tc, th, tf, td;
		char line[200];
		/* the quit's box (Erez, 29 Oct 2026): each step said as it
		   starts, the bar by what each has to write (a file's few
		   disk commands; the cache's answers by their size) */
		long pend, answers, total, done = 0;
		int wck = ck_unsaved() ? 3 : 0, whg = hg_unsaved() ? 3 : 0, wfl, wix;
		dc_unsaved(&pend, &answers);
		wfl = pend > 0 ? 2 + (int) (pend >> 15) : 0;
		wix = answers > 0 ? 3 + (int) (answers / 100) : 0;
		total = 2 + wck + whg + wfl + wix;
#define QUIT_STEP(words, weight) (quit_box(words, (int) (done * 1000 / total)), done += (weight))
		QUIT_STEP("Closing connections and windows...", 1);
		http_close_all();
		{
			/* the window in front last: its place is kept (WINDOW=) */
			ewin *front = cur, *w;
			for (w = wins; w != NULL; w = w->next)
				if (w != front) {
					win_use(w);
					win_close();
				}
			win_use(front);
			win_close();
		}
		tw = pf_ms();
		QUIT_STEP("Saving the settings...", 1);
		settings_save();
		ts = pf_ms();
		if (wck)
			QUIT_STEP("Saving the cookies...", wck);
		ck_save();	/* the cookies that last, once, if they changed */
		tc = pf_ms();
		if (whg)
			QUIT_STEP("Saving the history...", whg);
		hg_save();	/* the pages shown, once, if they changed */
		th = pf_ms();
		if (wfl)
			QUIT_STEP("Storing the cache's pages and pictures...", wfl);
		dc_flush();	/* the cache's answers still in memory */
		tf = pf_ms();
		if (wix)
			QUIT_STEP("Saving the cache's list...", wix);
		dc_close();	/* its index, once */
		td = pf_ms();
#undef QUIT_STEP
		quit_box("Done.", 1000);
		if (td - t0 > 500)
			evnt_timer_(300);	/* the full bar seen after a long quit */
		quit_box_close();
		snprintf(line, sizeof line, "quit: connections and windows %lu ms, settings %lu, cookies %lu, "
			 "history %lu, the cache's last answers %lu, its index %lu", tw - t0, ts - tw, tc - ts, th - tc,
			 tf - th, td - tf);
		conn_log("EWeb", " -- ", line, (long) strlen(line));
		log_event("%s", line);
	}
	if (opt.log)
		conn_log_hold(0);	/* the rest of the log, once */
	menu_bar(mb.tree, 0);
	av_exit();
	font_free();
	if (scr_exit())
		log_event("colours given back");
	v_clsvwk(vdi_h);
	crash_remove();
	appl_exit();
	return 0;
}
