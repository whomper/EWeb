/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * doc.c - Hubbub's tree handler building EWeb's document (after
 * tests/bench/dom.c: adjacent text joins up; a node goes when its last
 * reference does and it has no parent), then the element links, indexes
 * and flags the CSS engine reads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hubbub/hubbub.h>
#include <hubbub/parser.h>
#include <hubbub/tree.h>
#include <parserutils/charset/mibenum.h>

#include "doc.h"
#include "form.h"

static const char *const tag_names[T_NTAGS] = {
	"", "html", "head", "body", "title", "base", "link", "meta", "style", "script",
	"noscript", "a", "img", "br", "hr", "li", "ul", "ol", "table", "tr", "td", "th",
	"input", "button", "select", "option", "textarea", "pre", "iframe", "svg", "canvas",
	"video", "audio", "object", "embed", "template", "label", "form", "wbr",
	"fieldset", "legend", "optgroup"
};

typedef struct {
	doc *d;
	const ec_atom *tags[T_NTAGS];
	const ec_atom *a_class, *a_id;
	hubbub_parser *parser;
	const char *new_charset;
} BUILD;

static BUILD *bd;	/* the parse in progress (one at a time) */

static dnode *new_node(int type)
{
	dnode *n = calloc(1, sizeof *n);
	if (n != NULL) {
		n->type = (uint8_t) type;
		n->refs = 1;
		n->y = -1;
	}
	return n;
}

static int text_add(dnode *n, const uint8_t *s, size_t len)
{
	if (n->len + len + 1 > n->cap) {
		uint32_t cap = n->cap ? n->cap : 32;
		char *t;
		while (cap < n->len + len + 1)
			cap *= 2;
		t = realloc(n->text, cap);
		if (t == NULL)
			return 0;
		n->text = t;
		n->cap = cap;
	}
	memcpy(n->text + n->len, s, len);
	n->len += (uint32_t) len;
	n->text[n->len] = 0;
	return 1;
}

static void free_node(dnode *n)
{
	dnode *c, *next;
	uint16_t i;

	for (c = n->first; c != NULL; c = next) {
		next = c->next;
		free_node(c);
	}
	for (i = 0; i < n->nattr; i++)
		free((char *) n->attrs[i].val);
	free(n->attrs);
	free((char *) n->e.hint);
	free(n->cls);
	free(n->text);
	free(n);
}

static const ec_atom *intern(const uint8_t *s, size_t n)
{
	return ec_intern(bd->d->css, (const char *) s, n);
}

