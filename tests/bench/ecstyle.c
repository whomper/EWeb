/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's CSS with EWeb's own engine (src/css): the same sheets as
 * style.c, and the document tree given to the engine as ec_nodes. Making
 * those nodes is counted in the selection's time; in EWeb the document's
 * own nodes will be them. */
#include <stdlib.h>
#include <string.h>

#include <hubbub/types.h>

#include "ecss.h"
#include "ecstyle.h"

unsigned ecs_opts;
void (*ecs_setup)(ec_ctx *c);

/* lwc_string to atom: the same string is the same pointer, so a small
 * table keyed on it saves hashing the bytes again */
#define AMAP 1024
static struct { lwc_string *s; const ec_atom *a; } amap[AMAP];

static const ec_atom *atom_of(ec_ctx *c, lwc_string *s)
{
	unsigned i = ((uintptr_t) s >> 4) & (AMAP - 1);

	if (amap[i].s == s)
		return amap[i].a;
	amap[i].s = s;
	amap[i].a = ec_intern(c, lwc_string_data(s), lwc_string_length(s));
	return amap[i].a;
}

static void add_sheet(ec_ctx *c, const char *data, size_t len, int origin, style_stats *st)
{
	if (ec_sheet(c, data, len, origin) == 0) {
		st->sheets++;
		st->sheet_bytes += len;
	}
}

static void style_elements(ec_ctx *c, node *n, style_stats *st)
{
	node *ch;

	if (n->type == N_ELEMENT && n->ns == HUBBUB_NS_HTML && n->name == s_style) {
		size_t len = 0;
		char *buf;
		for (ch = n->first; ch != NULL; ch = ch->next)
			if (ch->type == N_TEXT)
				len += ch->len;
		buf = malloc(len + 1);
		if (buf != NULL) {
			len = 0;
			for (ch = n->first; ch != NULL; ch = ch->next)
				if (ch->type == N_TEXT) {
					memcpy(buf + len, ch->text, ch->len);
					len += ch->len;
				}
			add_sheet(c, buf, len, EC_ORIGIN_AUTHOR, st);
			free(buf);
		}
		return;
	}
	for (ch = n->first; ch != NULL; ch = ch->next)
		style_elements(c, ch, st);
}

ec_ctx *ecs_sheets(node *doc, const char **data, const size_t *len, int n, style_stats *st)
{
	static const ec_media media = { 640, 480, 16 };	/* the Falcon's screen */
	ec_ctx *c = ec_create(&media, ecs_opts);
	int i;

	if (c == NULL)
		return NULL;
	if (ecs_setup != NULL)
		ecs_setup(c);
	memset(amap, 0, sizeof amap);
	add_sheet(c, nsbench_ua_css, strlen(nsbench_ua_css), EC_ORIGIN_UA, st);
	for (i = 0; i < n; i++)
		add_sheet(c, data[i], len[i], EC_ORIGIN_AUTHOR, st);
	style_elements(c, doc, st);
	return c;
}

/* ------------------------------------------------------------ the tree */

static ec_node *nodes;
static const ec_atom **classes;
static ec_attr *attrs;
static uint32_t nn, ncl, nat;

/* each context's node arrays, freed with it (csscmp keeps several) */
typedef struct alloc {
	ec_ctx *c;
	void *nodes, *classes, *attrs;
	struct alloc *next;
} alloc;
static alloc *allocs;

static void count(const node *n, uint32_t *e, uint32_t *c, uint32_t *a)
{
	const node *ch;

	if (n->type == N_ELEMENT) {
		(*e)++;
		*c += n->nclass;
		*a += n->nattr;
	}
	for (ch = n->first; ch != NULL; ch = ch->next)
		count(ch, e, c, a);
}

static ec_node *build(ec_ctx *c, node *n, ec_node *parent)
{
	ec_node *e = &nodes[nn++], *prev = NULL;
	node *ch;
	uint32_t i;
	uint16_t idx = 0, empty = 1;
	lwc_string *href = NULL;

	memset(e, 0, sizeof *e);
	n->ecn = e;
	e->parent = parent;
	e->name = atom_of(c, n->name);
	e->id = n->id ? atom_of(c, n->id) : NULL;
	e->cls = classes + ncl;
	for (i = 0; i < n->nclass; i++)
		classes[ncl++] = atom_of(c, n->classes[i]);
	e->ncls = n->nclass;
	e->attr = attrs + nat;
	for (i = 0; i < n->nattr; i++) {
		ec_attr *a = &attrs[nat++];
		a->name = atom_of(c, n->attr[i].name);
		a->val = lwc_string_data(n->attr[i].value);
		a->len = lwc_string_length(n->attr[i].value);
		if (n->attr[i].name == s_href)
			href = n->attr[i].value;
	}
	e->nattr = n->nattr;
	if (href != NULL && (n->name == s_a || (lwc_string_length(n->name) == 4 &&
			memcmp(lwc_string_data(n->name), "area", 4) == 0)))
		e->flags |= EC_NODE_LINK;
	for (ch = n->first; ch != NULL; ch = ch->next) {
		if (ch->type == N_TEXT && ch->len > 0)
			empty = 0;
		if (ch->type != N_ELEMENT)
			continue;
		empty = 0;
		{
			ec_node *k = build(c, ch, e);
			k->index = ++idx;
			k->prev = prev;
			if (prev != NULL)
				prev->next = k;
			else
				e->first = k;
			prev = k;
		}
	}
	for (ec_node *k = e->first; k != NULL; k = k->next)
		k->nsib = idx;
	if (empty)
		e->flags |= EC_NODE_EMPTY;
	return e;
}

void ecs_select(ec_ctx *c, node *doc, style_stats *st)
{
	uint32_t e = 0, cl = 0, a = 0;
	node *root;

	count(doc, &e, &cl, &a);
	nodes = malloc((e + 1) * sizeof *nodes);
	classes = malloc((cl + 1) * sizeof *classes);
	attrs = malloc((a + 1) * sizeof *attrs);
	if (nodes == NULL || classes == NULL || attrs == NULL)
		return;
	{
		alloc *a2 = malloc(sizeof *a2);
		if (a2 != NULL) {
			a2->c = c;
			a2->nodes = nodes;
			a2->classes = classes;
			a2->attrs = attrs;
			a2->next = allocs;
			allocs = a2;
		}
	}
	nn = ncl = nat = 0;
	for (root = doc->first; root != NULL && root->type != N_ELEMENT; root = root->next)
		;
	if (root == NULL)
		return;
	{
		ec_node *r = build(c, root, NULL);
		r->index = r->nsib = 1;
		ec_select(c, r);
	}
	st->selected += nn;
}

void ecs_free(ec_ctx *c, node *doc)
{
	alloc **p = &allocs;

	while (*p != NULL) {
		alloc *a = *p;
		if (a->c == c) {
			free(a->nodes);
			free(a->classes);
			free(a->attrs);
			*p = a->next;
			free(a);
		} else {
			p = &a->next;
		}
	}
	nodes = NULL;
	classes = NULL;
	attrs = NULL;
	ec_destroy(c);
}
