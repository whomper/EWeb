/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * history.h - the pages EWeb has shown, kept in HISTORY.TXT next to
 * EWEB.PRG (a line each, oldest first: when, in seconds since 1970 UTC,
 * the address and the title, between tabs), read when first needed and
 * written once at quit when they changed (the guidelines' 6.5); the
 * newest 1000. They are about:history, and the links to them are
 * visited (:visited).
 */
#ifndef HISTORY_H
#define HISTORY_H

typedef struct {
	char *url;		/* with its #fragment, as other browsers keep them */
	char *title;		/* UTF-8, "" for none */
	long long when;		/* last shown: seconds since 1970, UTC */
	unsigned long hash;
	unsigned long ohash;	/* its origin's (http, https), 0 none */
} HG;

extern unsigned long hg_changes;	/* counts every change (the links to restyle) */

/* where they are kept: read from there when first needed (none: none) */
void hg_file(const char *path);
/* url shown (now its newest, with this title) */
void hg_add(const char *url, const char *title, long long when);
/* has url been shown (a place on a page is another address) */
int hg_visited(const char *url);
/* may a page from this origin ("https://host", len long, as a URL
   begins) have been shown: 0 none has */
int hg_origin(const char *origin, long len);
/* how many, and the one i from the oldest (NULL past the end) */
int hg_count(void);
const HG *hg_get(int i);
/* all gone (Clear history) */
void hg_clear(void);
/* the file's text, malloc'ed (*len); its lines in place of those there were
   (0 if out of memory) */
char *hg_write(long *len);
int hg_parse(const char *data, long len);
/* at quit: written if they changed; 1 done (or nothing to), 0 the disk
   wouldn't take it */
int hg_save(void);
void hg_free(void);

#endif
