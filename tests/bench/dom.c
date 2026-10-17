/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's document tree: Hubbub's tree handler, written after the
 * behaviour of Hubbub's own test/tree2.c (adjacent text joins up; a node
 * goes when its last reference does and it has no parent). Names, class
 * names and ids are interned, as LibCSS wants them. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <hubbub/hubbub.h>
#include <hubbub/parser.h>
#include <hubbub/tree.h>
#include <parserutils/charset/mibenum.h>

#include "dom.h"

lwc_string *s_class, *s_id, *s_style, *s_href, *s_lang, *s_a, *s_html,
	*s_link, *s_rel, *s_stylesheet;

static lwc_string *intern(const char *s, size_t n)
{
	lwc_string *r = NULL;
	lwc_intern_string(s, n, &r);
	return r;
}

void dom_names(void)
{
	if (s_class != NULL)
		return;
	s_class = intern("class", 5);
	s_id = intern("id", 2);
	s_style = intern("style", 5);
	s_href = intern("href", 4);
	s_lang = intern("lang", 4);
	s_a = intern("a", 1);
	s_html = intern("html", 4);
	s_link = intern("link", 4);
	s_rel = intern("rel", 3);
	s_stylesheet = intern("stylesheet", 10);
}

static node *new_node(int type)
{
	node *n = calloc(1, sizeof *n);
	if (n != NULL) {
		n->type = type;
		n->refs = 1;
	}
	return n;
}

static void text_add(node *n, const uint8_t *s, size_t len)
{
	if (n->len + len + 1 > n->cap) {
		uint32_t cap = n->cap ? n->cap : 32;
		while (cap < n->len + len + 1)
			cap *= 2;
		n->text = realloc(n->text, cap);
		n->cap = cap;
	}
	memcpy(n->text + n->len, s, len);
	n->len += len;
	n->text[n->len] = 0;
}

static void free_node(node *n)
{
	node *c, *next;
	uint32_t i;

	for (c = n->first; c != NULL; c = next) {
		next = c->next;
		free_node(c);
	}
	for (i = 0; i < n->nattr; i++) {
		lwc_string_unref(n->attr[i].name);
		lwc_string_unref(n->attr[i].value);
	}
	for (i = 0; i < n->nclass; i++)
		lwc_string_unref(n->classes[i]);
	if (n->name != NULL)
		lwc_string_unref(n->name);
	if (n->id != NULL)
		lwc_string_unref(n->id);
	free(n->attr);
	free(n->classes);
	free(n->text);
	free(n);
}

