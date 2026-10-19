/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * browse.h - EWeb's browsing, below the window: what an address typed
 * means, fetching a page (http, https, a file on disk) and its
 * stylesheets, styling and laying it out, the history behind Back and
 * Forward, and pages of EWeb's own (the start page, "can't open"). The
 * GEM side (ui/) shows the page and asks for these; tests/host/ewrender.c
 * does the same without a screen.
 */
#ifndef BROWSE_H
#define BROWSE_H

#include <stdint.h>

#include "doc.h"
#include "layout.h"

#define BR_HISTORY 50		/* pages Back can go to */

typedef struct {
	char *url;		/* as loaded, fragment included */
	int32_t y;		/* how far down it was scrolled */
	/* the page as it was laid out, kept so that Back and Forward show it
	   at once (NULL: loaded again, from the cache or the server) */
	doc *d;
	page *p;
	char *src;		/* its source as it came, for Save page */
	long src_len;		/* its bytes (its memory is about 8 times that) */
	char mime[64];		/* what it is ("" a file) */
	int status;
	char *security;
} BR_ENTRY;

/* the memory pages kept for Back and Forward may take, roughly (the
   front end sets it from the machine's free memory; 16 MB by default) */
extern long br_keep_max;

/* what a load came to */
enum {
	BR_SHOWN,		/* the page is shown (it may be EWeb's "can't open" page) */
	BR_STOPPED,		/* stopped (Esc, Stop): the page before stays */
	BR_SAME,		/* a place on the page shown: br.scroll_to says where */
	BR_ELSEWHERE,		/* an address EWeb doesn't open itself (mailto:): br.elsewhere */
	BR_DOWNLOAD		/* a file EWeb doesn't show: br.dl holds it, to be saved
				   (br_dl_save) or let go (br_dl_free); the page shown stays */
};

typedef struct browser {
	/* the page shown */
	doc *d;
	page *p;
	char url[4096];		/* its address, after redirects, fragment included */
	char title[256];	/* UTF-8, "" for none */
	char security[96];	/* how it came: "TLS 1.2, ..." or "" */
	int status;		/* the server's answer (200...), 0 for a file or EWeb's own page */
	int32_t scroll_to;	/* after a load: where to scroll (device px), -1 the top */
	char elsewhere[1024];	/* BR_ELSEWHERE: the address */
	/* how long the last page took, ms: fetching it (its stylesheets
	   too), parsing, styling, laying out */
	unsigned long ms_fetch, ms_parse, ms_style, ms_layout;

	/* the window */
	int32_t width;		/* device px */
	int32_t zoom;		/* device px per CSS px, x1024 */

	BR_ENTRY hist[BR_HISTORY];
	int nhist, cur;		/* cur: the entry shown, -1 none */
	int at;			/* the entry the page shown belongs to, -1 none */
	char *src;		/* the page shown's source as it came (a file, the server's
				   answer, EWeb's own page), for Save page */
	long src_len;		/* its bytes (what keeping the page costs) */
	char mime[64];		/* what the server said it is ("" a file) */

	/* BR_DOWNLOAD: what came */
	struct {
		char *data;
		long len;
		char url[1024];	/* where from, after redirects */
		char mime[64];
	} dl;

	/* the page shown's pictures, fetched after it is shown */
	int pic_stopped;	/* Esc stopped them: no more for this page */
	int pic_relayout;	/* one came whose size the layout didn't know */
	unsigned long pic_laid;	/* when the page was last laid out again for them (ms) */
	int pic_n, pic_done;	/* how many to fetch, and fetched, for the status line */

	/* settings */
	int pictures;		/* pictures are shown (1, the default) */
	int typed;		/* the page loading was typed (br_go): no Referer */
	const char *search;	/* words typed: this with %s for them ("" for none) */
	const char *extra_css;	/* a stylesheet after the page's own (ewrender's), or NULL */

	/* the front end: what is happening, for the status line ("Opening
	   example.com..."); NULL for none */
	void (*say)(void *ud, const char *what);
	/* a line for the log (each picture: what it was, how long it took);
	   NULL for none */
	void (*log)(void *ud, const char *line);
	void *ud;
} browser;

void br_init(browser *b, int32_t width, int32_t zoom);
void br_free(browser *b);

/* an address typed (or given on the command line): a URL, a file, or
   words to search for */
int br_go(browser *b, const char *typed);
/* a link on the page shown (page.links index) */
int br_follow(browser *b, int link);
/* a form on the page shown sent (form.h): by the button submitter, or
   (NULL) from the field from by Return; (x, y) where a picture button
   was clicked. BR_STOPPED too when nothing is sent */
struct fctl;
int br_submit(browser *b, struct fctl *from, struct fctl *submitter, int32_t x, int32_t y);
int br_back(browser *b);
int br_forward(browser *b);
int br_reload(browser *b);
int br_can_back(const browser *b);
int br_can_forward(const browser *b);
/* the front end scrolled: kept in the history for Back */
void br_scrolled(browser *b, int32_t y);
/* the window's width changed: lay out again (returns 0 if out of memory) */
int br_relayout(browser *b, int32_t width);
/* a link's address, absolute, as Back would load it (for the status line) */
void br_link_url(const browser *b, int link, char *out, int size);

/* the page's pictures still to come (or to decode at the size shown) */
int br_pictures_pending(browser *b);
/* what br_pictures_step did */
enum {
	BRP_NONE,		/* nothing: none were left */
	BRP_DRAWN,		/* one came: draw the page from *y0 to *y1 again */
	BRP_LAYOUT,		/* the page was laid out again: draw it all */
	BRP_STOPPED		/* stopped (conn_stop) */
};
/* the next picture, those in the window (top to top + height, device
   px) first, then those below, then above: fetched and decoded. The
   layout is done again when pictures came whose sizes it didn't know,
   at most once in two seconds and when the last has come. */
int br_pictures_step(browser *b, int32_t top, int32_t height, int32_t *y0, int32_t *y1);
/* stop fetching this page's pictures (Esc) */
void br_pictures_stop(browser *b);

/* ---- saving (File > Save page, a link's file, a file EWeb doesn't show) */

/* a name for a file from url on a TOS disk: 8.3, capitals, what a page
   is ending in .HTM (mime: what it is, "" if not known) */
void br_file_name(const char *url, const char *mime, char *out, int size);
/* the page shown's source to path, as it came (one safe save): 1 saved */
int br_save_page(browser *b, const char *path);
/* BR_DOWNLOAD's file to path (one safe save), then let go: 1 saved */
int br_dl_save(browser *b, const char *path);
void br_dl_free(browser *b);
/* url's file fetched to path as it comes, gathered br_save_buf bytes at
   a time in memory and written in pieces, the file open for the job
   (6.5); the status line tells how much has come. */
enum {
	BRS_SAVED,
	BRS_FAILED,		/* err says why; nothing is left at path */
	BRS_STOPPED		/* Esc; nothing is left at path */
};
int br_save_url(browser *b, const char *url, const char *path, char *err, int errlen);
/* the bytes br_save_url gathers before each write (256 KB; the front
   end may set less on a small machine), and how many the last brought */
extern long br_save_buf, br_saved;

/* the memory cache's size (pages and stylesheets kept for Back and for
   the next page of a site), bytes */
extern long br_cache_max;
void br_cache_clear(void);

#endif
