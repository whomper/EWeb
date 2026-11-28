/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* bandcamp.h - a Bandcamp album's page as EWeb's own, its tracks for EPlay
   (bandcamp.c; decided 27 Nov 2026) */
#ifndef EW_BANDCAMP_H
#define EW_BANDCAMP_H

#include "util.h"

#define BC_TRACKS 64

typedef struct {
	char title[128];
	long seconds;		/* its length, 0: not said */
	char url[512];		/* the stream's address (mp3-128), "": none */
} bc_track;

typedef struct {
	int id;			/* (the Play links' a=) */
	char page[1024];	/* the album's address */
	char title[160], artist[128];
	int album;		/* an album (Bandcamp's, a page's MusicAlbum or MusicPlaylist): EWeb's
				   own page for it; else the page's audio, a line at its top */
	int others;		/* the page's audio in forms EPlay doesn't play (left out) */
	int n;
	bc_track t[BC_TRACKS];
} bc_album;

/* 1: an https address of a Bandcamp album or track (*.bandcamp.com/album/,
   /track/) */
int bc_page(const char *url);
/* the album in a page's HTML (its data-tralbum), kept for its Play links
   (the last few); NULL: none in it */
bc_album *bc_read(const char *url, const char *data, long len);
/* the album kept with this id; NULL: gone (loaded again) */
const bc_album *bc_find(int id);
/* the album's tracks from from on, as an .M3U ("#EXTINF:SECONDS,ARTIST -
   TITLE", then the address); how many */
int bc_m3u(const bc_album *a, int from, SBUF *out);
/* EWeb's own page for the album: cover the picture's address (og:image,
   NULL none), style the own pages' <style> */
void bc_html(SBUF *s, const bc_album *a, const char *cover, const char *style);

/* the audio a page has, for EPlay (1.30; the owner, 28 Nov 2026: "try to
   create something that is generic"): Bandcamp's (bc_read), then a
   schema.org MusicAlbum, MusicPlaylist or MusicRecording in its JSON-LD,
   og:audio, <audio> and its <source>s, and links to audio files, the
   same address once; MP3, WAV and AIFF taken (and .m3u playlists), the
   others counted. Kept as bc_read's are; NULL: none */
bc_album *au_find(const char *url, const char *data, long len);
/* the page's HTML with a line at its top's (after <body>) naming its
   audio and Play in EPlay, into out; 0: no <body> found (the page as it is) */
int au_bar(SBUF *out, const bc_album *a, const char *data, long len);
/* 1: an address EPlay plays (MP3, WAV, AIFF, .m3u: a link's context menu
   offers Play in EPlay) */
int au_playable(const char *url);
/* one address as a list of its own, kept for Play (its id) */
bc_album *au_one(const char *url, const char *title);

#endif
