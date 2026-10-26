/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * doc.h - a web page's document: EWeb's HTML parser (html.c) makes these
 * nodes, whose elements hold the ec_nodes EWeb's CSS engine (src/css)
 * styles, so there is one tree and no copying. All of a
 * document's memory is taken in large blocks (doc_alloc) and given back
 * at once by doc_free.
 */
#ifndef EW_DOC_H
#define EW_DOC_H

#include <stddef.h>
#include <stdint.h>

#include "ecss.h"

/* DN_OTHER is a comment (its text); a DOCTYPE keeps its name, public and
   system identifiers in text, each ending in a 0; a processing
   instruction its target and data */
enum { DN_DOCUMENT, DN_ELEMENT, DN_TEXT, DN_OTHER, DN_DOCTYPE, DN_PI };
/* the namespaces */
enum { DN_NS_NULL, DN_NS_HTML, DN_NS_MATHML, DN_NS_SVG };

/* the elements layout and the loader look at */
enum {
	T_OTHER, T_HTML, T_HEAD, T_BODY, T_TITLE, T_BASE, T_LINK, T_META, T_STYLE, T_SCRIPT,
	T_NOSCRIPT, T_A, T_IMG, T_BR, T_HR, T_LI, T_UL, T_OL, T_TABLE, T_TR, T_TD, T_TH,
	T_INPUT, T_BUTTON, T_SELECT, T_OPTION, T_TEXTAREA, T_PRE, T_IFRAME, T_SVG, T_CANVAS,
	T_VIDEO, T_AUDIO, T_OBJECT, T_EMBED, T_TEMPLATE, T_LABEL, T_FORM, T_WBR,
	T_FIELDSET, T_LEGEND, T_OPTGROUP, T_PICTURE, T_SOURCE, T_DIV, T_P, T_CAPTION,
	T_THEAD, T_TBODY, T_TFOOT, T_DETAILS, T_SUMMARY, T_FRAMESET, T_FRAME, T_NTAGS
};

typedef struct dnode dnode;
struct dnode {
	uint8_t type;			/* DN_* */
	uint8_t ns;			/* an element's namespace: DN_NS_HTML, ... */
	uint16_t tag;			/* T_* */
	uint16_t hid;			/* the parser's name for it (html.c) */
	uint8_t pf;			/* the parser's flags (html.c) */
	dnode *parent, *first, *last, *prev, *next;
	char *text;			/* a text node's, UTF-8 */
	uint32_t len, cap;
	uint16_t nattr, ncls;		/* (0 but in an element: dn_attr asks any node) */
	int32_t y;			/* where layout put it (for #fragment), -1: nowhere */
	/* an element's (and the document node's) alone: the others are
	   DN_SHORT bytes, a page having about as many text nodes as elements */
	ec_node e;			/* an element as the CSS engine sees it */
	ec_attr *attrs;
	const ec_atom **cls;
	dnode *owner;			/* a form control: the form the parser put it in */
	struct fctl *ctl;		/* a form control: its state (form.h) */
};

/* the bytes of a node of that type (DN_*): a text node, a comment, a
   DOCTYPE or a processing instruction has no e, attrs, cls, owner or ctl */
#define DN_SHORT offsetof(dnode, e)
#define DN_SIZE(type) ((type) == DN_ELEMENT || (type) == DN_DOCUMENT ? sizeof(dnode) : DN_SHORT)
/* what a node of that type is given, and its part past DN_SIZE made
   unreadable: in the checks' builds (AddressSanitizer) a short node is
   given the whole of a dnode, the rest poisoned, so that a read of an
   element's part in it is caught */
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define DN_ALLOC(type) sizeof(dnode)
#define DN_POISON(n) ASAN_POISON_MEMORY_REGION((char *) (n) + DN_SIZE((n)->type), sizeof(dnode) - DN_SIZE((n)->type))
#else
#define DN_ALLOC(type) DN_SIZE(type)
#define DN_POISON(n) ((void) 0)
#endif

