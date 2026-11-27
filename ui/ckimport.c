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
#include "cookie.h"

#define IMP_PIECE 32768L

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
	long have;
	int n, h, skip, sites = 0;
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
	   only that (an export of every site Safari has was 860 KB) */
	if ((h = pf_open(ck_path, 0)) < 0 || (data = malloc(IMP_PIECE)) == NULL) {
		if (h >= 0)
			pf_close(h);
		alert(1, "[3][EWeb can't read that file.][ OK ]");
		return;
	}
	ck_import_start();
	n = 0;
	have = 0;
	skip = 0;
	for (;;) {
		long got = pf_read(h, data + have, IMP_PIECE - have), use;
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
		n += ck_import(data, use, &sites);
		have -= use;
		memmove(data, data + use, (size_t) have);
	}
	pf_close(h);
	free(data);
	ck_import_start();	/* (its table of sites given back) */
	log_event("cookies imported %d for %d sites", n, sites);
	if (n == 0) {
		alert(1, "[1][There are no cookies in that|file that EWeb can use.][ OK ]");
		return;
	}
	if (alert(1, "[2][Imported %d cookies for %d %s.|Delete the file you|imported from?][Delete|Keep]", n, sites,
		  sites == 1 ? "site" : "sites") == 1) {
		if (pf_remove(ck_path) != 0)
			alert(1, "[3][EWeb couldn't delete the file:|is the disk locked?][ OK ]");
		else
			log_event("cookies: the imported file deleted");
	}
}
