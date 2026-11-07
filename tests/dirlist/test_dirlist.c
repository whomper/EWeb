/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_dirlist.c - src/dirlist.c: the file dialog's folder (the masks,
 * the app's types, the sorts with folders first, the selection, the sizes
 * and dates as shown), and a folder of 5,000 entries. The types are
 * EPlay's, whose Add files the dialog was. With AddressSanitizer: make test
 */
#include <stdio.h>
#include <string.h>
#include "dirlist.h"

static int bad;
static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	bad += !ok;
}

enum { DT_MUSIC, DT_MP3, DT_WAV, DT_AIFF, DT_M3U, DT_ALL, DT_N };
static const DL_TYPE music[DT_N] = { { "Music files", "MP3 WAV AIF AIFF AIFC M3U" }, { "MP3", "MP3" },
				     { "WAV", "WAV" }, { "AIFF", "AIF AIFF AIFC" }, { "Playlists", "M3U" },
				     { "All files", "" } };

#define D(y, m, dd) ((unsigned short)(((y) - 1980) << 9 | (m) << 5 | (dd)))

static const char *names(DLIST *d)
{
	static char s[400];
	int r;
	s[0] = 0;
	for (r = 0; r < d->nview; r++) {
		if (r)
			strcat(s, " ");
		strcat(s, dl_at(d, r)->name);
	}
	return s;
}

