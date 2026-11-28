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
	check(a->album, "Bandcamp's is an album: EWeb's own page");

	/* a page's audio (1.30): JSON-LD's MusicAlbum, its recordings' names,
	   durations and addresses joined to the page's */
	{
		static const char ld[] = "<html><head><script type=\"application/ld+json\">"
			"{\"@context\": \"https://schema.org\", \"@type\": \"MusicAlbum\", \"name\": \"Night Lines\","
			" \"byArtist\": {\"@type\": \"MusicGroup\", \"name\": \"A Band\"},"
			" \"track\": [{\"@type\": \"MusicRecording\", \"name\": \"One\", \"duration\": \"PT3M20S\","
			" \"audio\": {\"@type\": \"AudioObject\", \"contentUrl\": \"/audio/one.mp3\"}},"
			" {\"@type\": \"MusicRecording\", \"name\": \"Two\", \"duration\": \"PT1H2S\","
			" \"audio\": {\"@type\": \"AudioObject\", \"contentUrl\": \"https://cdn.example/two.ogg\"}}]}"
			"</script></head><body><p>x</p></body></html>";
		bc_album *b = au_find("https://music.example/albums/night", ld, (long) sizeof ld - 1);
		check(b != NULL && b->album && !strcmp(b->title, "Night Lines") && !strcmp(b->artist, "A Band"),
		      "JSON-LD: an album, its name and artist");
		check(b != NULL && b->n == 1 && !strcmp(b->t[0].url, "https://music.example/audio/one.mp3") &&
		      !strcmp(b->t[0].title, "One") && b->t[0].seconds == 200, "JSON-LD: its MP3, joined, named, 3:20");
		check(b != NULL && b->others == 1, "JSON-LD: the Ogg counted, not taken");
	}
	/* og:audio, <audio> and its <source>, links to audio files, each
	   address once; not an album: a line at the page's top */
	{
		static const char pg[] = "<html><head><meta property=\"og:audio\" content=\"https://pod.example/ep1.mp3\">"
			"<title>t</title></head><body class=x>"
			"<audio src=\"ep1.mp3\"></audio><audio controls><source src=\"/raw/ep2.wav\" type=\"audio/wav\"></audio>"
			"<a href=\"files/ep3.aiff\"><b>Episode</b>   three</a> <a href=\"/p.html\">a page</a>"
			"<a href=\"ep4.flac\">flac</a> <a href=\"ep1.mp3?x=1&amp;y=2\">again</a></body></html>";
		bc_album *c = au_find("https://pod.example/show/", pg, (long) sizeof pg - 1);
		SBUF o;
		check(c != NULL && !c->album, "the page's audio: not an album");
		check(c != NULL && c->n == 5, "og:audio, <audio>, <source>, a link and a link with a query: five");
		check(c != NULL && c->n > 3 && !strcmp(c->t[1].url, "https://pod.example/show/ep1.mp3") &&
		      !strcmp(c->t[2].url, "https://pod.example/raw/ep2.wav") &&
		      !strcmp(c->t[3].url, "https://pod.example/show/files/ep3.aiff") &&
		      !strcmp(c->t[3].title, "Episode three"), "each joined to the page; a link's words, its tags left out");
		check(c != NULL && c->n > 4 && !strcmp(c->t[4].url, "https://pod.example/show/ep1.mp3?x=1&y=2"),
		      "&amp; undone in an address");
		check(c != NULL && c->others == 1, "the FLAC counted, not taken");
		if (c != NULL) {
			sb_init(&o);
			check(au_bar(&o, c, pg, (long) sizeof pg - 1) && o.s != NULL &&
			      strstr(o.s, "<body class=x><div ") != NULL && strstr(o.s, "5 audio tracks on this page") != NULL &&
			      strstr(o.s, "Play in EPlay</a></div><audio src=") != NULL, "the line after <body>");
			sb_free(&o);
		}
		check(au_find("https://x.example/", "<html><body><a href=\"a.html\">a</a></body></html>", 47) == NULL,
		      "a page with no audio: none");
	}
	printf("bctest: %s\n", fails ? "failed" : "all passed");
	return fails != 0;
}
