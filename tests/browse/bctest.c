/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bctest - a Bandcamp album's page as EWeb's own (src/browse/bandcamp.c,
 * decided 27 Nov 2026) on a page made here (no Bandcamp page kept): the
 * album's data in data-tralbum, its quotes as &quot;, an "artist" that is
 * null before the one that names, \u escapes, a track with no stream; the
 * addresses that are a Bandcamp album's or track's; the .M3U for EPlay.
 */
#include <stdio.h>
#include <string.h>
#include "bandcamp.h"

static int fails;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("bctest: %s\n", what);
		fails++;
	}
}

int main(void)
{
	static const char page[] =
		"<html><body><div id=x data-tralbum=\"{&quot;current&quot;:{&quot;title&quot;:&quot;n\\u00e4ht&quot;,"
		"&quot;artist&quot;:null},&quot;artist&quot;:&quot;Some &amp; One&quot;,&quot;trackinfo&quot;:["
		"{&quot;title&quot;:&quot;first&quot;,&quot;duration&quot;:61.5,&quot;file&quot;:{&quot;mp3-128&quot;:"
		"&quot;https://t4.example/stream/1?p=0&amp;t=a&quot;}},"
		"{&quot;title&quot;:&quot;sec\\&quot;ond&quot;,&quot;duration&quot;:0,&quot;file&quot;:null},"
		"{&quot;title&quot;:&quot;third&quot;,&quot;duration&quot;:3600,&quot;file&quot;:{&quot;mp3-128&quot;:"
		"&quot;https://t4.example/stream/3&quot;}}]}\"></div></body></html>";
	bc_album *a;
	SBUF m;
	check(bc_page("https://someone.bandcamp.com/album/a-b"), "an album's address");
	check(bc_page("https://someone.bandcamp.com/track/x"), "a track's address");
	check(!bc_page("https://someone.bandcamp.com/music"), "not an album's");
	check(!bc_page("https://bandcamp.com.example.org/album/x"), "not Bandcamp's host");
	check(!bc_page("http://someone.bandcamp.com/album/x"), "only https");
	a = bc_read("https://someone.bandcamp.com/album/a-b", page, (long) sizeof page - 1);
	check(a != NULL, "the album read");
	if (a == NULL)
		return 1;
	check(!strcmp(a->title, "n\xc3\xa4ht"), "the title, its \\u00e4 as UTF-8");
	check(!strcmp(a->artist, "Some & One"), "the artist that names, not current's null");
	check(a->n == 3, "three tracks");
	check(!strcmp(a->t[0].url, "https://t4.example/stream/1?p=0&t=a"), "the first's stream, &amp; undone");
	check(a->t[0].seconds == 61, "its length");
	check(!strcmp(a->t[1].title, "sec\"ond") && a->t[1].url[0] == 0, "a track not streamed, an escaped quote");
	check(bc_find(a->id) == a, "kept for its Play");
	sb_init(&m);
	check(bc_m3u(a, 1, &m) == 1, "from the second: the third alone (the second has no stream)");
	check(m.s != NULL && !strcmp(m.s, "#EXTM3U\r\n#EXTINF:3600,Some & One - third\r\nhttps://t4.example/stream/3\r\n"),
	      "the .M3U's lines");
	sb_free(&m);
	printf("bctest: %s\n", fails ? "failed" : "all passed");
	return fails != 0;
}
