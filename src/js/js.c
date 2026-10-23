/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js.c - a page's scripts on QuickJS (js.h): the runtime kept with the
 * document, the budget, running the scripts in order, events, timers,
 * and the lean DOM: EWeb's own nodes (doc.h) behind objects made when a
 * script first asks for them, one object a node. Selectors are EWeb's
 * CSS engine's; HTML is EWeb's parser's (html_fragment).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

#include "doc.h"
#include "form.h"
#include "html.h"
#include "http.h"
#include "js.h"
#include "layout.h"
#include "plat.h"
#include "url.h"

const int js_built = 1;

/* (EWEB.PRG has atari/js_ovl.c's: this is the build machine's) */
int js_available(void)
{
	return 1;
}

const char *js_problem(void)
{
	return "";
}

/* QuickJS's stack: the Atari program has 128 KB */
#ifdef EW_TOS
#define JS_STACK (96 * 1024)
#else
#define JS_STACK (512 * 1024)
#endif

typedef struct listener listener;
struct listener {
	listener *next;
	dnode *n;			/* NULL: the window */
	char type[24];
	JSValue fn;
	uint8_t capture, once, gone;
};

/* a details' toggle event to come (the HTML Standard, 4.11.1: its
   details toggle task tracker) */
typedef struct toggle toggle;
struct toggle {
	toggle *next;
	dnode *n;
	uint8_t was;			/* open before */
};

typedef struct timer timer;
struct timer {
	timer *next;
	int id;
	unsigned long due;
	long every;			/* setInterval's ms, 0 for a timeout */
	JSValue fn;			/* a function, or code as a string */
	int argc;
	JSValue argv[4];
};

typedef struct ejs {
	doc *d;
	js_host h;
	JSRuntime *rt;
	JSContext *ctx;
	char url[URL_MAX], base[URL_MAX];
	unsigned long deadline, ms, t0;
	long bytes;
	int over;			/* the budget ran out */
	int changed;			/* the document changed: style and lay out again */
	int stale;			/* the tree changed since the CSS engine's links were made */
	unsigned long edits, styled;	/* changes made; and at the last styling (getComputedStyle) */
	timer *running;			/* the timer whose function runs */
	int loaded;			/* load has fired */
	dnode *current;			/* the script running (document.currentScript) */
	dnode *write_after;		/* where document.write puts what it writes */
	struct wrap {
		dnode *n;
		JSValue v;
	} *map;
	long nmap, mapcap;
	listener *ls;
	timer *ts;
	toggle *tog;			/* toggle events to fire, in order */
	int next_timer;
	dnode **started;		/* scripts that have run (or never will) */
	long nstarted, startcap;
	char *nav, *nav_body;		/* where a script sent the browser */
	char *nav_dl;			/* the download attribute of the link it clicked
					   to get there (a.click()), NULL none */
	long nav_len;
	char nav_ctype[96];
	const page *pg;
	JSValue node_proto, elem_proto, text_proto, doc_proto, frag_proto, style_proto, cl_proto, ev_proto,
		comp_proto, loc_obj;
	uint8_t parts;			/* the prelude's parts made (lazy_get) */
	struct {
		char *s;
		const struct ec_sellist *l;
	} sel[16];
	int nsel;
} ejs;

static JSClassID node_class, style_class, cl_class, ev_class;

static ejs *J(JSContext *ctx)
{
	return JS_GetContextOpaque(ctx);
}

/* ---------------------------------------------------------------- strings */

typedef struct {
	char *s;
	size_t n, cap;
	int fail;
} SB;

static void jb_add(SB *b, const char *s, size_t n)
{
	if (b->fail)
		return;
	if (b->n + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 256;
		char *t;
		while (cap < b->n + n + 1)
			cap *= 2;
		if ((t = realloc(b->s, cap)) == NULL) {
			b->fail = 1;
			return;
		}
		b->s = t;
		b->cap = cap;
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void jb_str(SB *b, const char *s)
{
	jb_add(b, s, strlen(s));
}

static JSValue jb_value(JSContext *ctx, SB *b)
{
	JSValue v = b->fail ? JS_ThrowOutOfMemory(ctx) : JS_NewStringLen(ctx, b->s ? b->s : "", b->n);
	free(b->s);
	return v;
}

static int lower_eq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char) (*a + 32) : *a;
		if (x != *b)
			return 0;
	}
	return *a == 0 && *b == 0;
}

/* a JS value as a C string of the caller's (NULL: an exception) */
static char *cstr(JSContext *ctx, JSValueConst v, size_t *len)
{
	size_t n;
	const char *s = JS_ToCStringLen(ctx, &n, v);
	char *c;
	if (s == NULL)
		return NULL;
	c = malloc(n + 1);
	if (c != NULL) {
		memcpy(c, s, n);
		c[n] = 0;
	}
	JS_FreeCString(ctx, s);
	if (len)
		*len = n;
	return c;
}

static void say(ejs *j, const char *what, const char *s)
{
	char line[300];
	if (j->h.log == NULL)
		return;
	snprintf(line, sizeof line, "js %s: %.200s", what, s);
	j->h.log(j->h.ud, line);
}

/* the exception pending, to the log */
static void report(ejs *j)
{
	JSValue e = JS_GetException(j->ctx);
	const char *s = JS_ToCString(j->ctx, e);
	char where[120] = "";
	if (JS_IsObject(e)) {
		JSValue st = JS_GetPropertyStr(j->ctx, e, "stack");
		const char *t0 = JS_IsUndefined(st) ? NULL : JS_ToCString(j->ctx, st), *t = t0;
		if (t != NULL) {
			size_t n;
			while (*t == ' ')
				t++;
			n = strcspn(t, "\n");
			if (n > 100)
				n = 100;
			if (n > 0)
				snprintf(where, sizeof where, " (%.*s)", (int) n, t);
			JS_FreeCString(j->ctx, t0);
		}
		JS_FreeValue(j->ctx, st);
	}
	if (j->over)
		say(j, "stopped", "over its time");
	else {
		char m[260];
		snprintf(m, sizeof m, "%.140s%s", s ? s : "?", where);
		say(j, "error", m);
	}
	if (s)
		JS_FreeCString(j->ctx, s);
	JS_FreeValue(j->ctx, e);
}

/* ---------------------------------------------------------------- the budget */

static int interrupt(JSRuntime *rt, void *opaque)
{
	ejs *j = opaque;
	(void) rt;
	if (pf_ms() - j->t0 > j->deadline) {
		j->over = 1;
		return 1;
	}
	return 0;
}

static void arm(ejs *j, unsigned long ms)
{
	j->t0 = pf_ms();
	j->deadline = ms;
	j->over = 0;
}

/* the promise jobs waiting */
static void jobs(ejs *j)
{
	JSContext *c;
	while (!j->over && JS_IsJobPending(j->rt))
		if (JS_ExecutePendingJob(j->rt, &c) < 0)
			report(j);
}

/* ---------------------------------------------------------------- nodes and their objects */

static unsigned long hash_ptr(const void *p)
{
	return (unsigned long) (((uintptr_t) p >> 3) * 2654435761u);
}

static struct wrap *map_find(ejs *j, const dnode *n)
{
	unsigned long k;
	if (j->mapcap == 0)
		return NULL;
	for (k = hash_ptr(n) & (unsigned long) (j->mapcap - 1); j->map[k].n != NULL;
	     k = (k + 1) & (unsigned long) (j->mapcap - 1))
		if (j->map[k].n == n)
			return &j->map[k];
	return NULL;
}

static int map_put(ejs *j, dnode *n, JSValue v)
{
	unsigned long k;
	if ((j->nmap + 1) * 2 > j->mapcap) {
		long cap = j->mapcap ? j->mapcap * 2 : 256, i;
		struct wrap *m = calloc((size_t) cap, sizeof *m), *old = j->map;
		if (m == NULL)
			return 0;
		for (i = 0; i < j->mapcap; i++)
			if (old[i].n != NULL) {
				for (k = hash_ptr(old[i].n) & (unsigned long) (cap - 1); m[k].n != NULL;
				     k = (k + 1) & (unsigned long) (cap - 1))
					;
				m[k] = old[i];
			}
		free(old);
		j->map = m;
		j->mapcap = cap;
	}
	for (k = hash_ptr(n) & (unsigned long) (j->mapcap - 1); j->map[k].n != NULL;
	     k = (k + 1) & (unsigned long) (j->mapcap - 1))
		;
	j->map[k].n = n;
	j->map[k].v = v;
	j->nmap++;
	return 1;
}

/* the object for a node (a new reference), JS_NULL for none */
static JSValue wrap(JSContext *ctx, dnode *n)
{
	ejs *j = J(ctx);
	struct wrap *w;
	JSValue o, proto;
	if (n == NULL)
		return JS_NULL;
	if ((w = map_find(j, n)) != NULL)
		return JS_DupValue(ctx, w->v);
	proto = n->type == DN_ELEMENT ? j->elem_proto : n->type == DN_TEXT ? j->text_proto :
		n->type == DN_DOCUMENT ? (n == j->d->root ? j->doc_proto : j->frag_proto) : j->node_proto;
	o = JS_NewObjectProtoClass(ctx, proto, node_class);
	if (JS_IsException(o))
		return o;
	JS_SetOpaque(o, n);
	if (!map_put(j, n, JS_DupValue(ctx, o))) {
		JS_FreeValue(ctx, o);
		JS_FreeValue(ctx, o);
		return JS_ThrowOutOfMemory(ctx);
	}
	return o;
}

static dnode *node_of(JSValueConst v)
{
	return JS_GetOpaque(v, node_class);
}

static dnode *elem_of(JSValueConst v)
{
	dnode *n = node_of(v);
	return n != NULL && n->type == DN_ELEMENT ? n : NULL;
}

#define THIS_NODE(n) \
	dnode *n = node_of(this_val); \
	if (n == NULL) \
		return JS_ThrowTypeError(ctx, "not a node")
#define THIS_ELEM(n) \
	dnode *n = elem_of(this_val); \
	if (n == NULL) \
		return JS_ThrowTypeError(ctx, "not an element")

static int is_html(const dnode *n, int tag)
{
	return n != NULL && n->type == DN_ELEMENT && n->ns == DN_NS_HTML && n->tag == tag;
}

static const char *name_of(const dnode *n, size_t *len)
{
	return ec_atom_text(n->e.name, len);
}

/* the next node in tree order under root (into children when down) */
static dnode *next_in(dnode *n, const dnode *root, int down)
{
	if (down && n->first != NULL)
		return n->first;
	while (n != root && n != NULL) {
		if (n->next != NULL)
			return n->next;
		n = n->parent;
	}
	return NULL;
}

static int connected(const ejs *j, const dnode *n)
{
	while (n->parent != NULL)
		n = n->parent;
	return n == j->d->root;
}

static int inside(const dnode *n, const dnode *anc)
{
	for (; n != NULL; n = n->parent)
		if (n == anc)
			return 1;
	return 0;
}

static void dirty(ejs *j)
{
	j->changed = 1;
	j->edits++;
}

static void touch(ejs *j)
{
	dirty(j);
	j->stale = 1;
}

static dnode *new_node(doc *d, int type)
{
	dnode *n = doc_alloc(d, sizeof *n);
	if (n != NULL) {
		memset(n, 0, sizeof *n);
		n->type = (uint8_t) type;
		n->y = -1;
	}
	return n;
}

static dnode *new_text(doc *d, int type, const char *s, size_t len)
{
	dnode *n = new_node(d, type);
	if (n == NULL || (n->text = doc_alloc(d, len + 1)) == NULL)
		return NULL;
	memcpy(n->text, s, len);
	n->text[len] = 0;
	n->len = n->cap = (uint32_t) len;
	return n;
}

static void unlink_node(ejs *j, dnode *n)
{
	dnode *p = n->parent;
	if (p == NULL)
		return;
	if (n->prev)
		n->prev->next = n->next;
	else
		p->first = n->next;
	if (n->next)
		n->next->prev = n->prev;
	else
		p->last = n->prev;
	n->parent = n->prev = n->next = NULL;
	n->e.parent = n->e.prev = n->e.next = NULL;
	touch(j);
}

static void link_node(ejs *j, dnode *p, dnode *n, dnode *before)
{
	n->parent = p;
	n->next = before;
	n->prev = before ? before->prev : p->last;
	if (n->prev)
		n->prev->next = n;
	else
		p->first = n;
	if (before)
		before->prev = n;
	else
		p->last = n;
	touch(j);
}

/* the CSS engine's links, ids and flags, up to date for matching */
static void fresh(ejs *j)
{
	if (j->stale) {
		doc_refresh(j->d);
		j->stale = 0;
	}
}

static void run_script(ejs *j, dnode *n);

static int started(ejs *j, const dnode *n)
{
	long i;
	for (i = 0; i < j->nstarted; i++)
		if (j->started[i] == n)
			return 1;
	return 0;
}

static void mark_started(ejs *j, dnode *n)
{
	if (j->nstarted == j->startcap) {
		long cap = j->startcap ? j->startcap * 2 : 32;
		dnode **s = realloc(j->started, (size_t) cap * sizeof *s);
		if (s == NULL)
			return;
		j->started = s;
		j->startcap = cap;
	}
	j->started[j->nstarted++] = n;
}

static void toggle_queue(ejs *j, dnode *n, int was);

/* what the parser made of a script's HTML (innerHTML): its scripts
   never run; a details open in it has its toggle event to come (the
   attribute change steps as the parser sets open, 4.11.1) */
static void mark_all(ejs *j, dnode *n)
{
	dnode *k, *t;
	for (k = n; k != NULL; k = next_in(k, n, 1))
		if (is_html(k, T_SCRIPT)) {
			mark_started(j, k);
		} else if (DN_IS(k, T_DETAILS) && dn_attr(k, "open") != NULL) {
			for (t = k->parent; t != NULL && t != n && !is_html(t, T_TEMPLATE); t = t->parent)
				;
			if (t == NULL || t == n)	/* (not in a template's contents) */
				toggle_queue(j, k, 0);
		}
}

/* the scripts a script put in the document run when they are put in; a
   details put in closes if its group has an open one (4.11.1, its
   insertion steps: ensure details exclusivity by closing the given
   element if needed) */
static void run_inserted(ejs *j, dnode *n)
{
	dnode *k;
	if (!connected(j, n))
		return;
	for (k = n; k != NULL; k = next_in(k, n, 1))
		if (is_html(k, T_SCRIPT) && !started(j, k))
			run_script(j, k);
		else if (DN_IS(k, T_DETAILS) && dn_attr(k, "open") != NULL && dn_details_rival(j->d, k) != NULL &&
			 dn_set_open(j->d, k, 0))
			toggle_queue(j, k, 1);
}

/* n put into p before `before` (a fragment: its children); a script's
   checks first. Returns NULL or an exception. */
static JSValue insert(JSContext *ctx, dnode *p, dnode *n, dnode *before)
{
	ejs *j = J(ctx);
	if (p->type != DN_ELEMENT && p->type != DN_DOCUMENT)
		return JS_ThrowTypeError(ctx, "HierarchyRequestError: can't have children");
	if (inside(p, n) || n == j->d->root)
		return JS_ThrowTypeError(ctx, "HierarchyRequestError: would be inside itself");
	if (before != NULL && before->parent != p)
		return JS_ThrowTypeError(ctx, "NotFoundError: not a child");
	if (n->type == DN_DOCUMENT) {
		dnode *c, *first = n->first;
		while ((c = n->first) != NULL) {
			unlink_node(j, c);
			link_node(j, p, c, before);
		}
		for (c = first; c != NULL && c != before; c = c->next)
			run_inserted(j, c);
		return JS_NULL;
	}
	if (n == before)
		return JS_NULL;
	unlink_node(j, n);
	link_node(j, p, n, before);
	run_inserted(j, n);
	return JS_NULL;
}

/* a node from a JS value, or a text node from a string (append and the like) */
static dnode *node_or_text(JSContext *ctx, JSValueConst v)
{
	dnode *n = node_of(v);
	if (n == NULL) {
		size_t len;
		char *s = cstr(ctx, v, &len);
		if (s == NULL)
			return NULL;
		n = new_text(J(ctx)->d, DN_TEXT, s, len);
		free(s);
		if (n == NULL)
			JS_ThrowOutOfMemory(ctx);
	}
	return n;
}

/* ---------------------------------------------------------------- text and HTML */

static void text_of(const dnode *n, SB *b)
{
	const dnode *c;
	if (n->type == DN_TEXT) {
		jb_add(b, n->text, n->len);
		return;
	}
	if (EC_DEEP())
		return;			/* the stack's floor: no deeper */
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == DN_TEXT || c->type == DN_ELEMENT || c->type == DN_DOCUMENT)
			text_of(c, b);
}

static void esc(SB *b, const char *s, size_t n, int attr)
{
	size_t i, from = 0;
	for (i = 0; i < n; i++) {
		const char *r = NULL;
		unsigned char c = (unsigned char) s[i];
		if (c == '&')
			r = "&amp;";
		else if (c == 0xc2 && i + 1 < n && (unsigned char) s[i + 1] == 0xa0) {
			jb_add(b, s + from, i - from);
			jb_str(b, "&nbsp;");
			from = i + 2;
			i++;
			continue;
		} else if (attr && c == '"')
			r = "&quot;";
		else if (!attr && c == '<')
			r = "&lt;";
		else if (!attr && c == '>')
			r = "&gt;";
		if (r != NULL) {
			jb_add(b, s + from, i - from);
			jb_str(b, r);
			from = i + 1;
		}
	}
	jb_add(b, s + from, n - from);
}

static int is_void(const dnode *n)
{
	static const char *const v[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link",
					 "meta", "source", "track", "wbr", "basefont", "bgsound", "frame",
					 "keygen", "param", NULL };
	size_t len;
	const char *name = name_of(n, &len);
	int i;
	if (n->ns != DN_NS_HTML)
		return 0;
	for (i = 0; v[i]; i++)
		if (strlen(v[i]) == len && memcmp(v[i], name, len) == 0)
			return 1;
	return 0;
}

static int raw_text(const dnode *n)
{
	static const char *const r[] = { "style", "script", "xmp", "iframe", "noembed", "noframes",
					  "plaintext", "noscript", NULL };
	size_t len;
	const char *name;
	int i;
	if (n == NULL || n->type != DN_ELEMENT || n->ns != DN_NS_HTML)
		return 0;
	name = name_of(n, &len);
	for (i = 0; r[i]; i++)
		if (strlen(r[i]) == len && memcmp(r[i], name, len) == 0)
			return 1;
	return 0;
}

/* the HTML Standard's fragment serialisation (13.3) */
static void serialise(const dnode *n, SB *b, int self)
{
	const dnode *c;
	if (EC_DEEP())
		return;			/* the stack's floor: no deeper */
	if (self) {
		size_t len;
		const char *name;
		uint16_t i;
		switch (n->type) {
		case DN_TEXT:
			if (raw_text(n->parent))
				jb_add(b, n->text, n->len);
			else
				esc(b, n->text, n->len, 0);
			return;
		case DN_OTHER:
			jb_str(b, "<!--");
			jb_add(b, n->text, n->len);
			jb_str(b, "-->");
			return;
		case DN_ELEMENT:
			break;
		default:
			return;
		}
		name = name_of(n, &len);
		jb_str(b, "<");
		jb_add(b, name, len);
		for (i = 0; i < n->nattr; i++) {
			size_t al;
			const char *an = ec_atom_text(n->attrs[i].name, &al);
			jb_str(b, " ");
			jb_add(b, an, al);
			jb_str(b, "=\"");
			esc(b, n->attrs[i].val, n->attrs[i].len, 1);
			jb_str(b, "\"");
		}
		jb_str(b, ">");
		if (is_void(n))
			return;
	}
	for (c = n->first; c != NULL; c = c->next)
		serialise(c, b, 1);
	if (self) {
		size_t len;
		const char *name = name_of(n, &len);
		jb_str(b, "</");
		jb_add(b, name, len);
		jb_str(b, ">");
	}
}

/* html parsed in the context of element ctx: a fragment node, or NULL
   with an exception */
static dnode *parse_html(JSContext *ctx, const dnode *at, const char *s, size_t len)
{
	ejs *j = J(ctx);
	char name[32] = "body";
	dnode *f;
	if (at != NULL && at->type == DN_ELEMENT) {
		size_t nl;
		const char *nm = name_of(at, &nl);
		if (nl < sizeof name) {
			memcpy(name, nm, nl);
			name[nl] = 0;
		}
	}
	f = html_fragment(j->d, name, at != NULL && at->type == DN_ELEMENT ? at->ns : DN_NS_HTML, s, len, 1);
	if (f == NULL) {
		JS_ThrowOutOfMemory(ctx);
		return NULL;
	}
	mark_all(j, f);
	return f;
}

static void remove_children(ejs *j, dnode *n)
{
	while (n->first != NULL)
		unlink_node(j, n->first);
}

/* ---------------------------------------------------------------- selectors */

static const struct ec_sellist *selector(JSContext *ctx, JSValueConst v)
{
	ejs *j = J(ctx);
	size_t len;
	char *s = cstr(ctx, v, &len);
	const struct ec_sellist *l;
	int i;
	if (s == NULL)
		return NULL;
	for (i = 0; i < j->nsel; i++)
		if (strcmp(j->sel[i].s, s) == 0) {
			free(s);
			return j->sel[i].l;
		}
	l = ec_selector(j->d->css, s, len);
	if (l == NULL) {
		JS_ThrowSyntaxError(ctx, "SyntaxError: '%.60s' is not a valid selector", s);
		free(s);
		return NULL;
	}
	if (j->nsel == 16) {
		free(j->sel[0].s);
		memmove(&j->sel[0], &j->sel[1], 15 * sizeof j->sel[0]);
		j->nsel--;
	}
	j->sel[j->nsel].s = s;
	j->sel[j->nsel].l = l;
	j->nsel++;
	return l;
}

