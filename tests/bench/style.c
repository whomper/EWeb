/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's CSS: the sheets (a small UA sheet of our own, the author files
 * and the page's <style> elements), LibCSS's selection handler on dom.h's
 * tree, and selection for every element, composed with its parent's style
 * as NetSurf's nscss_get_style() does. The handler's shape follows LibCSS's
 * examples/example1.c (MIT licence). */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/libcss.h>
#include <hubbub/types.h>

#include "style.h"

/* The parts of the HTML rendering rules a text browser needs, written for
 * nsbench: enough that the UA sheet costs about what a real one does. */
static const char ua_css[] =
	"html, address, blockquote, body, center, dialog, div, figure, figcaption,"
	" footer, form, header, hr, legend, listing, main, p, plaintext, pre,"
	" search, xmp, article, aside, h1, h2, h3, h4, h5, h6, hgroup, nav,"
	" section, dir, dd, dl, dt, menu, ol, ul, details, summary,"
	" fieldset, optgroup { display: block }\n"
	"area, base, basefont, datalist, head, link, meta, noembed, noframes,"
	" param, rp, script, style, template, title, [hidden] { display: none }\n"
	"li { display: list-item }\n"
	"table { display: table; border-spacing: 2px; border-collapse: separate }\n"
	"caption { display: table-caption; text-align: center }\n"
	"colgroup { display: table-column-group } col { display: table-column }\n"
	"thead { display: table-header-group; vertical-align: middle }\n"
	"tbody { display: table-row-group; vertical-align: middle }\n"
	"tfoot { display: table-footer-group; vertical-align: middle }\n"
	"tr { display: table-row; vertical-align: inherit }\n"
	"td, th { display: table-cell; vertical-align: inherit; padding: 1px }\n"
	"th { font-weight: bold }\n"
	"input, select, button, textarea { display: inline-block }\n"
	"body { margin: 8px; line-height: 1.33 }\n"
	"p, blockquote, figure, listing, plaintext, pre, xmp, dl { margin-top: 1em; margin-bottom: 1em }\n"
	"blockquote, figure { margin-left: 40px; margin-right: 40px }\n"
	"address, cite, dfn, em, i, var { font-style: italic }\n"
	"b, strong { font-weight: bold }\n"
	"code, kbd, samp, tt, pre, listing, plaintext, xmp { font-family: monospace }\n"
	"pre, listing, plaintext, xmp { white-space: pre }\n"
	"h1 { font-size: 2em; margin: .67em 0 } h2 { font-size: 1.5em; margin: .83em 0 }\n"
	"h3 { font-size: 1.17em; margin: 1em 0 } h4 { margin: 1.33em 0 }\n"
	"h5 { font-size: .83em; margin: 1.67em 0 } h6 { font-size: .67em; margin: 2.33em 0 }\n"
	"h1, h2, h3, h4, h5, h6 { font-weight: bold }\n"
	"ul, ol, menu, dir { padding-left: 40px; margin: 1em 0 }\n"
	"ul { list-style-type: disc } ol { list-style-type: decimal }\n"
	"ul ul, ol ul { list-style-type: circle; margin: 0 }\n"
	"ol ol, ul ol { margin: 0 }\n"
	"dd { margin-left: 40px }\n"
	"hr { border: 1px inset; margin: .5em auto; color: gray }\n"
	"a:link { color: #0000ee; text-decoration: underline }\n"
	"a:visited { color: #551a8b }\n"
	"u, ins { text-decoration: underline } s, strike, del { text-decoration: line-through }\n"
	"sub { vertical-align: sub; font-size: smaller } sup { vertical-align: super; font-size: smaller }\n"
	"small { font-size: smaller } big { font-size: larger }\n"
	"center { text-align: center }\n"
	"img { display: inline-block }\n"
	"br:before { content: \"\\A\"; white-space: pre-line }\n";

static css_select_handler handler;

/* ---------------------------------------------------------------- sheets */

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
	*abs = lwc_string_ref(rel);
	return CSS_OK;
}