int main(void)
{
	DLIST d;
	char t[16];
	long b;
	int i, ok;
	/* masks */
	check(dl_match("*", "A.MP3") && dl_match("*.MP3", "night.mp3") && !dl_match("*.MP3", "A.WAV") &&
	      dl_match("0?_*", "01_NIGHT.MP3") && !dl_match("0?_*", "1_NIGHT.MP3") &&
	      dl_match("*LIVE*", "MYLIVE01.WAV") && !dl_match("A?", "A") && dl_match("", ""),
	      "masks: * and ?, the case aside");
	/* the folder */
	dl_init(&d);
	check(d.ntypes == 1 && !strcmp(dl_type_name(&d, 0), "All files") && dl_type_ok(&d, 0, "X.PRG"),
	      "no table: All files alone");
	dl_types(&d, music, DT_N);
	check(d.type == DT_MUSIC && d.ntypes == DT_N && !strcmp(dl_type_name(&d, DT_M3U), "Playlists") &&
	      !strcmp(dl_type_name(&d, DT_N), "?"), "the app's types, the first chosen");
	dl_add(&d, ".", 0, 0, 0, 1);
	dl_add(&d, "..", 0, 0, 0, 1);
	dl_add(&d, "09_RAIN.MP3", 6000000, D(2026, 11, 2), 0, 0);
	dl_add(&d, "LIVE", 0, D(2026, 10, 30), 0, 1);
	dl_add(&d, "01_NIGHT.MP3", 7100000, D(2026, 11, 1), 100, 0);
	dl_add(&d, "06_LAST.WAV", 67108864, D(2026, 11, 2), 5, 0);
	dl_add(&d, "COVER.JPG", 48000, D(2026, 11, 1), 0, 0);
	dl_add(&d, "07_ECHO.AIF", 45000000, D(2026, 11, 3), 0, 0);
	dl_add(&d, "BONUS", 0, D(2026, 11, 3), 0, 1);
	check(d.n == 7, "\".\" and \"..\" left out");
	dl_sort(&d);
	check(!strcmp(names(&d), "BONUS LIVE 01_NIGHT.MP3 06_LAST.WAV 07_ECHO.AIF 09_RAIN.MP3"),
	      "by name, folders first, music files only");
	d.sort = DS_DATE;
	dl_sort(&d);
	check(!strcmp(names(&d), "BONUS LIVE 07_ECHO.AIF 06_LAST.WAV 09_RAIN.MP3 01_NIGHT.MP3"),
	      "by date: the newest first (the same day by time)");
	d.sort = DS_SIZE;
	dl_sort(&d);
	check(!strcmp(names(&d), "BONUS LIVE 06_LAST.WAV 07_ECHO.AIF 01_NIGHT.MP3 09_RAIN.MP3"),
	      "by size: the biggest first");
	d.sort = DS_TYPE;
	dl_sort(&d);
	check(!strcmp(names(&d), "BONUS LIVE 07_ECHO.AIF 01_NIGHT.MP3 09_RAIN.MP3 06_LAST.WAV"),
	      "by type: AIF, MP3, WAV");
	d.sort = DS_NAME;
	d.type = DT_ALL;
	dl_sort(&d);
	check(d.nview == 7, "All files: the picture too");
	d.type = DT_MP3;
	dl_filter(&d);
	check(!strcmp(names(&d), "BONUS LIVE 01_NIGHT.MP3 09_RAIN.MP3"), "MP3 only (folders always)");
	check(dl_type_ok(&d, DT_MUSIC, "LIVE.M3U") && dl_type_ok(&d, DT_M3U, "a.m3u") &&
	      !dl_type_ok(&d, DT_M3U, "A.MP3") && dl_type_ok(&d, DT_AIFF, "x.aifc") && !dl_type_ok(&d, DT_AIFF, "x.AI") &&
	      !dl_type_ok(&d, DT_AIFF, "x.AIFFF") && !dl_type_ok(&d, DT_MP3, "MP3") && dl_type_ok(&d, DT_ALL, "README"),
	      "playlists: among the music files, and a type of their own");
	d.type = DT_MUSIC;
	strcpy(d.mask, "0?_*");
	dl_filter(&d);
	check(!strcmp(names(&d), "BONUS LIVE 01_NIGHT.MP3 06_LAST.WAV 07_ECHO.AIF 09_RAIN.MP3"), "a mask");
	strcpy(d.mask, "*E*");
	dl_filter(&d);
	check(!strcmp(names(&d), "BONUS LIVE 07_ECHO.AIF"), "a mask narrows the files, not the folders");
	d.mask[0] = 0;
	dl_filter(&d);
	/* the selection */
	dl_select_only(&d, 2);
	check(dl_selected(&d, 0) == 1 && dl_at(&d, 2)->sel, "a click: that file alone");
	dl_toggle(&d, 4);
	check(dl_selected(&d, &b) == 2 && b == 7100000 + 45000000, "Shift-click adds one; the bytes");
	dl_toggle(&d, 4);
	check(dl_selected(&d, 0) == 1, "Shift-click again takes it out");
	dl_select_range(&d, 5, 0);
	check(dl_selected(&d, &b) == 6 && dl_selected_dirs(&d) == 2 && dl_at(&d, 0)->sel && dl_at(&d, 1)->sel &&
	      b == 7100000 + 67108864 + 45000000 + 6000000,
	      "a range: its folders too (phase 4: added with their subfolders), the files' bytes alone");
	dl_select_only(&d, 0);
	check(dl_selected(&d, 0) == 1 && dl_selected_dirs(&d) == 1, "a folder selected by a click");
	dl_select_all(&d, 1);
	check(dl_selected(&d, 0) == 4 && dl_selected_dirs(&d) == 0, "All: the files, not the folders");
	strcpy(d.mask, "*E*");
	dl_filter(&d);
	d.mask[0] = 0;
	dl_filter(&d);
	check(dl_selected(&d, 0) == 1, "what a mask hid is no longer selected (not added unseen)");
	/* what is shown */
	dl_size_text(12, t);
	ok = !strcmp(t, "12");
	dl_size_text(524288, t);
	ok &= !strcmp(t, "512K");
	dl_size_text(7445000, t);
	ok &= !strcmp(t, "7.1M");
	dl_size_text(67108864, t);
	ok &= !strcmp(t, "64M");
	dl_date_text(D(2026, 11, 3), t);
	ok &= !strcmp(t, "03.11.26");
	dl_date_text(D(1999, 1, 9), t);
	ok &= !strcmp(t, "09.01.99");
	check(ok, "sizes and dates as the list shows them");
	/* a big folder: as many as memory holds */
	dl_clear(&d);
	for (i = 0, ok = 1; i < 5000; i++) {
		sprintf(t, "T%04d.MP3", 4999 - i);
		ok &= dl_add(&d, t, i, 0, 0, 0);
	}
	dl_sort(&d);
	check(ok && d.nview == 5000 && !strcmp(dl_at(&d, 0)->name, "T0000.MP3") &&
	      !strcmp(dl_at(&d, 4999)->name, "T4999.MP3"), "5,000 files, sorted");
	dl_free(&d);
	return bad != 0;
}