/* the class attribute, split at white space */
static void set_classes(dnode *n, const char *s, size_t len)
{
	const char *e = s + len;
	free(n->cls);
	n->cls = NULL;
	n->ncls = 0;
	while (s < e) {
		const char *w;
		while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		w = s;
		while (s < e && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		if (s > w) {
			const ec_atom **c = realloc((void *) n->cls, (n->ncls + 1) * sizeof *n->cls);
			if (c == NULL)
				return;
			n->cls = c;
			c[n->ncls++] = ec_intern(bd->d->css, w, s - w);
		}
	}
}

static void add_attr(dnode *n, const uint8_t *name, size_t nlen, const uint8_t *val, size_t vlen)
{
	ec_attr *at;
	char *v;
	size_t i;

	at = realloc(n->attrs, (n->nattr + 1) * sizeof *n->attrs);
	if (at == NULL)
		return;
	n->attrs = at;
	v = malloc(vlen + 1);
	if (v == NULL)
		return;
	memcpy(v, val, vlen);
	v[vlen] = 0;
	/* attribute names are case-insensitive in HTML; Hubbub gives HTML's
	   in lower case, but not foreign content's */
	{
		char lower[64];
		const ec_atom *a;
		if (nlen < sizeof lower) {
			for (i = 0; i < nlen; i++)
				lower[i] = (char) (name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
			a = ec_intern(bd->d->css, lower, nlen);
		} else {
			a = intern(name, nlen);
		}
		at[n->nattr].name = a;
		at[n->nattr].val = v;
		at[n->nattr].len = (uint32_t) vlen;
		n->nattr++;
		if (a == bd->a_class)
			set_classes(n, v, vlen);
		else if (a == bd->a_id && n->e.id == NULL && vlen > 0)
			n->e.id = intern(val, vlen);
	}
}

const char *dn_attr(const dnode *n, const char *name)
{
	uint16_t i;
	size_t len = strlen(name);
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		if (al == len && memcmp(an, name, len) == 0)
			return n->attrs[i].val;
	}
	return NULL;
}

/* ---------------------------------------------------------------- the tree handler */

static hubbub_error t_create_comment(void *ctx, const hubbub_string *data, void **result)
{
	dnode *n = new_node(DN_OTHER);
	(void) ctx;
	(void) data;
	if (n == NULL)
		return HUBBUB_NOMEM;
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_doctype(void *ctx, const hubbub_doctype *dt, void **result)
{
	dnode *n = new_node(DN_OTHER);
	(void) ctx;
	(void) dt;
	if (n == NULL)
		return HUBBUB_NOMEM;
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_element(void *ctx, const hubbub_tag *tag, void **result)
{
	dnode *n = new_node(DN_ELEMENT);
	uint32_t i;
	int t;

	(void) ctx;
	if (n == NULL)
		return HUBBUB_NOMEM;
	n->ns = (uint8_t) tag->ns;
	n->e.name = intern(tag->name.ptr, tag->name.len);
	for (t = 1; t < T_NTAGS; t++)
		if (bd->tags[t] == n->e.name)
			break;
	n->tag = (uint16_t) (t < T_NTAGS ? t : T_OTHER);
	if (tag->ns == HUBBUB_NS_SVG && n->tag != T_SVG)
		n->tag = T_OTHER;
	for (i = 0; i < tag->n_attributes; i++)
		add_attr(n, tag->attributes[i].name.ptr, tag->attributes[i].name.len,
			 tag->attributes[i].value.ptr, tag->attributes[i].value.len);
	bd->d->elements++;
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_create_text(void *ctx, const hubbub_string *data, void **result)
{
	dnode *n = new_node(DN_TEXT);
	(void) ctx;
	if (n == NULL || !text_add(n, data->ptr, data->len)) {
		free(n);
		return HUBBUB_NOMEM;
	}
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_ref_node(void *ctx, void *v)
{
	(void) ctx;
	((dnode *) v)->refs++;
	return HUBBUB_OK;
}

static hubbub_error t_unref_node(void *ctx, void *v)
{
	dnode *n = v;
	(void) ctx;
	if (n->refs > 0)
		n->refs--;
	if (n->refs == 0 && n->parent == NULL && n->type != DN_DOCUMENT)
		free_node(n);
	return HUBBUB_OK;
}

static void link_last(dnode *parent, dnode *child)
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
	dnode *parent = p, *child = c;

	(void) ctx;
	if (child->type == DN_TEXT && parent->last != NULL && parent->last->type == DN_TEXT) {
		text_add(parent->last, (const uint8_t *) child->text, child->len);
		*result = parent->last;
	} else {
		link_last(parent, child);
		*result = child;
	}
	((dnode *) *result)->refs++;
	return HUBBUB_OK;
}

static hubbub_error t_insert_before(void *ctx, void *p, void *c, void *r, void **result)
{
	dnode *parent = p, *child = c, *ref = r;

	(void) ctx;
	if (child->type == DN_TEXT && ref->prev != NULL && ref->prev->type == DN_TEXT) {
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
	((dnode *) *result)->refs++;
	return HUBBUB_OK;
}

static void unlink_node(dnode *child)
{
	dnode *parent = child->parent;

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
	dnode *child = c;
	(void) ctx;
	(void) p;
	unlink_node(child);
	child->refs++;
	*result = child;
	return HUBBUB_OK;
}

static dnode *clone(const dnode *old, int deep)
{
	dnode *n = new_node(old->type);
	const dnode *c;
	uint16_t i;

	if (n == NULL)
		return NULL;
	n->ns = old->ns;
	n->tag = old->tag;
	n->e.name = old->e.name;
	for (i = 0; i < old->nattr; i++) {
		size_t nl;
		const char *an = ec_atom_text(old->attrs[i].name, &nl);
		add_attr(n, (const uint8_t *) an, nl, (const uint8_t *) old->attrs[i].val, old->attrs[i].len);
	}
	if (old->len > 0)
		text_add(n, (const uint8_t *) old->text, old->len);
	if (old->type == DN_ELEMENT)
		bd->d->elements++;
	if (deep) {
		for (c = old->first; c != NULL; c = c->next) {
			dnode *k = clone(c, 1);
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
	dnode *n = clone(v, deep);
	(void) ctx;
	if (n == NULL)
		return HUBBUB_NOMEM;
	*result = n;
	return HUBBUB_OK;
}

static hubbub_error t_reparent_children(void *ctx, void *v, void *np)
{
	dnode *old = v, *parent = np, *c, *next;
	(void) ctx;
	for (c = old->first; c != NULL; c = next) {
		next = c->next;
		unlink_node(c);
		link_last(parent, c);
	}
	return HUBBUB_OK;
}

static hubbub_error t_get_parent(void *ctx, void *v, bool element_only, void **result)
{
	dnode *p = ((dnode *) v)->parent;
	(void) ctx;
	if (p != NULL && element_only && p->type != DN_ELEMENT)
		p = NULL;
	if (p != NULL)
		p->refs++;
	*result = p;
	return HUBBUB_OK;
}

static hubbub_error t_has_children(void *ctx, void *v, bool *result)
{
	(void) ctx;
	*result = ((dnode *) v)->first != NULL;
	return HUBBUB_OK;
}

/* a control and the form the parser had open (a form left open across a
   table's cells is not its ancestor) */
static hubbub_error t_form_associate(void *ctx, void *form, void *v)
{
	(void) ctx;
	((dnode *) v)->owner = form;
	return HUBBUB_OK;
}

static hubbub_error t_add_attributes(void *ctx, void *v, const hubbub_attribute *a, uint32_t n)
{
	dnode *e = v;
	uint32_t i;
	(void) ctx;
	for (i = 0; i < n; i++) {
		char name[64];
		if (a[i].name.len >= sizeof name)
			continue;
		memcpy(name, a[i].name.ptr, a[i].name.len);
		name[a[i].name.len] = 0;
		if (dn_attr(e, name) == NULL)
			add_attr(e, a[i].name.ptr, a[i].name.len, a[i].value.ptr, a[i].value.len);
	}
	return HUBBUB_OK;
}

static hubbub_error t_set_quirks_mode(void *ctx, hubbub_quirks_mode mode)
{
	(void) ctx;
	(void) mode;
	return HUBBUB_OK;
}

static hubbub_error t_complete_script(void *ctx, void *script)
{
	(void) ctx;
	(void) script;
	return HUBBUB_OK;
}

/* a <meta charset> found while parsing: start again in that charset unless
   the one in use is certain or the same */
static hubbub_error t_encoding_change(void *ctx, const char *name)
{
	hubbub_charset_source source;
	const char *cur = hubbub_parser_read_charset(bd->parser, &source);

	(void) ctx;
	if (cur == NULL || source == HUBBUB_CHARSET_CONFIDENT)
		return HUBBUB_OK;
	if (parserutils_charset_mibenum_from_name(name, strlen(name)) ==
			parserutils_charset_mibenum_from_name(cur, strlen(cur)))
		return HUBBUB_OK;
	bd->new_charset = name;
	return HUBBUB_ENCODINGCHANGE;
}

static hubbub_tree_handler handler = {
	t_create_comment, t_create_doctype, t_create_element, t_create_text,
	t_ref_node, t_unref_node, t_append_child, t_insert_before,
	t_remove_child, t_clone_node, t_reparent_children, t_get_parent,
	t_has_children, t_form_associate, t_add_attributes, t_set_quirks_mode,
	t_encoding_change, t_complete_script, NULL
};

/* ---------------------------------------------------------------- after parsing */

static void collapse_into(char *out, size_t size, const char *s, size_t n)
{
	size_t o = 0, i;
	int space = 0;
	for (i = 0; i < n && o + 1 < size; i++) {
		unsigned char c = (unsigned char) s[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
			space = o > 0;
			continue;
		}
		if (space && o + 2 < size)
			out[o++] = ' ';
		space = 0;
		out[o++] = (char) c;
	}
	out[o] = 0;
}

/* the element links, indexes and flags the CSS engine reads; the head's
   title and base */
/* ---------------------------------------------------------------- presentational hints */
/* HTML's attributes that stand for CSS (the HTML Standard's rendering
   section): written out as declarations for the CSS engine (ec_node.hint) */

typedef struct {
	char s[400];
	size_t n;
} HINT;

static void put(HINT *h, const char *prop, const char *val)
{
	size_t a = strlen(prop), b = strlen(val);
	if (h->n + a + b + 2 >= sizeof h->s)
		return;
	memcpy(h->s + h->n, prop, a);
	h->s[h->n + a] = ':';
	memcpy(h->s + h->n + a + 1, val, b);
	h->n += a + b + 1;
	h->s[h->n++] = ';';
	h->s[h->n] = 0;
}

/* a dimension: "120" is 120px, "50%" half; NULL unless a number above zero */
static const char *dimension(const char *v, char *out)
{
	unsigned long k = 0;
	int digits = 0;
	if (v == NULL)
		return NULL;
	while (*v == ' ' || *v == '\t' || *v == '\n')
		v++;
	while (*v >= '0' && *v <= '9' && k < 100000) {
		k = k * 10 + (unsigned long) (*v++ - '0');
		digits++;
	}
	while (*v >= '0' && *v <= '9')
		v++;
	if (*v == '.')
		for (v++; *v >= '0' && *v <= '9'; v++)
			;
	if (!digits || k == 0)
		return NULL;
	sprintf(out, "%lu%s", k, *v == '%' ? "%" : "px");
	return out;
}

/* a colour: #rgb or #rrggbb, with or without the #, or a name */
static const char *colour(const char *v, char *out)
{
	size_t i, n;
	int hex = 1, alpha = 1;
	if (v == NULL)
		return NULL;
	while (*v == ' ')
		v++;
	if (*v == '#')
		v++;
	n = strlen(v);
	while (n > 0 && v[n - 1] == ' ')
		n--;
	if (n == 0 || n > 20)
		return NULL;
	for (i = 0; i < n; i++) {
		char c = v[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			hex = 0;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			alpha = 0;
	}
	if (hex && (n == 3 || n == 6)) {
		out[0] = '#';
		memcpy(out + 1, v, n);
		out[n + 1] = 0;
		return out;
	}
	if (alpha) {
		memcpy(out, v, n);
		out[n] = 0;
		return out;
	}
	return NULL;
}

static int is(const char *v, const char *w)
{
	for (; *v && *w; v++, w++)
		if ((*v | 0x20) != *w)
			return 0;
	return *v == 0 && *w == 0;
}

static const char *list_type(const char *v, int ol)
{
	if (v == NULL)
		return NULL;
	if (ol || (v[0] && !v[1])) {
		switch (v[0]) {
		case '1': return "decimal";
		case 'a': return "lower-alpha";
		case 'A': return "upper-alpha";
		case 'i': return "lower-roman";
		case 'I': return "upper-roman";
		}
	}
	if (is(v, "disc") || is(v, "circle") || is(v, "square"))
		return is(v, "disc") ? "disc" : is(v, "circle") ? "circle" : "square";
	return NULL;
}

static void hints(dnode *n)
{
	HINT h;
	char buf[32], name[12];
	const char *v, *t;
	size_t nl;
	int cell = n->tag == T_TD || n->tag == T_TH;

	if (n->nattr == 0 && !cell)
		return;
	t = ec_atom_text(n->e.name, &nl);
	if (nl >= sizeof name)
		return;
	memcpy(name, t, nl);
	name[nl] = 0;
	h.n = 0;
	h.s[0] = 0;

	if ((v = dn_attr(n, "align")) != NULL) {
		if ((n->tag == T_TABLE || n->tag == T_IMG) && (is(v, "left") || is(v, "right"))) {
			/* the HTML Standard's rendering section: a float */
			put(&h, "float", is(v, "left") ? "left" : "right");
		} else if (n->tag == T_IMG && (is(v, "middle") || is(v, "top") || is(v, "bottom"))) {
			put(&h, "vertical-align", is(v, "middle") ? "middle" : is(v, "top") ? "top" : "baseline");
		} else if (n->tag == T_TABLE) {
			if (is(v, "center"))
				put(&h, "margin-left", "auto"), put(&h, "margin-right", "auto");
		} else if (n->tag != T_IMG && n->tag != T_HR && n->tag != T_INPUT) {
			if (is(v, "left") || is(v, "right") || is(v, "center") || is(v, "justify"))
				put(&h, "text-align", is(v, "left") ? "left" : is(v, "right") ? "right" :
				    is(v, "center") ? "center" : "justify");
			else if (is(v, "middle"))
				put(&h, "text-align", "center");
		}
	}
	if ((v = dn_attr(n, "valign")) != NULL &&
	    (is(v, "top") || is(v, "middle") || is(v, "bottom") || is(v, "baseline")))
		put(&h, "vertical-align", is(v, "top") ? "top" : is(v, "middle") ? "middle" :
		    is(v, "bottom") ? "bottom" : "baseline");
	if ((v = colour(dn_attr(n, "bgcolor"), buf)) != NULL)
		put(&h, "background-color", v);
	if (n->tag == T_BODY) {
		if ((v = colour(dn_attr(n, "text"), buf)) != NULL)
			put(&h, "color", v);
		if ((v = dimension(dn_attr(n, "marginwidth"), buf)) != NULL ||
		    (v = dimension(dn_attr(n, "leftmargin"), buf)) != NULL)
			put(&h, "margin-left", v), put(&h, "margin-right", v);
		if ((v = dimension(dn_attr(n, "marginheight"), buf)) != NULL ||
		    (v = dimension(dn_attr(n, "topmargin"), buf)) != NULL)
			put(&h, "margin-top", v), put(&h, "margin-bottom", v);
	}
	if (n->tag == T_IMG) {
		if ((v = dimension(dn_attr(n, "hspace"), buf)) != NULL)
			put(&h, "margin-left", v), put(&h, "margin-right", v);
		if ((v = dimension(dn_attr(n, "vspace"), buf)) != NULL)
			put(&h, "margin-top", v), put(&h, "margin-bottom", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_HR || n->tag == T_IMG || !strcmp(name, "col")) {
		if ((v = dimension(dn_attr(n, "width"), buf)) != NULL)
			put(&h, "width", v);
	}
	if (n->tag == T_TABLE || cell || n->tag == T_TR || n->tag == T_IMG) {
		if ((v = dimension(dn_attr(n, "height"), buf)) != NULL)
			put(&h, "height", v);
	}
	if (cell) {
		const dnode *tb;
		if (dn_attr(n, "nowrap") != NULL)
			put(&h, "white-space", "nowrap");
		for (tb = n->parent; tb != NULL && tb->tag != T_TABLE; tb = tb->parent)
			;
		if (tb != NULL) {
			const char *b = dn_attr(tb, "border");
			if (b != NULL && (*b == 0 || dimension(b, buf) != NULL))
				put(&h, "border", "1px inset gray");
			if ((v = dimension(dn_attr(tb, "cellpadding"), buf)) != NULL)
				put(&h, "padding", v);
			else if ((v = dn_attr(tb, "cellpadding")) != NULL && *v == '0')
				put(&h, "padding", "0");
		}
	}
	if (n->tag == T_TABLE) {
		const char *b = dn_attr(n, "border");
		if (b != NULL) {
			char bw[48];
			v = dimension(b, buf);
			if (*b == 0)
				v = "1px";
			if (v != NULL) {
				sprintf(bw, "%s outset gray", v);
				put(&h, "border", bw);
			}
		}
		if ((v = dimension(dn_attr(n, "cellspacing"), buf)) != NULL)
			put(&h, "border-spacing", v);
		else if ((v = dn_attr(n, "cellspacing")) != NULL && *v == '0')
			put(&h, "border-spacing", "0");
	}
	if (n->tag == T_HR && (v = colour(dn_attr(n, "color"), buf)) != NULL) {
		put(&h, "border-color", v);
		put(&h, "background-color", v);
	}
	if (!strcmp(name, "font")) {
		if ((v = colour(dn_attr(n, "color"), buf)) != NULL)
			put(&h, "color", v);
		if ((v = dn_attr(n, "size")) != NULL) {
			static const char *const sizes[] = { "x-small", "small", "medium", "large", "x-large",
							     "xx-large", "48px" };
			int k, rel = 0;
			while (*v == ' ')
				v++;
			if (*v == '+' || *v == '-')
				rel = *v++ == '+' ? 1 : -1;
			if (*v >= '0' && *v <= '9') {
				k = atoi(v);
				k = rel > 0 ? 3 + k : rel < 0 ? 3 - k : k;
				if (k < 1)
					k = 1;
				if (k > 7)
					k = 7;
				put(&h, "font-size", sizes[k - 1]);
			}
		}
		if ((v = dn_attr(n, "face")) != NULL) {
			char f[96];
			size_t i, k = 0;
			for (i = 0; v[i] && k < sizeof f - 1; i++) {
				char c = v[i];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				    c == ' ' || c == ',' || c == '-')
					f[k++] = c;
			}
			f[k] = 0;
			if (k > 0)
				put(&h, "font-family", f);
		}
	}
	if (n->tag == T_OL || n->tag == T_UL || n->tag == T_LI) {
		if ((v = list_type(dn_attr(n, "type"), n->tag == T_OL ||
				   (n->tag == T_LI && n->parent && n->parent->tag == T_OL))) != NULL)
			put(&h, "list-style-type", v);
	}
	if (h.n > 0) {
		char *s = malloc(h.n + 1);
		if (s != NULL) {
			memcpy(s, h.s, h.n + 1);
			n->e.hint = s;
			n->e.nhint = (uint32_t) h.n;
		}
	}
}

static void finish(doc *d, dnode *n, dnode *parent)
{
	dnode *c, *prev = NULL;
	uint16_t idx = 0;
	int empty = 1;

	n->e.parent = parent ? &parent->e : NULL;
	n->e.cls = n->cls;
	n->e.ncls = n->ncls;
	n->e.attr = n->attrs;
	n->e.nattr = n->nattr;
	if (n->tag == T_A && dn_attr(n, "href") != NULL)
		n->e.flags |= EC_NODE_LINK;
	if (n->type == DN_ELEMENT && n->ns == DN_NS_HTML && n->e.hint == NULL)
		hints(n);
	if (n->tag == T_HTML && d->html == NULL)
		d->html = n;
	else if (n->tag == T_BODY && d->body == NULL)
		d->body = n;
	else if (n->tag == T_TITLE && d->title[0] == 0) {
		size_t len = 0;
		char *buf;
		for (c = n->first; c != NULL; c = c->next)
			if (c->type == DN_TEXT)
				len += c->len;
		buf = malloc(len + 1);
		if (buf != NULL) {
			len = 0;
			for (c = n->first; c != NULL; c = c->next)
				if (c->type == DN_TEXT) {
					memcpy(buf + len, c->text, c->len);
					len += c->len;
				}
			collapse_into(d->title, sizeof d->title, buf, len);
			free(buf);
		}
	} else if (n->tag == T_BASE && d->base[0] == 0) {
		const char *h = dn_attr(n, "href");
		if (h != NULL && strlen(h) < sizeof d->base)
			strcpy(d->base, h);
	}
	for (c = n->first; c != NULL; c = c->next) {
		if (c->type == DN_TEXT && c->len > 0)
			empty = 0;
		if (c->type != DN_ELEMENT)
			continue;
		empty = 0;
		finish(d, c, n);
		c->e.index = ++idx;
		c->e.prev = prev ? &prev->e : NULL;
		c->e.next = NULL;
		if (prev != NULL)
			prev->e.next = &c->e;
		else
			n->e.first = &c->e;
		prev = c;
	}
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT)
			c->e.nsib = idx;
	if (empty)
		n->e.flags |= EC_NODE_EMPTY;
}

static dnode *parse_with(doc *d, const char *data, size_t len, const char *charset, int confident,
			 char *changed, size_t changed_size)
{
	hubbub_parser_optparams params;
	hubbub_error err = HUBBUB_OK;
	dnode *root = new_node(DN_DOCUMENT);
	size_t off;
	const char *used;
	hubbub_charset_source src;

	if (root == NULL)
		return NULL;
	if (hubbub_parser_create(charset, confident, &bd->parser) != HUBBUB_OK) {
		free(root);
		return NULL;
	}
	params.tree_handler = &handler;
	hubbub_parser_setopt(bd->parser, HUBBUB_PARSER_TREE_HANDLER, &params);
	params.document_node = root;
	hubbub_parser_setopt(bd->parser, HUBBUB_PARSER_DOCUMENT_NODE, &params);
	d->elements = 0;
	for (off = 0; off < len && err == HUBBUB_OK; off += 4096) {
		size_t n = len - off < 4096 ? len - off : 4096;
		err = hubbub_parser_parse_chunk(bd->parser, (const uint8_t *) data + off, n);
	}
	if (err == HUBBUB_ENCODINGCHANGE && changed != NULL) {
		strncpy(changed, bd->new_charset, changed_size - 1);
		changed[changed_size - 1] = 0;
		hubbub_parser_destroy(bd->parser);
		free_node(root);
		return NULL;
	}
	if (err == HUBBUB_OK)
		hubbub_parser_completed(bd->parser);
	used = hubbub_parser_read_charset(bd->parser, &src);
	if (used != NULL) {
		strncpy(d->charset, used, sizeof d->charset - 1);
		d->charset[sizeof d->charset - 1] = 0;
	}
	hubbub_parser_destroy(bd->parser);
	return root;
}

doc *doc_parse(const char *data, size_t len, const char *charset, const ec_media *media)
{
	BUILD b;
	char changed[40] = "";
	doc *d = calloc(1, sizeof *d);
	dnode *c;
	int t;

	if (d == NULL)
		return NULL;
	d->css = ec_create(media, 0);
	if (d->css == NULL) {
		free(d);
		return NULL;
	}
	memset(&b, 0, sizeof b);
	b.d = d;
	bd = &b;
	for (t = 1; t < T_NTAGS; t++)
		b.tags[t] = ec_intern(d->css, tag_names[t], strlen(tag_names[t]));
	b.a_class = ec_intern(d->css, "class", 5);
	b.a_id = ec_intern(d->css, "id", 2);
	if (charset != NULL && charset[0] == 0)
		charset = NULL;
	d->root = parse_with(d, data, len, charset, charset != NULL, changed, sizeof changed);
	if (d->root == NULL && changed[0] != 0)	/* <meta charset> said otherwise: start again */
		d->root = parse_with(d, data, len, changed, 1, NULL, 0);
	bd = NULL;
	if (d->root == NULL) {
		ec_destroy(d->css);
		free(d);
		return NULL;
	}
	for (c = d->root->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT) {
			finish(d, c, NULL);
			c->e.index = c->e.nsib = 1;
			break;
		}
	form_init(d);		/* out of memory: the controls it made */
	return d;
}

void doc_free(doc *d)
{
	if (d == NULL)
		return;
	form_free(d);
	if (d->root != NULL)
		free_node(d->root);
	if (d->css != NULL)
		ec_destroy(d->css);
	free(d);
}

/* ---------------------------------------------------------------- styles */

static int word_in(const char *list, const char *word)
{
	size_t wl = strlen(word);
	const char *s = list;
	while (*s) {
		const char *w;
		size_t i;
		while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f')
			s++;
		w = s;
		while (*s && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		if ((size_t) (s - w) == wl) {
			for (i = 0; i < wl; i++)
				if ((w[i] | 0x20) != word[i])
					break;
			if (i == wl)
				return 1;
		}
	}
	return 0;
}

/* a media attribute EWeb takes: none, all or screen (or a list with one) */
static int media_ok(const char *m)
{
	const char *s;
	if (m == NULL || *m == 0)
		return 1;
	for (s = m; *s; s++) {
		if ((s[0] | 0x20) == 's' && strncmp(s, "screen", 6) == 0)
			return 1;
		if ((s[0] | 0x20) == 'a' && strncmp(s, "all", 3) == 0)
			return 1;
	}
	return 0;
}

static void sheets_in(dnode *n, void (*cb)(void *, int, const char *, size_t), void *ud)
{
	dnode *c;
	if (n->type == DN_ELEMENT) {
		if (n->tag == T_STYLE && n->ns == HUBBUB_NS_HTML && media_ok(dn_attr(n, "media"))) {
			size_t len = 0;
			char *buf;
			for (c = n->first; c != NULL; c = c->next)
				if (c->type == DN_TEXT)
					len += c->len;
			buf = malloc(len + 1);
			if (buf != NULL) {
				len = 0;
				for (c = n->first; c != NULL; c = c->next)
					if (c->type == DN_TEXT) {
						memcpy(buf + len, c->text, c->len);
						len += c->len;
					}
				buf[len] = 0;
				cb(ud, 0, buf, len);
				free(buf);
			}
			return;
		}
		if (n->tag == T_LINK) {
			const char *rel = dn_attr(n, "rel"), *href = dn_attr(n, "href");
			if (rel != NULL && href != NULL && word_in(rel, "stylesheet") && !word_in(rel, "alternate") &&
			    media_ok(dn_attr(n, "media")))
				cb(ud, 1, href, strlen(href));
			return;
		}
		if (n->tag == T_TEMPLATE || n->tag == T_SVG)
			return;
	}
	for (c = n->first; c != NULL; c = c->next)
		sheets_in(c, cb, ud);
}

void doc_sheets(doc *d, void (*cb)(void *ud, int is_link, const char *s, size_t len), void *ud)
{
	if (d->root != NULL)
		sheets_in(d->root, cb, ud);
}

void doc_add_ua(doc *d)
{
	ec_sheet(d->css, doc_ua_css, strlen(doc_ua_css), EC_ORIGIN_UA);
}

void doc_select(doc *d)
{
	dnode *c;
	if (d->root == NULL)
		return;
	for (c = d->root->first; c != NULL; c = c->next)
		if (c->type == DN_ELEMENT) {
			ec_select(d->css, &c->e);
			break;
		}
}