static css_stylesheet *sheet_parse(const char *data, size_t len, bool inline_style)
{
	css_stylesheet_params p;
	css_stylesheet *sheet;
	css_error err;

	memset(&p, 0, sizeof p);
	p.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	p.level = CSS_LEVEL_3;
	p.charset = "UTF-8";
	p.url = "file:///nsbench";
	p.allow_quirks = false;
	p.inline_style = inline_style;
	p.resolve = resolve_url;
	if (css_stylesheet_create(&p, &sheet) != CSS_OK)
		return NULL;
	err = css_stylesheet_append_data(sheet, (const uint8_t *) data, len);
	if (err != CSS_OK && err != CSS_NEEDDATA) {
		css_stylesheet_destroy(sheet);
		return NULL;
	}
	err = css_stylesheet_data_done(sheet);
	if (err != CSS_OK && err != CSS_IMPORTS_PENDING) {
		css_stylesheet_destroy(sheet);
		return NULL;
	}
	return sheet;
}

#define MAX_SHEETS 64
static css_stylesheet *sheets[MAX_SHEETS];
static int nsheets;

static void add_sheet(css_select_ctx *ctx, const char *data, size_t len, css_origin origin,
		style_stats *st)
{
	css_stylesheet *s;

	if (nsheets == MAX_SHEETS)
		return;
	s = sheet_parse(data, len, false);
	if (s == NULL)
		return;
	sheets[nsheets++] = s;
	css_select_ctx_append_sheet(ctx, s, origin, NULL);
	st->sheets++;
	st->sheet_bytes += len;
}

/* the text of every <style> element, in document order */
static void style_elements(css_select_ctx *ctx, node *n, style_stats *st)
{
	node *c;

	if (n->type == N_ELEMENT && n->ns == HUBBUB_NS_HTML && n->name == s_style) {
		size_t len = 0;
		char *buf;
		for (c = n->first; c != NULL; c = c->next)
			if (c->type == N_TEXT)
				len += c->len;
		buf = malloc(len + 1);
		if (buf != NULL) {
			len = 0;
			for (c = n->first; c != NULL; c = c->next)
				if (c->type == N_TEXT) {
					memcpy(buf + len, c->text, c->len);
					len += c->len;
				}
			add_sheet(ctx, buf, len, CSS_ORIGIN_AUTHOR, st);
			free(buf);
		}
		return;
	}
	for (c = n->first; c != NULL; c = c->next)
		style_elements(ctx, c, st);
}

css_select_ctx *style_sheets(node *doc, const char **data, const size_t *len, int n,
		style_stats *st)
{
	css_select_ctx *ctx;
	int i;

	if (css_select_ctx_create(&ctx) != CSS_OK)
		return NULL;
	add_sheet(ctx, ua_css, sizeof ua_css - 1, CSS_ORIGIN_UA, st);
	for (i = 0; i < n; i++)
		add_sheet(ctx, data[i], len[i], CSS_ORIGIN_AUTHOR, st);
	style_elements(ctx, doc, st);
	return ctx;
}

/* ---------------------------------------------------------------- selection */

static const css_unit_ctx unit_ctx = {
	.viewport_width    = 640 * (1 << CSS_RADIX_POINT),	/* the Falcon's screen */
	.viewport_height   = 480 * (1 << CSS_RADIX_POINT),
	.font_size_default =  16 * (1 << CSS_RADIX_POINT),
	.font_size_minimum =   6 * (1 << CSS_RADIX_POINT),
	.device_dpi        =  96 * (1 << CSS_RADIX_POINT),
};

