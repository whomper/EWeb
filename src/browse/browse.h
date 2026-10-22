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

/* a load's phases, in order */
enum { BRPH_CONNECT, BRPH_PAGE, BRPH_STYLES, BRPH_BUILD, BRPH_PICTURES };

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
	/* its encoding (View > Encoding): the one chosen by hand (1 + its
	   place in br_encodings, 0 Auto), whether it may be read in another,
	   and while it is kept the HTTP answer's charset (NULL none) */
	uint8_t enc, encodable;
	char *charset;
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
	BR_DOWNLOAD,		/* a file EWeb doesn't show, or one its server says to
				   save (Content-Disposition: attachment): br.dl holds it, to
				   be saved (br_dl_save) or let go (br_dl_free) */
	BR_SAVE			/* a link to save, not to follow (<a download> to the page's
				   own site): br.dl's url and name say which, for br_save_url
				   to fetch */
};
/* Each but BR_SHOWN leaves the page shown as it was, unless the load
   showed a new one on the way: a page whose script, as it loaded, clicked
   a download link ("your download starts now") or sent the browser on to
   a file, elsewhere or to a page that was stopped. br.shows tells. */

typedef struct browser {
	/* the page shown */
	doc *d;
	page *p;
	char url[4096];		/* its address, after redirects, fragment included */
	char title[256];	/* UTF-8, "" for none */
	char security[96];	/* how it came: "TLS 1.2, ..." or "" */
	int status;		/* the server's answer (200...), 0 for a file or EWeb's own page */
	int32_t scroll_to;	/* after a load: where to scroll (device px), -1 the top */
	unsigned long shows;	/* pages shown so far, each new one counts: a front end
				   that remembers it knows when one is to be drawn, whatever
				   the load came to */
	char elsewhere[1024];	/* BR_ELSEWHERE: the address */
	/* how long the last page took, ms: fetching it (its stylesheets
	   too), parsing, styling, laying out */
	unsigned long ms_fetch, ms_parse, ms_style, ms_layout;

	/* the window */
	int32_t width;		/* device px */
	int32_t zoom;		/* device px per CSS px, x1024 */
	int32_t text;		/* the text's size, x1024 (LAYOUT_OPT.text) */

	BR_ENTRY hist[BR_HISTORY];
	int nhist, cur;		/* cur: the entry shown, -1 none */
	int at;			/* the entry the page shown belongs to, -1 none */
	char *src;		/* the page shown's source as it came (a file, the server's
				   answer, EWeb's own page), for Save page */
	long src_len;		/* its bytes (what keeping the page costs) */
	char mime[64];		/* what the server said it is ("" a file) */
	/* its encoding (View > Encoding): the HTTP answer's charset ("" none),
	   the one chosen by hand ("" Auto: the page's own, as the HTML
	   Standard's sniffing finds it; d->charset says which that was), and
	   whether it may be read in another (a page or text file that came,
	   not EWeb's own page or a picture's) */
	char charset[24], enc[16];
	int encodable;

	/* BR_DOWNLOAD: what came; BR_SAVE: what to save; br_save_url: what
	   is being saved */
	struct {
		char *data;
		long len;
		char url[1024];	/* where from, after redirects */
		char mime[64];
		/* the name its server gave (Content-Disposition's filename) or
		   the link (<a download>'s), UTF-8; "" none */
		char name[256];
	} dl;

	/* the page shown's pictures, fetched after it is shown */
	int pic_stopped;	/* Esc stopped them: no more for this page */
	int pic_relayout;	/* one came whose size the layout didn't know */
	unsigned long pic_laid;	/* when the page was last laid out again for them (ms) */
	int pic_n, pic_done;	/* how many to fetch, and fetched, for the status line */

	/* settings */
	int pictures;		/* pictures are shown (1, the default) */
	/* pictures through the Pi's gateway as the screen's pens, made there
	   (webgate's aerapens): 1 always (EWEB.PRG's PENS=, as it starts); -1
	   (br_init's) on the ST edition and without the DSP always, else when
	   answers come fast enough (rate); 0 never */
	int pens;
	long rate;		/* bytes a second the last big answer came at (0: not known) */
	int snapshot;		/* pages through the Pi's browser: webgate's snapshots,
				   after their scripts ran (with the gateway only) */
	int typed;		/* the page loading was typed (br_go): no Referer */
	const char *search;	/* words typed: this with %s for them ("" for none) */
	/* scripts (src/js): may they run at all here (an FPU, EWEB.PRG), and
	   the sites whose scripts run: host names with spaces between ("*"
	   all, "file" pages on the disk); off by default (the design) */
	int scripts;
	const char *script_sites;
	char note[96];		/* about the page shown, for the status line ("" none): its
				   scripts were too much, it came from the cache out of date,
				   it goes on to another page in a while */
	/* the page shown's refresh (br_refresh_due): where it goes (malloc'ed;
	   NULL none), after how many seconds, and when that is (pf_ms) */
	char *refresh;
	unsigned long refresh_s, refresh_at;
	int refresh_run;	/* refreshes at once in a row that brought the page shown */
	int refresh_pics;	/* the page itself again: its time counts once its
				   pictures are in (refresh_at not yet set) */
	int refresh_loop;	/* its refresh was stopped, at once again and again
				   (BR_REFRESH_RUN): br_refresh_note says so */
	const char *extra_css;	/* a stylesheet after the page's own (ewrender's), or NULL */

	/* the front end: what is happening, for the status line ("Opening
	   example.com..."); NULL for none */
	void (*say)(void *ud, const char *what);
	/* the load's phase now (BRPH_*), for the status bar's ribbon; NULL: none */
	void (*phase)(void *ud, int ph);
	/* a line for the log (each picture: what it was, how long it took);
	   NULL for none */
	void (*log)(void *ud, const char *line);
	/* may this address be opened? 0: no (the front end has said why: a
	   secure page with neither TLS nor the Pi gateway); NULL: all may */
	int (*can_open)(void *ud, const char *url);
	/* a script's alert, confirm or prompt (js.h's JS_ALERT...): 1 OK, 0
	   Cancel (prompt's answer in out); NULL: OK at once */
	int (*ask)(void *ud, int kind, const char *text, char *out, int size);
	/* what a script's console.log writes; NULL: nowhere */
	void (*console)(void *ud, const char *line);
	/* where a file br_save_url saves goes, asked once its answer has
	   begun (its name is known then): name the TOS name to offer; 1 and
	   path (size bytes) set, 0 cancelled. NULL: none is saved */
	int (*save_as)(void *ud, const char *name, char *path, int size);
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
/* a link's download attribute (<a download>): its value, "" when it
   gives no name, NULL when the link has none */
const char *br_link_download(const browser *b, int link);

/* ---- encodings */

/* the page shown read again from its source in another encoding (View >
   Encoding): name one of br_encodings (or a label of one), or "" for
   Auto (the page's own). The choice stays with the page until another
   is loaded: Reload, Back and Forward to it read it so too. BR_SHOWN;
   BR_STOPPED when it can't be (not encodable, a name not offered) and
   the page stays */
int br_encoding(browser *b, const char *name);
/* the encodings View > Encoding offers: those EWeb decodes whose
   letters EWEB.FNT has (no Greek, Cyrillic, Arabic or Thai), each by
   its name as html_encoding gives it and as d->charset has it */
typedef struct {
	const char *label, *name;
} BR_ENC;
extern const BR_ENC br_encodings[];
extern const int br_nencodings;
/* the label of an encoding of br_encodings, else its name */
const char *br_encoding_label(const char *name);

/* the encoding of a page at url that names none and isn't UTF-8 (the
   sniffing's last step, the locale's default, which the standard lets
   come from the address's top-level domain): windows-1255 for Israel's
   .il, windows-1252 for the rest */
const char *br_fallback(const char *url);

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

/* ---- the page shown's refresh: its Refresh header, else the first
   <meta http-equiv=refresh> the parser put in (the HTML Standard's shared
   declarative refresh steps). A page loaded anew (not by Back or Forward)
   goes on to the address it gives its seconds after it is shown, or
   comes again its seconds after its pictures are in (the standard's
   "completely loaded"); any load, and Esc, cancel it. */

/* refreshes at once (0 s) in a row EWeb follows: a page that refreshes
   itself, or two that send each other on, stop after these */
#define BR_REFRESH_RUN 5
/* ms until it is due (0: now), -1 for none; the page itself again while
   its pictures come: all its seconds (at least 1 ms) */
long br_refresh_due(const browser *b);
/* it is due: the page it goes to loaded in place of the page shown in
   the history, the page itself fetched anew, or a place on it (BR_*) */
int br_refresh_go(browser *b);
/* Esc: no refresh for the page shown */
void br_refresh_cancel(browser *b);
/* what the status line says at rest while it waits, with the time left
   ("Going on to example.com in 5 s: Esc stays here."), or once refreshes
   at once in a row were stopped (until the next load): 0 when there is
   nothing to say (none, or at once) */
int br_refresh_note(const browser *b, char *out, int size);

/* ---- the page shown's scripts (js.h) */

/* are url's scripts run (its site is one of script_sites) */
int br_scripts_for(const browser *b, const char *url);
/* ms until its next timer, -1 for none */
long br_js_due(const browser *b);
/* what a script did (after br_js_step, br_js_event, br_js_click) */
enum {
	BRJ_NONE,		/* nothing to show */
	BRJ_DRAWN,		/* the page was laid out again: draw it all */
	BRJ_LOAD		/* a script sent the browser elsewhere: *load is the load's BR_* */
};
/* its timers that are due run */
int br_js_step(browser *b, int *load);
/* a click at (x, y) on the page: its element's click event; *cancelled
   if a script said no to the default (following the link, pressing the
   button) */
int br_js_click(browser *b, int32_t x, int32_t y, int *cancelled, int *load);
/* an event at a form control ("input", "change", "focus", "blur",
   "submit" at its form): *cancelled likewise */
int br_js_event(browser *b, struct fctl *c, const char *type, int *cancelled, int *load);
/* a link to javascript:...: its code run */
int br_js_url(browser *b, const char *url, int *load);

/* the page shown to the bookmarks (bookmark.h; about:bookmarks lists
   them): 1 added and saved, -1 already there, 0 not (EWeb's own page,
   or the disk wouldn't take it) */
int br_bookmark(browser *b);

/* ---- saving (File > Save page, a link's file, a file EWeb doesn't show) */

/* a name for a file from url on a TOS disk: 8.3, capitals, only the
   characters GEMDOS takes, what a page is ending in .HTM (mime: what it
   is, "" if not known). given: the name its server or link gave (dl.name;
   NULL or "" none), used before the address's: its last part, its ending
   kept */
void br_file_name(const char *url, const char *given, const char *mime, char *out, int size);
/* the page shown's source to path, as it came (one safe save): 1 saved */
int br_save_page(browser *b, const char *path);
/* BR_DOWNLOAD's file to path (one safe save), then let go: 1 saved */
int br_dl_save(browser *b, const char *path);
void br_dl_free(browser *b);
/* url's file fetched to a file as it comes, gathered br_save_buf bytes
   at a time in memory and written in pieces, the file open for the job
   (6.5); the status line tells how much has come. The file is path, or
   when path is "" the one b->save_as gives once the answer has begun,
   with the name to offer: Content-Disposition's, else given (the link's
   download attribute, when the answer is from the page's own site), else
   the address's (the HTML Standard's suggested filename); path is then
   the one chosen. b->dl's url, mime and name say what came. A gzip file
   is saved as it came (nothing packed is asked for). */
enum {
	BRS_SAVED,
	BRS_FAILED,		/* err says why; nothing is left at path */
	BRS_STOPPED,		/* Esc; nothing is left at path */
	BRS_CANCELLED		/* save_as said no: nothing was written */
};
int br_save_url(browser *b, const char *url, const char *given, char *path, int size, char *err, int errlen);
/* the bytes br_save_url gathers before each write (256 KB; the front
   end may set less on a small machine), and how many the last brought */
extern long br_save_buf, br_saved;

/* the memory cache's size (pages and stylesheets kept for Back and for
   the next page of a site), bytes */
extern long br_cache_max;
void br_cache_clear(void);

#endif
