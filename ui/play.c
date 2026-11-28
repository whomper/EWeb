/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * play.c - a Bandcamp page's Play (decided 27 Nov 2026: "Album to EPlay";
 * src/browse/bandcamp.c makes the page): the album's tracks from the one
 * chosen on, as an .M3U with each track's "#EXTINF:SECONDS,ARTIST - TITLE",
 * written to EWeb's folder (eweb.m3u, written over each time) and handed to
 * EPlay: by VA_START when it runs ("EPLAY   ", the STE edition's
 * "EPLAYST "), else started with it where the AES runs programs side by
 * side (MiNT, MagiC).
 * On plain TOS a program started would take EWeb's place: EPlay is to be
 * running first, and an alert says so. A file of its own, linked last.
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
#include "av.h"
#include "bandcamp.h"

/* PATH from EWeb's environment (the basepage's p_env), or 0 */
static const char *env_path(void)
{
	const char *e;
	long k = 0;
	memcpy(&e, (const char *) _BasePage + 0x2C, sizeof e);
	while (e != NULL && *e && k < 4096) {
		long n = (long) strlen(e);
		if (!strncmp(e, "PATH=", 5))
			return e + 5;
		e += n + 1;
		k += n + 1;
	}
	return NULL;
}

/* EPLAY.PRG where the AES's shel_find would look (EPlay is a program
   only, its EPLAY.INF beside it: no folder to read from an INF; the hub,
   27 Nov 2026): EWeb's own folder, each folder of PATH, the boot drive's
   root; 0 none */
static int eplay_path(char *out, int size)
{
	const char *p = env_path();
	char dir[200];
	path_join(out, size, opt.workdir, "eplay.prg");
	if (pf_exists(out))
		return 1;
	while (p != NULL && *p) {
		int k = (int) strcspn(p, ";,");
		if (k > 0 && k < (int) sizeof dir) {
			memcpy(dir, p, (size_t) k);
			dir[k] = 0;
			path_join(out, size, dir, "eplay.prg");
			if (pf_exists(out))
				return 1;
		}
		p += k;
		if (*p)
			p++;
	}
	snprintf(out, (size_t) size, "%c:\\eplay.prg", 'A' + boot_drive());
	return pf_exists(out);
}

void play_tracks(const char *about)
{
	const char *a = strstr(about, "a="), *f = strstr(about, "from=");
	const bc_album *al = a != NULL ? bc_find(atoi(a + 2)) : NULL;
	char m3u[256];
	static char prog[256];
	SBUF s;
	int n;
	if (al == NULL) {
		alert(1, "[1][The tracks have gone from|memory: load the page again|(^R), then Play.][ OK ]");
		return;
	}
	sb_init(&s);
	n = bc_m3u(al, f != NULL ? atoi(f + 5) : 0, &s);
	path_join(m3u, sizeof m3u, opt.workdir, "eweb.m3u");
	if (n == 0 || pf_save(m3u, s.s, s.len) != 0) {
		sb_free(&s);
		alert(1, "[3][EWeb can't write the track list|for EPlay: is the disk locked|or full?][ OK ]");
		return;
	}
	sb_free(&s);
	log_event("play: %d tracks to EPlay", n);
	if (av_send("EPLAY|EPLAYST", m3u)) {
		win_status("Playing in EPlay.");
		return;
	}
	if (aes_count() != 1 || get_cookie(0x4D616758L)) {	/* (MiNT, MagiC: side by side) */
		if (eplay_path(prog, sizeof prog)) {
			static char tail[128];
			int k = (int) strlen(m3u);
			if (k > 120)
				k = 120;
			tail[0] = (char) k;
			memcpy(tail + 1, m3u, (size_t) k);
			tail[k + 1] = 0;
			shel_write(1, 1, get_cookie(0x4D616758L) ? 100 : 1, prog, tail);
			win_status(al->album ? "EPlay is starting with the album." : "EPlay is starting.");
			log_event("play: EPlay started");
			return;
		}
		alert(1, "[1][EPlay plays this page's audio,|and EWeb can't find it: start|EPlay first, then Play again.][ OK ]");
		return;
	}
	alert(1, "[1][EPlay plays this page's audio.|Start EPlay first, then|Play again.][ OK ]");
}
