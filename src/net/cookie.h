/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cookie.h - cookies (cookie.c), after RFC 6265bis: what servers set
 * with Set-Cookie, kept by domain and path, and sent back with Cookie.
 * Those that last (Expires, Max-Age) are kept on the disk, in one file
 * read once and written once at the end (Erez's decision, 19 Oct 2026;
 * the guidelines' 6.5); those that last only while EWeb runs never go
 * there. A site's cookies go only to the site itself: none are set or
 * sent for a picture or stylesheet from another site.
 */
#ifndef COOKIE_H
#define COOKIE_H

#include "url.h"
#include "util.h"

/* the cookies kept: as many as a browser keeps */
#define CK_MAX        3000
#define CK_PER_DOMAIN 60
/* the store's size in bytes, its cookies' strings and records (the cookie
   jar's size, Settings > Secure, COOKIEKB=; decided 28 Nov 2026): the least
   used go first when it is over; 0 no limit but CK_MAX */
extern long ck_max_bytes;
#define CK_JAR_KB     192

/* how a request came about, for SameSite and other sites' cookies */
typedef struct {
	int top;		/* a page (a navigation), not a picture or a stylesheet */
	int safe;		/* GET or HEAD */
	const char *site;	/* the page it came from (its address), NULL: typed */
	int script;		/* document.cookie: no HttpOnly cookie is seen, set or replaced */
} CKCTX;

/* a Set-Cookie header's value from the answer to u */
void ck_set(const URL *u, const char *line, const CKCTX *x);
/* the Cookie header's value for a request to u into out ("" for none);
   1 if there is one */
int ck_get(const URL *u, const CKCTX *x, SBUF *out);

/* the file they are kept in, read the first time cookies are needed,
   and the key their values are encrypted with there (common's secret.c:
   a cookie is often a login); NULL or "": no file (in memory only), no
   key (the values as they are) */
void ck_file(const char *path, const char *key);
/* lasting cookies changed since the file was read: written now (one
   safe save); 1 when nothing needed writing or it was written */
int ck_save(void);
/* 1 when ck_save has something to write (the quit's box weighs its steps) */
int ck_unsaved(void);
/* every cookie forgotten, the file too */
void ck_clear(void);
long ck_count(void);
long ck_bytes(void);
void ck_trim(void);		/* the store's size, as ck_max_bytes counts it */

/* the time, UTC seconds since 1970 (from the clock and tls_tz_minutes);
   tests set their own */
extern long long (*ck_now)(void);
/* a date as cookies and HTTP give them (RFC 6265bis's cookie-date
   algorithm, which reads HTTP's three forms too): 1 and *t, UTC seconds
   since 1970, or 0 */
int ck_date(const char *s, long long *t);
/* two hosts of the same site (example.co.il and www.example.co.il) */
int ck_same_site(const char *a, const char *b);

/* the store as text (the file's lines, values encrypted when there is a
   key) and back */
char *ck_text(long *len);
void ck_load(const char *s, long len);
/* a Netscape cookies.txt put into the store as its sites' Set-Cookie
   would (File > Import cookies...): the count taken, *sites their sites */
int ck_import(const char *text, long len, int *sites);
/* (a file read in pieces, whole lines each: ck_import_start first, *sites
   then the sites of all the pieces so far) */
void ck_import_start(void);
long ck_import_dropped(void);

#endif
