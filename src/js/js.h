/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js.h - a little JavaScript (the design's part 2): QuickJS running a
 * page's scripts on a lean DOM made of EWeb's own document nodes
 * (doc.h), with a hard budget. Each document whose scripts run has its
 * own runtime, kept with it (doc.js) and let go by doc_free, so a page
 * kept for Back keeps its scripts' state.
 *
 * The scripts run once the page is parsed, in document order (as if
 * each were deferred); then DOMContentLoaded and load. What they ask of
 * the browser (files, cookies, a new address, an alert) goes through
 * js_host. After a script, an event or a timer, js_changed says whether
 * the document must be styled and laid out again.
 *
 * Only EWEB.PRG has it, and only with an FPU: QuickJS is built for the
 * 68881 (DESIGN.md). The ST edition and the hosts without it get
 * js_none.c's stubs.
 */
#ifndef EW_JS_H
#define EW_JS_H

#include "doc.h"

typedef struct js_host {
	/* a file a script asks for (a <script src>, fetch, XMLHttpRequest):
	   1 and *data (malloc'd, the caller frees it), *len, *status and
	   mime; 0 if it couldn't be had. site: the page asking (its address);
	   method "GET" or "POST" (body, its ctype). */
	int (*fetch)(void *ud, const char *site, const char *url, const char *method, const char *body, long blen,
		     const char *ctype, char **data, long *len, int *status, char *mime, int msize);
	/* href made absolute against base ("" when it can't be), its query
	   in the page's encoding (HTML's "encoding-parsing a URL"), or in
	   UTF-8 when utf8 (fetch(), whose Request parses it so) */
	void (*resolve)(void *ud, const char *base, const char *href, char *out, int size, int utf8);
	/* document.cookie: the cookies a script may see for url (no
	   HttpOnly ones), and one it sets */
	void (*cookie_get)(void *ud, const char *url, char *out, int size);
	void (*cookie_set)(void *ud, const char *url, const char *line);
	/* alert, confirm and prompt (out: the answer, NULL for alert and
	   confirm): 1 OK, 0 Cancel */
	int (*ask)(void *ud, int kind, const char *text, char *out, int size);
	/* a line for the log (a script's error, the budget); NULL: none */
	void (*log)(void *ud, const char *line);
	/* what console.log writes (NULL: nowhere; EWEB.PRG's log never has
	   it: a page may log what was typed, 7.3) */
	void (*console)(void *ud, const char *line);
	void *ud;
} js_host;

enum { JS_ALERT, JS_CONFIRM, JS_PROMPT };

/* the budget (the design: roughly 100-200 KB of script and a few
   seconds a page) */
#define JS_MAX_SOURCE	(200L * 1024)	/* bytes of script a page may bring */
#define JS_PAGE_MS	5000		/* its scripts, DOMContentLoaded and load */
#define JS_EVENT_MS	2000		/* each event, timer or promise job after */
#define JS_MAX_MEMORY	(8L << 20)	/* the runtime's heap */

/* what js_page came to */
enum {
	JS_NONE,	/* the page has no scripts: nothing was made */
	JS_DONE,	/* they ran (some may have failed: the log says) */
	JS_TOO_MUCH,	/* over the budget: show the page without scripts */
	JS_NO_MEMORY
};

/* is there JavaScript in this build (0: js_none.c) */
extern const int js_built;

/* the page's scripts run: url is its address, base what its links are
   resolved against. The document's js is set (JS_DONE). */
int js_page(doc *d, const char *url, const char *base, const js_host *h);
/* the document changed (since the last call): style and lay it out
   again (doc_refresh first) */
int js_changed(doc *d);
/* where a script sent the browser (location, form.submit's address),
   or NULL; its method and body for a form sent with POST (NULL: GET) */
const char *js_navigation(doc *d, const char **post, long *len, const char **ctype);
/* when that is a link a script clicked (a.click()) that has a download
   attribute: its value ("" for no name), else NULL */
const char *js_navigation_download(doc *d);
void js_navigation_done(doc *d);
/* ms until the next timer is due (0: now), -1 for none */
long js_due(doc *d);
/* the timers that are due, and the promise jobs waiting, run */
void js_run_timers(doc *d);
/* an event at n ("click", "input", "change", "submit", "focus",
   "blur"), from the user: 1 if a listener cancelled its default action */
int js_event(doc *d, dnode *n, const char *type);
/* a javascript: address followed: its code run */
void js_run(doc *d, const char *code);
/* a details element opened or closed by the user (a click on its
   summary): its toggle event queued, fired with the timers (js_due 0) */
void js_toggled(doc *d, dnode *n);
/* where elements are, for getBoundingClientRect and the like: the
   front end's page (layout.h) as it is now, or NULL */
struct page;
void js_set_page(doc *d, const struct page *p);
/* the time the page's scripts took (ms) and their bytes, for the log */
void js_stats(doc *d, unsigned long *ms, long *bytes);

#endif
