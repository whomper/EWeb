/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dcache.h - EWeb's disk cache: what servers sent (pages, stylesheets,
 * pictures) kept from one run to the next, as HTTP caching has a
 * browser keep it (RFC 9111): fresh for as long as the answer said,
 * then asked again with its ETag or date, a 304 keeping what is there.
 *
 * On the disk (the guidelines' 6.5): one pack, CACHE\EWEB.PAK, the
 * answers one after the other, and its index, CACHE\EWEB.IDX, read in one
 * open the first time the cache is needed and written at quit. What comes
 * is gathered in memory and added to the pack in one write when a
 * sixteenth of the cache has gathered, or at quit (a reset loses only
 * what is fetched again); the pack is opened when a job first reads from
 * it, and closed when the job ends. At quit a pack grown past its size, or half
 * of it left over from answers replaced, is written again with the
 * answers used last, in big pieces. The pack and the index carry the
 * same number, so an index from before a reset doesn't read another
 * pack.
 */
#ifndef DCACHE_H
#define DCACHE_H

typedef struct {
	char *data;		/* malloc'ed, NUL-terminated */
	long len;
	char url[1024];		/* where it came from, after redirects */
	char mime[64], charset[40];
	char security[96];	/* how it came ("TLS 1.2, ..."), "" plain */
	int fresh;		/* 1: as fresh as the server said; 0: to be asked again */
	/* to ask again: "If-None-Match: ...\r\nIf-Modified-Since: ...\r\n",
	   "" when it has neither */
	char validate[200];
} DC_GOT;

/* where the files are (a folder, made when first written; "" none: the
   cache is off) and how big the pack may grow, bytes */
void dc_dir(const char *dir, long max);

/* the answer kept for key (an address, no fragment): 1 and *g filled,
   or 0 */
int dc_get(const char *key, DC_GOT *g);

/* an answer that came (status 200): kept if HTTP lets it be, under key
   (the address asked for), from url (after redirects); its header lines
   ("Name: value\n") say for how long. data is copied. */
void dc_put(const char *key, const char *url, const char *headers, const char *mime, const char *charset,
	    const char *security, const char *data, long len);

/* a 304 for key: what is kept is fresh again, for as long as headers say */
void dc_fresh(const char *key, const char *headers);

/* the end of a job (a page and its stylesheets, a page's pictures): the
   pack closed */
void dc_job_end(void);

/* quit: the job ended, the pack written again if it must be, the index
   written; the memory let go */
void dc_close(void);

/* everything forgotten, the files too */
void dc_clear(void);

/* how many answers are kept, and their bytes */
long dc_count(long *bytes);

/* the clock: UTC seconds since 1970 (the tests set their own) */
extern long long (*dc_now)(void);

#endif