static int matches(ejs *j, const struct ec_sellist *l, const dnode *n)
{
	return n->type == DN_ELEMENT && ec_matches(j->d->css, l, &n->e);
}

/* the elements under root that match (all: every one into an array) */
static JSValue query(JSContext *ctx, dnode *root, JSValueConst sel, int all)
{
	ejs *j = J(ctx);
	const struct ec_sellist *l = selector(ctx, sel);
	JSValue a;
	uint32_t k = 0;
	dnode *n;
	if (l == NULL)
		return JS_EXCEPTION;
	fresh(j);
	a = all ? JS_NewArray(ctx) : JS_NULL;
	for (n = root->first ? root->first : NULL; n != NULL; n = next_in(n, root, 1))
		if (matches(j, l, n)) {
			if (!all)
				return wrap(ctx, n);
			JS_SetPropertyUint32(ctx, a, k++, wrap(ctx, n));
		}
	return a;
}

/* the elements under root for which want() says yes, in an array */
static JSValue collect(JSContext *ctx, dnode *root, int (*want)(const dnode *, const void *), const void *arg)
{
	JSValue a = JS_NewArray(ctx);
	uint32_t k = 0;
	dnode *n;
	for (n = root->first; n != NULL; n = next_in(n, root, 1))
		if (n->type == DN_ELEMENT && want(n, arg))
			JS_SetPropertyUint32(ctx, a, k++, wrap(ctx, n));
	return a;
}

static int want_tag(const dnode *n, const void *arg)
{
	const char *t = arg;
	size_t len;
	const char *name;
	if (t[0] == '*' && t[1] == 0)
		return 1;
	name = name_of(n, &len);
	return strlen(t) == len && memcmp(t, name, len) == 0;
}

static int has_class(const dnode *n, const char *c, size_t cl)
{
	uint16_t i;
	for (i = 0; i < n->ncls; i++) {
		size_t al;
		const char *a = ec_atom_text(n->cls[i], &al);
		if (al == cl && memcmp(a, c, cl) == 0)
			return 1;
	}
	return 0;
}

static int want_classes(const dnode *n, const void *arg)
{
	const char *s = arg;
	while (*s) {
		const char *w;
		while (*s == ' ' || *s == '\t' || *s == '\n')
			s++;
		w = s;
		while (*s && *s != ' ' && *s != '\t' && *s != '\n')
			s++;
		if (s > w && !has_class(n, w, (size_t) (s - w)))
			return 0;
	}
	return 1;
}

static int want_name(const dnode *n, const void *arg)
{
	const char *v = dn_attr(n, "name");
	return v != NULL && strcmp(v, arg) == 0;
}

static JSValue by_tag(JSContext *ctx, dnode *root, JSValueConst v)
{
	char *t = cstr(ctx, v, NULL), *p;
	JSValue r;
	if (t == NULL)
		return JS_EXCEPTION;
	for (p = t; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char) (*p + 32);
	r = collect(ctx, root, want_tag, t);
	free(t);
	return r;
}

static JSValue by_class(JSContext *ctx, dnode *root, JSValueConst v)
{
	char *t = cstr(ctx, v, NULL);
	JSValue r;
	if (t == NULL)
		return JS_EXCEPTION;
	r = collect(ctx, root, want_classes, t);
	free(t);
	return r;
}

static dnode *by_id(ejs *j, const dnode *root, const char *id)
{
	dnode *n;
	for (n = root->first; n != NULL; n = next_in(n, root, 1))
		if (n->type == DN_ELEMENT) {
			const char *v = dn_attr(n, "id");
			if (v != NULL && strcmp(v, id) == 0)
				return n;
		}
	(void) j;
	return NULL;
}

/* ---------------------------------------------------------------- Node */

enum { NP_TYPE, NP_NAME, NP_VALUE, NP_PARENT, NP_PARENT_EL, NP_FIRST, NP_LAST, NP_PREV, NP_NEXT, NP_CHILDREN,
       NP_OWNER, NP_TEXT, NP_CONNECTED };

static JSValue tag_name(JSContext *ctx, const dnode *n)
{
	char b[64];
	size_t len, i;
	const char *name = name_of(n, &len);
	if (len >= sizeof b)
		len = sizeof b - 1;
	for (i = 0; i < len; i++)
		b[i] = n->ns == DN_NS_HTML && name[i] >= 'a' && name[i] <= 'z' ? (char) (name[i] - 32) : name[i];
	return JS_NewStringLen(ctx, b, len);
}

static JSValue node_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	THIS_NODE(n);
	switch (magic) {
	case NP_TYPE:
		return JS_NewInt32(ctx, n->type == DN_ELEMENT ? 1 : n->type == DN_TEXT ? 3 : n->type == DN_OTHER ? 8 :
				   n->type == DN_DOCTYPE ? 10 : n->type == DN_PI ? 7 : n == j->d->root ? 9 : 11);
	case NP_NAME:
		return n->type == DN_ELEMENT ? tag_name(ctx, n) :
		       JS_NewString(ctx, n->type == DN_TEXT ? "#text" : n->type == DN_OTHER ? "#comment" :
				    n == j->d->root ? "#document" : "#document-fragment");
	case NP_VALUE:
		return n->type == DN_TEXT || n->type == DN_OTHER ? JS_NewStringLen(ctx, n->text, n->len) : JS_NULL;
	case NP_PARENT:
		return wrap(ctx, n->parent);
	case NP_PARENT_EL:
		return wrap(ctx, n->parent && n->parent->type == DN_ELEMENT ? n->parent : NULL);
	case NP_FIRST:
		return wrap(ctx, n->first);
	case NP_LAST:
		return wrap(ctx, n->last);
	case NP_PREV:
		return wrap(ctx, n->prev);
	case NP_NEXT:
		return wrap(ctx, n->next);
	case NP_CHILDREN: {
		JSValue a = JS_NewArray(ctx);
		uint32_t k = 0;
		dnode *c;
		for (c = n->first; c != NULL; c = c->next)
			JS_SetPropertyUint32(ctx, a, k++, wrap(ctx, c));
		return a;
	}
	case NP_OWNER:
		return n == j->d->root ? JS_NULL : wrap(ctx, j->d->root);
	case NP_TEXT: {
		SB b = { 0 };
		if (n == j->d->root)
			return JS_NULL;
		text_of(n, &b);
		return jb_value(ctx, &b);
	}
	case NP_CONNECTED:
		return JS_NewBool(ctx, connected(j, n));
	}
	return JS_UNDEFINED;
}

static JSValue node_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	ejs *j = J(ctx);
	size_t len;
	char *s;
	THIS_NODE(n);
	if (magic != NP_TEXT && magic != NP_VALUE)
		return JS_UNDEFINED;
	if (n == j->d->root)
		return JS_UNDEFINED;
	if ((s = JS_IsNull(v) ? strdup("") : cstr(ctx, v, &len)) == NULL)
		return JS_EXCEPTION;
	if (JS_IsNull(v))
		len = 0;
	if (n->type == DN_TEXT || n->type == DN_OTHER) {
		char *t = doc_alloc(j->d, len + 1);
		if (t == NULL) {
			free(s);
			return JS_ThrowOutOfMemory(ctx);
		}
		memcpy(t, s, len + 1);
		n->text = t;
		n->len = n->cap = (uint32_t) len;
		dirty(j);
	} else if (magic == NP_TEXT) {
		dnode *t = len ? new_text(j->d, DN_TEXT, s, len) : NULL;
		remove_children(j, n);
		if (t != NULL)
			link_node(j, n, t, NULL);
	}
	free(s);
	return JS_UNDEFINED;
}

static JSValue node_append(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *c;
	JSValue r;
	THIS_NODE(n);
	if (argc < 1 || (c = node_of(argv[0])) == NULL)
		return JS_ThrowTypeError(ctx, "appendChild: not a node");
	r = insert(ctx, n, c, NULL);
	if (JS_IsException(r))
		return r;
	return JS_DupValue(ctx, argv[0]);
}

static JSValue node_insert_before(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *c, *before = NULL;
	JSValue r;
	THIS_NODE(n);
	if (argc < 1 || (c = node_of(argv[0])) == NULL)
		return JS_ThrowTypeError(ctx, "insertBefore: not a node");
	if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]) && (before = node_of(argv[1])) == NULL)
		return JS_ThrowTypeError(ctx, "insertBefore: not a node");
	r = insert(ctx, n, c, before);
	if (JS_IsException(r))
		return r;
	return JS_DupValue(ctx, argv[0]);
}

static JSValue node_remove_child(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *c;
	THIS_NODE(n);
	if (argc < 1 || (c = node_of(argv[0])) == NULL || c->parent != n)
		return JS_ThrowTypeError(ctx, "NotFoundError: not a child");
	unlink_node(J(ctx), c);
	return JS_DupValue(ctx, argv[0]);
}

static JSValue node_replace_child(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *c, *old, *at;
	JSValue r;
	THIS_NODE(n);
	if (argc < 2 || (c = node_of(argv[0])) == NULL || (old = node_of(argv[1])) == NULL || old->parent != n)
		return JS_ThrowTypeError(ctx, "NotFoundError: not a child");
	if (c == old)
		return JS_DupValue(ctx, argv[1]);
	at = old->next == c ? c->next : old->next;
	unlink_node(J(ctx), old);
	r = insert(ctx, n, c, at);
	if (JS_IsException(r))
		return r;
	return JS_DupValue(ctx, argv[1]);
}

static dnode *clone(ejs *j, const dnode *n, int deep)
{
	dnode *c = new_node(j->d, n->type), *k;
	if (c == NULL)
		return NULL;
	c->ns = n->ns;
	c->tag = n->tag;
	c->hid = n->hid;
	c->e.name = n->e.name;
	c->e.id = n->e.id;
	if (n->type != DN_ELEMENT) {
		if (n->text != NULL && (c->text = doc_alloc(j->d, n->len + 1)) != NULL) {
			memcpy(c->text, n->text, n->len + 1);
			c->len = c->cap = n->len;
		}
	} else if (n->nattr > 0) {
		if ((c->attrs = doc_alloc(j->d, n->nattr * sizeof *c->attrs)) == NULL)
			return NULL;
		memcpy(c->attrs, n->attrs, n->nattr * sizeof *c->attrs);
		c->nattr = n->nattr;
		c->cls = n->cls;
		c->ncls = n->ncls;
	}
	if (deep && !EC_DEEP())		/* the stack's floor: no deeper */
		for (k = n->first; k != NULL; k = k->next) {
			dnode *ck = clone(j, k, 1);
			if (ck == NULL)
				return NULL;
			ck->parent = c;
			ck->prev = c->last;
			if (c->last)
				c->last->next = ck;
			else
				c->first = ck;
			c->last = ck;
		}
	return c;
}

static JSValue node_clone(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	dnode *c;
	THIS_NODE(n);
	if (n == j->d->root)
		return JS_ThrowTypeError(ctx, "NotSupportedError: the document");
	c = clone(j, n, argc > 0 && JS_ToBool(ctx, argv[0]) > 0);
	if (c == NULL)
		return JS_ThrowOutOfMemory(ctx);
	return wrap(ctx, c);
}

static JSValue node_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *c;
	THIS_NODE(n);
	if (argc < 1 || (c = node_of(argv[0])) == NULL)
		return JS_FALSE;
	return JS_NewBool(ctx, inside(c, n));
}

static JSValue node_has_children(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	THIS_NODE(n);
	return JS_NewBool(ctx, n->first != NULL);
}

static JSValue node_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_UNDEFINED;
}

static JSValue node_same(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewBool(ctx, argc > 0 && node_of(argv[0]) == node_of(this_val));
}

static JSValue add_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue remove_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue dispatch_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

/* append, prepend, before, after, replaceWith (magic: which) */
enum { AP_APPEND, AP_PREPEND, AP_BEFORE, AP_AFTER, AP_REPLACE };

static JSValue node_put(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	dnode *p, *at;
	int i;
	THIS_NODE(n);
	switch (magic) {
	case AP_APPEND:
		p = n, at = NULL;
		break;
	case AP_PREPEND:
		p = n, at = n->first;
		break;
	case AP_BEFORE:
		p = n->parent, at = n;
		break;
	default:
		p = n->parent, at = n->next;
		break;
	}
	if (p == NULL)
		return JS_UNDEFINED;
	for (i = 0; i < argc; i++) {
		dnode *c = node_or_text(ctx, argv[i]);
		JSValue r;
		if (c == NULL)
			return JS_EXCEPTION;
		if (c == at)
			at = at->next;
		r = insert(ctx, p, c, at);
		if (JS_IsException(r))
			return r;
	}
	if (magic == AP_REPLACE && n->parent == p)
		unlink_node(j, n);
	return JS_UNDEFINED;
}

static JSValue node_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	THIS_NODE(n);
	unlink_node(J(ctx), n);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry node_funcs[] = {
	JS_CGETSET_MAGIC_DEF("nodeType", node_get, NULL, NP_TYPE),
	JS_CGETSET_MAGIC_DEF("nodeName", node_get, NULL, NP_NAME),
	JS_CGETSET_MAGIC_DEF("nodeValue", node_get, node_set, NP_VALUE),
	JS_CGETSET_MAGIC_DEF("parentNode", node_get, NULL, NP_PARENT),
	JS_CGETSET_MAGIC_DEF("parentElement", node_get, NULL, NP_PARENT_EL),
	JS_CGETSET_MAGIC_DEF("firstChild", node_get, NULL, NP_FIRST),
	JS_CGETSET_MAGIC_DEF("lastChild", node_get, NULL, NP_LAST),
	JS_CGETSET_MAGIC_DEF("previousSibling", node_get, NULL, NP_PREV),
	JS_CGETSET_MAGIC_DEF("nextSibling", node_get, NULL, NP_NEXT),
	JS_CGETSET_MAGIC_DEF("childNodes", node_get, NULL, NP_CHILDREN),
	JS_CGETSET_MAGIC_DEF("ownerDocument", node_get, NULL, NP_OWNER),
	JS_CGETSET_MAGIC_DEF("textContent", node_get, node_set, NP_TEXT),
	JS_CGETSET_MAGIC_DEF("isConnected", node_get, NULL, NP_CONNECTED),
	JS_CFUNC_DEF("appendChild", 1, node_append),
	JS_CFUNC_DEF("insertBefore", 2, node_insert_before),
	JS_CFUNC_DEF("removeChild", 1, node_remove_child),
	JS_CFUNC_DEF("replaceChild", 2, node_replace_child),
	JS_CFUNC_DEF("cloneNode", 1, node_clone),
	JS_CFUNC_DEF("contains", 1, node_contains),
	JS_CFUNC_DEF("hasChildNodes", 0, node_has_children),
	JS_CFUNC_DEF("normalize", 0, node_noop),
	JS_CFUNC_DEF("isSameNode", 1, node_same),
	JS_CFUNC_DEF("isEqualNode", 1, node_same),
	JS_CFUNC_DEF("addEventListener", 3, add_listener),
	JS_CFUNC_DEF("removeEventListener", 3, remove_listener),
	JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),
	JS_PROP_INT32_DEF("ELEMENT_NODE", 1, 0),
	JS_PROP_INT32_DEF("TEXT_NODE", 3, 0),
	JS_PROP_INT32_DEF("COMMENT_NODE", 8, 0),
	JS_PROP_INT32_DEF("DOCUMENT_NODE", 9, 0),
	JS_PROP_INT32_DEF("DOCUMENT_FRAGMENT_NODE", 11, 0),
};

/* in both elements and fragments (ParentNode, ChildNode) */
static JSValue el_query(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int all)
{
	THIS_NODE(n);
	if (argc < 1)
		return JS_ThrowTypeError(ctx, "a selector is needed");
	return query(ctx, n, argv[0], all);
}

static JSValue el_by_tag(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	THIS_NODE(n);
	return argc < 1 ? JS_NewArray(ctx) : by_tag(ctx, n, argv[0]);
}

static JSValue el_by_class(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	THIS_NODE(n);
	return argc < 1 ? JS_NewArray(ctx) : by_class(ctx, n, argv[0]);
}

enum { EP_CHILDREN, EP_FIRST_EL, EP_LAST_EL, EP_PREV_EL, EP_NEXT_EL, EP_COUNT };

static dnode *el_step(dnode *n, int next)
{
	while (n != NULL && n->type != DN_ELEMENT)
		n = next ? n->next : n->prev;
	return n;
}

