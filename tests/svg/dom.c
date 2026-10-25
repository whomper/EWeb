/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dom.c - libdom as libsvgtiny asks for it (dom/dom.h), on EWeb's own
 * document: its nodes are dnodes, an SVG file parsed by EWeb's HTML parser.
 */
#include <stdlib.h>
#include <string.h>

#include "dom/dom.h"
#include "dom/bindings/xml/xmlparser.h"
#include "doc.h"

/* ------------------------------------------------------------ strings */

static dom_string *str_of(const char *s, size_t len)
{
	dom_string *r = malloc(sizeof *r);
	if (r != NULL) {
		r->data = s;
		r->len = len;
		r->ref = 1;
	}
	return r;
}

dom_exception dom_string_create_interned(const uint8_t *data, size_t len, dom_string **s)
{
	dom_string *r = malloc(sizeof *r + len + 1);
	if (r == NULL) {
		*s = NULL;
		return DOM_NO_MEM_ERR;
	}
	memcpy(r->own, data, len);
	r->own[len] = 0;
	r->data = r->own;
	r->len = len;
	r->ref = 1;
	*s = r;
	return DOM_NO_ERR;
}

lwc_error lwc_intern_string(const char *s, size_t len, lwc_string **r)
{
	return dom_string_create_interned((const uint8_t *) s, len, r) == DOM_NO_ERR ? lwc_error_ok : lwc_error_oom;
}

dom_string *dom_string_ref(dom_string *s)
{
	if (s != NULL)
		s->ref++;
	return s;
}

void dom_string_unref(dom_string *s)
{
	if (s != NULL && --s->ref == 0)
		free(s);
}

static int same_caseless(const char *a, size_t al, const char *b, size_t bl)
{
	size_t i;
	if (al != bl)
		return 0;
	for (i = 0; i < al; i++) {
		unsigned char x = (unsigned char) a[i], y = (unsigned char) b[i];
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
	}
	return 1;
}

bool dom_string_caseless_isequal(const dom_string *a, const dom_string *b)
{
	if (a == NULL || b == NULL)
		return a == b;
	return same_caseless(a->data, a->len, b->data, b->len);
}

bool dom_string_isequal(const dom_string *a, const dom_string *b)
{
	return dom_string_caseless_isequal(a, b);
}

/* ------------------------------------------------------------ nodes */

/* the documents parsed here (dom_xml_parser), freed with their document
   node */
static struct owned {
	struct owned *next;
	doc *d;
} *owned;

void dom_node_release(dom_node *n)
{
	struct owned **o, *k;
	if (n == NULL || n->type != DN_DOCUMENT)
		return;
	for (o = &owned; *o != NULL; o = &(*o)->next)
		if ((*o)->d->root == n) {
			k = *o;
			*o = k->next;
			doc_free(k->d);
			free(k);
			return;
		}
}

dom_exception dom_node_get_node_type(dom_node *n, dom_node_type *t)
{
	*t = n->type == DN_ELEMENT ? DOM_ELEMENT_NODE : n->type == DN_TEXT ? DOM_TEXT_NODE : DOM_OTHER_NODE;
	return DOM_NO_ERR;
}

dom_exception dom_node_get_node_name(dom_node *n, dom_string **name)
{
	size_t len;
	const char *s;
	if (n == NULL || n->type != DN_ELEMENT) {
		*name = NULL;
		return DOM_NO_MEM_ERR;
	}
	s = ec_atom_text(n->e.name, &len);
	*name = str_of(s, len);
	return *name != NULL ? DOM_NO_ERR : DOM_NO_MEM_ERR;
}

dom_exception dom_node_get_first_child(dom_node *n, dom_node **c)
{
	*c = n->first;
	return DOM_NO_ERR;
}

dom_exception dom_node_get_next_sibling(dom_node *n, dom_node **c)
{
	*c = n->next;
	return DOM_NO_ERR;
}

dom_exception dom_node_get_parent_node(dom_node *n, dom_node **c)
{
	*c = n->parent;
	return DOM_NO_ERR;
}

dom_exception dom_text_get_whole_text(dom_node *n, dom_string **s)
{
	*s = n->type == DN_TEXT ? str_of(n->text, n->len) : NULL;
	return DOM_NO_ERR;
}

/* an attribute by its name (or, prefixed: xlink:href by href) */
static const ec_attr *attr_of(const dnode *e, const char *name, size_t nl, int prefixed)
{
	uint16_t i;
	if (e == NULL || e->type != DN_ELEMENT)
		return NULL;
	for (i = 0; i < e->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(e->attrs[i].name, &al);
		if (prefixed) {
			const char *c = memchr(an, ':', al);
			if (c == NULL)
				continue;
			al -= (size_t) (c + 1 - an);
			an = c + 1;
		}
		if (same_caseless(an, al, name, nl))
			return &e->attrs[i];
	}
	return NULL;
}

dom_exception dom_element_get_attribute(dom_element *e, const dom_string *name, dom_string **v)
{
	const ec_attr *a = attr_of(e, name->data, name->len, 0);
	*v = NULL;
	if (a == NULL)
		return DOM_NO_ERR;
	*v = str_of(a->val, a->len);
	return *v != NULL ? DOM_NO_ERR : DOM_NO_MEM_ERR;
}

