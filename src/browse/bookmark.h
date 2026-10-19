/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bookmark.h - EWeb's bookmarks: kept in BOOKMARK.HTM next to EWEB.PRG
 * in the bookmark file other browsers read and write (Netscape's, an
 * HTML page: a <DT><A HREF> for each), read once a start and written in
 * one safe save at each change (the guidelines' 6.5). A file another
 * browser exported is read too: its folders are left out, its links
 * kept in order.
 */
#ifndef BOOKMARK_H
#define BOOKMARK_H

typedef struct {
	char *url;
	char *title;		/* UTF-8, "" for none */
} BM;

extern BM *bm_list;
extern int bm_n;
extern unsigned bm_changes;	/* counts every change (a menu of them is made again) */

/* where the bookmarks are kept: read from there now (none there: none) */
void bm_file(const char *path);
/* the bookmarks from a file's bytes, in place of those there were;
   0 if out of memory */
int bm_parse(const char *data, long len);
/* the file for them, malloc'ed, *len its bytes; NULL if out of memory */
char *bm_write(long *len);
/* the one for url, or -1 */
int bm_find(const char *url);
/* at the end and saved: 1; -1 already there; 0 out of memory or the
   disk wouldn't take it (then it isn't added) */
int bm_add(const char *url, const char *title);
/* gone and saved: 1; 0 the disk wouldn't take it (then it stays) */
int bm_remove(int i);
void bm_free(void);

#endif