static JSValue parent_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	THIS_NODE(n);
	switch (magic) {
	case EP_CHILDREN: {
		JSValue a = JS_NewArray(ctx);
		uint32_t k = 0;
		dnode *c;
		for (c = el_step(n->first, 1); c != NULL; c = el_step(c->next, 1))
			JS_SetPropertyUint32(ctx, a, k++, wrap(ctx, c));
		return a;
	}
	case EP_FIRST_EL:
		return wrap(ctx, el_step(n->first, 1));
	case EP_LAST_EL:
		return wrap(ctx, el_step(n->last, 0));
	case EP_PREV_EL:
		return wrap(ctx, el_step(n->prev, 0));
	case EP_NEXT_EL:
		return wrap(ctx, el_step(n->next, 1));
	case EP_COUNT: {
		int k = 0;
		dnode *c;
		for (c = el_step(n->first, 1); c != NULL; c = el_step(c->next, 1))
			k++;
		return JS_NewInt32(ctx, k);
	}
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry parent_funcs[] = {
	JS_CGETSET_MAGIC_DEF("children", parent_get, NULL, EP_CHILDREN),
	JS_CGETSET_MAGIC_DEF("firstElementChild", parent_get, NULL, EP_FIRST_EL),
	JS_CGETSET_MAGIC_DEF("lastElementChild", parent_get, NULL, EP_LAST_EL),
	JS_CGETSET_MAGIC_DEF("childElementCount", parent_get, NULL, EP_COUNT),
	JS_CFUNC_MAGIC_DEF("querySelector", 1, el_query, 0),
	JS_CFUNC_MAGIC_DEF("querySelectorAll", 1, el_query, 1),
	JS_CFUNC_DEF("getElementsByTagName", 1, el_by_tag),
	JS_CFUNC_DEF("getElementsByClassName", 1, el_by_class),
	JS_CFUNC_MAGIC_DEF("append", 1, node_put, AP_APPEND),
	JS_CFUNC_MAGIC_DEF("prepend", 1, node_put, AP_PREPEND),
};

static const JSCFunctionListEntry child_funcs[] = {
	JS_CGETSET_MAGIC_DEF("previousElementSibling", parent_get, NULL, EP_PREV_EL),
	JS_CGETSET_MAGIC_DEF("nextElementSibling", parent_get, NULL, EP_NEXT_EL),
	JS_CFUNC_MAGIC_DEF("before", 1, node_put, AP_BEFORE),
	JS_CFUNC_MAGIC_DEF("after", 1, node_put, AP_AFTER),
	JS_CFUNC_MAGIC_DEF("replaceWith", 1, node_put, AP_REPLACE),
	JS_CFUNC_DEF("remove", 0, node_remove),
};

/* ---------------------------------------------------------------- Text */

static JSValue text_len(JSContext *ctx, JSValueConst this_val)
{
	THIS_NODE(n);
	if (n->type != DN_TEXT && n->type != DN_OTHER)
		return JS_UNDEFINED;
	{
		/* in UTF-16 units */
		uint32_t i, k = 0;
		for (i = 0; i < n->len; i++) {
			unsigned char c = (unsigned char) n->text[i];
			if ((c & 0xc0) != 0x80)
				k += c >= 0xf0 ? 2 : 1;
		}
		return JS_NewInt32(ctx, (int32_t) k);
	}
}

static const JSCFunctionListEntry text_funcs[] = {
	JS_CGETSET_MAGIC_DEF("data", node_get, node_set, NP_VALUE),
	JS_CGETSET_MAGIC_DEF("wholeText", node_get, NULL, NP_VALUE),
	JS_CGETSET_DEF("length", text_len, NULL),
};

/* ---------------------------------------------------------------- Element: attributes */

static char *attr_name(JSContext *ctx, JSValueConst v)
{
	char *s = cstr(ctx, v, NULL), *p;
	if (s != NULL)
		for (p = s; *p; p++)
			if (*p >= 'A' && *p <= 'Z')
				*p = (char) (*p + 32);
	return s;
}

/* ---------------------------------------------------------------- details */

/* a details' toggle event queued (the HTML Standard, 4.11.1, queue a
   details toggle event task): one an element, the old state of the one
   it replaces kept */
static void toggle_queue(ejs *j, dnode *n, int was)
{
	toggle *t, **p;
	for (p = &j->tog; (t = *p) != NULL; p = &t->next)
		if (t->n == n) {
			*p = t->next;
			was = t->was;
			free(t);
			break;
		}
	if ((t = malloc(sizeof *t)) == NULL)
		return;
	t->next = NULL;
	t->n = n;
	t->was = (uint8_t) was;
	for (p = &j->tog; *p != NULL; p = &(*p)->next)
		;
	*p = t;
}

static void toggle_cb(void *ud, dnode *k)
{
	toggle_queue(ud, k, dn_attr(k, "open") == NULL);
}

/* after a script set or removed an attribute (was: it was there): a
   details' attribute change steps (4.11.1) */
static void attr_changed(ejs *j, dnode *n, const char *name, int was)
{
	int now;
	dnode *k;
	if (!DN_IS(n, T_DETAILS))
		return;
	now = dn_attr(n, "open") != NULL;
	if (strcmp(name, "name") == 0) {
		/* ensure details exclusivity by closing the given element if
		   needed */
		if (now && dn_details_rival(j->d, n) != NULL && dn_set_open(j->d, n, 0))
			toggle_queue(j, n, 1);
	} else if (strcmp(name, "open") == 0 && was != now) {
		toggle_queue(j, n, was);
		/* ensure details exclusivity by closing other elements if
		   needed */
		if (now && (k = dn_details_rival(j->d, n)) != NULL && dn_set_open(j->d, k, 0))
			toggle_queue(j, k, 1);
	}
}

static fctl *ctl_of(ejs *j, dnode *n);

/* the details whose summary a click on n activates (4.11.2: n in a
   summary, the summary for its parent details, with no link or control
   nearer n, which a click activates instead), or NULL */
static dnode *summary_of(ejs *j, dnode *n)
{
	for (; n != NULL && n->type == DN_ELEMENT; n = n->parent) {
		if ((is_html(n, T_A) && dn_attr(n, "href") != NULL) || ctl_of(j, n) != NULL || is_html(n, T_LABEL))
			return NULL;
		if (DN_IS(n, T_SUMMARY))
			return n->parent != NULL && DN_IS(n->parent, T_DETAILS) && dn_summary(n->parent) == n ?
			       n->parent : NULL;
	}
	return NULL;
}

/* ---------------------------------------------------------------- Element: attributes, again */

static JSValue set_attr(JSContext *ctx, dnode *n, const char *name, const char *v, size_t len)
{
	ejs *j = J(ctx);
	int was = dn_attr(n, name) != NULL;
	if (!dn_set_attr(j->d, n, name, v, len))
		return JS_ThrowOutOfMemory(ctx);
	touch(j);
	attr_changed(j, n, name, was);
	return JS_UNDEFINED;
}

static void remove_attr(ejs *j, dnode *n, const char *name)
{
	int was = dn_attr(n, name) != NULL;
	dn_remove_attr(j->d, n, name);
	touch(j);
	attr_changed(j, n, name, was);
}

static JSValue el_get_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name;
	const char *v;
	THIS_ELEM(n);
	if (argc < 1 || (name = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	v = dn_attr(n, name);
	free(name);
	return v ? JS_NewString(ctx, v) : JS_NULL;
}

static JSValue el_has_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name;
	const char *v;
	THIS_ELEM(n);
	if (argc < 1 || (name = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	v = dn_attr(n, name);
	free(name);
	return JS_NewBool(ctx, v != NULL);
}

static JSValue el_set_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name, *v;
	size_t len;
	JSValue r;
	THIS_ELEM(n);
	if (argc < 2 || (name = attr_name(ctx, argv[0])) == NULL)
		return JS_ThrowTypeError(ctx, "setAttribute: a name and a value");
	if ((v = cstr(ctx, argv[1], &len)) == NULL) {
		free(name);
		return JS_EXCEPTION;
	}
	r = set_attr(ctx, n, name, v, len);
	free(name);
	free(v);
	return r;
}

static JSValue el_remove_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name;
	THIS_ELEM(n);
	if (argc < 1 || (name = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	remove_attr(J(ctx), n, name);
	free(name);
	return JS_UNDEFINED;
}

static JSValue el_toggle_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name;
	int on;
	THIS_ELEM(n);
	if (argc < 1 || (name = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	on = argc > 1 ? JS_ToBool(ctx, argv[1]) : dn_attr(n, name) == NULL;
	if (on && dn_attr(n, name) == NULL)
		set_attr(ctx, n, name, "", 0);
	else if (!on)
		remove_attr(J(ctx), n, name);
	touch(J(ctx));
	free(name);
	return JS_NewBool(ctx, on);
}

static JSValue el_attr_names(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	JSValue a = JS_NewArray(ctx);
	uint16_t i;
	THIS_ELEM(n);
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		JS_SetPropertyUint32(ctx, a, i, JS_NewStringLen(ctx, an, al));
	}
	return a;
}

static JSValue el_attributes(JSContext *ctx, JSValueConst this_val)
{
	JSValue a = JS_NewArray(ctx);
	uint16_t i;
	THIS_ELEM(n);
	for (i = 0; i < n->nattr; i++) {
		size_t al;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		JSValue o = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, o, "name", JS_NewStringLen(ctx, an, al));
		JS_SetPropertyStr(ctx, o, "value", JS_NewStringLen(ctx, n->attrs[i].val, n->attrs[i].len));
		JS_SetPropertyUint32(ctx, a, i, o);
	}
	return a;
}

/* attributes as properties: strings, addresses (made absolute), flags */
#define REFLECT(X) \
	X(id, "id", 0) X(className, "class", 0) X(title, "title", 0) X(lang, "lang", 0) X(dir, "dir", 0) \
	X(name, "name", 0) X(alt, "alt", 0) X(rel, "rel", 0) X(target, "target", 0) X(media, "media", 0) \
	X(placeholder, "placeholder", 0) X(htmlFor, "for", 0) X(method, "method", 0) \
	X(enctype, "enctype", 0) X(content, "content", 0) X(accessKey, "accesskey", 0) \
	X(autocomplete, "autocomplete", 0) X(pattern, "pattern", 0) X(min, "min", 0) X(max, "max", 0) \
	X(step, "step", 0) X(label, "label", 0) X(role, "role", 0) X(slot, "slot", 0) \
	X(href, "href", 1) X(src, "src", 1) X(action, "action", 1) X(cite, "cite", 1) \
	X(hidden, "hidden", 2) X(disabled, "disabled", 2) X(required, "required", 2) \
	X(readOnly, "readonly", 2) X(multiple, "multiple", 2) X(autofocus, "autofocus", 2) \
	X(noValidate, "novalidate", 2) X(defaultChecked, "checked", 2) X(defaultSelected, "selected", 2) \
	X(open, "open", 2) X(async, "async", 2) X(defer, "defer", 2)
#define R_ENUM(js, a, k) R_##js,
enum { REFLECT(R_ENUM) R_N };
#define R_ATTR(js, a, k) a,
static const char *const r_attr[] = { REFLECT(R_ATTR) };
#define R_KIND(js, a, k) k,
static const uint8_t r_kind[] = { REFLECT(R_KIND) };

static JSValue reflect_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	const char *v;
	THIS_ELEM(n);
	v = dn_attr(n, r_attr[magic]);
	if (r_kind[magic] == 2)
		return JS_NewBool(ctx, v != NULL);
	if (v == NULL)
		return JS_NewString(ctx, "");
	if (r_kind[magic] == 1 && j->h.resolve != NULL) {
		char *out = malloc(URL_MAX);
		JSValue r;
		if (out == NULL)
			return JS_ThrowOutOfMemory(ctx);
		j->h.resolve(j->h.ud, j->base, v, out, URL_MAX, 0);
		r = JS_NewString(ctx, out[0] ? out : v);
		free(out);
		return r;
	}
	return JS_NewString(ctx, v);
}

static JSValue reflect_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
{
	size_t len;
	char *s;
	JSValue r;
	THIS_ELEM(n);
	if (r_kind[magic] == 2) {
		if (JS_ToBool(ctx, val))
			return dn_attr(n, r_attr[magic]) ? JS_UNDEFINED : set_attr(ctx, n, r_attr[magic], "", 0);
		remove_attr(J(ctx), n, r_attr[magic]);
		return JS_UNDEFINED;
	}
	if ((s = cstr(ctx, val, &len)) == NULL)
		return JS_EXCEPTION;
	r = set_attr(ctx, n, r_attr[magic], s, len);
	free(s);
	return r;
}

/* ---------------------------------------------------------------- Element: HTML, text, place */

enum { EH_INNER, EH_OUTER, EH_TAG, EH_LOCAL, EH_INNERTEXT };

static JSValue html_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	SB b = { 0 };
	THIS_NODE(n);
	switch (magic) {
	case EH_TAG:
		return n->type == DN_ELEMENT ? tag_name(ctx, n) : JS_UNDEFINED;
	case EH_LOCAL: {
		size_t len;
		const char *name;
		if (n->type != DN_ELEMENT)
			return JS_UNDEFINED;
		name = name_of(n, &len);
		return JS_NewStringLen(ctx, name, len);
	}
	case EH_INNERTEXT:
		text_of(n, &b);
		break;
	default:
		serialise(n, &b, magic == EH_OUTER);
		break;
	}
	return jb_value(ctx, &b);
}

static JSValue html_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	ejs *j = J(ctx);
	size_t len;
	char *s;
	dnode *f;
	THIS_NODE(n);
	if (magic == EH_INNERTEXT)
		return node_set(ctx, this_val, v, NP_TEXT);
	if ((s = cstr(ctx, v, &len)) == NULL)
		return JS_EXCEPTION;
	if (magic == EH_OUTER) {
		dnode *p = n->parent;
		f = p ? parse_html(ctx, p, s, len) : NULL;
		free(s);
		if (p == NULL)
			return JS_UNDEFINED;
		if (f == NULL)
			return JS_EXCEPTION;
		{
			dnode *at = n->next, *c;
			unlink_node(j, n);
			while ((c = f->first) != NULL) {
				unlink_node(j, c);
				link_node(j, p, c, at);
			}
		}
		return JS_UNDEFINED;
	}
	f = parse_html(ctx, n, s, len);
	free(s);
	if (f == NULL)
		return JS_EXCEPTION;
	remove_children(j, n);
	{
		dnode *c;
		while ((c = f->first) != NULL) {
			unlink_node(j, c);
			link_node(j, n, c, NULL);
		}
	}
	return JS_UNDEFINED;
}

/* insertAdjacentHTML, insertAdjacentElement, insertAdjacentText (magic) */
static JSValue el_adjacent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	char *where;
	dnode *p, *at, *what = NULL;
	THIS_ELEM(n);
	if (argc < 2 || (where = attr_name(ctx, argv[0])) == NULL)
		return JS_ThrowTypeError(ctx, "insertAdjacent: a place and what to put");
	if (!strcmp(where, "beforebegin"))
		p = n->parent, at = n;
	else if (!strcmp(where, "afterbegin"))
		p = n, at = n->first;
	else if (!strcmp(where, "beforeend"))
		p = n, at = NULL;
	else if (!strcmp(where, "afterend"))
		p = n->parent, at = n->next;
	else {
		free(where);
		return JS_ThrowSyntaxError(ctx, "SyntaxError: insertAdjacent: not a place");
	}
	free(where);
	if (p == NULL)
		return JS_NULL;
	if (magic == 0) {
		size_t len;
		char *s = cstr(ctx, argv[1], &len);
		if (s == NULL)
			return JS_EXCEPTION;
		what = parse_html(ctx, p, s, len);
		free(s);
		if (what == NULL)
			return JS_EXCEPTION;
		{
			dnode *c;
			while ((c = what->first) != NULL) {
				unlink_node(j, c);
				link_node(j, p, c, at);
			}
		}
		return JS_UNDEFINED;
	}
	if ((what = magic == 1 ? node_of(argv[1]) : node_or_text(ctx, argv[1])) == NULL)
		return magic == 1 ? JS_ThrowTypeError(ctx, "not an element") : JS_EXCEPTION;
	{
		JSValue r = insert(ctx, p, what, at);
		if (JS_IsException(r))
			return r;
	}
	return magic == 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
}

static JSValue el_matches(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int closest)
{
	ejs *j = J(ctx);
	const struct ec_sellist *l;
	dnode *k;
	THIS_ELEM(n);
	if (argc < 1 || (l = selector(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	fresh(j);
	if (!closest)
		return JS_NewBool(ctx, matches(j, l, n));
	for (k = n; k != NULL; k = k->parent)
		if (matches(j, l, k))
			return wrap(ctx, k);
	return JS_NULL;
}

enum { RECT_OBJ, RECT_TOP, RECT_LEFT, RECT_WIDTH, RECT_HEIGHT };

static int rect_of(ejs *j, const dnode *n, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	*x = *y = *w = *h = 0;
#ifndef EW_ST
	if (j->pg != NULL && page_node_rect(j->pg, n, x, y, w, h))
		return 1;
#endif
	return 0;
}

static JSValue el_rect(JSContext *ctx, JSValueConst this_val, int magic)
{
	int32_t x, y, w, h;
	THIS_ELEM(n);
	rect_of(J(ctx), n, &x, &y, &w, &h);
	switch (magic) {
	case RECT_TOP:
		return JS_NewInt32(ctx, y);
	case RECT_LEFT:
		return JS_NewInt32(ctx, x);
	case RECT_WIDTH:
		return JS_NewInt32(ctx, w);
	case RECT_HEIGHT:
		return JS_NewInt32(ctx, h);
	}
	{
		JSValue o = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, x));
		JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, y));
		JS_SetPropertyStr(ctx, o, "left", JS_NewInt32(ctx, x));
		JS_SetPropertyStr(ctx, o, "top", JS_NewInt32(ctx, y));
		JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, w));
		JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, h));
		JS_SetPropertyStr(ctx, o, "right", JS_NewInt32(ctx, x + w));
		JS_SetPropertyStr(ctx, o, "bottom", JS_NewInt32(ctx, y + h));
		return o;
	}
}

static JSValue el_rect_fn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return el_rect(ctx, this_val, RECT_OBJ);
}

static JSValue el_dataset(JSContext *ctx, JSValueConst this_val)
{
	JSValue o = JS_NewObject(ctx);
	uint16_t i;
	THIS_ELEM(n);
	for (i = 0; i < n->nattr; i++) {
		size_t al, k = 0, m;
		const char *an = ec_atom_text(n->attrs[i].name, &al);
		char key[64];
		if (al <= 5 || memcmp(an, "data-", 5) != 0 || al - 5 >= sizeof key)
			continue;
		for (m = 5; m < al; m++)
			if (an[m] == '-' && m + 1 < al && an[m + 1] >= 'a' && an[m + 1] <= 'z')
				key[k++] = (char) (an[++m] - 32);
			else
				key[k++] = an[m];
		key[k] = 0;
		JS_SetPropertyStr(ctx, o, key, JS_NewStringLen(ctx, n->attrs[i].val, n->attrs[i].len));
	}
	return o;
}

/* ---------------------------------------------------------------- style */

#define STYLE_PROPS(X) \
	X(display, "display") X(visibility, "visibility") X(color, "color") X(background, "background") \
	X(backgroundColor, "background-color") X(backgroundImage, "background-image") X(width, "width") \
	X(height, "height") X(minWidth, "min-width") X(maxWidth, "max-width") X(minHeight, "min-height") \
	X(maxHeight, "max-height") X(margin, "margin") X(marginTop, "margin-top") \
	X(marginRight, "margin-right") X(marginBottom, "margin-bottom") X(marginLeft, "margin-left") \
	X(padding, "padding") X(paddingTop, "padding-top") X(paddingRight, "padding-right") \
	X(paddingBottom, "padding-bottom") X(paddingLeft, "padding-left") X(border, "border") \
	X(borderTop, "border-top") X(borderRight, "border-right") X(borderBottom, "border-bottom") \
	X(borderLeft, "border-left") X(borderColor, "border-color") X(borderWidth, "border-width") \
	X(borderStyle, "border-style") X(borderRadius, "border-radius") X(font, "font") \
	X(fontSize, "font-size") X(fontWeight, "font-weight") X(fontStyle, "font-style") \
	X(fontFamily, "font-family") X(lineHeight, "line-height") X(textAlign, "text-align") \
	X(textDecoration, "text-decoration") X(textTransform, "text-transform") \
	X(textIndent, "text-indent") X(whiteSpace, "white-space") X(verticalAlign, "vertical-align") \
	X(position, "position") X(top, "top") X(right, "right") X(bottom, "bottom") X(left, "left") \
	X(zIndex, "z-index") X(cssFloat, "float") X(clear, "clear") X(overflow, "overflow") \
	X(overflowX, "overflow-x") X(overflowY, "overflow-y") X(opacity, "opacity") \
	X(cursor, "cursor") X(listStyle, "list-style") X(listStyleType, "list-style-type") \
	X(flex, "flex") X(flexDirection, "flex-direction") X(flexWrap, "flex-wrap") \
	X(justifyContent, "justify-content") X(alignItems, "align-items") X(gap, "gap") \
	X(transform, "transform") X(transition, "transition") X(boxSizing, "box-sizing") \
	X(direction, "direction") X(wordWrap, "word-wrap") X(outline, "outline")
#define SP_ENUM(js, css) SP_##js,
enum { STYLE_PROPS(SP_ENUM) SP_N };
#define SP_CSS(js, css) css,
static const char *const sp_css[] = { STYLE_PROPS(SP_CSS) };

/* the declarations of a style attribute, one at a time: *name and *val
   (trimmed, without !important), from s; returns where the next starts,
   or NULL at the end */
static const char *next_decl(const char *s, const char **name, size_t *nl, const char **val, size_t *vl)
{
	const char *e, *c;
	int depth = 0;
	char q = 0;
	while (*s == ';' || *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
		s++;
	if (*s == 0)
		return NULL;
	for (e = s; *e; e++) {
		if (q) {
			if (*e == q)
				q = 0;
		} else if (*e == '"' || *e == '\'')
			q = *e;
		else if (*e == '(')
			depth++;
		else if (*e == ')' && depth > 0)
			depth--;
		else if (*e == ';' && depth == 0)
			break;
	}
	c = memchr(s, ':', (size_t) (e - s));
	*name = s;
	*nl = c ? (size_t) (c - s) : (size_t) (e - s);
	while (*nl > 0 && (s[*nl - 1] == ' ' || s[*nl - 1] == '\t'))
		(*nl)--;
	if (c == NULL) {
		*val = e;
		*vl = 0;
	} else {
		const char *v = c + 1, *ve = e;
		while (v < ve && (*v == ' ' || *v == '\t' || *v == '\n'))
			v++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\n'))
			ve--;
		if (ve - v >= 10 && !strncmp(ve - 10, "!important", 10))
			for (ve -= 10; ve > v && ve[-1] == ' '; ve--)
				;
		*val = v;
		*vl = (size_t) (ve - v);
	}
	return e;
}

static int name_is(const char *s, size_t n, const char *prop)
{
	size_t i;
	if (strlen(prop) != n)
		return 0;
	for (i = 0; i < n; i++)
		if ((s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]) != prop[i])
			return 0;
	return 1;
}

static JSValue style_value(JSContext *ctx, const dnode *n, const char *prop)
{
	const char *s = dn_attr(n, "style"), *name, *val, *res = "";
	size_t nl, vl, rl = 0;
	while (s != NULL && (s = next_decl(s, &name, &nl, &val, &vl)) != NULL)
		if (name_is(name, nl, prop))
			res = val, rl = vl;
	return JS_NewStringLen(ctx, res, rl);
}

/* the style attribute with prop set to val (removed: vl 0) */
static JSValue style_put(JSContext *ctx, dnode *n, const char *prop, const char *v, size_t vl)
{
	const char *s = dn_attr(n, "style"), *name, *val;
	size_t nl, l;
	SB b = { 0 };
	JSValue r;
	while (s != NULL && (s = next_decl(s, &name, &nl, &val, &l)) != NULL) {
		if (name_is(name, nl, prop) || nl == 0)
			continue;
		jb_add(&b, name, nl);
		jb_str(&b, ": ");
		jb_add(&b, val, l);
		jb_str(&b, "; ");
	}
	if (vl > 0) {
		jb_str(&b, prop);
		jb_str(&b, ": ");
		jb_add(&b, v, vl);
		jb_str(&b, ";");
	}
	while (b.n > 0 && b.s[b.n - 1] == ' ')
		b.s[--b.n] = 0;
	if (b.fail) {
		free(b.s);
		return JS_ThrowOutOfMemory(ctx);
	}
	r = set_attr(ctx, n, "style", b.s ? b.s : "", b.n);
	free(b.s);
	return r;
}

static JSValue style_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	dnode *n = JS_GetOpaque(this_val, style_class);
	if (n == NULL)
		return JS_UNDEFINED;
	return style_value(ctx, n, sp_css[magic]);
}

static JSValue style_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	dnode *n = JS_GetOpaque(this_val, style_class);
	size_t len = 0;
	char *s;
	JSValue r;
	if (n == NULL)
		return JS_UNDEFINED;
	if (JS_IsNull(v) || JS_IsUndefined(v))
		return style_put(ctx, n, sp_css[magic], "", 0);
	if ((s = cstr(ctx, v, &len)) == NULL)
		return JS_EXCEPTION;
	/* a number for a length: px (as browsers took it in quirks mode) */
	if (JS_IsNumber(v) && magic != SP_opacity && magic != SP_zIndex && magic != SP_lineHeight &&
	    magic != SP_fontWeight && magic != SP_flex && strcmp(s, "0") != 0) {
		char *t = realloc(s, len + 3);
		if (t == NULL) {
			free(s);
			return JS_ThrowOutOfMemory(ctx);
		}
		s = t;
		memcpy(s + len, "px", 3);
		len += 2;
	}
	r = style_put(ctx, n, sp_css[magic], s, len);
	free(s);
	return r;
}

static JSValue style_css_text(JSContext *ctx, JSValueConst this_val)
{
	dnode *n = JS_GetOpaque(this_val, style_class);
	const char *s = n ? dn_attr(n, "style") : NULL;
	return JS_NewString(ctx, s ? s : "");
}