/* the class attribute, split at white space */
static void set_classes(node *n, lwc_string *v)
{
	const char *s = lwc_string_data(v), *e = s + lwc_string_length(v);
	uint32_t i;

	for (i = 0; i < n->nclass; i++)
		lwc_string_unref(n->classes[i]);
	n->nclass = 0;
	while (s < e) {
		const char *w;
		while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		w = s;
		while (s < e && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		if (s > w) {
			n->classes = realloc(n->classes, (n->nclass + 1) * sizeof *n->classes);
			n->classes[n->nclass++] = intern(w, s - w);
		}
	}
}

static void add_attr(node *n, const hubbub_attribute *a)
{
	lwc_string *name = intern((const char *) a->name.ptr, a->name.len);
	lwc_string *value = intern((const char *) a->value.ptr, a->value.len);
	attr *at;

	at = realloc(n->attr, (n->nattr + 1) * sizeof *n->attr);
	if (at == NULL)
		return;
	n->attr = at;
	at[n->nattr].name = name;
	at[n->nattr].value = value;
	n->nattr++;
	if (name == s_class)
		set_classes(n, value);
	else if (name == s_id && n->id == NULL)
		n->id = lwc_string_ref(value);
}

lwc_string *dom_attr(const node *n, lwc_string *name)
{
	uint32_t i;
	for (i = 0; i < n->nattr; i++)
		if (n->attr[i].name == name)
			return n->attr[i].value;
	return NULL;
}

/* ---------------------------------------------------------------- the tree handler */

static hubbub_error t_create_comment(void *ctx, const hubbub_string *data, void **result)
{
	node *n = new_node(N_COMMENT);
	if (n == NULL)
		return HUBBUB_NOMEM;
	text_add(n, data->ptr, data->len);
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_doctype(void *ctx, const hubbub_doctype *dt, void **result)
{
	node *n = new_node(N_DOCTYPE);
	if (n == NULL)
		return HUBBUB_NOMEM;
	n->name = intern((const char *) dt->name.ptr, dt->name.len);
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_element(void *ctx, const hubbub_tag *tag, void **result)
{
	node *n = new_node(N_ELEMENT);
	uint32_t i;

	if (n == NULL)
		return HUBBUB_NOMEM;
	n->ns = tag->ns;
	n->name = intern((const char *) tag->name.ptr, tag->name.len);
	for (i = 0; i < tag->n_attributes; i++)
		add_attr(n, &tag->attributes[i]);
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_text(void *ctx, const hubbub_string *data, void **result)
{
	node *n = new_node(N_TEXT);
	if (n == NULL)
		return HUBBUB_NOMEM;
	text_add(n, data->ptr, data->len);
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_ref_node(void *ctx, void *v)
{
	((node *) v)->refs++;
	return HUBBUB_OK;
}

static hubbub_error t_unref_node(void *ctx, void *v)
{
	node *n = v;
	if (n->refs > 0)
		n->refs--;
	if (n->refs == 0 && n->parent == NULL && n->type != N_DOCUMENT)
		free_node(n);
	return HUBBUB_OK;
}

static void link_last(node *parent, node *child)
{
	child->parent = parent;
	child->prev = parent->last;
	child->next = NULL;
	if (parent->last != NULL)
		parent->last->next = child;
	else
		parent->first = child;
	parent->last = child;
}

static hubbub_error t_append_child(void *ctx, void *p, void *c, void **result)
{
	node *parent = p, *child = c;

	if (child->type == N_TEXT && parent->last != NULL && parent->last->type == N_TEXT) {
		text_add(parent->last, (const uint8_t *) child->text, child->len);
		*result = parent->last;
	} else {
		link_last(parent, child);
		*result = child;
	}
	((node *) *result)->refs++;
	return HUBBUB_OK;
}

static hubbub_error t_insert_before(void *ctx, void *p, void *c, void *r, void **result)
{
	node *parent = p, *child = c, *ref = r;

	if (child->type == N_TEXT && ref->prev != NULL && ref->prev->type == N_TEXT) {
		text_add(ref->prev, (const uint8_t *) child->text, child->len);
		*result = ref->prev;
	} else {
		child->parent = parent;
		child->prev = ref->prev;
		child->next = ref;
		if (ref->prev != NULL)
			ref->prev->next = child;
		else
			parent->first = child;
		ref->prev = child;
		*result = child;
	}
	((node *) *result)->refs++;
	return HUBBUB_OK;
}

static void unlink_node(node *child)
{
	node *parent = child->parent;

	if (parent == NULL)
		return;
	if (child->prev != NULL)
		child->prev->next = child->next;
	else
		parent->first = child->next;
	if (child->next != NULL)
		child->next->prev = child->prev;
	else
		parent->last = child->prev;
	child->parent = child->prev = child->next = NULL;
}

static hubbub_error t_remove_child(void *ctx, void *p, void *c, void **result)
{
	node *child = c;

	unlink_node(child);
	child->refs++;
	*result = child;
	return HUBBUB_OK;
}

static node *clone(const node *old, bool deep)
{
	node *n = new_node(old->type);
	const node *c;
	uint32_t i;

	if (n == NULL)
		return NULL;
	n->ns = old->ns;
	if (old->name != NULL)
		n->name = lwc_string_ref(old->name);
	for (i = 0; i < old->nattr; i++) {
		hubbub_attribute a;
		a.ns = HUBBUB_NS_NULL;
		a.name.ptr = (const uint8_t *) lwc_string_data(old->attr[i].name);
		a.name.len = lwc_string_length(old->attr[i].name);
		a.value.ptr = (const uint8_t *) lwc_string_data(old->attr[i].value);
		a.value.len = lwc_string_length(old->attr[i].value);
		add_attr(n, &a);
	}
	if (old->len > 0)
		text_add(n, (const uint8_t *) old->text, old->len);
	if (deep) {
		for (c = old->first; c != NULL; c = c->next) {
			node *k = clone(c, true);
			if (k != NULL) {
				k->refs = 0;
				link_last(n, k);
			}
		}
	}
	return n;
}

static hubbub_error t_clone_node(void *ctx, void *v, bool deep, void **result)
{
	node *n = clone(v, deep);
	if (n == NULL)
		return HUBBUB_NOMEM;
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_reparent_children(void *ctx, void *v, void *np)
{
	node *old = v, *parent = np, *c, *next;

	for (c = old->first; c != NULL; c = next) {
		next = c->next;
		unlink_node(c);
		link_last(parent, c);
	}
	return HUBBUB_OK;
}

static hubbub_error t_get_parent(void *ctx, void *v, bool element_only, void **result)
{
	node *p = ((node *) v)->parent;

	if (p != NULL && element_only && p->type != N_ELEMENT)
		p = NULL;
	if (p != NULL)
		p->refs++;
	*result = p;
	return HUBBUB_OK;
}

static hubbub_error t_has_children(void *ctx, void *v, bool *result)
{
	*result = ((node *) v)->first != NULL;
	return HUBBUB_OK;
}

static hubbub_error t_form_associate(void *ctx, void *form, void *v)
{
	return HUBBUB_OK;
}

static hubbub_error t_add_attributes(void *ctx, void *v, const hubbub_attribute *a, uint32_t n)
{
	node *e = v;
	uint32_t i;

	for (i = 0; i < n; i++) {
		lwc_string *name = intern((const char *) a[i].name.ptr, a[i].name.len);
		if (dom_attr(e, name) == NULL)
			add_attr(e, &a[i]);
		lwc_string_unref(name);
	}
	return HUBBUB_OK;
}

static hubbub_error t_set_quirks_mode(void *ctx, hubbub_quirks_mode mode)
{
	return HUBBUB_OK;
}

static hubbub_error t_complete_script(void *ctx, void *script)
{
	return HUBBUB_OK;
}

static hubbub_parser *cur_parser;
static const char *new_charset;

/* a <meta charset> found while parsing: start again in that charset unless
 * the one in use is certain or the same (what libdom's binding tells
 * NetSurf, which then parses the page again) */
static hubbub_error t_encoding_change(void *ctx, const char *name)
{
	hubbub_charset_source source;
	const char *cur = hubbub_parser_read_charset(cur_parser, &source);

	if (cur == NULL || source == HUBBUB_CHARSET_CONFIDENT)
		return HUBBUB_OK;
	if (parserutils_charset_mibenum_from_name(name, strlen(name)) ==
			parserutils_charset_mibenum_from_name(cur, strlen(cur)))
		return HUBBUB_OK;
	new_charset = name;
	return HUBBUB_ENCODINGCHANGE;
}

static hubbub_tree_handler handler = {
	t_create_comment, t_create_doctype, t_create_element, t_create_text,
	t_ref_node, t_unref_node, t_append_child, t_insert_before,
	t_remove_child, t_clone_node, t_reparent_children, t_get_parent,
	t_has_children, t_form_associate, t_add_attributes, t_set_quirks_mode,
	t_encoding_change, t_complete_script, NULL
};

static node *parse_with(const uint8_t *data, size_t len, size_t chunk,
		const char *charset, char *changed, size_t changed_size)
{
	hubbub_parser *parser;
	hubbub_parser_optparams params;
	hubbub_error err = HUBBUB_OK;
	node *doc = new_node(N_DOCUMENT);
	size_t off;

	if (doc == NULL)
		return NULL;
	if (hubbub_parser_create(charset, false, &parser) != HUBBUB_OK) {
		free(doc);
		return NULL;
	}
	cur_parser = parser;
	params.tree_handler = &handler;
	hubbub_parser_setopt(parser, HUBBUB_PARSER_TREE_HANDLER, &params);
	params.document_node = doc;
	hubbub_parser_setopt(parser, HUBBUB_PARSER_DOCUMENT_NODE, &params);

	for (off = 0; off < len && err == HUBBUB_OK; off += chunk) {
		size_t n = len - off < chunk ? len - off : chunk;
		err = hubbub_parser_parse_chunk(parser, data + off, n);
	}
	if (err == HUBBUB_ENCODINGCHANGE && changed != NULL) {
		strncpy(changed, new_charset, changed_size - 1);
		changed[changed_size - 1] = 0;
		hubbub_parser_destroy(parser);
		free_node(doc);
		return NULL;
	}
	if (err == HUBBUB_OK)
		hubbub_parser_completed(parser);
	hubbub_parser_destroy(parser);
	return doc;
}

node *dom_parse(const uint8_t *data, size_t len, size_t chunk)
{
	char changed[40] = "";
	node *doc;

	dom_names();
	doc = parse_with(data, len, chunk, NULL, changed, sizeof changed);
	if (doc == NULL && changed[0] != 0)	/* <meta charset> said otherwise: start again */
		doc = parse_with(data, len, chunk, changed, NULL, 0);
	return doc;
}

void dom_free(node *doc)
{
	if (doc != NULL)
		free_node(doc);
}

static void count(const node *n, dom_stats *st, uint32_t depth)
{
	const node *c;

	if (depth > st->depth)
		st->depth = depth;
	if (n->type == N_ELEMENT) {
		st->elements++;
		st->attrs += n->nattr;
	} else if (n->type == N_TEXT) {
		st->texts++;
		st->text_bytes += n->len;
	}
	for (c = n->first; c != NULL; c = c->next)
		count(c, st, depth + 1);
}

void dom_count(const node *doc, dom_stats *st)
{
	memset(st, 0, sizeof *st);
	count(doc, st, 0);
}
