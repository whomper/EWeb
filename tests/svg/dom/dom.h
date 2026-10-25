/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dom/dom.h - the part of NetSurf's libdom that libsvgtiny
 * (third_party/netsurf/libsvgtiny) asks for, on EWeb's own document
 * (src/doc): a node is a dnode, the document its document node, a string
 * a piece of an attribute's value or a name (dom.c). An SVG file is
 * parsed by EWeb's HTML parser (dom_xml_parser), as a page's inline <svg>
 * is, so the two are drawn alike.
 *
 * The document keeps names in lower case (an SVG attribute's camel case,
 * viewBox, is the HTML Standard's adjustment of it), so names compare
 * case aside, dom_string_isequal too.
 */
#ifndef EW_SVG_DOM_H
#define EW_SVG_DOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { DOM_NO_ERR = 0, DOM_NO_MEM_ERR = 1 } dom_exception;
typedef enum { DOM_ELEMENT_NODE = 1, DOM_TEXT_NODE = 3, DOM_OTHER_NODE = 8 } dom_node_type;

typedef struct dnode dom_node;
typedef struct dnode dom_element;
typedef struct dnode dom_document;
typedef struct dom_nodelist dom_nodelist;

/* a string: its bytes (not NUL-terminated) and a count of its holders */
typedef struct dom_string {
	const char *data;
	size_t len;
	int ref;
	char own[];			/* an interned string's own bytes */
} dom_string;

dom_exception dom_string_create_interned(const uint8_t *data, size_t len, dom_string **s);
dom_string *dom_string_ref(dom_string *s);
void dom_string_unref(dom_string *s);
#define dom_string_data(s) ((s)->data)
#define dom_string_byte_length(s) ((s)->len)
bool dom_string_isequal(const dom_string *a, const dom_string *b);
bool dom_string_caseless_isequal(const dom_string *a, const dom_string *b);

/* libwapcaplet's interned strings, as far as libsvgtiny needs them */
typedef dom_string lwc_string;
typedef enum { lwc_error_ok = 0, lwc_error_oom = 1 } lwc_error;
lwc_error lwc_intern_string(const char *s, size_t len, lwc_string **r);
#define lwc_string_unref(s) dom_string_unref(s)
#define dom_string_caseless_lwc_isequal(a, b) dom_string_caseless_isequal((a), (b))

/* nodes live as long as their document: no counting */
#define dom_node_ref(n) (n)
#define dom_node_unref(n) dom_node_release((dom_node *) (n))
void dom_node_release(dom_node *n);

dom_exception dom_node_get_node_type(dom_node *n, dom_node_type *t);
dom_exception dom_node_get_node_name(dom_node *n, dom_string **name);
dom_exception dom_node_get_first_child(dom_node *n, dom_node **c);
dom_exception dom_node_get_next_sibling(dom_node *n, dom_node **c);
dom_exception dom_node_get_parent_node(dom_node *n, dom_node **c);
dom_exception dom_text_get_whole_text(dom_node *n, dom_string **s);

dom_exception dom_element_get_attribute(dom_element *e, const dom_string *name, dom_string **v);
dom_exception dom_element_get_attribute_ns(dom_element *e, const dom_string *ns, const dom_string *name,
					   dom_string **v);
dom_exception dom_element_get_elements_by_tag_name(dom_element *e, const dom_string *name, dom_nodelist **l);
dom_exception dom_nodelist_get_length(dom_nodelist *l, uint32_t *n);
dom_exception dom_nodelist_item(dom_nodelist *l, uint32_t i, dom_node **n);
void dom_nodelist_unref(dom_nodelist *l);

dom_exception dom_document_get_document_element(dom_document *d, dom_element **e);
dom_exception dom_document_get_element_by_id(dom_document *d, const dom_string *id, dom_element **e);

#endif