static JSValue style_set_css_text(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
	dnode *n = JS_GetOpaque(this_val, style_class);
	size_t len;
	char *s;
	JSValue r;
	if (n == NULL)
		return JS_UNDEFINED;
	if ((s = cstr(ctx, v, &len)) == NULL)
		return JS_EXCEPTION;
	r = set_attr(ctx, n, "style", s, len);
	free(s);
	return r;
}

/* setProperty, getPropertyValue, removeProperty (magic) */
static JSValue style_prop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	dnode *n = JS_GetOpaque(this_val, style_class);
	char *p, *v = NULL;
	size_t vl = 0;
	JSValue r;
	if (n == NULL || argc < 1 || (p = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	if (magic == 1)
		r = style_value(ctx, n, p);
	else if (magic == 0 && argc > 1 && !JS_IsNull(argv[1]) && (v = cstr(ctx, argv[1], &vl)) == NULL)
		r = JS_EXCEPTION;
	else {
		if (magic == 2)
			r = style_value(ctx, n, p);
		else
			r = JS_UNDEFINED;
		JS_FreeValue(ctx, style_put(ctx, n, p, v ? v : "", v ? vl : 0));
	}
	free(p);
	free(v);
	return r;
}

#define SP_DEF(js, css) JS_CGETSET_MAGIC_DEF(#js, style_get, style_set, SP_##js),
static const JSCFunctionListEntry style_funcs[] = {
	STYLE_PROPS(SP_DEF)
	JS_CGETSET_DEF("cssText", style_css_text, style_set_css_text),
	JS_CFUNC_MAGIC_DEF("setProperty", 2, style_prop, 0),
	JS_CFUNC_MAGIC_DEF("getPropertyValue", 1, style_prop, 1),
	JS_CFUNC_MAGIC_DEF("removeProperty", 1, style_prop, 2),
};

static JSValue el_style(JSContext *ctx, JSValueConst this_val)
{
	ejs *j = J(ctx);
	JSValue o;
	THIS_ELEM(n);
	o = JS_NewObjectProtoClass(ctx, j->style_proto, style_class);
	if (!JS_IsException(o))
		JS_SetOpaque(o, n);
	return o;
}

static JSValue el_set_style(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
	size_t len;
	char *s;
	JSValue r;
	THIS_ELEM(n);
	if ((s = cstr(ctx, v, &len)) == NULL)
		return JS_EXCEPTION;
	r = set_attr(ctx, n, "style", s, len);
	free(s);
	return r;
}

/* ---------------------------------------------------------------- classList */

static int cl_find(const char *s, const char *c, size_t cl, size_t *at)
{
	size_t i = 0, n = strlen(s);
	while (i < n) {
		size_t w;
		while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f'))
			i++;
		w = i;
		while (i < n && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f'))
			i++;
		if (i - w == cl && cl > 0 && memcmp(s + w, c, cl) == 0) {
			*at = w;
			return 1;
		}
	}
	return 0;
}

/* the class attribute with c added (add 1) or taken out (0) */
static JSValue cl_change(JSContext *ctx, dnode *n, const char *c, size_t cl, int add)
{
	const char *s = dn_attr(n, "class");
	size_t at;
	SB b = { 0 };
	JSValue r;
	if (s == NULL)
		s = "";
	if (cl == 0)
		return JS_ThrowSyntaxError(ctx, "SyntaxError: an empty class name");
	if (add) {
		if (cl_find(s, c, cl, &at))
			return JS_UNDEFINED;
		jb_str(&b, s);
		if (b.n > 0)
			jb_str(&b, " ");
		jb_add(&b, c, cl);
	} else {
		if (!cl_find(s, c, cl, &at))
			return JS_UNDEFINED;
		while (cl_find(s, c, cl, &at)) {
			size_t e = at + cl;
			while (at > 0 && s[at - 1] == ' ')
				at--;
			jb_add(&b, s, at);
			if (at > 0 && s[e] != 0)
				jb_str(&b, " ");
			while (s[e] == ' ')
				e++;
			s += e;
			if (!cl_find(s, c, cl, &at))
				break;
		}
		jb_str(&b, s);
	}
	if (b.fail) {
		free(b.s);
		return JS_ThrowOutOfMemory(ctx);
	}
	r = set_attr(ctx, n, "class", b.s ? b.s : "", b.n);
	free(b.s);
	return r;
}

enum { CL_ADD, CL_REMOVE, CL_TOGGLE, CL_CONTAINS, CL_REPLACE, CL_ITEM };

static JSValue cl_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	dnode *n = JS_GetOpaque(this_val, cl_class);
	int i;
	if (n == NULL)
		return JS_UNDEFINED;
	switch (magic) {
	case CL_ADD:
	case CL_REMOVE:
		for (i = 0; i < argc; i++) {
			size_t len;
			char *c = cstr(ctx, argv[i], &len);
			JSValue r;
			if (c == NULL)
				return JS_EXCEPTION;
			r = cl_change(ctx, n, c, len, magic == CL_ADD);
			free(c);
			if (JS_IsException(r))
				return r;
		}
		return JS_UNDEFINED;
	case CL_TOGGLE:
	case CL_CONTAINS: {
		size_t len, at;
		const char *s;
		char *c;
		int has, on;
		JSValue r = JS_UNDEFINED;
		if (argc < 1 || (c = cstr(ctx, argv[0], &len)) == NULL)
			return JS_EXCEPTION;
		s = dn_attr(n, "class");
		has = s != NULL && cl_find(s, c, len, &at);
		if (magic == CL_CONTAINS) {
			free(c);
			return JS_NewBool(ctx, has);
		}
		on = argc > 1 && !JS_IsUndefined(argv[1]) ? JS_ToBool(ctx, argv[1]) : !has;
		if (on != has)
			r = cl_change(ctx, n, c, len, on);
		free(c);
		if (JS_IsException(r))
			return r;
		return JS_NewBool(ctx, on);
	}
	case CL_REPLACE: {
		size_t ol, nl, at;
		char *o, *nw;
		const char *s = dn_attr(n, "class");
		int has;
		if (argc < 2 || (o = cstr(ctx, argv[0], &ol)) == NULL)
			return JS_EXCEPTION;
		if ((nw = cstr(ctx, argv[1], &nl)) == NULL) {
			free(o);
			return JS_EXCEPTION;
		}
		has = s != NULL && cl_find(s, o, ol, &at);
		if (has) {
			JS_FreeValue(ctx, cl_change(ctx, n, o, ol, 0));
			JS_FreeValue(ctx, cl_change(ctx, n, nw, nl, 1));
		}
		free(o);
		free(nw);
		return JS_NewBool(ctx, has);
	}
	case CL_ITEM: {
		int32_t k = 0;
		if (argc > 0)
			JS_ToInt32(ctx, &k, argv[0]);
		if (k < 0 || k >= n->ncls)
			return JS_NULL;
		{
			size_t al;
			const char *a = ec_atom_text(n->cls[k], &al);
			return JS_NewStringLen(ctx, a, al);
		}
	}
	}
	return JS_UNDEFINED;
}

static JSValue cl_length(JSContext *ctx, JSValueConst this_val)
{
	dnode *n = JS_GetOpaque(this_val, cl_class);
	return JS_NewInt32(ctx, n ? n->ncls : 0);
}

static JSValue cl_value(JSContext *ctx, JSValueConst this_val)
{
	dnode *n = JS_GetOpaque(this_val, cl_class);
	const char *s = n ? dn_attr(n, "class") : NULL;
	return JS_NewString(ctx, s ? s : "");
}

static const JSCFunctionListEntry cl_funcs[] = {
	JS_CFUNC_MAGIC_DEF("add", 1, cl_call, CL_ADD),
	JS_CFUNC_MAGIC_DEF("remove", 1, cl_call, CL_REMOVE),
	JS_CFUNC_MAGIC_DEF("toggle", 2, cl_call, CL_TOGGLE),
	JS_CFUNC_MAGIC_DEF("contains", 1, cl_call, CL_CONTAINS),
	JS_CFUNC_MAGIC_DEF("replace", 2, cl_call, CL_REPLACE),
	JS_CFUNC_MAGIC_DEF("item", 1, cl_call, CL_ITEM),
	JS_CGETSET_DEF("length", cl_length, NULL),
	JS_CGETSET_DEF("value", cl_value, NULL),
};

static JSValue el_class_list(JSContext *ctx, JSValueConst this_val)
{
	ejs *j = J(ctx);
	JSValue o;
	THIS_ELEM(n);
	o = JS_NewObjectProtoClass(ctx, j->cl_proto, cl_class);
	if (!JS_IsException(o))
		JS_SetOpaque(o, n);
	return o;
}

/* ---------------------------------------------------------------- form controls */

/* the control's state, made now if the document hasn't one yet */
static fctl *ctl_of(ejs *j, dnode *n)
{
	if (n->ctl == NULL && connected(j, n) &&
	    (is_html(n, T_INPUT) || is_html(n, T_SELECT) || is_html(n, T_TEXTAREA) || is_html(n, T_BUTTON))) {
		doc_refresh(j->d);
		j->stale = 0;
	}
	return n->ctl;
}

static dnode *select_of(dnode *o)
{
	dnode *p;
	for (p = o->parent; p != NULL && !is_html(p, T_SELECT); p = p->parent)
		;
	return p;
}

static void option_text(const dnode *o, SB *b)
{
	char t[512];
	form_option_text(o, t, sizeof t);
	jb_str(b, t);
}

enum { FP_VALUE, FP_CHECKED, FP_TYPE, FP_SEL_INDEX, FP_OPTIONS, FP_FORM, FP_TEXT, FP_SELECTED, FP_DEFAULT,
       FP_ELEMENTS, FP_LENGTH, FP_INDEX };

static JSValue fprop_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	fctl *c;
	THIS_ELEM(n);
	c = ctl_of(j, n);
	switch (magic) {
	case FP_VALUE:
		if (is_html(n, T_OPTION)) {
			const char *v = dn_attr(n, "value");
			SB b = { 0 };
			if (v != NULL)
				return JS_NewString(ctx, v);
			option_text(n, &b);
			return jb_value(ctx, &b);
		}
		if (c != NULL && c->kind == FC_SELECT) {
			int i;
			for (i = 0; i < c->nopt; i++)
				if (c->sel[i]) {
					const char *v = dn_attr(c->opt[i], "value");
					SB b = { 0 };
					if (v != NULL)
						return JS_NewString(ctx, v);
					option_text(c->opt[i], &b);
					return jb_value(ctx, &b);
				}
			return JS_NewString(ctx, "");
		}
		if (c != NULL && (c->kind == FC_CHECKBOX || c->kind == FC_RADIO)) {
			const char *v = dn_attr(n, "value");
			return JS_NewString(ctx, v ? v : "on");
		}
		if (c != NULL && c->v != NULL)
			return JS_NewStringLen(ctx, c->v, (size_t) c->len);
		{
			const char *v = dn_attr(n, "value");
			if (is_html(n, T_TEXTAREA)) {
				SB b = { 0 };
				text_of(n, &b);
				return jb_value(ctx, &b);
			}
			return JS_NewString(ctx, v ? v : "");
		}
	case FP_DEFAULT: {
		const char *v = dn_attr(n, "value");
		return JS_NewString(ctx, v ? v : "");
	}
	case FP_CHECKED:
		return JS_NewBool(ctx, c != NULL ? c->checked : dn_attr(n, "checked") != NULL);
	case FP_TYPE: {
		const char *t = dn_attr(n, "type");
		char b[24];
		size_t i;
		if (is_html(n, T_SELECT))
			return JS_NewString(ctx, dn_attr(n, "multiple") ? "select-multiple" : "select-one");
		if (is_html(n, T_TEXTAREA))
			return JS_NewString(ctx, "textarea");
		if (is_html(n, T_BUTTON))
			return JS_NewString(ctx, t && (lower_eq(t, "button") || lower_eq(t, "reset")) ?
					   (lower_eq(t, "button") ? "button" : "reset") : "submit");
		if (!is_html(n, T_INPUT))
			return JS_NewString(ctx, t ? t : "");
		if (t == NULL || *t == 0)
			return JS_NewString(ctx, "text");
		for (i = 0; t[i] && i < sizeof b - 1; i++)
			b[i] = t[i] >= 'A' && t[i] <= 'Z' ? (char) (t[i] + 32) : t[i];
		b[i] = 0;
		return JS_NewString(ctx, b);
	}
	case FP_SEL_INDEX: {
		int i;
		if (c == NULL || c->kind != FC_SELECT)
			return JS_NewInt32(ctx, -1);
		for (i = 0; i < c->nopt; i++)
			if (c->sel[i])
				return JS_NewInt32(ctx, i);
		return JS_NewInt32(ctx, -1);
	}
	case FP_OPTIONS: {
		JSValue a = JS_NewArray(ctx);
		dnode *k;
		uint32_t i = 0;
		for (k = n->first; k != NULL; k = next_in(k, n, 1))
			if (is_html(k, T_OPTION))
				JS_SetPropertyUint32(ctx, a, i++, wrap(ctx, k));
		return a;
	}
	case FP_LENGTH: {
		int32_t i = 0;
		dnode *k;
		if (is_html(n, T_FORM)) {
			fctl *f;
			for (f = j->d->ctls; f != NULL; f = f->next)
				if (f->form == n)
					i++;
			return JS_NewInt32(ctx, i);
		}
		for (k = n->first; k != NULL; k = next_in(k, n, 1))
			if (is_html(k, T_OPTION))
				i++;
		return JS_NewInt32(ctx, i);
	}
	case FP_FORM: {
		dnode *p;
		if (c != NULL)
			return wrap(ctx, c->form);
		for (p = n->parent; p != NULL && !is_html(p, T_FORM); p = p->parent)
			;
		return wrap(ctx, p);
	}
	case FP_TEXT: {
		SB b = { 0 };
		option_text(n, &b);
		return jb_value(ctx, &b);
	}
	case FP_SELECTED:
	case FP_INDEX: {
		dnode *s = select_of(n);
		fctl *sc = s ? ctl_of(j, s) : NULL;
		int i;
		if (sc == NULL)
			return magic == FP_INDEX ? JS_NewInt32(ctx, 0) : JS_NewBool(ctx, dn_attr(n, "selected") != NULL);
		for (i = 0; i < sc->nopt; i++)
			if (sc->opt[i] == n)
				return magic == FP_INDEX ? JS_NewInt32(ctx, i) : JS_NewBool(ctx, sc->sel[i]);
		return magic == FP_INDEX ? JS_NewInt32(ctx, 0) : JS_FALSE;
	}
	case FP_ELEMENTS: {
		JSValue a = JS_NewArray(ctx);
		fctl *f;
		uint32_t i = 0;
		doc_refresh(j->d);
		j->stale = 0;
		for (f = j->d->ctls; f != NULL; f = f->next)
			if (f->form == n) {
				JSValue w = wrap(ctx, f->n);
				const char *name = dn_attr(f->n, "name");
				if (name != NULL && *name)
					JS_SetPropertyStr(ctx, a, name, JS_DupValue(ctx, w));
				JS_SetPropertyUint32(ctx, a, i++, w);
			}
		return a;
	}
	}
	return JS_UNDEFINED;
}

static JSValue fprop_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	ejs *j = J(ctx);
	fctl *c;
	THIS_ELEM(n);
	c = ctl_of(j, n);
	dirty(j);
	switch (magic) {
	case FP_VALUE: {
		size_t len;
		char *s = cstr(ctx, v, &len);
		if (s == NULL)
			return JS_EXCEPTION;
		if (c != NULL && c->kind == FC_SELECT) {
			int i;
			for (i = 0; i < c->nopt; i++) {
				const char *ov = dn_attr(c->opt[i], "value");
				char t[512];
				if (ov == NULL) {
					form_option_text(c->opt[i], t, sizeof t);
					ov = t;
				}
				c->sel[i] = strcmp(ov, s) == 0;
				if (c->sel[i] && !c->multiple) {
					int k;
					for (k = i + 1; k < c->nopt; k++)
						c->sel[k] = 0;
					break;
				}
			}
		} else if (c != NULL && c->kind != FC_CHECKBOX && c->kind != FC_RADIO && c->kind != FC_SUBMIT &&
			   c->kind != FC_RESET && c->kind != FC_BUTTON && c->kind != FC_IMAGE) {
			form_set(c, s, (long) len);
		} else {
			JSValue r = set_attr(ctx, n, "value", s, len);
			free(s);
			return r;
		}
		free(s);
		return JS_UNDEFINED;
	}
	case FP_DEFAULT: {
		size_t len;
		char *s = cstr(ctx, v, &len);
		JSValue r;
		if (s == NULL)
			return JS_EXCEPTION;
		r = set_attr(ctx, n, "value", s, len);
		free(s);
		return r;
	}
	case FP_CHECKED: {
		int on = JS_ToBool(ctx, v);
		if (c == NULL)
			return on ? set_attr(ctx, n, "checked", "", 0) : JS_UNDEFINED;
		if (c->kind == FC_RADIO && on && !c->checked)
			form_check(j->d, c);
		else if (c->kind == FC_CHECKBOX)
			c->checked = (uint8_t) on;
		else if (c->kind == FC_RADIO)
			c->checked = (uint8_t) on;
		return JS_UNDEFINED;
	}
	case FP_SEL_INDEX: {
		int32_t k = -1, i;
		JS_ToInt32(ctx, &k, v);
		if (c == NULL || c->kind != FC_SELECT)
			return JS_UNDEFINED;
		for (i = 0; i < c->nopt; i++)
			c->sel[i] = i == k;
		return JS_UNDEFINED;
	}
	case FP_SELECTED: {
		dnode *s = select_of(n);
		fctl *sc = s ? ctl_of(j, s) : NULL;
		int i, on = JS_ToBool(ctx, v);
		if (sc == NULL)
			return JS_UNDEFINED;
		for (i = 0; i < sc->nopt; i++)
			if (sc->opt[i] == n)
				sc->sel[i] = (uint8_t) on;
			else if (on && !sc->multiple)
				sc->sel[i] = 0;
		return JS_UNDEFINED;
	}
	}
	return JS_UNDEFINED;
}

/* where a script sent the browser forgotten, before it sends it elsewhere */
static void nav_clear(ejs *j)
{
	free(j->nav);
	free(j->nav_body);
	free(j->nav_dl);
	j->nav = j->nav_body = j->nav_dl = NULL;
	j->nav_len = 0;
}

/* click(), focus(), blur(), submit(), reset() (magic) */
enum { EC_CLICK, EC_FOCUS, EC_BLUR, EC_SUBMIT, EC_RESET };
static int fire(ejs *j, dnode *n, const char *type, int bubbles, int cancelable);
static void send_form(ejs *j, dnode *form, fctl *submitter);

static JSValue el_act(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	fctl *c;
	dnode *det;
	THIS_ELEM(n);
	switch (magic) {
	case EC_CLICK:
		c = ctl_of(j, n);
		if (c != NULL && c->disabled)
			return JS_UNDEFINED;
		if (!fire(j, n, "click", 1, 1)) {
			/* the default action a script may ask for */
			if (c != NULL && (c->kind == FC_CHECKBOX || c->kind == FC_RADIO)) {
				form_check(j->d, c);
				dirty(j);
				fire(j, n, "input", 1, 0);
				fire(j, n, "change", 1, 0);
			} else if (c != NULL && (c->kind == FC_SUBMIT || c->kind == FC_IMAGE) && c->form != NULL) {
				if (!fire(j, c->form, "submit", 1, 1))
					send_form(j, c->form, c);
			} else if (is_html(n, T_A) && dn_attr(n, "href") != NULL && j->h.resolve != NULL) {
				char *out = malloc(URL_MAX);
				if (out != NULL) {
					j->h.resolve(j->h.ud, j->base, dn_attr(n, "href"), out, URL_MAX, 0);
					if (out[0] && strncmp(out, "javascript:", 11) != 0) {
						/* a link with a download attribute is
						   downloaded however it is activated
						   (the HTML Standard, 4.6.5): src/browse
						   saves it, as a click on it */
						const char *dl = dn_attr(n, "download");
						nav_clear(j);
						j->nav = out;
						j->nav_dl = dl != NULL ? strdup(dl) : NULL;
						out = NULL;
					}
					free(out);
				}
			} else if ((det = summary_of(j, n)) != NULL && dn_details_toggle(j->d, det, toggle_cb, j)) {
				dirty(j);	/* a details opened or closed */
			}
		}
		return JS_UNDEFINED;
	case EC_FOCUS:
	case EC_BLUR:
		c = ctl_of(j, n);
		if (c != NULL) {
			fctl *k;
			for (k = j->d->ctls; k != NULL; k = k->next)
				k->focus = 0;
			c->focus = magic == EC_FOCUS;
			dirty(j);
		}
		return JS_UNDEFINED;
	case EC_SUBMIT:
		if (is_html(n, T_FORM))
			send_form(j, n, NULL);
		return JS_UNDEFINED;
	case EC_RESET:
		if (is_html(n, T_FORM)) {
			form_reset(j->d, n);
			dirty(j);
		}
		return JS_UNDEFINED;
	}
	return JS_UNDEFINED;
}

static JSValue el_offset_parent(JSContext *ctx, JSValueConst this_val)
{
	THIS_ELEM(n);
	return wrap(ctx, J(ctx)->d->body);
}

static JSValue zero(JSContext *ctx, JSValueConst this_val)
{
	return JS_NewInt32(ctx, 0);
}

static JSValue ignore_set(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
	return JS_UNDEFINED;
}