static void select_tree(css_select_ctx *ctx, const css_media *media, node *n,
		const css_computed_style *parent_style, style_stats *st)
{
	node *c;
	const css_computed_style *mine = parent_style;

	if (n->type == N_ELEMENT) {
		css_stylesheet *inline_sheet = NULL;
		css_select_results *res = NULL;
		lwc_string *sa = dom_attr(n, s_style);

		if (sa != NULL) {
			inline_sheet = sheet_parse(lwc_string_data(sa), lwc_string_length(sa), true);
			st->inline_styles++;
		}
		if (css_select_style(ctx, n, &unit_ctx, media, inline_sheet, &handler, NULL,
				&res) != CSS_OK || res == NULL) {
			st->failed++;
		} else {
			int pe;
			css_computed_style *composed;

			if (parent_style != NULL && css_computed_style_compose(parent_style,
					res->styles[CSS_PSEUDO_ELEMENT_NONE], &unit_ctx,
					&composed) == CSS_OK) {
				css_computed_style_destroy(res->styles[CSS_PSEUDO_ELEMENT_NONE]);
				res->styles[CSS_PSEUDO_ELEMENT_NONE] = composed;
			}
			for (pe = CSS_PSEUDO_ELEMENT_NONE + 1; pe < CSS_PSEUDO_ELEMENT_COUNT; pe++) {
				if (pe == CSS_PSEUDO_ELEMENT_FIRST_LETTER ||
						pe == CSS_PSEUDO_ELEMENT_FIRST_LINE ||
						res->styles[pe] == NULL)
					continue;
				if (css_computed_style_compose(res->styles[CSS_PSEUDO_ELEMENT_NONE],
						res->styles[pe], &unit_ctx, &composed) == CSS_OK) {
					css_computed_style_destroy(res->styles[pe]);
					res->styles[pe] = composed;
				}
			}
			n->style = res;
			mine = res->styles[CSS_PSEUDO_ELEMENT_NONE];
			st->selected++;
		}
		if (inline_sheet != NULL)
			css_stylesheet_destroy(inline_sheet);
	}
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == N_ELEMENT || c->first != NULL)
			select_tree(ctx, media, c, mine, st);
}

void style_select(css_select_ctx *ctx, node *doc, style_stats *st)
{
	css_media media;

	memset(&media, 0, sizeof media);
	media.type = CSS_MEDIA_SCREEN;
	media.width = 640 * (1 << CSS_RADIX_POINT);
	media.height = 480 * (1 << CSS_RADIX_POINT);
	select_tree(ctx, &media, doc, NULL, st);
}

static void free_styles(node *n)
{
	node *c;

	if (n->style != NULL)
		css_select_results_destroy(n->style);
	n->style = NULL;
	if (n->css_data != NULL)
		css_libcss_node_data_handler(&handler, CSS_NODE_DELETED, NULL, n, NULL, n->css_data);
	n->css_data = NULL;
	for (c = n->first; c != NULL; c = c->next)
		free_styles(c);
}

void style_free(css_select_ctx *ctx, node *doc)
{
	int i;

	if (doc != NULL)
		free_styles(doc);
	if (ctx != NULL)
		css_select_ctx_destroy(ctx);
	for (i = 0; i < nsheets; i++)
		css_stylesheet_destroy(sheets[i]);
	nsheets = 0;
}

/* ---------------------------------------------------------------- the handler */

static node *parent_element(node *n)
{
	n = n->parent;
	return n != NULL && n->type == N_ELEMENT ? n : NULL;
}

static node *prev_element(node *n)
{
	for (n = n->prev; n != NULL; n = n->prev)
		if (n->type == N_ELEMENT)
			return n;
	return NULL;
}

static bool same_name(const node *n, lwc_string *name)
{
	bool m = false;
	if (n->name == name)
		return true;
	lwc_string_caseless_isequal(n->name, name, &m);
	return m;
}

static lwc_string *find_attr(const node *n, lwc_string *name)
{
	uint32_t i;
	for (i = 0; i < n->nattr; i++) {
		bool m = false;
		if (n->attr[i].name == name)
			return n->attr[i].value;
		lwc_string_caseless_isequal(n->attr[i].name, name, &m);
		if (m)
			return n->attr[i].value;
	}
	return NULL;
}

static css_error h_node_name(void *pw, void *v, css_qname *q)
{
	q->ns = NULL;
	q->name = lwc_string_ref(((node *) v)->name);
	return CSS_OK;
}

static css_error h_node_classes(void *pw, void *v, lwc_string ***classes, uint32_t *n)
{
	node *e = v;
	uint32_t i;

	for (i = 0; i < e->nclass; i++)
		lwc_string_ref(e->classes[i]);	/* LibCSS unrefs each when done */
	*classes = e->nclass ? e->classes : NULL;
	*n = e->nclass;
	return CSS_OK;
}

