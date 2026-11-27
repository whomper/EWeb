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

#endif