#define R_DEF(js, a, k) JS_CGETSET_MAGIC_DEF(#js, reflect_get, reflect_set, R_##js),
static const JSCFunctionListEntry elem_funcs[] = {
	REFLECT(R_DEF)
	JS_CGETSET_MAGIC_DEF("tagName", html_get, NULL, EH_TAG),
	JS_CGETSET_MAGIC_DEF("localName", html_get, NULL, EH_LOCAL),
	JS_CGETSET_MAGIC_DEF("innerHTML", html_get, html_set, EH_INNER),
	JS_CGETSET_MAGIC_DEF("outerHTML", html_get, html_set, EH_OUTER),
	JS_CGETSET_MAGIC_DEF("innerText", html_get, html_set, EH_INNERTEXT),
	JS_CGETSET_DEF("attributes", el_attributes, NULL),
	JS_CFUNC_DEF("getAttribute", 1, el_get_attr),
	JS_CFUNC_DEF("setAttribute", 2, el_set_attr),
	JS_CFUNC_DEF("removeAttribute", 1, el_remove_attr),
	JS_CFUNC_DEF("hasAttribute", 1, el_has_attr),
	JS_CFUNC_DEF("toggleAttribute", 2, el_toggle_attr),
	JS_CFUNC_DEF("getAttributeNames", 0, el_attr_names),
	JS_CFUNC_MAGIC_DEF("insertAdjacentHTML", 2, el_adjacent, 0),
	JS_CFUNC_MAGIC_DEF("insertAdjacentElement", 2, el_adjacent, 1),
	JS_CFUNC_MAGIC_DEF("insertAdjacentText", 2, el_adjacent, 2),
	JS_CFUNC_MAGIC_DEF("matches", 1, el_matches, 0),
	JS_CFUNC_MAGIC_DEF("closest", 1, el_matches, 1),
	JS_CGETSET_DEF("classList", el_class_list, NULL),
	JS_CGETSET_DEF("style", el_style, el_set_style),
	JS_CGETSET_DEF("dataset", el_dataset, NULL),
	JS_CFUNC_DEF("getBoundingClientRect", 0, el_rect_fn),
	JS_CGETSET_MAGIC_DEF("offsetTop", el_rect, NULL, RECT_TOP),
	JS_CGETSET_MAGIC_DEF("offsetLeft", el_rect, NULL, RECT_LEFT),
	JS_CGETSET_MAGIC_DEF("offsetWidth", el_rect, NULL, RECT_WIDTH),
	JS_CGETSET_MAGIC_DEF("offsetHeight", el_rect, NULL, RECT_HEIGHT),
	JS_CGETSET_MAGIC_DEF("clientWidth", el_rect, NULL, RECT_WIDTH),
	JS_CGETSET_MAGIC_DEF("clientHeight", el_rect, NULL, RECT_HEIGHT),
	JS_CGETSET_MAGIC_DEF("scrollWidth", el_rect, NULL, RECT_WIDTH),
	JS_CGETSET_MAGIC_DEF("scrollHeight", el_rect, NULL, RECT_HEIGHT),
	JS_CGETSET_DEF("offsetParent", el_offset_parent, NULL),
	JS_CGETSET_DEF("scrollTop", zero, ignore_set),
	JS_CGETSET_DEF("scrollLeft", zero, ignore_set),
	JS_CFUNC_DEF("scrollIntoView", 0, node_noop),
	JS_CFUNC_MAGIC_DEF("click", 0, el_act, EC_CLICK),
	JS_CFUNC_MAGIC_DEF("focus", 0, el_act, EC_FOCUS),
	JS_CFUNC_MAGIC_DEF("blur", 0, el_act, EC_BLUR),
	JS_CFUNC_MAGIC_DEF("submit", 0, el_act, EC_SUBMIT),
	JS_CFUNC_MAGIC_DEF("requestSubmit", 0, el_act, EC_SUBMIT),
	JS_CFUNC_MAGIC_DEF("reset", 0, el_act, EC_RESET),
	JS_CGETSET_MAGIC_DEF("value", fprop_get, fprop_set, FP_VALUE),
	JS_CGETSET_MAGIC_DEF("defaultValue", fprop_get, fprop_set, FP_DEFAULT),
	JS_CGETSET_MAGIC_DEF("checked", fprop_get, fprop_set, FP_CHECKED),
	JS_CGETSET_MAGIC_DEF("type", fprop_get, NULL, FP_TYPE),
	JS_CGETSET_MAGIC_DEF("selectedIndex", fprop_get, fprop_set, FP_SEL_INDEX),
	JS_CGETSET_MAGIC_DEF("options", fprop_get, NULL, FP_OPTIONS),
	JS_CGETSET_MAGIC_DEF("form", fprop_get, NULL, FP_FORM),
	JS_CGETSET_MAGIC_DEF("text", fprop_get, NULL, FP_TEXT),
	JS_CGETSET_MAGIC_DEF("selected", fprop_get, fprop_set, FP_SELECTED),
	JS_CGETSET_MAGIC_DEF("index", fprop_get, NULL, FP_INDEX),
	JS_CGETSET_MAGIC_DEF("elements", fprop_get, NULL, FP_ELEMENTS),
	JS_CGETSET_MAGIC_DEF("length", fprop_get, NULL, FP_LENGTH),
};

/* ---------------------------------------------------------------- computed style */

static JSClassID comp_class;

/* styles as they are now: the page's first styling may not have been
   made yet (its scripts run before), or a script changed it since */
static void restyle_now(ejs *j)
{
	if (j->styled == 0 || j->styled != j->edits + 1) {
		doc_refresh(j->d);
		j->stale = 0;
		ec_unstyle(j->d->css);
		doc_select(j->d);
		j->styled = j->edits + 1;
	}
}

static void computed_text(const ec_style *s, int p, char *out, size_t size)
{
	int32_t v = s->v[p].i;
	out[0] = 0;
	switch (s->k[p]) {
	case EK_KW:
		snprintf(out, size, "%s", ec_kw_name(v) ? ec_kw_name(v) : "");
		break;
	case EK_PX:
		if (v % 1024 == 0)
			snprintf(out, size, "%ldpx", (long) (v / 1024));
		else
			snprintf(out, size, "%ld.%02ldpx", (long) (v / 1024), (long) ((v < 0 ? -v : v) % 1024 * 100 / 1024));
		break;
	case EK_PCT:
		snprintf(out, size, "%ld%%", (long) (v / 1024));
		break;
	case EK_NUM:
		if (v % 1024 == 0)
			snprintf(out, size, "%ld", (long) (v / 1024));
		else
			snprintf(out, size, "%ld.%02ld", (long) (v / 1024), (long) ((v < 0 ? -v : v) % 1024 * 100 / 1024));
		break;
	case EK_INT:
		snprintf(out, size, "%ld", (long) v);
		break;
	case EK_CURCOLOR:
		v = s->v[EP_COLOR].i;
		/* fall through */
	case EK_COLOR: {
		unsigned long c = (unsigned long) (uint32_t) v;
		if ((c >> 24) == 255)
			snprintf(out, size, "rgb(%lu, %lu, %lu)", (c >> 16) & 255, (c >> 8) & 255, c & 255);
		else
			snprintf(out, size, "rgba(%lu, %lu, %lu, %lu.%02lu)", (c >> 16) & 255, (c >> 8) & 255, c & 255,
				 (c >> 24) / 255, (c >> 24) % 255 * 100 / 255);
		break;
	}
	}
}

static JSValue computed(JSContext *ctx, JSValueConst this_val, const char *prop)
{
	ejs *j = J(ctx);
	dnode *n = JS_GetOpaque(this_val, comp_class);
	char out[64];
	int p;
	if (n == NULL)
		return JS_NewString(ctx, "");
	restyle_now(j);
	if (n->e.style == NULL)
		return JS_NewString(ctx, n->type == DN_ELEMENT && !strcmp(prop, "display") ? "none" : "");
	for (p = 0; p < EC_NPROP; p++)
		if (!strcmp(ec_prop_name(p), prop))
			break;
	if (p == EC_NPROP)
		return JS_NewString(ctx, "");
	computed_text(n->e.style, p, out, sizeof out);
	return JS_NewString(ctx, out);
}

static JSValue comp_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	return computed(ctx, this_val, sp_css[magic]);
}

static JSValue comp_value(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *p;
	JSValue r;
	if (argc < 1 || (p = attr_name(ctx, argv[0])) == NULL)
		return JS_EXCEPTION;
	r = computed(ctx, this_val, p);
	free(p);
	return r;
}

#define CP_DEF(js, css) JS_CGETSET_MAGIC_DEF(#js, comp_get, NULL, SP_##js),
static const JSCFunctionListEntry comp_funcs[] = {
	STYLE_PROPS(CP_DEF)
	JS_CFUNC_DEF("getPropertyValue", 1, comp_value),
};

static JSValue get_computed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dnode *n = argc > 0 ? elem_of(argv[0]) : NULL;
	JSValue o;
	if (n == NULL)
		return JS_ThrowTypeError(ctx, "getComputedStyle: not an element");
	o = JS_NewObjectProtoClass(ctx, J(ctx)->comp_proto, comp_class);
	if (!JS_IsException(o))
		JS_SetOpaque(o, n);
	return o;
}

/* ---------------------------------------------------------------- Document */

static JSValue doc_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	char *id;
	dnode *n;
	if (argc < 1 || (id = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_EXCEPTION;
	n = *id ? by_id(j, j->d->root, id) : NULL;
	free(id);
	return wrap(ctx, n);
}

static JSValue doc_by_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char *name;
	JSValue r;
	if (argc < 1 || (name = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_EXCEPTION;
	r = collect(ctx, J(ctx)->d->root, want_name, name);
	free(name);
	return r;
}

static JSValue doc_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	size_t len;
	char *s;
	dnode *n;
	if (magic == 3) {
		n = new_node(j->d, DN_DOCUMENT);
		return n ? wrap(ctx, n) : JS_ThrowOutOfMemory(ctx);
	}
	if (argc < 1 + (magic == 4) || (s = cstr(ctx, argv[magic == 4 ? 1 : 0], &len)) == NULL)
		return JS_ThrowTypeError(ctx, "a name or text is needed");
	if (magic == 0 || magic == 4) {
		char *p;
		for (p = s; *p; p++)
			if (*p >= 'A' && *p <= 'Z')
				*p = (char) (*p + 32);
		n = html_element(j->d, s, len);
	} else {
		n = new_text(j->d, magic == 1 ? DN_TEXT : DN_OTHER, s, len);
	}
	free(s);
	return n ? wrap(ctx, n) : JS_ThrowOutOfMemory(ctx);
}

enum { DP_ELEMENT, DP_HEAD, DP_BODY, DP_TITLE, DP_URL, DP_COOKIE, DP_STATE, DP_REFERRER, DP_CURRENT,
       DP_FORMS, DP_IMAGES, DP_LINKS, DP_SCRIPTS, DP_CHARSET, DP_MODE, DP_ACTIVE, DP_DOMAIN, DP_VISIBILITY,
       DP_HIDDEN, DP_LOCATION, DP_VIEW };

static int want_link(const dnode *n, const void *arg)
{
	(void) arg;
	return (n->tag == T_A || want_tag(n, "area")) && dn_attr(n, "href") != NULL;
}

static int want_html_tag(const dnode *n, const void *arg)
{
	return n->ns == DN_NS_HTML && n->tag == *(const int *) arg;
}

static JSValue doc_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	doc *d = j->d;
	switch (magic) {
	case DP_ELEMENT:
		return wrap(ctx, d->html);
	case DP_HEAD: {
		dnode *n;
		for (n = d->html ? d->html->first : NULL; n != NULL && !is_html(n, T_HEAD); n = n->next)
			;
		return wrap(ctx, n);
	}
	case DP_BODY:
		if (d->body == NULL || d->body->parent == NULL)
			fresh(j);
		return wrap(ctx, d->body);
	case DP_TITLE:
		return JS_NewString(ctx, d->title);
	case DP_URL:
		return JS_NewString(ctx, j->url);
	case DP_COOKIE: {
		char *out = malloc(4096);
		JSValue r;
		if (out == NULL)
			return JS_ThrowOutOfMemory(ctx);
		out[0] = 0;
		if (j->h.cookie_get != NULL)
			j->h.cookie_get(j->h.ud, j->url, out, 4096);
		r = JS_NewString(ctx, out);
		free(out);
		return r;
	}
	case DP_STATE:
		return JS_NewString(ctx, j->loaded ? "complete" : j->current ? "loading" : "interactive");
	case DP_REFERRER:
		return JS_NewString(ctx, "");
	case DP_CURRENT:
		return wrap(ctx, j->current);
	case DP_FORMS: {
		int t = T_FORM;
		return collect(ctx, d->root, want_html_tag, &t);
	}
	case DP_IMAGES: {
		int t = T_IMG;
		return collect(ctx, d->root, want_html_tag, &t);
	}
	case DP_LINKS:
		return collect(ctx, d->root, want_link, NULL);
	case DP_SCRIPTS: {
		int t = T_SCRIPT;
		return collect(ctx, d->root, want_html_tag, &t);
	}
	case DP_CHARSET:
		return JS_NewString(ctx, d->charset[0] ? d->charset : "UTF-8");
	case DP_MODE:
		return JS_NewString(ctx, d->quirks == 1 ? "BackCompat" : "CSS1Compat");
	case DP_ACTIVE: {
		fctl *c;
		for (c = d->ctls; c != NULL; c = c->next)
			if (c->focus)
				return wrap(ctx, c->n);
		return wrap(ctx, d->body);
	}
	case DP_DOMAIN: {
		URL *u = malloc(sizeof *u);
		JSValue r;
		char host[256] = "";
		if (u == NULL)
			return JS_ThrowOutOfMemory(ctx);
		if (url_parse(u, j->url, NULL))
			url_host(u, host, sizeof host);
		free(u);
		r = JS_NewString(ctx, host);
		return r;
	}
	case DP_VISIBILITY:
		return JS_NewString(ctx, "visible");
	case DP_HIDDEN:
		return JS_FALSE;
	case DP_LOCATION:
		return JS_DupValue(ctx, j->loc_obj);
	case DP_VIEW:
		return JS_GetGlobalObject(ctx);
	}
	return JS_UNDEFINED;
}

static JSValue doc_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	ejs *j = J(ctx);
	char *s;
	if ((s = cstr(ctx, v, NULL)) == NULL)
		return JS_EXCEPTION;
	if (magic == DP_TITLE) {
		str_copy(j->d->title, s, sizeof j->d->title);
		dirty(j);
	} else if (magic == DP_COOKIE && j->h.cookie_set != NULL) {
		j->h.cookie_set(j->h.ud, j->url, s);
	} else if (magic == DP_LOCATION) {
		JSValue r = JS_SetPropertyStr(ctx, j->loc_obj, "href", JS_DupValue(ctx, v)) < 0 ? JS_EXCEPTION :
			    JS_UNDEFINED;
		free(s);
		return r;
	}
	free(s);
	return JS_UNDEFINED;
}

/* document.write: while the page's own scripts run, what is written goes
   in after the script (and what it wrote before); later it does nothing */
static JSValue doc_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	SB b = { 0 };
	int i;
	dnode *f, *p, *c;
	if (j->loaded || j->write_after == NULL || (p = j->write_after->parent) == NULL)
		return JS_UNDEFINED;
	for (i = 0; i < argc; i++) {
		size_t len;
		char *s = cstr(ctx, argv[i], &len);
		if (s == NULL)
			return JS_EXCEPTION;
		jb_add(&b, s, len);
		free(s);
	}
	if (magic)
		jb_str(&b, "\n");
	if (b.fail || b.n == 0) {
		free(b.s);
		return b.fail ? JS_ThrowOutOfMemory(ctx) : JS_UNDEFINED;
	}
	f = html_fragment(j->d, p->type == DN_ELEMENT ? "body" : "body", DN_NS_HTML, b.s, b.n, 1);
	free(b.s);
	if (f == NULL)
		return JS_ThrowOutOfMemory(ctx);
	while ((c = f->first) != NULL) {
		unlink_node(j, c);
		link_node(j, p, c, j->write_after->next);
		j->write_after = c;
	}
	return JS_UNDEFINED;
}

static JSValue doc_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_TRUE;
}

static JSValue doc_create_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static const JSCFunctionListEntry doc_funcs[] = {
	JS_CFUNC_DEF("getElementById", 1, doc_by_id),
	JS_CFUNC_DEF("getElementsByName", 1, doc_by_name),
	JS_CFUNC_MAGIC_DEF("createElement", 1, doc_create, 0),
	JS_CFUNC_MAGIC_DEF("createElementNS", 2, doc_create, 4),
	JS_CFUNC_MAGIC_DEF("createTextNode", 1, doc_create, 1),
	JS_CFUNC_MAGIC_DEF("createComment", 1, doc_create, 2),
	JS_CFUNC_MAGIC_DEF("createDocumentFragment", 0, doc_create, 3),
	JS_CFUNC_DEF("createEvent", 1, doc_create_event),
	JS_CFUNC_MAGIC_DEF("write", 1, doc_write, 0),
	JS_CFUNC_MAGIC_DEF("writeln", 1, doc_write, 1),
	JS_CFUNC_DEF("open", 0, node_noop),
	JS_CFUNC_DEF("close", 0, node_noop),
	JS_CFUNC_DEF("hasFocus", 0, doc_true),
	JS_CGETSET_MAGIC_DEF("documentElement", doc_get, NULL, DP_ELEMENT),
	JS_CGETSET_MAGIC_DEF("head", doc_get, NULL, DP_HEAD),
	JS_CGETSET_MAGIC_DEF("body", doc_get, NULL, DP_BODY),
	JS_CGETSET_MAGIC_DEF("title", doc_get, doc_set, DP_TITLE),
	JS_CGETSET_MAGIC_DEF("URL", doc_get, NULL, DP_URL),
	JS_CGETSET_MAGIC_DEF("documentURI", doc_get, NULL, DP_URL),
	JS_CGETSET_MAGIC_DEF("cookie", doc_get, doc_set, DP_COOKIE),
	JS_CGETSET_MAGIC_DEF("readyState", doc_get, NULL, DP_STATE),
	JS_CGETSET_MAGIC_DEF("referrer", doc_get, NULL, DP_REFERRER),
	JS_CGETSET_MAGIC_DEF("currentScript", doc_get, NULL, DP_CURRENT),
	JS_CGETSET_MAGIC_DEF("forms", doc_get, NULL, DP_FORMS),
	JS_CGETSET_MAGIC_DEF("images", doc_get, NULL, DP_IMAGES),
	JS_CGETSET_MAGIC_DEF("links", doc_get, NULL, DP_LINKS),
	JS_CGETSET_MAGIC_DEF("scripts", doc_get, NULL, DP_SCRIPTS),
	JS_CGETSET_MAGIC_DEF("characterSet", doc_get, NULL, DP_CHARSET),
	JS_CGETSET_MAGIC_DEF("charset", doc_get, NULL, DP_CHARSET),
	JS_CGETSET_MAGIC_DEF("compatMode", doc_get, NULL, DP_MODE),
	JS_CGETSET_MAGIC_DEF("activeElement", doc_get, NULL, DP_ACTIVE),
	JS_CGETSET_MAGIC_DEF("domain", doc_get, NULL, DP_DOMAIN),
	JS_CGETSET_MAGIC_DEF("visibilityState", doc_get, NULL, DP_VISIBILITY),
	JS_CGETSET_MAGIC_DEF("hidden", doc_get, NULL, DP_HIDDEN),
	JS_CGETSET_MAGIC_DEF("location", doc_get, doc_set, DP_LOCATION),
	JS_CGETSET_MAGIC_DEF("defaultView", doc_get, NULL, DP_VIEW),
};

/* ---------------------------------------------------------------- events */

typedef struct jsev {
	char type[24];
	uint8_t bubbles, cancelable, prevented, stop, stopnow, phase, trusted;
	dnode *target, *current;
	uint8_t target_window, current_window;
} jsev;

static void ev_finalizer(JSRuntime *rt, JSValue v)
{
	free(JS_GetOpaque(v, ev_class));
}

static jsev *ev_of(JSValueConst v)
{
	return JS_GetOpaque(v, ev_class);
}

static JSValue new_event(JSContext *ctx, const char *type, int bubbles, int cancelable)
{
	ejs *j = J(ctx);
	jsev *e = calloc(1, sizeof *e);
	JSValue o;
	if (e == NULL)
		return JS_ThrowOutOfMemory(ctx);
	str_copy(e->type, type, sizeof e->type);
	e->bubbles = (uint8_t) bubbles;
	e->cancelable = (uint8_t) cancelable;
	o = JS_NewObjectProtoClass(ctx, j->ev_proto, ev_class);
	if (JS_IsException(o)) {
		free(e);
		return o;
	}
	JS_SetOpaque(o, e);
	return o;
}