/* a <meta http-equiv=refresh> as the parser put it in (doc.refresh) */
typedef struct drefresh {
	struct drefresh *next;		/* the next put in, or NULL */
	const char *content;		/* its content then (a script may change it) */
	uint8_t based;			/* a <base href> was in the document then */
} drefresh;

typedef struct doc {
	dnode *root;			/* the document node */
	dnode *html, *body;		/* NULL if the page has none */
	ec_ctx *css;			/* made on the UA sheet's base (ua) when there is one */
	const ec_ctx *ua;
	char title[256];		/* UTF-8, white space collapsed */
	char base[1024];		/* <base href>, "" if none */
	char target[64];		/* <base target>, "" if none */
	char charset[40];		/* what the page was read as */
	uint8_t quirks;			/* 0, 1 quirks mode, 2 limited quirks */
	struct dblk *mem;		/* the blocks of memory (doc_alloc) */
	uint32_t elements;
	struct fctl *ctls;		/* the form controls, in document order (form.h) */
	/* the <meta http-equiv=refresh>s the parser put in whose content is
	   one (the HTML Standard's Refresh state), in that order, or NULL */
	struct drefresh *refresh;
	/* its scripts' state (src/js), let go first by doc_free */
	void *js;
	void (*js_free)(void *js);
	/* the frames it shows (src/browse: their documents), let go by
	   doc_free after its scripts */
	void *frames;
	void (*frames_free)(void *frames);
	unsigned long marked;		/* the history's changes its links were marked at, + 1
					   (0: never); browse's */
	uint8_t scripting;		/* parsed for scripts run (its <noscript>s text) */
	/* its sheets as they were added (src/browse): the widths, CSS px,
	   their media queries hold at as they did then (ec_media_span), and
	   with scripting, which they were (doc_sheets_key) */
	int32_t span_lo, span_hi;
	uint32_t sheets_key;
} doc;

/* Parses an HTML page. charset is the HTTP answer's (confident), or
   NULL to find it from the page; media: the screen, for @media.
   Returns NULL only if out of memory, or stopped (doc_poll). */
doc *doc_parse(const char *data, size_t len, const char *charset, const ec_media *media);
/* the same with the parser's options (html.h): scripting, a fragment */
struct html_opts;
doc *doc_parse_opts(const char *data, size_t len, const struct html_opts *o, const ec_media *media);
void doc_free(doc *d);

/* asked now and then while a page is made, when set: every 64 tags the
   parser reads (DOC_POLL_PARSE) and every 16 boxes the layout makes or
   lays out (DOC_POLL_LAYOUT; ecss.h's ec_poll while it is styled). 0
   stops it, given up as when out of memory (Esc, while the program's
   window is kept alive meanwhile) */
enum { DOC_POLL_PARSE, DOC_POLL_LAYOUT };
extern int (*doc_poll)(int what);

/* n bytes of the document's memory, aligned for any node or pointer;
   NULL if out of memory. doc_grow makes a block of old bytes new bytes
   long, in place when it was the last taken, and keeps its contents. */
void *doc_alloc(doc *d, size_t n);
void *doc_grow(doc *d, void *p, size_t old, size_t n);

/* the value of an element's attribute (NUL-terminated), or NULL */
const char *dn_attr(const dnode *n, const char *name);
/* for scripts: an attribute set (its id and classes kept with it, the
   element's hints made again by doc_refresh) or removed; name in lower
   case. dn_set_attr returns 0 if out of memory */
int dn_set_attr(doc *d, dnode *n, const char *name, const char *val, size_t len);
void dn_remove_attr(doc *d, dnode *n, const char *name);
/* after a script changed the tree: the element links, flags and hints
   the CSS engine reads, and the form controls, made again */
void doc_refresh(doc *d);

/* The parsing in the HTML Standard's shared declarative refresh steps
   (4.2.5.3), for a <meta http-equiv=refresh>'s content and the Refresh
   header: 1 when in is one, *secs its time (whole seconds, at most
   DOC_REFRESH_MAX) and *url, *ulen the address as written, its quotes
   gone (*url NULL: none, the page itself); 0 when it isn't (ignored). */