static css_error h_node_id(void *pw, void *v, lwc_string **id)
{
	node *e = v;
	*id = e->id != NULL ? lwc_string_ref(e->id) : NULL;
	return CSS_OK;
}

static css_error h_named_ancestor_node(void *pw, void *v, const css_qname *q, void **r)
{
	node *n;
	for (n = parent_element(v); n != NULL; n = parent_element(n))
		if (same_name(n, q->name))
			break;
	*r = n;
	return CSS_OK;
}

static css_error h_named_parent_node(void *pw, void *v, const css_qname *q, void **r)
{
	node *n = parent_element(v);
	*r = n != NULL && same_name(n, q->name) ? n : NULL;
	return CSS_OK;
}

static css_error h_named_sibling_node(void *pw, void *v, const css_qname *q, void **r)
{
	node *n = prev_element(v);
	*r = n != NULL && same_name(n, q->name) ? n : NULL;
	return CSS_OK;
}

static css_error h_named_generic_sibling_node(void *pw, void *v, const css_qname *q, void **r)
{
	node *n;
	for (n = prev_element(v); n != NULL; n = prev_element(n))
		if (same_name(n, q->name))
			break;
	*r = n;
	return CSS_OK;
}

static css_error h_parent_node(void *pw, void *v, void **r)
{
	*r = parent_element(v);
	return CSS_OK;
}

static css_error h_sibling_node(void *pw, void *v, void **r)
{
	*r = prev_element(v);
	return CSS_OK;
}

static css_error h_node_has_name(void *pw, void *v, const css_qname *q, bool *m)
{
	*m = same_name(v, q->name);
	return CSS_OK;
}

static css_error h_node_has_class(void *pw, void *v, lwc_string *name, bool *m)
{
	node *e = v;
	uint32_t i;
	*m = false;
	for (i = 0; i < e->nclass; i++)
		if (e->classes[i] == name) {
			*m = true;
			break;
		}
	return CSS_OK;
}

static css_error h_node_has_id(void *pw, void *v, lwc_string *name, bool *m)
{
	*m = ((node *) v)->id == name;
	return CSS_OK;
}

static css_error h_node_has_attribute(void *pw, void *v, const css_qname *q, bool *m)
{
	*m = find_attr(v, q->name) != NULL;
	return CSS_OK;
}

static css_error h_node_has_attribute_equal(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	*m = a != NULL && a == value;
	return CSS_OK;
}

static css_error h_node_has_attribute_dashmatch(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	size_t vl = lwc_string_length(value);
	*m = a != NULL && lwc_string_length(a) >= vl &&
		memcmp(lwc_string_data(a), lwc_string_data(value), vl) == 0 &&
		(lwc_string_length(a) == vl || lwc_string_data(a)[vl] == '-');
	return CSS_OK;
}

static css_error h_node_has_attribute_includes(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	size_t vl = lwc_string_length(value);
	const char *s, *e;

	*m = false;
	if (a == NULL || vl == 0)
		return CSS_OK;
	s = lwc_string_data(a);
	e = s + lwc_string_length(a);
	while (s < e) {
		const char *w = s;
		while (s < e && *s != ' ')
			s++;
		if ((size_t) (s - w) == vl && memcmp(w, lwc_string_data(value), vl) == 0) {
			*m = true;
			break;
		}
		s++;
	}
	return CSS_OK;
}

static css_error h_node_has_attribute_prefix(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	size_t vl = lwc_string_length(value);
	*m = a != NULL && vl > 0 && lwc_string_length(a) >= vl &&
		memcmp(lwc_string_data(a), lwc_string_data(value), vl) == 0;
	return CSS_OK;
}

static css_error h_node_has_attribute_suffix(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	size_t vl = lwc_string_length(value);
	*m = a != NULL && vl > 0 && lwc_string_length(a) >= vl &&
		memcmp(lwc_string_data(a) + lwc_string_length(a) - vl,
			lwc_string_data(value), vl) == 0;
	return CSS_OK;
}