/* new Event(type, {bubbles, cancelable}), CustomEvent (detail) and the rest */
static JSValue ev_construct(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
	char *type;
	int bubbles = 0, cancelable = 0;
	JSValue o;
	if (argc < 1 || (type = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_ThrowTypeError(ctx, "Event: a type is needed");
	if (argc > 1 && JS_IsObject(argv[1])) {
		JSValue b = JS_GetPropertyStr(ctx, argv[1], "bubbles"), c = JS_GetPropertyStr(ctx, argv[1], "cancelable");
		bubbles = JS_ToBool(ctx, b) > 0;
		cancelable = JS_ToBool(ctx, c) > 0;
		JS_FreeValue(ctx, b);
		JS_FreeValue(ctx, c);
	}
	o = new_event(ctx, type, bubbles, cancelable);
	free(type);
	if (!JS_IsException(o) && argc > 1 && JS_IsObject(argv[1])) {
		JSValue dt = JS_GetPropertyStr(ctx, argv[1], "detail");
		JS_SetPropertyStr(ctx, o, "detail", JS_IsUndefined(dt) ? JS_NULL : dt);
	}
	return o;
}

/* new ToggleEvent(type, {oldState, newState}): the two "" when not given
   (the HTML Standard, 6.13, ToggleEventInit) */
static JSValue toggle_construct(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
	static const char *const names[] = { "oldState", "newState" };
	JSValue o = ev_construct(ctx, new_target, argc, argv);
	int i;
	for (i = 0; i < 2 && !JS_IsException(o); i++) {
		JSValue v = argc > 1 && JS_IsObject(argv[1]) ? JS_GetPropertyStr(ctx, argv[1], names[i]) : JS_UNDEFINED;
		if (JS_IsException(v)) {
			JS_FreeValue(ctx, o);
			return v;
		}
		if (JS_IsUndefined(v)) {
			v = JS_NewString(ctx, "");
		} else {
			JSValue t = JS_ToString(ctx, v);
			JS_FreeValue(ctx, v);
			if (JS_IsException(t)) {
				JS_FreeValue(ctx, o);
				return t;
			}
			v = t;
		}
		JS_SetPropertyStr(ctx, o, names[i], v);
	}
	return o;
}

static JSValue doc_create_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return new_event(ctx, "", 0, 0);
}

enum { EV_TYPE, EV_BUBBLES, EV_CANCELABLE, EV_PREVENTED, EV_PHASE, EV_TARGET, EV_CURRENT, EV_TRUSTED, EV_TIME };

static JSValue ev_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	jsev *e = ev_of(this_val);
	if (e == NULL)
		return JS_UNDEFINED;
	switch (magic) {
	case EV_TYPE:
		return JS_NewString(ctx, e->type);
	case EV_BUBBLES:
		return JS_NewBool(ctx, e->bubbles);
	case EV_CANCELABLE:
		return JS_NewBool(ctx, e->cancelable);
	case EV_PREVENTED:
		return JS_NewBool(ctx, e->prevented);
	case EV_PHASE:
		return JS_NewInt32(ctx, e->phase);
	case EV_TARGET:
		return e->target_window ? JS_GetGlobalObject(ctx) : wrap(ctx, e->target);
	case EV_CURRENT:
		return e->current_window ? JS_GetGlobalObject(ctx) : wrap(ctx, e->current);
	case EV_TRUSTED:
		return JS_NewBool(ctx, e->trusted);
	case EV_TIME:
		return JS_NewInt32(ctx, (int32_t) pf_ms());
	}
	return JS_UNDEFINED;
}

static JSValue ev_set_return(JSContext *ctx, JSValueConst this_val, JSValueConst v)
{
	jsev *e = ev_of(this_val);
	if (e != NULL && e->cancelable && !JS_ToBool(ctx, v))
		e->prevented = 1;
	return JS_UNDEFINED;
}

enum { EVM_PREVENT, EVM_STOP, EVM_STOPNOW, EVM_INIT };

static JSValue ev_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	jsev *e = ev_of(this_val);
	if (e == NULL)
		return JS_UNDEFINED;
	switch (magic) {
	case EVM_PREVENT:
		if (e->cancelable)
			e->prevented = 1;
		break;
	case EVM_STOP:
		e->stop = 1;
		break;
	case EVM_STOPNOW:
		e->stop = e->stopnow = 1;
		break;
	case EVM_INIT:
		if (argc > 0) {
			char *t = cstr(ctx, argv[0], NULL);
			if (t == NULL)
				return JS_EXCEPTION;
			str_copy(e->type, t, sizeof e->type);
			free(t);
		}
		e->bubbles = argc > 1 && JS_ToBool(ctx, argv[1]) > 0;
		e->cancelable = argc > 2 && JS_ToBool(ctx, argv[2]) > 0;
		break;
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry ev_funcs[] = {
	JS_CGETSET_MAGIC_DEF("type", ev_get, NULL, EV_TYPE),
	JS_CGETSET_MAGIC_DEF("bubbles", ev_get, NULL, EV_BUBBLES),
	JS_CGETSET_MAGIC_DEF("cancelable", ev_get, NULL, EV_CANCELABLE),
	JS_CGETSET_MAGIC_DEF("defaultPrevented", ev_get, NULL, EV_PREVENTED),
	JS_CGETSET_MAGIC_DEF("eventPhase", ev_get, NULL, EV_PHASE),
	JS_CGETSET_MAGIC_DEF("target", ev_get, NULL, EV_TARGET),
	JS_CGETSET_MAGIC_DEF("srcElement", ev_get, NULL, EV_TARGET),
	JS_CGETSET_MAGIC_DEF("currentTarget", ev_get, NULL, EV_CURRENT),
	JS_CGETSET_MAGIC_DEF("isTrusted", ev_get, NULL, EV_TRUSTED),
	JS_CGETSET_MAGIC_DEF("timeStamp", ev_get, NULL, EV_TIME),
	JS_CGETSET_DEF("returnValue", NULL, ev_set_return),
	JS_CFUNC_MAGIC_DEF("preventDefault", 0, ev_call, EVM_PREVENT),
	JS_CFUNC_MAGIC_DEF("stopPropagation", 0, ev_call, EVM_STOP),
	JS_CFUNC_MAGIC_DEF("stopImmediatePropagation", 0, ev_call, EVM_STOPNOW),
	JS_CFUNC_MAGIC_DEF("initEvent", 3, ev_call, EVM_INIT),
	JS_PROP_INT32_DEF("NONE", 0, 0),
	JS_PROP_INT32_DEF("CAPTURING_PHASE", 1, 0),
	JS_PROP_INT32_DEF("AT_TARGET", 2, 0),
	JS_PROP_INT32_DEF("BUBBLING_PHASE", 3, 0),
};

/* the node an event listener goes on (NULL with *win: the window) */
static int target_of(JSContext *ctx, JSValueConst this_val, dnode **n)
{
	*n = node_of(this_val);
	return *n != NULL || JS_IsObject(this_val);
}

static int same_fn(JSValueConst a, JSValueConst b)
{
	return JS_VALUE_GET_PTR(a) == JS_VALUE_GET_PTR(b);
}

static void listen_opts(JSContext *ctx, int argc, JSValueConst *argv, int *capture, int *once)
{
	*capture = *once = 0;
	if (argc < 3)
		return;
	if (JS_IsObject(argv[2])) {
		JSValue c = JS_GetPropertyStr(ctx, argv[2], "capture"), o = JS_GetPropertyStr(ctx, argv[2], "once");
		*capture = JS_ToBool(ctx, c) > 0;
		*once = JS_ToBool(ctx, o) > 0;
		JS_FreeValue(ctx, c);
		JS_FreeValue(ctx, o);
	} else {
		*capture = JS_ToBool(ctx, argv[2]) > 0;
	}
}

static JSValue add_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	dnode *n;
	char *type;
	int capture, once;
	listener *l, **tail;
	if (!target_of(ctx, this_val, &n) || argc < 2 || !JS_IsObject(argv[1]))
		return JS_UNDEFINED;
	if ((type = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_EXCEPTION;
	listen_opts(ctx, argc, argv, &capture, &once);
	for (tail = &j->ls; *tail != NULL; tail = &(*tail)->next) {
		l = *tail;
		if (!l->gone && l->n == n && l->capture == capture && !strcmp(l->type, type) && same_fn(l->fn, argv[1])) {
			free(type);
			return JS_UNDEFINED;
		}
	}
	if ((l = calloc(1, sizeof *l)) == NULL) {
		free(type);
		return JS_ThrowOutOfMemory(ctx);
	}
	l->n = n;
	str_copy(l->type, type, sizeof l->type);
	l->fn = JS_DupValue(ctx, argv[1]);
	l->capture = (uint8_t) capture;
	l->once = (uint8_t) once;
	*tail = l;
	free(type);
	return JS_UNDEFINED;
}

static JSValue remove_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	dnode *n;
	char *type;
	int capture, once;
	listener *l;
	if (!target_of(ctx, this_val, &n) || argc < 2)
		return JS_UNDEFINED;
	if ((type = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_EXCEPTION;
	listen_opts(ctx, argc, argv, &capture, &once);
	for (l = j->ls; l != NULL; l = l->next)
		if (!l->gone && l->n == n && l->capture == capture && !strcmp(l->type, type) && same_fn(l->fn, argv[1]))
			l->gone = 1;
	free(type);
	return JS_UNDEFINED;
}

/* the listeners let go after a dispatch */
static void sweep(ejs *j)
{
	listener **p = &j->ls, *l;
	while ((l = *p) != NULL)
		if (l->gone) {
			*p = l->next;
			JS_FreeValue(j->ctx, l->fn);
			free(l);
		} else
			p = &l->next;
}

static void call(ejs *j, JSValueConst fn, JSValueConst thisv, JSValueConst ev, jsev *e)
{
	JSContext *ctx = j->ctx;
	JSValue r;
	if (JS_IsFunction(ctx, fn)) {
		r = JS_Call(ctx, fn, thisv, 1, &ev);
	} else {
		JSValue h = JS_GetPropertyStr(ctx, fn, "handleEvent");
		r = JS_IsFunction(ctx, h) ? JS_Call(ctx, h, fn, 1, &ev) : JS_UNDEFINED;
		JS_FreeValue(ctx, h);
	}
	if (JS_IsException(r))
		report(j);
	else if (e != NULL && JS_IsBool(r) && !JS_ToBool(ctx, r) && e->cancelable)
		e->prevented = 1;	/* an on... handler that returned false */
	JS_FreeValue(ctx, r);
}

/* the listeners of one target in one phase (capture 1: those for the
   capturing phase; 0 those for the others) */
static void at_target(ejs *j, JSValueConst ev, jsev *e, dnode *n, int capture, int both)
{
	JSContext *ctx = j->ctx;
	listener *l;
	JSValue thisv = n ? wrap(ctx, n) : JS_GetGlobalObject(ctx);
	e->current = n;
	e->current_window = n == NULL;
	for (l = j->ls; l != NULL && !e->stopnow && !j->over; l = l->next) {
		if (l->gone || l->n != n || strcmp(l->type, e->type) != 0 || (!both && l->capture != capture))
			continue;
		if (l->once)
			l->gone = 1;
		{
			JSValue fn = JS_DupValue(ctx, l->fn);
			call(j, fn, thisv, ev, NULL);
			JS_FreeValue(ctx, fn);
		}
	}
	/* the on... handler: a property, or the attribute made into one */
	if (!capture && !e->stopnow && !j->over) {
		char prop[32];
		const char *attr = NULL;
		JSValue h;
		snprintf(prop, sizeof prop, "on%s", e->type);
		if (n != NULL)
			attr = dn_attr(n, prop);
		else if (j->d->body != NULL && (!strcmp(e->type, "load") || !strcmp(e->type, "unload")))
			attr = dn_attr(j->d->body, prop);
		if (n != NULL && attr == NULL && map_find(j, n) == NULL) {
			JS_FreeValue(ctx, thisv);
			return;
		}
		h = JS_GetPropertyStr(ctx, thisv, prop);
		if (JS_IsUndefined(h) && attr != NULL) {
			SB b = { 0 };
			jb_str(&b, "(function(event){");
			jb_str(&b, attr);
			jb_str(&b, "\n})");
			if (!b.fail) {
				h = JS_Eval(ctx, b.s, b.n, prop, JS_EVAL_TYPE_GLOBAL);
				if (JS_IsException(h)) {
					report(j);
					h = JS_NULL;
				} else
					JS_SetPropertyStr(ctx, thisv, prop, JS_DupValue(ctx, h));
			}
			free(b.s);
		}
		if (JS_IsFunction(ctx, h))
			call(j, h, thisv, ev, e);
		JS_FreeValue(ctx, h);
	}
	JS_FreeValue(ctx, thisv);
}

/* the event ev at n (NULL: the window): capture, target, bubble; 1 if
   its default action was cancelled */
static int dispatch(ejs *j, dnode *n, JSValueConst ev)
{
	jsev *e = ev_of(ev);
	dnode *path[256];
	int np = 0, i;
	dnode *k;
	if (e == NULL)
		return 0;
	e->target = n;
	e->target_window = n == NULL;
	e->stop = e->stopnow = 0;
	for (k = n ? n->parent : NULL; k != NULL && np < 256; k = k->parent)
		path[np++] = k;
	/* the window is above the document, for all but load */
	e->phase = 1;
	if (n != NULL && connected(j, n) && strcmp(e->type, "load") != 0)
		at_target(j, ev, e, NULL, 1, 0);
	for (i = np - 1; i >= 0 && !e->stop && !j->over; i--)
		at_target(j, ev, e, path[i], 1, 0);
	e->phase = 2;
	if (!e->stop && !j->over)
		at_target(j, ev, e, n, 0, 1);
	e->phase = 3;
	if (e->bubbles) {
		for (i = 0; i < np && !e->stop && !j->over; i++)
			at_target(j, ev, e, path[i], 0, 0);
		if (n != NULL && connected(j, n) && !e->stop && !j->over && strcmp(e->type, "load") != 0)
			at_target(j, ev, e, NULL, 0, 0);
	}
	e->phase = 0;
	e->current = NULL;
	e->current_window = 0;
	return e->prevented;
}

static JSValue dispatch_event(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	dnode *n;
	int prevented;
	if (argc < 1 || ev_of(argv[0]) == NULL || !target_of(ctx, this_val, &n))
		return JS_ThrowTypeError(ctx, "dispatchEvent: not an event");
	prevented = dispatch(j, n, argv[0]);
	return JS_NewBool(ctx, !prevented);
}

static int fire(ejs *j, dnode *n, const char *type, int bubbles, int cancelable)
{
	JSValue ev = new_event(j->ctx, type, bubbles, cancelable);
	int r;
	if (JS_IsException(ev)) {
		report(j);
		return 0;
	}
	ev_of(ev)->trusted = 1;
	r = dispatch(j, n, ev);
	JS_FreeValue(j->ctx, ev);
	return r;
}

/* form.submit() and a submit button clicked by a script: where the form
   sends the browser */
static void send_form(ejs *j, dnode *form, fctl *submitter)
{
	FSUBMIT s;
	fctl *from = submitter, *c;
	const char *action;
	char *to;
	int made;
	if (from == NULL)
		for (c = j->d->ctls; c != NULL; c = c->next)
			if (c->form == form) {
				from = c;
				break;
			}
	memset(&s, 0, sizeof s);
	made = from != NULL && form_submit(j->d, from, submitter, 0, 0, &s);
	action = made ? s.action : dn_attr(form, "action");
	if ((to = malloc(URL_MAX)) == NULL) {
		if (made)
			form_submit_free(&s);
		return;
	}
	if (j->h.resolve != NULL)
		j->h.resolve(j->h.ud, j->base, action && *action ? action : j->url, to, URL_MAX, 0);
	else
		str_copy(to, j->url, URL_MAX);
	nav_clear(j);
	if (made && s.post) {
		j->nav_body = s.body;
		j->nav_len = s.len;
		str_copy(j->nav_ctype, s.ctype, sizeof j->nav_ctype);
		s.body = NULL;
	} else if (made && s.body != NULL) {
		char *q = strchr(to, '#'), *qq;
		size_t n;
		if (q)
			*q = 0;
		if ((qq = strchr(to, '?')) != NULL)
			*qq = 0;
		n = strlen(to);
		if (n + 1 + (size_t) s.len < URL_MAX) {
			to[n] = '?';
			memcpy(to + n + 1, s.body, (size_t) s.len);
			to[n + 1 + s.len] = 0;
		}
	}
	j->nav = to;
	if (made)
		form_submit_free(&s);
}

/* the queued toggle events fired (ToggleEvent's oldState and newState) */
static void toggles(ejs *j)
{
	int k = 0;
	while (j->tog != NULL && !j->over && k++ < 64) {
		toggle *t = j->tog;
		JSValue ev = new_event(j->ctx, "toggle", 0, 0);
		j->tog = t->next;
		if (JS_IsException(ev)) {
			report(j);
		} else {
			ev_of(ev)->trusted = 1;
			JS_SetPropertyStr(j->ctx, ev, "oldState", JS_NewString(j->ctx, t->was ? "open" : "closed"));
			JS_SetPropertyStr(j->ctx, ev, "newState",
					  JS_NewString(j->ctx, dn_attr(t->n, "open") ? "open" : "closed"));
			dispatch(j, t->n, ev);
			JS_FreeValue(j->ctx, ev);
		}
		free(t);
		jobs(j);
	}
}

/* ---------------------------------------------------------------- timers */

static JSValue set_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int every)
{
	ejs *j = J(ctx);
	timer *t, **tail;
	int32_t ms = 0;
	int i;
	if (argc < 1 || (!JS_IsFunction(ctx, argv[0]) && !JS_IsString(argv[0])))
		return JS_NewInt32(ctx, 0);
	if (argc > 1)
		JS_ToInt32(ctx, &ms, argv[1]);
	if (ms < 4)
		ms = every ? 10 : 4;
	if ((t = calloc(1, sizeof *t)) == NULL)
		return JS_ThrowOutOfMemory(ctx);
	t->id = ++j->next_timer;
	t->due = pf_ms() + (unsigned long) ms;
	t->every = every ? ms : 0;
	t->fn = JS_DupValue(ctx, argv[0]);
	for (i = 2; i < argc && t->argc < 4; i++)
		t->argv[t->argc++] = JS_DupValue(ctx, argv[i]);
	for (tail = &j->ts; *tail != NULL; tail = &(*tail)->next)
		;
	*tail = t;
	return JS_NewInt32(ctx, t->id);
}

static void timer_free(ejs *j, timer *t)
{
	int i;
	JS_FreeValue(j->ctx, t->fn);
	for (i = 0; i < t->argc; i++)
		JS_FreeValue(j->ctx, t->argv[i]);
	free(t);
}

static JSValue clear_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	int32_t id = 0;
	timer **p, *t;
	if (argc < 1 || JS_ToInt32(ctx, &id, argv[0]) < 0)
		return JS_UNDEFINED;
	for (p = &j->ts; (t = *p) != NULL; p = &t->next)
		if (t->id == id) {
			*p = t->next;
			timer_free(j, t);
			return JS_UNDEFINED;
		}
	if (j->running != NULL && j->running->id == id)
		j->running->every = 0;	/* clearInterval in its own function */
	return JS_UNDEFINED;
}

/* ---------------------------------------------------------------- the window */

static JSValue win_ask(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int kind)
{
	ejs *j = J(ctx);
	char *text = argc > 0 ? cstr(ctx, argv[0], NULL) : strdup(""), out[256];
	unsigned long t;
	int ok = 1;
	if (text == NULL)
		return JS_EXCEPTION;
	out[0] = 0;
	if (kind == JS_PROMPT && argc > 1) {
		char *def = cstr(ctx, argv[1], NULL);
		if (def != NULL) {
			str_copy(out, def, sizeof out);
			free(def);
		}
	}
	t = pf_ms();
	if (j->h.ask != NULL)
		ok = j->h.ask(j->h.ud, kind, text, kind == JS_PROMPT ? out : NULL, sizeof out);
	j->t0 += pf_ms() - t;	/* the time it waited isn't the script's */
	free(text);
	if (kind == JS_ALERT)
		return JS_UNDEFINED;
	if (kind == JS_CONFIRM)
		return JS_NewBool(ctx, ok);
	return ok ? JS_NewString(ctx, out) : JS_NULL;
}

static JSValue win_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int level)
{
	ejs *j = J(ctx);
	SB b = { 0 };
	int i;
	if (j->h.console == NULL)
		return JS_UNDEFINED;
	for (i = 0; i < argc && b.n < 200; i++) {
		size_t len;
		const char *s = JS_ToCStringLen(ctx, &len, argv[i]);
		if (s == NULL) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			continue;
		}
		if (i)
			jb_str(&b, " ");
		jb_add(&b, s, len);
		JS_FreeCString(ctx, s);
	}
	if (!b.fail)
		j->h.console(j->h.ud, b.s ? b.s : "");
	(void) level;
	free(b.s);
	return JS_UNDEFINED;
}

/* the prelude's __ew_fetch(method, url, body, ctype, utf8): [status,
   text, mime, url], or a TypeError. utf8: fetch()'s (the Fetch
   Standard's Request parses its URL in UTF-8), else XMLHttpRequest's
   (in the page's encoding) */
static JSValue win_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ejs *j = J(ctx);
	char *method, *href, *body = NULL, *ctype = NULL, *to, *data = NULL, mime[96] = "";
	size_t blen = 0;
	long len = 0;
	int status = 0, ok;
	unsigned long t;
	JSValue a;
	if (argc < 2 || j->h.fetch == NULL)
		return JS_ThrowTypeError(ctx, "NetworkError");
	if ((method = cstr(ctx, argv[0], NULL)) == NULL)
		return JS_EXCEPTION;
	if ((href = cstr(ctx, argv[1], NULL)) == NULL) {
		free(method);
		return JS_EXCEPTION;
	}
	if (argc > 2 && !JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
		body = cstr(ctx, argv[2], &blen);
	if (argc > 3 && !JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3]))
		ctype = cstr(ctx, argv[3], NULL);
	to = malloc(URL_MAX);
	if (to != NULL && j->h.resolve != NULL)
		j->h.resolve(j->h.ud, j->base, href, to, URL_MAX, argc > 4 && JS_ToBool(ctx, argv[4]));
	t = pf_ms();
	ok = to != NULL && to[0] &&
	     j->h.fetch(j->h.ud, j->url, to, strcmp(method, "POST") == 0 ? "POST" : "GET", body, (long) blen,
			ctype ? ctype : "text/plain;charset=UTF-8", &data, &len, &status, mime, sizeof mime);
	j->t0 += pf_ms() - t;	/* the network's time isn't the script's */
	free(method);
	free(href);
	free(body);
	free(ctype);
	if (!ok) {
		free(to);
		free(data);
		return JS_ThrowTypeError(ctx, "NetworkError: it couldn't be fetched");
	}
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, status ? status : 200));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewStringLen(ctx, data ? data : "", (size_t) len));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewString(ctx, mime));
	JS_SetPropertyUint32(ctx, a, 3, JS_NewString(ctx, to));
	free(to);
	free(data);
	return a;
}