#define DOC_REFRESH_MAX 2000000UL	/* 23 days: ms of it fit a long */
int doc_refresh_parse(const char *in, unsigned long *secs, const char **url, size_t *ulen);
/* n is a <meta http-equiv=refresh> whose content is one, not in a
   template's contents (html.c asks as it inserts each <meta>) */
int doc_refresh_meta(const dnode *n);
/* the most doc.refresh holds: the first whose address can be used is
   the page's refresh, so those after are wanted only when the ones
   before are javascript: */
#define DOC_REFRESH_METAS 16
/* details and summary (the HTML Standard, 4.11.1 and 4.11.2), in both
   editions: an HTML element of that tag */
#define DN_IS(n, t) ((n)->type == DN_ELEMENT && (n)->ns == DN_NS_HTML && (n)->tag == (t))
/* a details element's summary, the one a click opens and closes it by
   (its first summary child), or NULL (the layout shows one of its own) */
dnode *dn_summary(const dnode *details);
/* its open attribute set ("") or removed, its hints and the CSS engine's
   links kept, so that styling it again needs no doc_refresh; 0 if out of
   memory */
int dn_set_open(doc *d, dnode *n, int open);
/* the other open details of n's group (the same, not empty, name
   attribute, in the same tree: a detached one's, a template's contents'
   own), or NULL */
dnode *dn_details_rival(const doc *d, const dnode *n);
/* details opened or closed, as a click on its summary does (4.11.2, the
   summary's activation behaviour): opened, the open one of its group is
   closed (4.11.1, ensure details exclusivity by closing other elements if
   needed). changed(ud, k) for each whose open attribute changed, n
   first; 0 if out of memory */
int dn_details_toggle(doc *d, dnode *n, void (*changed)(void *ud, dnode *k), void *ud);
/* 1 when a closed details folds n away: n is in its content, not its
   summary (n then has no box, whatever its y says) */
int dn_folded(const dnode *n);
/* the ancestor details revealing algorithm (4.11.1; scroll to the
   fragment, 7.4.6.4): each closed details n is in the content of opened,
   the innermost first, as dn_details_toggle does (changed for each).
   Returns how many were opened */
int dn_reveal(doc *d, dnode *n, void (*changed)(void *ud, dnode *k), void *ud);

/* The page's stylesheets in document order: cb is called for each
   <style> with its text (is_link 0) and each <link rel=stylesheet> with
   its href (is_link 1) that is to be applied: its media holding for the
   document's screen (the CSS engine's media queries, as for @media), its
   type CSS, not disabled, and of the preferred set (an alternative sheet
   only when it is that set's). With d->scripting, d->sheets_key is set. */
void doc_sheets(doc *d, void (*cb)(void *ud, int is_link, const char *s, size_t len), void *ud);
/* which elements and attributes the page's sheets are now, as a number:
   another than d->sheets_key when a script changed them (a <link>'s
   media, rel or disabled, a <style> added or its text). Only the elements
   that are sheets count: another <link> added or changed (a preconnect,
   an icon) leaves it as it was */
uint32_t doc_sheets_key(const doc *d);
/* n is a <link> whose sheet is fetched when its media holds (rel
   stylesheet, an alternative too, href, not disabled, a CSS type) or
   one preloaded as a style: for scripts, its load event */
int doc_link_loads(const dnode *n);
/* has the page a script or an on... attribute: 1 (the scripts' runtime
   is needed, EWEB.PRG's EWEBJS.OVL loaded for it) */
int doc_has_scripts(const doc *d);

/* styles every element: the UA sheet, then the sheets added with
   ec_sheet() in between (the loader's), then selection; doc_select:
   0, or -1 when out of memory (some elements have no style) */
void doc_add_ua(doc *d);
int doc_select(doc *d);
/* each link's EC_NODE_VISITED from visited(ud, href) (its href as the
   page has it; visited NULL: none is), before doc_select; how many
   changed. dry: only counted */
long doc_mark_links(doc *d, int (*visited)(void *ud, const char *href), void *ud, int dry);

/* EWeb's UA stylesheet, and what it adds in quirks mode */
extern const char doc_ua_css[];
extern const char doc_quirks_css[];

#endif
