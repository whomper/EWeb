/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dcache.h - EWeb's disk cache: what servers sent (pages, stylesheets,
 * pictures) kept from one run to the next, as HTTP caching has a
 * browser keep it (RFC 9111): fresh for as long as the answer said,
 * then asked again with its ETag or date, a 304 keeping what is there.
 *
 * On the disk (the guidelines' 6.5): a ring of pieces, CACHE\EWEBnn.PAK
 * (4 MB at most), each with its list, and an index of them all,
 * CACHE\EWEB.IDX, read the first time the cache is needed and written at
 * quit (dcache.c). What comes is gathered in memory and added to the
 * current piece in one write when a sixteenth of the cache (a megabyte
 * at most) has gathered, or at quit (a reset loses only what is fetched
 * again); a piece is opened when a job first reads from it, and closed
 * when the job ends. When the next answer wouldn't fit, the next piece is
 * begun and what it held forgotten, so quit writes only what has
 * gathered, the lists that changed and the index.
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
	int stale_ok;		/* may be shown stale when the server can't be reached (it
				   didn't say must-revalidate or no-cache) */
	int immutable;		/* Cache-Control: immutable: fresh, it isn't asked about even
				   on Reload (RFC 8246) */
} DC_GOT;

/* where the files are (a folder, made when first written; "" none: the
   cache is off) and how big the pack may grow, bytes */
void dc_dir(const char *dir, long max);

/* the answer kept for key (an address, no fragment): 1 and *g filled,
   or 0 */
int dc_get(const char *key, DC_GOT *g);
/* is anything kept for key (fresh or not): 1, without reading it */
int dc_has(const char *key);

/* an answer that came (status 200): kept if HTTP lets it be, under key
   (the address asked for), from url (after redirects); its header lines
   ("Name: value\n") say for how long. data is copied. */
void dc_put(const char *key, const char *url, const char *headers, const char *mime, const char *charset,
	    const char *security, const char *data, long len);

/* a 304 for key: what is kept is fresh again, for as long as headers say */
void dc_fresh(const char *key, const char *headers);

/* what is kept for key forgotten (a form sent there with POST changed
   it: RFC 9111, 4.4) */
void dc_drop(const char *key);

/* EWeb's own data kept under key (a picture decoded and dithered for the
   screen, under a key made from its file's contents): never stale, kept
   until the ring comes round to it. data is copied. */
void dc_put_own(const char *key, const char *what, const char *data, long len);
/* the most dc_put_own keeps (0: there is no cache) */
long dc_own_max(void);

/* the end of a job (a page and its stylesheets, a page's pictures): the
   pack closed */
void dc_job_end(void);
/* answers are kept in memory as they come and written later, a step at a
   time when EWeb is idle: dc_write_due says whether any wait,
   dc_write_step writes the next (about 64 KB, one answer at least); until
   bytes have gathered (at least 1 MB, dc_hold), when they are all written
   at once */
void dc_hold(long bytes);
int dc_write_due(void);
void dc_write_step(void);

/* quit: the job ended, the pack written again if it must be, the index
   written; the memory let go */
/* what has gathered in memory written to the pack (dc_close does it too;
   apart for the quit's log) */
void dc_flush(void);
/* told how far dc_flush or dc_close has got (done of total) as they write
   (the quit's bar, which moved only between steps: Erez, 30 Oct 2026) */
extern void (*dc_progress)(long done, long total);
/* what the quit has to write, without reading anything: the answers still
   in memory (bytes) and the index's answers (0 when it hasn't changed) */
void dc_unsaved(long *bytes, long *answers);
void dc_close(void);

/* everything forgotten, the files too */
void dc_clear(void);

/* how many answers are kept, and their bytes */
long dc_count(long *bytes);

/* the clock: UTC seconds since 1970 (the tests set their own) */
extern long long (*dc_now)(void);

#endif