static const char b64c[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* btoa and atob: Latin-1 text (each character a byte) */
static JSValue win_b64(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int decode)
{
	size_t len, i, k = 0;
	const char *s;
	unsigned char *in, *out;
	JSValue r;
	if (argc < 1 || (s = JS_ToCStringLen(ctx, &len, argv[0])) == NULL)
		return JS_EXCEPTION;
	/* from UTF-8 to bytes */
	in = malloc(len + 1);
	out = malloc(len * 2 + 8);
	if (in == NULL || out == NULL) {
		free(in);
		free(out);
		JS_FreeCString(ctx, s);
		return JS_ThrowOutOfMemory(ctx);
	}
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char) s[i];
		if (c >= 0x80) {
			if ((c & 0xe0) != 0xc0 || c > 0xc3 || i + 1 >= len) {
				free(in);
				free(out);
				JS_FreeCString(ctx, s);
				return JS_ThrowTypeError(ctx, "InvalidCharacterError");
			}
			c = (unsigned char) (((c & 3) << 6) | ((unsigned char) s[++i] & 0x3f));
		}
		in[k++] = c;
	}
	JS_FreeCString(ctx, s);
	len = k;
	k = 0;
	if (!decode) {
		for (i = 0; i < len; i += 3) {
			unsigned long v = (unsigned long) in[i] << 16 | (i + 1 < len ? (unsigned long) in[i + 1] << 8 : 0) |
					  (i + 2 < len ? in[i + 2] : 0);
			out[k++] = (unsigned char) b64c[v >> 18 & 63];
			out[k++] = (unsigned char) b64c[v >> 12 & 63];
			out[k++] = i + 1 < len ? (unsigned char) b64c[v >> 6 & 63] : '=';
			out[k++] = i + 2 < len ? (unsigned char) b64c[v & 63] : '=';
		}
		r = JS_NewStringLen(ctx, (char *) out, k);
	} else {
		unsigned long v = 0;
		int bits = 0;
		for (i = 0; i < len; i++) {
			const char *p;
			if (in[i] == '=' || in[i] == ' ' || in[i] == '\n' || in[i] == '\r' || in[i] == '\t')
				continue;
			if ((p = strchr(b64c, in[i])) == NULL || in[i] == 0) {
				free(in);
				free(out);
				return JS_ThrowTypeError(ctx, "InvalidCharacterError");
			}
			v = v << 6 | (unsigned long) (p - b64c);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out[k++] = (unsigned char) (v >> bits & 255);
			}
		}
		/* back to UTF-8 */
		{
			unsigned char *u = malloc(k * 2 + 1);
			size_t m = 0;
			if (u == NULL) {
				free(in);
				free(out);
				return JS_ThrowOutOfMemory(ctx);
			}
			for (i = 0; i < k; i++)
				if (out[i] < 0x80)
					u[m++] = out[i];
				else {
					u[m++] = (unsigned char) (0xc0 | out[i] >> 6);
					u[m++] = (unsigned char) (0x80 | (out[i] & 0x3f));
				}
			r = JS_NewStringLen(ctx, (char *) u, m);
			free(u);
		}
	}
	free(in);
	free(out);
	return r;
}

static JSValue win_null(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NULL;
}

enum { WP_WIDTH, WP_HEIGHT, WP_ZERO, WP_RATIO };

static JSValue win_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	switch (magic) {
	case WP_WIDTH:
		return JS_NewInt32(ctx, j->pg != NULL ? j->pg->width : 640);
	case WP_HEIGHT:
		return JS_NewInt32(ctx, 400);
	case WP_RATIO:
		return JS_NewInt32(ctx, 1);
	}
	return JS_NewInt32(ctx, 0);
}

/* the prelude's parts that are made when first asked for */
static const char *const part_src[] = {
	/* 0: storage (kept for the page only) */
	"(function(g){function S(){var d=Object.create(null);return{getItem:function(k){k=String(k);"
	"return k in d?d[k]:null},setItem:function(k,v){d[String(k)]=String(v)},removeItem:function(k)"
	"{delete d[String(k)]},clear:function(){d=Object.create(null)},key:function(i){var k=Object.keys(d);"
	"return i<k.length?k[i]:null},get length(){return Object.keys(d).length}}}"
	"g.localStorage=S();g.sessionStorage=S()})(globalThis)",
	/* 1: the network: fetch, Headers, Response, XMLHttpRequest */
	"(function(g){function H(t){this._h={};if(t)for(var k in t)this._h[k.toLowerCase()]=String(t[k])}"
	"H.prototype.get=function(k){k=String(k).toLowerCase();return k in this._h?this._h[k]:null};"
	"H.prototype.has=function(k){return String(k).toLowerCase()in this._h};"
	"H.prototype.set=H.prototype.append=function(k,v){this._h[String(k).toLowerCase()]=String(v)};"
	"H.prototype.forEach=function(f){for(var k in this._h)f(this._h[k],k,this)};g.Headers=H;"
	"function R(r){this.status=r[0];this.ok=r[0]>=200&&r[0]<300;this.statusText=this.ok?'OK':'';"
	"this.url=r[3];this.headers=new H({'content-type':r[2]});this._t=r[1];this.redirected=false;"
	"this.type='basic'}R.prototype.text=function(){return Promise.resolve(this._t)};"
	"R.prototype.json=function(){var t=this._t;return new Promise(function(y){y(JSON.parse(t))})};"
	"R.prototype.clone=function(){return this};g.Response=R;"
	"function ct(h){return h?(h instanceof H?h.get('content-type'):h['Content-Type']||h['content-type']"
	"||null):null}"
	"g.fetch=function(u,o){o=o||{};return new Promise(function(y,n){var r;try{r=__ew_fetch(String(o.method"
	"||'GET').toUpperCase(),String(u&&u.url||u),o.body==null?null:String(o.body),ct(o.headers),1)}"
	"catch(e){n(new TypeError('Failed to fetch'));return}y(new R(r))})};"
	"function X(){this.readyState=0;this.status=0;this.responseText='';this.response='';"
	"this.responseType='';this._l={};this._h=null}var P=X.prototype;"
	"P.open=function(m,u,a){this._m=String(m).toUpperCase();this._u=String(u);this._a=a!==false;"
	"this.readyState=1};P.setRequestHeader=function(k,v){if(String(k).toLowerCase()=='content-type')"
	"this._h=String(v)};P.getResponseHeader=function(k){return String(k).toLowerCase()=='content-type'"
	"?this._ct||null:null};P.getAllResponseHeaders=function(){return this._ct?'content-type: '+"
	"this._ct+'\\r\\n':''};P.addEventListener=function(t,f){(this._l[t]=this._l[t]||[]).push(f)};"
	"P.removeEventListener=function(t,f){var a=this._l[t],i=a?a.indexOf(f):-1;if(i>=0)a.splice(i,1)};"
	"P._e=function(t){var e={type:t,target:this,currentTarget:this},f=this['on'+t],a=this._l[t]||[],i;"
	"if(typeof f=='function')f.call(this,e);for(i=0;i<a.length;i++)a[i].call(this,e)};"
	"P.abort=P.overrideMimeType=function(){};"
	"P.send=function(b){var x=this,r=null;try{r=__ew_fetch(x._m,x._u,b==null?null:String(b),x._h)}"
	"catch(e){}function done(){if(r){x.status=r[0];x.statusText=r[0]==200?'OK':'';x.responseText=r[1];"
	"x._ct=r[2];x.responseURL=r[3];if(x.responseType=='json'){try{x.response=JSON.parse(r[1])}catch(e)"
	"{x.response=null}}else x.response=r[1]}x.readyState=4;x._e('readystatechange');"
	"x._e(r?'load':'error');x._e('loadend')}if(x._a)setTimeout(done,0);else done()};"
	"X.UNSENT=0;X.OPENED=1;X.HEADERS_RECEIVED=2;X.LOADING=3;X.DONE=4;g.XMLHttpRequest=X})(globalThis)",
};
/* the names each part defines */
static const char *const part_names[][5] = {
	{ "localStorage", "sessionStorage", NULL },
	{ "fetch", "Headers", "Response", "XMLHttpRequest", NULL },
};

/* a name the script sets itself replaces the part's */
static JSValue lazy_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	JSValue g = JS_GetGlobalObject(ctx);
	JS_DefinePropertyValueStr(ctx, g, part_names[magic >> 4][magic & 15], JS_DupValue(ctx, v), JS_PROP_C_W_E);
	JS_FreeValue(ctx, g);
	return JS_UNDEFINED;
}

static JSValue lazy_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	int part = magic >> 4;
	JSValue g = JS_GetGlobalObject(ctx), r;
	if (!(j->parts & (1 << part))) {
		int i;
		j->parts |= (uint8_t) (1 << part);
		/* the names become plain properties, then the part sets them */
		for (i = 0; part_names[part][i] != NULL; i++)
			JS_DefinePropertyValueStr(ctx, g, part_names[part][i], JS_UNDEFINED, JS_PROP_C_W_E);
		r = JS_Eval(ctx, part_src[part], strlen(part_src[part]), "<eweb>", JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r)) {
			JS_FreeValue(ctx, g);
			return r;
		}
		JS_FreeValue(ctx, r);
	}
	r = JS_GetPropertyStr(ctx, g, part_names[part][magic & 15]);
	JS_FreeValue(ctx, g);
	return r;
}

static const JSCFunctionListEntry win_funcs[] = {
	JS_CFUNC_MAGIC_DEF("setTimeout", 2, set_timer, 0),
	JS_CFUNC_MAGIC_DEF("setInterval", 2, set_timer, 1),
	JS_CFUNC_DEF("clearTimeout", 1, clear_timer),
	JS_CFUNC_DEF("clearInterval", 1, clear_timer),
	JS_CFUNC_MAGIC_DEF("alert", 1, win_ask, JS_ALERT),
	JS_CFUNC_MAGIC_DEF("confirm", 1, win_ask, JS_CONFIRM),
	JS_CFUNC_MAGIC_DEF("prompt", 2, win_ask, JS_PROMPT),
	JS_CFUNC_DEF("getComputedStyle", 1, get_computed),
	JS_CFUNC_MAGIC_DEF("btoa", 1, win_b64, 0),
	JS_CFUNC_MAGIC_DEF("atob", 1, win_b64, 1),
	JS_CFUNC_DEF("scrollTo", 2, node_noop),
	JS_CFUNC_DEF("scrollBy", 2, node_noop),
	JS_CFUNC_DEF("scroll", 2, node_noop),
	JS_CFUNC_DEF("focus", 0, node_noop),
	JS_CFUNC_DEF("blur", 0, node_noop),
	JS_CFUNC_DEF("print", 0, node_noop),
	JS_CFUNC_DEF("close", 0, node_noop),
	JS_CFUNC_DEF("postMessage", 2, node_noop),
	JS_CFUNC_DEF("open", 3, win_null),
	JS_CFUNC_DEF("addEventListener", 3, add_listener),
	JS_CFUNC_DEF("removeEventListener", 3, remove_listener),
	JS_CFUNC_DEF("dispatchEvent", 1, dispatch_event),
	JS_CFUNC_DEF("__ew_fetch", 5, win_fetch),
	JS_CGETSET_MAGIC_DEF("innerWidth", win_get, NULL, WP_WIDTH),
	JS_CGETSET_MAGIC_DEF("innerHeight", win_get, NULL, WP_HEIGHT),
	JS_CGETSET_MAGIC_DEF("outerWidth", win_get, NULL, WP_WIDTH),
	JS_CGETSET_MAGIC_DEF("outerHeight", win_get, NULL, WP_HEIGHT),
	JS_CGETSET_MAGIC_DEF("pageXOffset", win_get, NULL, WP_ZERO),
	JS_CGETSET_MAGIC_DEF("pageYOffset", win_get, NULL, WP_ZERO),
	JS_CGETSET_MAGIC_DEF("scrollX", win_get, NULL, WP_ZERO),
	JS_CGETSET_MAGIC_DEF("scrollY", win_get, NULL, WP_ZERO),
	JS_CGETSET_MAGIC_DEF("devicePixelRatio", win_get, NULL, WP_RATIO),
	JS_CGETSET_MAGIC_DEF("localStorage", lazy_get, lazy_set, 0x00),
	JS_CGETSET_MAGIC_DEF("sessionStorage", lazy_get, lazy_set, 0x01),
	JS_CGETSET_MAGIC_DEF("fetch", lazy_get, lazy_set, 0x10),
	JS_CGETSET_MAGIC_DEF("Headers", lazy_get, lazy_set, 0x11),
	JS_CGETSET_MAGIC_DEF("Response", lazy_get, lazy_set, 0x12),
	JS_CGETSET_MAGIC_DEF("XMLHttpRequest", lazy_get, lazy_set, 0x13),
};

static const JSCFunctionListEntry console_funcs[] = {
	JS_CFUNC_MAGIC_DEF("log", 1, win_log, 0),
	JS_CFUNC_MAGIC_DEF("info", 1, win_log, 0),
	JS_CFUNC_MAGIC_DEF("debug", 1, win_log, 0),
	JS_CFUNC_MAGIC_DEF("warn", 1, win_log, 1),
	JS_CFUNC_MAGIC_DEF("error", 1, win_log, 1),
	JS_CFUNC_MAGIC_DEF("trace", 0, win_log, 0),
	JS_CFUNC_DEF("group", 0, node_noop),
	JS_CFUNC_DEF("groupEnd", 0, node_noop),
	JS_CFUNC_DEF("time", 0, node_noop),
	JS_CFUNC_DEF("timeEnd", 0, node_noop),
};

/* the small prelude every page with scripts has */
static const char prelude[] =
	"(function(g){g.requestAnimationFrame=function(f){return setTimeout(function(){f(Date.now())},40)};"
	"g.cancelAnimationFrame=function(i){clearTimeout(i)};"
	"g.queueMicrotask=function(f){Promise.resolve().then(f)};"
	"g.matchMedia=function(q){return{matches:false,media:String(q),onchange:null,addListener:function(){},"
	"removeListener:function(){},addEventListener:function(){},removeEventListener:function(){}}};"
	"g.getSelection=function(){return{rangeCount:0,toString:function(){return''},removeAllRanges:"
	"function(){}}};if(!g.performance)g.performance={now:function(){return Date.now()}}})(globalThis)";

/* ---------------------------------------------------------------- location and history */

enum { LP_HREF, LP_PROTOCOL, LP_HOST, LP_HOSTNAME, LP_PORT, LP_PATH, LP_SEARCH, LP_HASH, LP_ORIGIN };

static JSValue loc_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	ejs *j = J(ctx);
	URL *u = malloc(sizeof *u);
	JSValue r;
	char b[300];
	size_t len;
	if (u == NULL)
		return JS_ThrowOutOfMemory(ctx);
	if (magic == LP_HREF || !url_parse(u, j->url, NULL)) {
		free(u);
		return magic == LP_HREF ? JS_NewString(ctx, j->url) : JS_NewString(ctx, "");
	}
	len = strlen(u->href);
	switch (magic) {
	case LP_PROTOCOL:
		r = JS_NewStringLen(ctx, u->href, (size_t) u->scheme + 1);
		break;
	case LP_HOST:
		url_hostport(u, b, sizeof b);
		r = JS_NewString(ctx, b);
		break;
	case LP_HOSTNAME:
		url_host(u, b, sizeof b);
		r = JS_NewString(ctx, b);
		break;
	case LP_PORT: {
		char *c;
		url_hostport(u, b, sizeof b);
		c = strrchr(b, ':');
		r = JS_NewString(ctx, c != NULL && strchr(c, ']') == NULL ? c + 1 : "");
		break;
	}
	case LP_PATH:
		r = JS_NewStringLen(ctx, u->href + u->path, (size_t) (u->query - u->path));
		break;
	case LP_SEARCH:
		r = u->frag - u->query > 1 ? JS_NewStringLen(ctx, u->href + u->query, (size_t) (u->frag - u->query)) :
		    JS_NewString(ctx, "");
		break;
	case LP_HASH:
		r = len - u->frag > 1 ? JS_NewString(ctx, u->href + u->frag) : JS_NewString(ctx, "");
		break;
	default:
		url_hostport(u, b, sizeof b);
		{
			char o[400];
			snprintf(o, sizeof o, "%.*s://%s", (int) u->scheme, u->href, b);
			r = JS_NewString(ctx, o);
		}
		break;
	}
	free(u);
	return r;
}

/* the browser to go to href (resolved against the page) */
static JSValue go(JSContext *ctx, JSValueConst v, int hash)
{
	ejs *j = J(ctx);
	char *href = cstr(ctx, v, NULL), *to;
	if (href == NULL)
		return JS_EXCEPTION;
	if ((to = malloc(URL_MAX)) == NULL) {
		free(href);
		return JS_ThrowOutOfMemory(ctx);
	}
	if (hash) {
		char *f;
		str_copy(to, j->url, URL_MAX);
		if ((f = strchr(to, '#')) != NULL)
			*f = 0;
		if (strlen(to) + strlen(href) + 2 < URL_MAX) {
			if (href[0] != '#')
				strcat(to, "#");
			strcat(to, href);
		}
	} else if (j->h.resolve != NULL) {
		j->h.resolve(j->h.ud, j->base, href, to, URL_MAX, 0);
	} else {
		str_copy(to, href, URL_MAX);
	}
	free(href);
	if (to[0] == 0 || strncmp(to, "javascript:", 11) == 0) {
		free(to);
		return JS_UNDEFINED;
	}
	nav_clear(j);
	j->nav = to;
	return JS_UNDEFINED;
}

static JSValue loc_set(JSContext *ctx, JSValueConst this_val, JSValueConst v, int magic)
{
	if (magic == LP_HASH)
		return go(ctx, v, 1);
	if (magic == LP_HREF)
		return go(ctx, v, 0);
	return JS_UNDEFINED;
}

static JSValue loc_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	switch (magic) {
	case 0:			/* assign, replace */
		return argc > 0 ? go(ctx, argv[0], 0) : JS_UNDEFINED;
	case 1: {		/* reload */
		JSValue u = JS_NewString(ctx, j->url), r = go(ctx, u, 0);
		JS_FreeValue(ctx, u);
		return r;
	}
	default:		/* toString */
		return JS_NewString(ctx, j->url);
	}
}

static const JSCFunctionListEntry loc_funcs[] = {
	JS_CGETSET_MAGIC_DEF("href", loc_get, loc_set, LP_HREF),
	JS_CGETSET_MAGIC_DEF("protocol", loc_get, NULL, LP_PROTOCOL),
	JS_CGETSET_MAGIC_DEF("host", loc_get, NULL, LP_HOST),
	JS_CGETSET_MAGIC_DEF("hostname", loc_get, NULL, LP_HOSTNAME),
	JS_CGETSET_MAGIC_DEF("port", loc_get, NULL, LP_PORT),
	JS_CGETSET_MAGIC_DEF("pathname", loc_get, NULL, LP_PATH),
	JS_CGETSET_MAGIC_DEF("search", loc_get, NULL, LP_SEARCH),
	JS_CGETSET_MAGIC_DEF("hash", loc_get, loc_set, LP_HASH),
	JS_CGETSET_MAGIC_DEF("origin", loc_get, NULL, LP_ORIGIN),
	JS_CFUNC_MAGIC_DEF("assign", 1, loc_call, 0),
	JS_CFUNC_MAGIC_DEF("replace", 1, loc_call, 0),
	JS_CFUNC_MAGIC_DEF("reload", 0, loc_call, 1),
	JS_CFUNC_MAGIC_DEF("toString", 0, loc_call, 2),
};

/* history: pushState and replaceState change the address shown, back
   and forward go through the browser */
static JSValue hist_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
	ejs *j = J(ctx);
	if (magic == 0) {
		char *href, *to;
		if (argc < 3 || JS_IsNull(argv[2]) || JS_IsUndefined(argv[2]))
			return JS_UNDEFINED;
		if ((href = cstr(ctx, argv[2], NULL)) == NULL)
			return JS_EXCEPTION;
		if ((to = malloc(URL_MAX)) != NULL && j->h.resolve != NULL) {
			j->h.resolve(j->h.ud, j->base, href, to, URL_MAX, 0);
			if (to[0])
				str_copy(j->url, to, sizeof j->url);
		}
		free(to);
		free(href);
		return JS_UNDEFINED;
	}
	nav_clear(j);
	j->nav = strdup(magic == 1 ? "about:back" : "about:forward");
	return JS_UNDEFINED;
}

static JSValue hist_go(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t k = 0;
	if (argc > 0)
		JS_ToInt32(ctx, &k, argv[0]);
	if (k == 0)
		return JS_UNDEFINED;
	return hist_call(ctx, this_val, 0, NULL, k < 0 ? 1 : 2);
}