dom_exception dom_element_get_attribute_ns(dom_element *e, const dom_string *ns, const dom_string *name,
					   dom_string **v)
{
	const ec_attr *a = attr_of(e, name->data, name->len, 1);
	(void) ns;
	*v = NULL;
	if (a == NULL)
		return DOM_NO_ERR;
	*v = str_of(a->val, a->len);
	return *v != NULL ? DOM_NO_ERR : DOM_NO_MEM_ERR;
}

/* the next node in tree order under top, or NULL */
static dnode *next_in(const dnode *top, const dnode *n)
{
	if (n->first != NULL)
		return n->first;
	while (n != top && n->next == NULL)
		n = n->parent;
	return n == top ? NULL : n->next;
}

struct dom_nodelist {
	uint32_t n, cap;
	dnode **a;
};

dom_exception dom_element_get_elements_by_tag_name(dom_element *e, const dom_string *name, dom_nodelist **l)
{
	dom_nodelist *r = calloc(1, sizeof *r);
	dnode *k;
	*l = r;
	if (r == NULL)
		return DOM_NO_MEM_ERR;
	for (k = next_in(e, e); k != NULL; k = next_in(e, k)) {
		size_t len;
		const char *s;
		if (k->type != DN_ELEMENT)
			continue;
		s = ec_atom_text(k->e.name, &len);
		if (!same_caseless(s, len, name->data, name->len))
			continue;
		if (r->n == r->cap) {
			dnode **t = realloc(r->a, (r->cap = r->cap ? r->cap * 2 : 8) * sizeof *t);
			if (t == NULL)
				return DOM_NO_MEM_ERR;
			r->a = t;
		}
		r->a[r->n++] = k;
	}
	return DOM_NO_ERR;
}

dom_exception dom_nodelist_get_length(dom_nodelist *l, uint32_t *n)
{
	*n = l->n;
	return DOM_NO_ERR;
}

dom_exception dom_nodelist_item(dom_nodelist *l, uint32_t i, dom_node **n)
{
	*n = i < l->n ? l->a[i] : NULL;
	return DOM_NO_ERR;
}

void dom_nodelist_unref(dom_nodelist *l)
{
	if (l != NULL) {
		free(l->a);
		free(l);
	}
}

dom_exception dom_document_get_document_element(dom_document *d, dom_element **e)
{
	dnode *k;
	*e = NULL;
	/* the first <svg>: the HTML parser puts the file's in a body */
	for (k = d; k != NULL; k = next_in(d, k))
		if (k->type == DN_ELEMENT && k->ns == DN_NS_SVG && k->tag == T_SVG) {
			*e = k;
			break;
		}
	return DOM_NO_ERR;
}

dom_exception dom_document_get_element_by_id(dom_document *d, const dom_string *id, dom_element **e)
{
	dnode *k;
	*e = NULL;
	for (k = d; k != NULL; k = next_in(d, k)) {
		const char *v;
		if (k->type == DN_ELEMENT && (v = dn_attr(k, "id")) != NULL && strlen(v) == id->len &&
		    memcmp(v, id->data, id->len) == 0) {
			*e = k;
			break;
		}
	}
	return DOM_NO_ERR;
}

/* ------------------------------------------------------------ the parser */

struct dom_xml_parser {
	dom_document **out;
	char *buf;
	size_t len, cap;
};

dom_xml_parser *dom_xml_parser_create(const char *enc, const char *int_enc, dom_msg msg, void *mctx,
				      dom_document **document)
{
	dom_xml_parser *p = calloc(1, sizeof *p);
	(void) enc;
	(void) int_enc;
	(void) msg;
	(void) mctx;
	*document = NULL;
	if (p != NULL)
		p->out = document;
	return p;
}

dom_xml_error dom_xml_parser_parse_chunk(dom_xml_parser *p, const uint8_t *data, size_t len)
{
	if (p->len + len > p->cap) {
		size_t cap = p->cap * 2 > p->len + len ? p->cap * 2 : p->len + len;
		char *t = realloc(p->buf, cap);
		if (t == NULL)
			return DOM_XML_NOMEM;
		p->buf = t;
		p->cap = cap;
	}
	memcpy(p->buf + p->len, data, len);
	p->len += len;
	return DOM_XML_OK;
}

dom_xml_error dom_xml_parser_completed(dom_xml_parser *p)
{
	/* (the media only for the parser's CSS context, which keeps its
	   names: an SVG file's own sheets aren't looked at) */
	static const ec_media media = { 1024, 768, 16 };
	struct owned *o = malloc(sizeof *o);
	doc *d;
	if (o == NULL)
		return DOM_XML_NOMEM;
	/* UTF-8, as XML is when the file doesn't say */
	d = doc_parse(p->buf != NULL ? p->buf : "", p->len, "UTF-8", &media);
	if (d == NULL) {
		free(o);
		return DOM_XML_NOMEM;
	}
	o->d = d;
	o->next = owned;
	owned = o;
	*p->out = d->root;
	return DOM_XML_OK;
}

void dom_xml_parser_destroy(dom_xml_parser *p)
{
	if (p != NULL) {
		free(p->buf);
		free(p);
	}
}
