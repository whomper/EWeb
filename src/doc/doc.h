/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * doc.h - a web page's document: Hubbub parses the HTML into these
 * nodes, whose elements are at the same time the ec_nodes EWeb's CSS
 * engine (src/css) styles, so there is one tree and no copying.
 */
#ifndef EW_DOC_H
#define EW_DOC_H

#include <stddef.h>
#include <stdint.h>

#include "ecss.h"

enum { DN_DOCUMENT, DN_ELEMENT, DN_TEXT, DN_OTHER };
/* the namespaces, as Hubbub numbers them */
enum { DN_NS_NULL, DN_NS_HTML, DN_NS_MATHML, DN_NS_SVG };

/* the elements layout and the loader look at */
enum {
	T_OTHER, T_HTML, T_HEAD, T_BODY, T_TITLE, T_BASE, T_LINK, T_META, T_STYLE, T_SCRIPT,
	T_NOSCRIPT, T_A, T_IMG, T_BR, T_HR, T_LI, T_UL, T_OL, T_TABLE, T_TR, T_TD, T_TH,
	T_INPUT, T_BUTTON, T_SELECT, T_OPTION, T_TEXTAREA, T_PRE, T_IFRAME, T_SVG, T_CANVAS,
	T_VIDEO, T_AUDIO, T_OBJECT, T_EMBED, T_TEMPLATE, T_LABEL, T_FORM, T_WBR, T_NTAGS
};

typedef struct dnode dnode;
struct dnode {
	ec_node e;			/* an element as the CSS engine sees it (first: a cast away) */
	uint8_t type;			/* DN_* */
	uint8_t ns;			/* Hubbub's namespace of an element: DN_NS_HTML, ... */
	uint16_t tag;			/* T_* */
	dnode *parent, *first, *last, *prev, *next;
	char *text;			/* a text node's, UTF-8 */
	uint32_t len, cap;
	ec_attr *attrs;
	const ec_atom **cls;
	uint16_t nattr, ncls;
	uint32_t refs;			/* Hubbub's */
	int32_t y;			/* where layout put it (for #fragment), -1: nowhere */
};

typedef struct doc {
	dnode *root;			/* the document node */
	dnode *html, *body;		/* NULL if the page has none */
	ec_ctx *css;
	char title[256];		/* UTF-8, white space collapsed */
	char base[1024];		/* <base href>, "" if none */
	char charset[40];		/* what the page was read as */
	uint32_t elements;
} doc;

/* Parses an HTML page. charset is the HTTP answer's (confident), or
   NULL to find it from the page; media: the screen, for @media.
   Returns NULL only if out of memory. */
doc *doc_parse(const char *data, size_t len, const char *charset, const ec_media *media);
void doc_free(doc *d);

/* the value of an element's attribute (NUL-terminated), or NULL */
const char *dn_attr(const dnode *n, const char *name);

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