static css_error h_node_has_attribute_substring(void *pw, void *v, const css_qname *q,
		lwc_string *value, bool *m)
{
	lwc_string *a = find_attr(v, q->name);
	size_t vl = lwc_string_length(value), al, i;

	*m = false;
	if (a == NULL || vl == 0)
		return CSS_OK;
	al = lwc_string_length(a);
	for (i = 0; i + vl <= al; i++)
		if (memcmp(lwc_string_data(a) + i, lwc_string_data(value), vl) == 0) {
			*m = true;
			break;
		}
	return CSS_OK;
}

static css_error h_node_is_root(void *pw, void *v, bool *m)
{
	*m = parent_element(v) == NULL;
	return CSS_OK;
}

static css_error h_node_count_siblings(void *pw, void *v, bool same, bool after, int32_t *count)
{
	node *e = v, *n;
	int32_t c = 0;

	for (n = after ? e->next : e->prev; n != NULL; n = after ? n->next : n->prev)
		if (n->type == N_ELEMENT && (!same || same_name(n, e->name)))
			c++;
	*count = c;
	return CSS_OK;
}

static css_error h_node_is_empty(void *pw, void *v, bool *m)
{
	node *n;
	*m = true;
	for (n = ((node *) v)->first; n != NULL; n = n->next)
		if (n->type == N_ELEMENT || (n->type == N_TEXT && n->len > 0)) {
			*m = false;
			break;
		}
	return CSS_OK;
}

static css_error h_node_is_link(void *pw, void *v, bool *m)
{
	node *e = v;
	*m = e->name == s_a && dom_attr(e, s_href) != NULL;
	return CSS_OK;
}

static css_error h_false(void *pw, void *v, bool *m)
{
	*m = false;
	return CSS_OK;
}

static css_error h_node_is_lang(void *pw, void *v, lwc_string *lang, bool *m)
{
	*m = false;
	return CSS_OK;
}

static css_error h_node_presentational_hint(void *pw, void *v, uint32_t *nhints, css_hint **hints)
{
	*nhints = 0;
	*hints = NULL;
	return CSS_OK;
}

static css_error h_ua_default_for_property(void *pw, uint32_t property, css_hint *hint)
{
	if (property == CSS_PROP_COLOR) {
		hint->data.color = 0xff000000;
		hint->status = CSS_COLOR_COLOR;
	} else if (property == CSS_PROP_FONT_FAMILY) {
		hint->data.strings = NULL;
		hint->status = CSS_FONT_FAMILY_SANS_SERIF;
	} else if (property == CSS_PROP_QUOTES) {
		hint->data.strings = NULL;
		hint->status = CSS_QUOTES_NONE;
	} else if (property == CSS_PROP_VOICE_FAMILY) {
		hint->data.strings = NULL;
		hint->status = 0;
	} else {
		return CSS_INVALID;
	}
	return CSS_OK;
}

static css_error h_set_libcss_node_data(void *pw, void *v, void *data)
{
	((node *) v)->css_data = data;
	return CSS_OK;
}

static css_error h_get_libcss_node_data(void *pw, void *v, void **data)
{
	*data = ((node *) v)->css_data;
	return CSS_OK;
}

static css_select_handler handler = {
	CSS_SELECT_HANDLER_VERSION_1,
	h_node_name, h_node_classes, h_node_id,
	h_named_ancestor_node, h_named_parent_node, h_named_sibling_node,
	h_named_generic_sibling_node, h_parent_node, h_sibling_node,
	h_node_has_name, h_node_has_class, h_node_has_id,
	h_node_has_attribute, h_node_has_attribute_equal, h_node_has_attribute_dashmatch,
	h_node_has_attribute_includes, h_node_has_attribute_prefix,
	h_node_has_attribute_suffix, h_node_has_attribute_substring,
	h_node_is_root, h_node_count_siblings, h_node_is_empty,
	h_node_is_link, h_false, h_false, h_false, h_false,	/* visited hover active focus */
	h_false, h_false, h_false, h_false,			/* enabled disabled checked target */
	h_node_is_lang, h_node_presentational_hint, h_ua_default_for_property,
	h_set_libcss_node_data, h_get_libcss_node_data,
};
