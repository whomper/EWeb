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
#include "aboutbox.h"
#include "av.h"
#include "clip.h"
#include "conn.h"
#include "cookie.h"
#include "crash.h"
#include "dd.h"
#include "font.h"
#include "help.h"
#include "http.h"
#include "img.h"
#include "mbar.h"
#include "plat.h"
#include "secret.h"
#include "tls.h"
#include "util.h"

#ifndef EWEB_VERSION
#define EWEB_VERSION ""
#endif

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;
short ap_id;
EWOPT opt = { 1, 1, "", "http://frogfind.com/?q=%s", 100, 0, 0, 0, 0, "", 1 };
browser br;
int loading;
static short quit;
static int pics;		/* the page's pictures are coming: the window scrolls meanwhile */
/* what came while a picture did and isn't the window's own (a menu, a
   click, a key that isn't scrolling): done when the picture has stopped */
static struct {
	short which, msg[8];
	AEVENT ev;
} later;

#define FROGFIND "http://frogfind.com/?q=%s"

/* ---------------------------------------------------------------- the menu bar */

static const MBAR_ITEM m_desk[] = {
	{ "About EWeb...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "Open address...", "^L", C_OPENURL },
	{ "Open file...", "^O", C_OPENFILE },
	{ "-", 0, 0 },
	{ "Reload", "^R", C_RELOAD },
	{ "Stop", "Esc", C_STOP },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Cut", "^X", C_CUT },
	{ "Copy", "^C", C_COPY },
	{ "Paste", "^V", C_PASTE },
	{ "-", 0, 0 },
	{ "Select all", "^A", C_SELALL },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_view[] = {
	{ "Back", "Undo", C_BACK },
	{ "Forward", "\x01^Z", C_FORWARD },
	{ "Home", 0, C_HOME },
	{ "-", 0, 0 },
	{ "Larger text", "^+", C_LARGER },
	{ "Smaller text", "^-", C_SMALLER },
	{ "Actual size", "^0", C_ACTUAL },
	{ "-", 0, 0 },
	{ "Show pictures", 0, C_PICTURES },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_opts[] = {
	{ "Use this page as home", 0, C_SETHOME },
	{ "-", 0, 0 },
	{ "Forget cookies...", 0, C_COOKIES },
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
	{ " Options ", m_opts },
	{ " Help ", m_help },
	{ 0, 0 }
};
static MBAR mb;

static void enable(int cmd, int on)
{
	short i = mbar_object(&mb, cmd);
	if (i >= 0)
		menu_ienable(mb.tree, i, on ? 1 : 0);
}

void menu_update(void)
{
	int addr = win_address_has_focus();
	enable(C_RELOAD, !loading && br.cur >= 0);
	enable(C_STOP, loading || br_pictures_pending(&br));
	enable(C_BACK, !loading && br_can_back(&br));
	enable(C_FORWARD, !loading && br_can_forward(&br));
	enable(C_HOME, !loading);
	enable(C_OPENURL, !loading);
	enable(C_OPENFILE, !loading);
	enable(C_CUT, addr);
	enable(C_PASTE, addr);
	enable(C_COPY, addr || br.p != NULL);
	enable(C_SELALL, 1);
	enable(C_LARGER, !loading && opt.zoom < 200);
	enable(C_SMALLER, !loading && opt.zoom > 50);
	enable(C_ACTUAL, !loading && opt.zoom != 100);
	enable(C_SETHOME, !loading && br.cur >= 0);
	enable(C_PICTURES, !loading);
	{
		short i = mbar_object(&mb, C_PICTURES);
		if (i >= 0)
			menu_icheck(mb.tree, i, opt.pictures ? 1 : 0);
	}
}

/* ---------------------------------------------------------------- the settings */
/* EWEB.INF next to the program (a program without an accessory keeps its
   settings there, the guidelines' 6.1): NAME=value lines, a ';' note
   above each, other lines kept as they are. The file's text is read once,
   at start, and kept: a save puts the settings into it and writes it only
   when that changed it (the guidelines' 6.5) */

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
}

/* one setting into the text: its line replaced, or its note and line
   added at the end */
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
			sb_adds(&t, at + ll);
			sb_free(s);
			*s = t;
			return;
		}
		at += ll;
	}
	if (s->len > 0 && s->s[s->len - 1] != '\n')
		sb_adds(s, "\r\n");
	sb_printf(s, "; %s\r\n%s=%s\r\n", note, key, value);
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
		sb_add(&s, inf_text, inf_len);
	} else {
		sb_adds(&s, "; EWeb's settings (EWeb writes them; lines it doesn't know are kept)\r\n");
	}
	put(&s, "HOME", opt.home, "the start page: an address, or nothing for EWeb's own");
	put(&s, "SEARCH", opt.search, "words typed in the address go here, %s for them (FrogFind: " FROGFIND ")");
	snprintf(v, sizeof v, "%d", opt.zoom);
	put(&s, "ZOOM", v, "the size of pages, per cent (100: a CSS pixel is 0.8 of the screen's)");
	snprintf(v, sizeof v, "%d", opt.dsp);
	put(&s, "DSP", v, "1: the DSP does the secure connections' arithmetic, 0: the 68030 does");
	snprintf(v, sizeof v, "%d", opt.pictures);
	put(&s, "PICTURES", v, "1: pages' pictures are shown, 0: only what they say instead (a slow line)");
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
static void log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_event(const char *fmt, ...)
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

