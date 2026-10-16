/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* A small document tree for nsbench, built by Hubbub's tree handler. */
#ifndef NSBENCH_DOM_H
#define NSBENCH_DOM_H

#include <stddef.h>
#include <stdint.h>
#include <libwapcaplet/libwapcaplet.h>

enum { N_DOCUMENT, N_DOCTYPE, N_ELEMENT, N_TEXT, N_COMMENT };

typedef struct attr {
	lwc_string *name;
	lwc_string *value;
} attr;

typedef struct node node;
struct node {
	uint8_t type;
	uint8_t ns;			/* hubbub_ns of an element */
	uint16_t nattr;
	uint32_t refs;
	node *parent, *first, *last, *prev, *next;
	lwc_string *name;		/* element and doctype names */
	attr *attr;
	lwc_string **classes;		/* the class attribute, split */
	uint32_t nclass;
	lwc_string *id;
	char *text;			/* text and comments */
	uint32_t len, cap;
	void *css_data;			/* LibCSS's per-node data */
	void *style;			/* css_select_results, after selection */
};

typedef struct dom_stats {
	uint32_t elements, texts, attrs;
	uint32_t text_bytes;
	uint32_t depth;
} dom_stats;

/* Parses len bytes of HTML (UTF-8 unless it says otherwise) in chunks of
 * chunk bytes. Returns the document node, or NULL. */
node *dom_parse(const uint8_t *data, size_t len, size_t chunk);
void dom_free(node *doc);
void dom_count(const node *doc, dom_stats *st);

/* The value of an element's attribute, or NULL */
lwc_string *dom_attr(const node *n, lwc_string *name);

/* Interned names nsbench and style.c look for */
extern lwc_string *s_class, *s_id, *s_style, *s_href, *s_lang, *s_a, *s_html,
	*s_link, *s_rel, *s_stylesheet;
void dom_names(void);

#endif
