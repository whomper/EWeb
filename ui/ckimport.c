/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ckimport.c - File > Import cookies... (decided 27 Nov 2026; docs/GUIDE.md,
 * Cookies from a Mac's Safari): a Netscape cookies.txt, as
 * tools/safari-cookies.py writes it on a Mac, chosen in the family's file
 * dialog (5.12; Settings' SYSFSEL= the system's), read whole in one go
 * (6.5) and put into the store as its sites' own Set-Cookie would be
 * (src/net/cookie.c's ck_import); the lasting ones go to COOKIES.TXT,
 * encrypted, at its next write. Then an offer to delete the file, which
 * holds logins in clear text. The log has the count only (7.3). A file of
 * its own, linked last: EWEB.PRG's code stays where the speed rig had it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "plat.h"
#include "util.h"
#include "alert.h"
#include "filepick.h"
extern short _is_acc;
#include "cookie.h"
#include "conn.h"
#include "progbar.h"

#define IMP_PIECE 32768L
#define IMP_TURN 30		/* ms of lines a turn of the event loop (the guidelines' Speed) */

static char ck_dir[FP_PATH], ck_recent[FP_RECENT][FP_PATH], ck_path[FP_PATH + 16];

static void take(const char *path)
{
	str_copy(ck_path, path, sizeof ck_path);
}

void cookies_import(void)
{
	static DL_TYPE types[2];
	FP_SPEC spec;
	const char *base;
	char *data = NULL;
	long have, total, pos;
	int n, h, skip, shown, stopped, sites = 0;
	long dropped;
	types[0].name = "Cookie files";
	types[0].exts = "TXT";
	types[1].name = "All files";
	types[1].exts = "";
	memset(&spec, 0, sizeof spec);
	spec.title = "Import cookies";
	spec.ok = "Import";
	spec.none = "No folder imported from yet";
	spec.log = "EWEB";
	spec.types = types;
	spec.ntypes = 2;
	ck_path[0] = 0;
	filepick_system = opt.sysfsel;
	if (filepick_run(&spec, ck_dir, ck_recent, take, NULL) <= 0 || !ck_path[0])
		return;
	base = strrchr(ck_path, '\\');
	base = base ? base + 1 : ck_path;
	if (strcasecmp(base, "cookies.txt") == 0) {
		/* EWeb's own store, its values encrypted: not a file to import */
		alert(1, "[1][That is EWeb's own cookie file.|Choose the file made on|the Mac (safari-cookies.txt).][ OK ]");
		return;
	}
	/* read in pieces of IMP_PIECE, whole lines each (a line cut by a
	   piece's end carried to the next), so that a file of any size takes
	   only that (an export of every site Safari has was 860 KB); the
	   lines in turns of the event loop, the bar moving in the status
	   line, Esc, Undo or Stop stopping it with nothing added (the owner,
	   28 Nov 2026: "Can you make a progress bar for the import?"); the
	   accessory's file shut across each turn (no file open across an
	   event call: 2.2), opened again at its place for the next piece */
	if ((h = pf_open(ck_path, 0)) < 0 || (data = malloc(IMP_PIECE)) == NULL) {
		if (h >= 0)
			pf_close(h);
		alert(1, "[3][EWeb can't read that file.][ OK ]");
		return;
	}
	total = pf_open_size(h);
	if (total <= 0)
		total = 1;
	graf_mouse(BUSYBEE, 0);
	conn_stop = 0;
	shown = -1;
	ck_import_start();
	n = 0;
	have = 0;
	skip = 0;
	pos = 0;
	stopped = 0;
	for (;;) {
		long got, use, at;
		if (h < 0 && ((h = pf_open(ck_path, 0)) < 0 || pf_seek(h, pos) != pos)) {
			got = -1;	/* (gone meanwhile: what was read is kept) */
		} else {
			got = pf_read(h, data + have, IMP_PIECE - have);
			if (got > 0)
				pos += got;
			if (_is_acc) {
				pf_close(h);
				h = -1;
			}
		}
		if (got <= 0) {
			if (have > 0 && !skip)
				n += ck_import(data, have, &sites);	/* (the last line, without its end) */
			break;
		}
		have += got;
		if (skip) {
			/* a line longer than a piece (no cookie is): passed over to its end */
			char *e = memchr(data, '\n', (size_t) have);
			if (e == NULL) {
				have = 0;
				continue;
			}
			have -= (long) (e + 1 - data);
			memmove(data, e + 1, (size_t) have);
			skip = 0;
		}
		for (use = have; use > 0 && data[use - 1] != '\n'; use--)
			;
		if (use == 0) {
			if (have == IMP_PIECE) {
				have = 0;
				skip = 1;
			}
			continue;
		}
		/* the piece's lines, IMP_TURN ms of them a turn */
		for (at = 0; at < use && !stopped;) {
			unsigned long t0 = pf_ms();
			int pc;
			while (at < use && (long) (pf_ms() - t0) < IMP_TURN) {
				char *e = memchr(data + at, '\n', (size_t) (use - at));
				long l = (long) (e - (data + at)) + 1;
				n += ck_import(data + at, l, &sites);
				at += l;
			}
			pc = (int) ((pos - (have - at)) * 100 / total);
			if (pc > 99)
				pc = 99;
			if (pc != shown) {
				char s[48];
				shown = pc;
				snprintf(s, sizeof s, "Importing cookies... %d%%", pc);
				win_status(s);
				win_progress(0, (int) ((long) PB_FULL * pc / 100));
			}
			if (busy_turn())
				stopped = 1;
		}
		if (stopped)
			break;
		have -= use;
		memmove(data, data + use, (size_t) have);
	}
	if (h >= 0)
		pf_close(h);
	free(data);
	ck_import_end(!stopped);	/* (kept: the jar held to its size; stopped: the store as it was) */
	dropped = ck_import_dropped();
	conn_stop = 0;
	graf_mouse(ARROW, 0);
	win_progress(-1, 0);
	win_status("");
	if (stopped) {
		log_event("cookies: the import stopped, none added");
		alert(1, "[1][Import stopped: no cookies|were added.][ OK ]");
		return;
	}
	log_event("cookies imported %d for %d sites", n, sites);
	if (n == 0) {
		alert(1, "[1][There are no cookies in that|file that EWeb can use.][ OK ]");
		return;
	}
	log_event("cookies: %ld dropped by the jar's size", dropped);
	/* (the cookie jar's size, Settings > Secure: the least used went,
	   the newest kept: decided 28 Nov 2026) */
	if ((dropped > 0 ?
	     alert(1, "[2][Imported %d cookies for %d %s.|The cookie jar was full: the|%ld least used went (Settings|> Secure makes it larger).|Delete the imported file?][Delete|Keep]",
		   n, sites, sites == 1 ? "site" : "sites", dropped) :
	     alert(1, "[2][Imported %d cookies for %d %s.|Delete the file you|imported from?][Delete|Keep]", n, sites,
		   sites == 1 ? "site" : "sites")) == 1) {
		if (pf_remove(ck_path) != 0)
			alert(1, "[3][EWeb couldn't delete the file:|is the disk locked?][ OK ]");
		else
			log_event("cookies: the imported file deleted");
	}
}