/* a picture is stopped for what came: it is done after */
static void put_off(short which, const short *msg, const AEVENT *ev)
{
	if (later.which == 0) {
		later.which = which;
		memcpy(later.msg, msg, sizeof later.msg);
		later.ev = *ev;
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
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 5, msg, &ev);
	if (pics) {
		if (which & MU_MESAG) {
			if (msg[0] == MN_SELECTED) {
				if (mbar_command(&mb, msg[4]) == C_STOP) {
					conn_stop = 1;
					br_pictures_stop(&br);
					menu_tnormal(mb.tree, msg[3], 1);
				} else {
					put_off(MU_MESAG, msg, &ev);
				}
			} else if (msg[0] == AP_TERM) {
				put_off(MU_MESAG, msg, &ev);
			} else {
				win_message(msg);
			}
		}
		if (which & MU_KEYBD) {
			short scan = KEY_SCAN(ev.kreturn);
			if (scan == 0x01) {
				conn_stop = 1;
				br_pictures_stop(&br);
			} else if (scan == 0x48 || scan == 0x50 ||
				   (!win_address_has_focus() && (scan == 0x39 || scan == 0x47) &&
				    !key_ctrl(ev.kstate, ev.kreturn))) {
				win_key(ev.kstate, ev.kreturn);	/* scrolling: the page stays */
			} else {
				put_off(MU_KEYBD, msg, &ev);
			}
		}
		if (which & MU_BUTTON)
			put_off(MU_BUTTON, msg, &ev);
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
	graf_mouse(BUSYBEE, 0);
	win_toolbar();
	menu_update();
}

/* what a load came to, on the screen */
static void loaded(int r)
{
	loading = 0;
	graf_mouse(ARROW, 0);
	said[0] = 0;
	switch (r) {
	case BR_SHOWN: {
		unsigned long ms_drawn = pf_ms();
		char shown[440];
		win_page_changed();
		ms_drawn = pf_ms() - ms_drawn;
		log_url(shown, sizeof shown);
		{
			short x, y, w, h;
			win_page_area(&x, &y, &w, &h);
			log_event("shown %.400s height %ld links %d area %d %d %d %d top %ld ms %lu "
				  "(fetch %lu, parse %lu, style %lu, layout %lu, drawn %lu) title %.200s", shown,
				  br.p ? (long) br.p->height : 0L, br.p ? br.p->nlinks : 0, x, y, w, h, (long) win_top(),
				  pf_ms() - load_t0, br.ms_fetch, br.ms_parse, br.ms_style, br.ms_layout, ms_drawn,
				  br.title);
		}
		break;
	}
	case BR_SAME:
		log_event("same %.*s top %ld", (int) strcspn(br.url, "?"), br.url, (long) br.scroll_to);
		win_address(br.url);
		win_scroll_to(br.scroll_to >= 0 ? br.scroll_to : 0);
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
		log_event("stopped");
		keys_flush();
		win_address(br.url);
		win_status(conn_stop || r == BR_STOPPED ? "Stopped." : "");
		win_toolbar();
		break;
	}
	conn_stop = 0;
	menu_update();
}

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

void submit(struct fctl *from, struct fctl *by, int32_t x, int32_t y)
{
	if (loading)
		return;
	loading_on();
	loaded(br_submit(&br, from, by, x, y));
}

static void home(void)
{
	go(opt.home[0] ? opt.home : "about:start");
}

/* the size of pages: a CSS pixel is 0.8 of the screen's at 100% */
static void set_zoom(int z)
{
	if (z < 50 || z > 200 || z == opt.zoom)
		return;
	opt.zoom = (short) z;
	settings_save();	/* a setting: on the disk at once (6.5) */
	br.zoom = (int32_t) (819L * opt.zoom / 100);
	if (br.d != NULL) {
		graf_mouse(BUSYBEE, 0);
		br_relayout(&br, win_page_w());
		graf_mouse(ARROW, 0);
	}
	win_scroll_to(win_top());
	win_redraw(NULL);
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

void command(int cmd)
{
	if (loading && cmd != C_STOP && cmd != C_ABOUT && cmd != C_KEYS && cmd != C_HELP && cmd != C_COPY)
		return;
	switch (cmd) {
	case C_ABOUT: dlg_about(); break;
	case C_OPENURL: win_address_focus(1); break;
	case C_OPENFILE: open_file(); break;
	case C_RELOAD:
		loading_on();
		loaded(br_reload(&br));
		break;
	case C_STOP:
		conn_stop = 1;
		br_pictures_stop(&br);
		win_status("");
		break;
	case C_QUIT: quit = 1; break;
	case C_CUT: win_clip(CLIP_CUT); break;
	case C_COPY: win_clip(CLIP_COPY); break;
	case C_PASTE: win_clip(CLIP_PASTE); break;
	case C_SELALL: win_address_focus(1); break;
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
	case C_LARGER: set_zoom(opt.zoom + 10); break;
	case C_SMALLER: set_zoom(opt.zoom - 10); break;
	case C_ACTUAL: set_zoom(100); break;
	case C_PICTURES:
		/* a setting: on the disk at once (6.5); the page laid out again
		   with its pictures or without */
		opt.pictures = (short) !opt.pictures;
		settings_save();
		br.pictures = opt.pictures;
		if (br.d != NULL) {
			graf_mouse(BUSYBEE, 0);
			br_relayout(&br, win_page_w());
			graf_mouse(ARROW, 0);
			win_page_relaid();
		}
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
	case C_HELP:
		if (help_key(opt.workdir, "EWEB.HYP", 0, "Keys"))
			dlg_keys();
		break;
	case C_KEYS: dlg_keys(); break;
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
		case 0x18: cmd = C_OPENFILE; break;		/* O */
		case 0x13: cmd = C_RELOAD; break;		/* R */
		case 0x10: cmd = C_QUIT; break;			/* Q */
		case 0x11: cmd = C_QUIT; break;			/* W: the one window */
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

/* an event of the main loop (or one put off while a picture came) */
static void handle(short which, short *msg, AEVENT *pev, short *hover_x, short *hover_y)
{
	AEVENT ev = *pev;
	if (which & MU_M1) {
		short k;
		graf_mkstate(hover_x, hover_y, &k, &k);
		win_hover(*hover_x, *hover_y);
	}
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
	}
	if (win_iconified)	/* an icon takes no keys or clicks */
		which &= ~(MU_KEYBD | MU_BUTTON);
	if (which & MU_KEYBD) {
		if (!shortcut(ev.kstate, ev.kreturn))
			win_key(ev.kstate, ev.kreturn);
		menu_update();
	}
	if (which & MU_BUTTON) {
		short h = wind_find(ev.mx, ev.my), t, dd, mx, my, mbt, ks;
		wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
		if (h > 0 && h != t)	/* a click in a window behind: bring it up too */
			wind_set(h, WF_TOP, 0, 0, 0, 0);
		if (win_mouse(ev.mx, ev.my, ev.mbutton, ev.breturn >= 2 ? 2 : 1, ev.kstate))
			menu_update();
		do {			/* the button let go */
			graf_mkstate(&mx, &my, &mbt, &ks);
			if (mbt & 3)
				evnt_timer_(10);
		} while (mbt & 3);
	}
}

/* the next of the page's pictures: fetched and decoded, then drawn */
static void pictures(void)
{
	int32_t y0, y1;
	int r;
	unsigned long t = pf_ms();
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
		log_event("relaid %.*s height %ld ms %lu", (int) strcspn(br.url, "?"), br.url, br.p ? (long) br.p->height : 0L, pf_ms() - t);
		win_page_relaid();
		break;
	case BRP_STOPPED:
		break;
	}
	conn_stop = 0;
	said[0] = 0;
	if (!br_pictures_pending(&br)) {
		win_status(br.pic_stopped ? "Pictures stopped." : "");
		log_event("pictures %.*s %d", (int) strcspn(br.url, "?"), br.url, br.pic_done);
		menu_update();
	}
}

/* ---------------------------------------------------------------- start and end */

static void work_dir(char *out, int size)
{
	char path[160];
	short drv = (short) Dgetdrv();
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(out, size, "%c:%s", 'A' + drv, path);
	if (out[strlen(out) - 1] == '\\')
		out[strlen(out) - 1] = 0;
}

/* EWeb crashed and CRASH.LOG is written (common/atari/crash.c): say so
   and leave GEM tidy, with the AES and VDI alone (the heap may be what
   went wrong) */
static void crash_quit(const char *what)
{
	char a[200];
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof a, "[3][EWeb has crashed|(%s).|What happened is in|CRASH.LOG, in EWeb's folder.][ Quit ]", what);
	form_alert(1, a);
	if (win_h > 0) {
		wind_close(win_h);
		wind_delete(win_h);
	}
	menu_bar(mb.tree, 0);
	v_clsvwk(vdi_h);
	appl_exit();
}

/* the command line: an address or a file to open first */
static void command_line(char *out, int size)
{
	unsigned char *bp = _BasePage;
	int len = bp[0x80];
	out[0] = 0;
	if (len <= 0 || len >= 127)
		return;
	if (len >= size)
		len = size - 1;
	memcpy(out, bp + 0x81, len);
	out[len] = 0;
	str_trim(out);
}

/* VA_START (common/ui/av.c) from the desktop or another program: an
   address or a file to show, or nothing (the window to the front) */
static void av_start(const char *s)
{
	char what[512];
	if (win_h > 0 && win_iconified) {
		short m[8] = { WM_UNICONIFY, 0, 0, 0, 0, 0, 0, 0 };
		m[3] = win_h;
		wind_get(win_h, WF_PREVXYWH, &m[4], &m[5], &m[6], &m[7]);
		win_message(m);
	}
	win_open();
	if (av_arg(s, what, sizeof what) != 0 && what[0])
		go(what);
}

int main(void)
{
	short work_out[57], msg[8], d;
	char path[256], first[256];
	AEVENT ev;
	short hover_x = -1, hover_y = -1;

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
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(BUSYBEE, 0);

	pf_size_open = pf_open_size;	/* a file read: opened, not looked up first (6.5) */
	work_dir(opt.workdir, sizeof opt.workdir);
	crash_install(opt.workdir, "EWeb", EWEB_VERSION);
	crash_cleanup = crash_quit;
	help_init();
	av_init("EWEB");
	settings_load();
	if (!scr_init()) {
		graf_mouse(ARROW, 0);
		alert(1, "[3][EWeb can't draw pages on this|screen: it needs 2 to 256|colours, or the Falcon's true|colour.][ Quit ]");
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	path_join(path, sizeof path, opt.workdir, "EWEB.FNT");
	if (!font_init(path)) {
		graf_mouse(ARROW, 0);
		alert(1, "[3][EWeb's fonts are missing:|put EWEB.FNT next to|EWEB.PRG.][ Quit ]");
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	/* secure connections: the certificates, the seed and the signatures
	   kept next to the program, as EWGET.TTP keeps them */
	net_init();
	path_join(conn_cacert, sizeof conn_cacert, opt.workdir, "CACERT.PEM");
	{
		/* the cookies that last: read when the first page needs them */
		char ck[256], key[256];
		path_join(ck, sizeof ck, opt.workdir, "COOKIES.TXT");
		path_join(key, sizeof key, opt.workdir, "EWEB.KEY");
		ck_file(ck, key);
		secret_warn = key_warn;
	}
	path_join(tls_roots_cache, sizeof tls_roots_cache, opt.workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof tls_seed_path, opt.workdir, "SEED.DAT");
	path_join(tls_sigs_path, sizeof tls_sigs_path, opt.workdir, "SIGS.DAT");
	tls_file_stamp = pf_stamp;
	tls_file_info = pf_info;	/* CACERT.PEM looked up once (6.5) */
	falcon_dsp_init();
	falcon_dsp_ec_init();
	pf_idle = idle;
	snprintf(http_user_agent, sizeof http_user_agent, "EWeb/%s (Atari Falcon030)", EWEB_VERSION);

	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EWeb: not enough memory.][ Quit ]");
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);

	br_init(&br, 600, (int32_t) (819L * opt.zoom / 100));
	{
		/* pages kept for Back and Forward (the heap takes TT RAM first):
		   a quarter of the free TT RAM up to 24 MB, else an eighth of
		   the free ST RAM up to 2 MB */
		long tt = (long) Mxalloc(-1L, 1), st = (long) Mxalloc(-1L, 0);
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
	}
	br.search = opt.search;
	br.pictures = opt.pictures;
	br.say = say;
	br.log = log_line;
	win_open();
	menu_update();
	crash_offer("EWeb", "in EWeb's folder");
	if (strcmp(net_stack(), "none") == 0)
		status("No network: load STinG to reach web sites (files can be opened).");

	log_event("ready %s %dx%d buf %d", scr_mode(), scr_w, scr_h, scr_rows());
	command_line(first, sizeof first);
	if (first[0])
		go(first);
	else
		home();
	evnt_set_m1(1, 0, 0, 1, 1);

	while (!quit) {
		short which;
		int more = !loading && !win_iconified && br_pictures_pending(&br);
		which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_M1 | (more ? MU_TIMER : 0), 0x102, 3, 0, 0, msg, &ev);
		handle(which & ~MU_TIMER, msg, &ev, &hover_x, &hover_y);
		if ((which & MU_TIMER) && !quit) {
			pictures();
			if (later.which) {
				short w = later.which;
				later.which = 0;
				handle(w, later.msg, &later.ev, &hover_x, &hover_y);
			}
		}
	}

	log_event("quit");
	graf_mouse(BUSYBEE, 0);
	http_close_all();
	win_close();
	settings_save();
	ck_save();		/* the cookies that last, once, if they changed */
	menu_bar(mb.tree, 0);
	av_exit();
	font_free();
	v_clsvwk(vdi_h);
	crash_remove();
	appl_exit();
	return 0;
}