static const JSCFunctionListEntry hist_funcs[] = {
	JS_CFUNC_MAGIC_DEF("pushState", 3, hist_call, 0),
	JS_CFUNC_MAGIC_DEF("replaceState", 3, hist_call, 0),
	JS_CFUNC_MAGIC_DEF("back", 0, hist_call, 1),
	JS_CFUNC_MAGIC_DEF("forward", 0, hist_call, 2),
	JS_CFUNC_DEF("go", 1, hist_go),
	JS_PROP_INT32_DEF("length", 1, 0),
};

/* ---------------------------------------------------------------- the page's scripts */

static JSValue illegal(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
	return JS_ThrowTypeError(ctx, "Illegal constructor");
}

/* a constructor named name for proto (alias: one more name for it) */
static void ctor(JSContext *ctx, JSValueConst g, const char *name, JSValueConst proto, JSCFunction *fn, int alias)
{
	JSValue f = JS_NewCFunction2(ctx, fn, name, 1, JS_CFUNC_constructor, 0);
	if (alias)
		JS_DefinePropertyValueStr(ctx, f, "prototype", JS_DupValue(ctx, proto), 0);
	else
		JS_SetConstructor(ctx, f, proto);
	JS_SetPropertyStr(ctx, g, name, f);
}

static void js_drop(void *p);

/* the runtime, the classes and the window's objects */
static int setup(ejs *j)
{
	static const JSClassDef node_def = { "Node", NULL, NULL, NULL, NULL };
	static const JSClassDef style_def = { "CSSStyleDeclaration", NULL, NULL, NULL, NULL };
	static const JSClassDef comp_def = { "CSSStyleDeclaration", NULL, NULL, NULL, NULL };
	static const JSClassDef cl_def = { "DOMTokenList", NULL, NULL, NULL, NULL };
	static const JSClassDef ev_def = { "Event", ev_finalizer, NULL, NULL, NULL };
	static const char *const el_names[] = {
		"HTMLElement", "HTMLDivElement", "HTMLSpanElement", "HTMLAnchorElement", "HTMLImageElement",
		"HTMLInputElement", "HTMLFormElement", "HTMLSelectElement", "HTMLOptionElement",
		"HTMLTextAreaElement", "HTMLButtonElement", "HTMLScriptElement", "HTMLTableElement",
		"HTMLLIElement", "HTMLUListElement", "HTMLParagraphElement", "HTMLBodyElement",
		"HTMLIFrameElement", "HTMLCanvasElement", "HTMLTemplateElement", "HTMLDetailsElement", "SVGElement",
		NULL };
	static const char *const ev_names[] = { "CustomEvent", "UIEvent", "MouseEvent", "KeyboardEvent",
						 "FocusEvent", "InputEvent", "SubmitEvent", "PointerEvent", NULL };
	JSContext *ctx;
	JSValue g, o;
	int i;

	if (node_class == 0) {
		JS_NewClassID(&node_class);
		JS_NewClassID(&style_class);
		JS_NewClassID(&comp_class);
		JS_NewClassID(&cl_class);
		JS_NewClassID(&ev_class);
	}
	if ((j->rt = JS_NewRuntime()) == NULL)
		return 0;
	JS_SetMemoryLimit(j->rt, JS_MAX_MEMORY);
	JS_SetMaxStackSize(j->rt, JS_STACK);
	JS_SetInterruptHandler(j->rt, interrupt, j);
	if (JS_NewClass(j->rt, node_class, &node_def) < 0 || JS_NewClass(j->rt, style_class, &style_def) < 0 ||
	    JS_NewClass(j->rt, comp_class, &comp_def) < 0 || JS_NewClass(j->rt, cl_class, &cl_def) < 0 ||
	    JS_NewClass(j->rt, ev_class, &ev_def) < 0)
		return 0;
	if ((j->ctx = ctx = JS_NewContext(j->rt)) == NULL)
		return 0;
	JS_SetContextOpaque(ctx, j);

#define LIST(o, l) JS_SetPropertyFunctionList(ctx, o, l, (int) (sizeof l / sizeof l[0]))
	j->node_proto = JS_NewObject(ctx);
	LIST(j->node_proto, node_funcs);
	j->elem_proto = JS_NewObjectProto(ctx, j->node_proto);
	LIST(j->elem_proto, elem_funcs);
	LIST(j->elem_proto, parent_funcs);
	LIST(j->elem_proto, child_funcs);
	j->text_proto = JS_NewObjectProto(ctx, j->node_proto);
	LIST(j->text_proto, text_funcs);
	LIST(j->text_proto, child_funcs);
	j->doc_proto = JS_NewObjectProto(ctx, j->node_proto);
	LIST(j->doc_proto, doc_funcs);
	LIST(j->doc_proto, parent_funcs);
	j->frag_proto = JS_NewObjectProto(ctx, j->node_proto);
	LIST(j->frag_proto, parent_funcs);
	j->style_proto = JS_NewObject(ctx);
	LIST(j->style_proto, style_funcs);
	j->comp_proto = JS_NewObject(ctx);
	LIST(j->comp_proto, comp_funcs);
	j->cl_proto = JS_NewObject(ctx);
	LIST(j->cl_proto, cl_funcs);
	j->ev_proto = JS_NewObject(ctx);
	LIST(j->ev_proto, ev_funcs);
	j->loc_obj = JS_NewObject(ctx);
	LIST(j->loc_obj, loc_funcs);

	g = JS_GetGlobalObject(ctx);
	LIST(g, win_funcs);
	JS_SetPropertyStr(ctx, g, "window", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "self", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "top", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "parent", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "frames", JS_DupValue(ctx, g));
	JS_SetPropertyStr(ctx, g, "document", wrap(ctx, j->d->root));
	JS_SetPropertyStr(ctx, g, "location", JS_DupValue(ctx, j->loc_obj));
	o = JS_NewObject(ctx);
	LIST(o, console_funcs);
	JS_SetPropertyStr(ctx, g, "console", o);
	o = JS_NewObject(ctx);
	LIST(o, hist_funcs);
	JS_SetPropertyStr(ctx, g, "history", o);
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "userAgent", JS_NewString(ctx, http_user_agent));
	JS_SetPropertyStr(ctx, o, "appName", JS_NewString(ctx, "Netscape"));
	JS_SetPropertyStr(ctx, o, "appCodeName", JS_NewString(ctx, "Mozilla"));
	JS_SetPropertyStr(ctx, o, "appVersion", JS_NewString(ctx, "5.0"));
	JS_SetPropertyStr(ctx, o, "product", JS_NewString(ctx, "Gecko"));
	JS_SetPropertyStr(ctx, o, "platform", JS_NewString(ctx, "Atari"));
	JS_SetPropertyStr(ctx, o, "vendor", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, o, "language", JS_NewString(ctx, "en"));
	JS_SetPropertyStr(ctx, o, "cookieEnabled", JS_TRUE);
	JS_SetPropertyStr(ctx, o, "onLine", JS_TRUE);
	JS_SetPropertyStr(ctx, o, "javaEnabled", JS_NewCFunction2(ctx, (JSCFunction *) win_null, "javaEnabled", 0,
								      JS_CFUNC_generic, 0));
	JS_SetPropertyStr(ctx, g, "navigator", o);
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "width", JS_NewInt32(ctx, 640));
	JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, 480));
	JS_SetPropertyStr(ctx, o, "availWidth", JS_NewInt32(ctx, 640));
	JS_SetPropertyStr(ctx, o, "availHeight", JS_NewInt32(ctx, 480));
	JS_SetPropertyStr(ctx, o, "colorDepth", JS_NewInt32(ctx, 8));
	JS_SetPropertyStr(ctx, g, "screen", o);

	ctor(ctx, g, "Node", j->node_proto, illegal, 0);
	ctor(ctx, g, "Element", j->elem_proto, illegal, 0);
	for (i = 0; el_names[i]; i++)
		ctor(ctx, g, el_names[i], j->elem_proto, illegal, 1);
	ctor(ctx, g, "Text", j->text_proto, illegal, 0);
	ctor(ctx, g, "CharacterData", j->text_proto, illegal, 1);
	ctor(ctx, g, "Document", j->doc_proto, illegal, 0);
	ctor(ctx, g, "HTMLDocument", j->doc_proto, illegal, 1);
	ctor(ctx, g, "DocumentFragment", j->frag_proto, illegal, 0);
	ctor(ctx, g, "Event", j->ev_proto, ev_construct, 0);
	for (i = 0; ev_names[i]; i++)
		ctor(ctx, g, ev_names[i], j->ev_proto, ev_construct, 1);
	ctor(ctx, g, "ToggleEvent", j->ev_proto, toggle_construct, 1);
	JS_FreeValue(ctx, g);
#undef LIST
	o = JS_Eval(ctx, prelude, sizeof prelude - 1, "<eweb>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(o)) {
		report(j);
		return 0;
	}
	JS_FreeValue(ctx, o);
	return 1;
}

/* a script's type that is JavaScript (modules aren't run: EWeb has no
   module loader; their nomodule fallbacks are) */
static int classic(const char *t)
{
	static const char *const ok[] = { "text/javascript", "application/javascript", "text/ecmascript",
					  "application/ecmascript", "application/x-javascript", "text/jscript",
					  "text/livescript", "text/x-javascript", "javascript", NULL };
	char b[40];
	size_t n = 0;
	int i;
	while (*t == ' ' || *t == '\t')
		t++;
	while (t[n] && t[n] != ';' && t[n] != ' ' && n < sizeof b - 1) {
		b[n] = t[n] >= 'A' && t[n] <= 'Z' ? (char) (t[n] + 32) : t[n];
		n++;
	}
	b[n] = 0;
	if (n == 0)
		return 1;
	for (i = 0; ok[i]; i++)
		if (!strcmp(ok[i], b))
			return 1;
	return 0;
}

static void run_script(ejs *j, dnode *n)
{
	const char *type = dn_attr(n, "type"), *src = dn_attr(n, "src");
	char *code = NULL, *name = NULL;
	long len = 0;
	dnode *was = j->current, *wrote = j->write_after;
	JSValue r;

	mark_started(j, n);
	if (type != NULL && !classic(type))
		return;
	if (!connected(j, n))
		return;
	if (src != NULL) {
		char mime[96];
		int status = 0;
		if (*src == 0 || j->h.fetch == NULL || j->h.resolve == NULL || (name = malloc(URL_MAX)) == NULL)
			return;
		j->h.resolve(j->h.ud, j->base, src, name, URL_MAX, 0);
		if (!name[0] || !j->h.fetch(j->h.ud, j->url, name, "GET", NULL, 0, NULL, &code, &len, &status, mime, sizeof mime) ||
		    (status != 0 && (status < 200 || status >= 300))) {
			say(j, "not fetched", name);
			free(code);
			free(name);
			fire(j, n, "error", 0, 0);
			return;
		}
		{
			char *c = realloc(code, (size_t) len + 1);
			if (c == NULL) {
				free(code);
				free(name);
				return;
			}
			code = c;
			code[len] = 0;
		}
	} else {
		SB b = { 0 };
		text_of(n, &b);
		if (b.fail || b.n == 0) {
			free(b.s);
			return;
		}
		code = b.s;
		len = (long) b.n;
	}
	j->bytes += len;
	if (j->bytes > JS_MAX_SOURCE) {
		say(j, "stopped", "over its 200 KB");
		j->over = 1;
		free(code);
		free(name);
		return;
	}
	j->current = n;
	j->write_after = n;
	r = JS_Eval(j->ctx, code, (size_t) len, name ? name : j->url, JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r))
		report(j);
	JS_FreeValue(j->ctx, r);
	free(code);
	free(name);
	jobs(j);
	j->current = was;
	j->write_after = wrote;
	if (src != NULL && !j->over)
		fire(j, n, "load", 0, 0);
}

/* each stylesheet's load event, as its sheet came, at the <link>s
   whose sheet is fetched when its media holds and at those preloaded as
   a style (doc_link_loads), when a handler may hear it: what it does
   then (the async-CSS pattern's media='all' or rel='stylesheet') changes
   the page's sheets, and they are taken again (src/browse). A sheet
   whose media holds on no screen (print) isn't fetched until it is made
   to hold: its load comes as if it had been */
static void sheet_loads(ejs *j)
{
	dnode *n;
	listener *l;
	int any = 0;
	for (l = j->ls; l != NULL; l = l->next)
		if (!l->gone && l->n != NULL && !strcmp(l->type, "load"))
			any = 1;		/* (the window's hears no element's) */
	for (n = j->d->root; n != NULL && !j->over; n = next_in(n, j->d->root, !is_html(n, T_TEMPLATE)))
		if (is_html(n, T_LINK) && doc_link_loads(n) &&
		    (any || dn_attr(n, "onload") != NULL || map_find(j, n) != NULL))
			fire(j, n, "load", 0, 0);
}

/* does the page have scripts (or handlers in its attributes) */
static int has_scripts(const doc *d)
{
	dnode *n;
	if (d->root == NULL)
		return 0;
	for (n = d->root; n != NULL; n = next_in(n, d->root, 1)) {
		uint16_t i;
		if (n->type != DN_ELEMENT)
			continue;
		if (is_html(n, T_SCRIPT))
			return 1;
		for (i = 0; i < n->nattr; i++) {
			size_t al;
			const char *a = ec_atom_text(n->attrs[i].name, &al);
			if (al > 2 && a[0] == 'o' && a[1] == 'n')
				return 1;
		}
	}
	return 0;
}

int js_page(doc *d, const char *url, const char *base, const js_host *h)
{
	ejs *j;
	dnode *n;
	unsigned long t = pf_ms();

	if (!has_scripts(d))
		return JS_NONE;
	if ((j = calloc(1, sizeof *j)) == NULL)
		return JS_NO_MEMORY;
	j->d = d;
	j->h = *h;
	str_copy(j->url, url, sizeof j->url);
	str_copy(j->base, base, sizeof j->base);
	d->js = j;
	d->js_free = js_drop;
	arm(j, JS_PAGE_MS);
	if (!setup(j)) {
		d->js = NULL;
		d->js_free = NULL;
		js_drop(j);
		return JS_NO_MEMORY;
	}
	arm(j, JS_PAGE_MS);
	/* the details open as parsed: a toggle event each to come (the
	   parser's setting open runs the attribute change steps, 4.11.1),
	   fired after the scripts with the timers; not in a template's
	   contents (an inert document's) */
	for (n = d->root; n != NULL; n = next_in(n, d->root, !is_html(n, T_TEMPLATE)))
		if (DN_IS(n, T_DETAILS) && dn_attr(n, "open") != NULL)
			toggle_queue(j, n, 0);
	/* in document order; what a script writes comes after it, and is
	   run in its turn */
	for (n = d->root; n != NULL && !j->over;) {
		if (is_html(n, T_SCRIPT) && !started(j, n)) {
			run_script(j, n);
			n = connected(j, n) ? next_in(n, d->root, 1) : d->root;
			continue;
		}
		n = next_in(n, d->root, 1);
	}
	if (!j->over) {
		j->write_after = NULL;
		fire(j, d->root, "readystatechange", 0, 0);
		fire(j, d->root, "DOMContentLoaded", 1, 0);
		sheet_loads(j);
		j->loaded = 1;
		fire(j, d->root, "readystatechange", 0, 0);
		if (!j->over)
			fire(j, NULL, "load", 0, 0);
		jobs(j);
		sweep(j);
	}
	j->ms = pf_ms() - t;
	if (j->over)
		return JS_TOO_MUCH;
	/* the timers due at once (setTimeout(f, 0) as a page loads) */
	js_run_timers(d);
	j->ms = pf_ms() - t;
	if (j->over)
		return JS_TOO_MUCH;
	return JS_DONE;
}

int js_changed(doc *d)
{
	ejs *j = d->js;
	int r;
	if (j == NULL)
		return 0;
	r = j->changed || j->stale;
	j->changed = 0;
	j->stale = 0;
	return r;
}

const char *js_navigation(doc *d, const char **post, long *len, const char **ctype)
{
	ejs *j = d->js;
	if (j == NULL || j->nav == NULL)
		return NULL;
	if (post != NULL)
		*post = j->nav_body;
	if (len != NULL)
		*len = j->nav_len;
	if (ctype != NULL)
		*ctype = j->nav_ctype;
	return j->nav;
}

const char *js_navigation_download(doc *d)
{
	ejs *j = d->js;
	return j != NULL && j->nav != NULL ? j->nav_dl : NULL;
}

void js_navigation_done(doc *d)
{
	ejs *j = d->js;
	if (j == NULL)
		return;
	nav_clear(j);
}

long js_due(doc *d)
{
	ejs *j = d->js;
	timer *t;
	long best = -1;
	unsigned long now = pf_ms();
	if (j == NULL)
		return -1;
	if (JS_IsJobPending(j->rt) || j->tog != NULL)
		return 0;
	for (t = j->ts; t != NULL; t = t->next) {
		long w = (long) (t->due - now);
		if (w < 0)
			w = 0;
		if (best < 0 || w < best)
			best = w;
	}
	return best;
}

void js_run_timers(doc *d)
{
	ejs *j = d->js;
	unsigned long now;
	int k = 0;
	if (j == NULL)
		return;
	arm(j, JS_EVENT_MS);
	toggles(j);
	now = pf_ms();
	while (k++ < 64) {
		timer **p, **due = NULL, *t;
		for (p = &j->ts; *p != NULL; p = &(*p)->next)
			if ((long) ((*p)->due - now) <= 0 && (due == NULL || (long) ((*p)->due - (*due)->due) < 0))
				due = p;
		if (due == NULL)
			break;
		t = *due;
		*due = t->next;
		t->next = NULL;
		arm(j, JS_EVENT_MS);
		j->running = t;
		if (JS_IsString(t->fn)) {
			size_t len;
			const char *s = JS_ToCStringLen(j->ctx, &len, t->fn);
			if (s != NULL) {
				JSValue r = JS_Eval(j->ctx, s, len, "<timer>", JS_EVAL_TYPE_GLOBAL);
				if (JS_IsException(r))
					report(j);
				JS_FreeValue(j->ctx, r);
				JS_FreeCString(j->ctx, s);
			}
		} else {
			JSValue r = JS_Call(j->ctx, t->fn, JS_UNDEFINED, t->argc, t->argv);
			if (JS_IsException(r))
				report(j);
			JS_FreeValue(j->ctx, r);
		}
		j->running = NULL;
		jobs(j);
		if (t->every > 0 && !j->over) {
			timer **tail;
			t->due = now + (unsigned long) t->every;
			for (tail = &j->ts; *tail != NULL; tail = &(*tail)->next)
				;
			*tail = t;
		} else {
			timer_free(j, t);
		}
	}
	arm(j, JS_EVENT_MS);
	jobs(j);
	sweep(j);
}

int js_event(doc *d, dnode *n, const char *type)
{
	ejs *j = d->js;
	int r, bubbles, cancelable;
	if (j == NULL)
		return 0;
	bubbles = strcmp(type, "focus") != 0 && strcmp(type, "blur") != 0;
	cancelable = !strcmp(type, "click") || !strcmp(type, "submit") || !strcmp(type, "keydown");
	arm(j, JS_EVENT_MS);
	r = fire(j, n, type, bubbles, cancelable);
	jobs(j);
	sweep(j);
	return r;
}

void js_run(doc *d, const char *code)
{
	ejs *j = d->js;
	JSValue r;
	if (j == NULL)
		return;
	arm(j, JS_EVENT_MS);
	r = JS_Eval(j->ctx, code, strlen(code), "<javascript:>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r))
		report(j);
	JS_FreeValue(j->ctx, r);
	jobs(j);
	sweep(j);
}

void js_toggled(doc *d, dnode *n)
{
	if (d->js != NULL)
		toggle_queue(d->js, n, dn_attr(n, "open") == NULL);
}

void js_set_page(doc *d, const page *p)
{
	ejs *j = d->js;
	if (j != NULL)
		j->pg = p;
}

void js_stats(doc *d, unsigned long *ms, long *bytes)
{
	ejs *j = d->js;
	*ms = j ? j->ms : 0;
	*bytes = j ? j->bytes : 0;
}

static void js_drop(void *p)
{
	ejs *j = p;
	long i;
	if (j == NULL)
		return;
	if (j->ctx != NULL) {
		JSContext *ctx = j->ctx;
		listener *l, *ln;
		timer *t, *tn;
		for (l = j->ls; l != NULL; l = ln) {
			ln = l->next;
			JS_FreeValue(ctx, l->fn);
			free(l);
		}
		for (t = j->ts; t != NULL; t = tn) {
			tn = t->next;
			timer_free(j, t);
		}
		while (j->tog != NULL) {
			toggle *g = j->tog;
			j->tog = g->next;
			free(g);
		}
		for (i = 0; i < j->mapcap; i++)
			if (j->map[i].n != NULL)
				JS_FreeValue(ctx, j->map[i].v);
		JS_FreeValue(ctx, j->node_proto);
		JS_FreeValue(ctx, j->elem_proto);
		JS_FreeValue(ctx, j->text_proto);
		JS_FreeValue(ctx, j->doc_proto);
		JS_FreeValue(ctx, j->frag_proto);
		JS_FreeValue(ctx, j->style_proto);
		JS_FreeValue(ctx, j->comp_proto);
		JS_FreeValue(ctx, j->cl_proto);
		JS_FreeValue(ctx, j->ev_proto);
		JS_FreeValue(ctx, j->loc_obj);
		JS_FreeContext(ctx);
	}
	if (j->rt != NULL)
		JS_FreeRuntime(j->rt);
	for (i = 0; i < j->nsel; i++)
		free(j->sel[i].s);
	free(j->map);
	free(j->started);
	nav_clear(j);
	free(j);
}
