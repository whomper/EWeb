/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * doc.h - a web page's document: EWeb's HTML parser (html.c) makes these
 * nodes, whose elements are at the same time the ec_nodes EWeb's CSS
 * engine (src/css) styles, so there is one tree and no copying. All of a
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
	T_FIELDSET, T_LEGEND, T_OPTGROUP, T_PICTURE, T_SOURCE, T_NTAGS
};

typedef struct dnode dnode;
struct dnode {
	ec_node e;			/* an element as the CSS engine sees it (first: a cast away) */
	uint8_t type;			/* DN_* */
	uint8_t ns;			/* an element's namespace: DN_NS_HTML, ... */
	uint16_t tag;			/* T_* */
	uint16_t hid;			/* the parser's name for it (html.c) */
	uint8_t pf;			/* the parser's flags (html.c) */
	dnode *parent, *first, *last, *prev, *next;
	char *text;			/* a text node's, UTF-8 */
	uint32_t len, cap;
	ec_attr *attrs;
	const ec_atom **cls;
	uint16_t nattr, ncls;
	int32_t y;			/* where layout put it (for #fragment), -1: nowhere */
	dnode *owner;			/* a form control: the form the parser put it in */
	struct fctl *ctl;		/* a form control: its state (form.h) */
};

typedef struct doc {
	dnode *root;			/* the document node */
	dnode *html, *body;		/* NULL if the page has none */
	ec_ctx *css;			/* made on the UA sheet's base (ua) when there is one */
	const ec_ctx *ua;
	char title[256];		/* UTF-8, white space collapsed */
	char base[1024];		/* <base href>, "" if none */
	char charset[40];		/* what the page was read as */
	uint8_t quirks;			/* 0, 1 quirks mode, 2 limited quirks */
	struct dblk *mem;		/* the blocks of memory (doc_alloc) */
	uint32_t elements;
	struct fctl *ctls;		/* the form controls, in document order (form.h) */
	/* its scripts' state (src/js), let go first by doc_free */
	void *js;
	void (*js_free)(void *js);
} doc;

/* Parses an HTML page. charset is the HTTP answer's (confident), or
   NULL to find it from the page; media: the screen, for @media.
   Returns NULL only if out of memory. */
doc *doc_parse(const char *data, size_t len, const char *charset, const ec_media *media);
/* the same with the parser's options (html.h): scripting, a fragment */
struct html_opts;
doc *doc_parse_opts(const char *data, size_t len, const struct html_opts *o, const ec_media *media);
void doc_free(doc *d);

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

/* The page's stylesheets in document order: cb is called for each
   <style> with its text (is_link 0) and each <link rel=stylesheet> for a
   screen with its href (is_link 1). */
void doc_sheets(doc *d, void (*cb)(void *ud, int is_link, const char *s, size_t len), void *ud);

/* styles every element: the UA sheet, then the sheets added with
   ec_sheet() in between (the loader's), then selection */
void doc_add_ua(doc *d);
void doc_select(doc *d);

/* EWeb's UA stylesheet */
extern const char doc_ua_css[];

#endif
