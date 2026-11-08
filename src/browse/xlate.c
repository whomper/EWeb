/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * xlate.c - a page translated on the Gateway, Direct (Erez, 8 Nov 2026;
 * AERA-Atari's standards/TRANSLATION-SERVICE.md, DESIGN.md: Translation).
 *
 * The page is shown at once as it came; then its text goes to the
 * Gateway's translategate (common's translate.c) in pieces of about 2 KB,
 * those on the screen first, and each text node is given its translation
 * in the document's memory (nothing of it on the disk). The page is laid
 * out again when the screen's pieces are back, and after that at most
 * every XL_RELAYOUT_MS while the rest comes.
 *
 * A unit is one text node's words (white space collapsed, a space kept at
 * either end where it had one), a paragraph of the request: the answer has
 * as many, in order (translate.c checks it has no more). Left as they are:
 * scripts, styles, <textarea>, <pre> (code, mostly), what translate="no"
 * marks, SVG and MathML, and text without a letter.
 *
 * With Translate off none of this is called (browser.translate NULL);
 * br_translation_runs counts every call that does work, for the rigs.
 */
#include <stdlib.h>
#include <string.h>

#include "browse.h"
#include "translate.h"
#include "conn.h"
#include "plat.h"
#include "util.h"

#ifndef EW_ST

#define XL_RELAYOUT_MS 3000L	/* the rest's pieces: a layout at most this often */

typedef struct {
	dnode *n;		/* NULL: the page's title */
	int32_t y;		/* where it was laid out (the nearest that has a place) */
	long off, len;		/* its words in xl.words */
	uint8_t lead, trail;	/* a space before, after */
	uint8_t done;
} XUNIT;

struct xlate {
	doc *d;			/* the document it is for, and its showing */
	unsigned long shows;
	char host[104];
	unsigned short port;
	char to[TR_LANG];
	XUNIT *u;
	int nu, left;		/* units, and those not yet done */
	SBUF words;
	int changed;		/* translations put in since the last layout */
	int dir_set;		/* the page's <html> has the language's dir and lang */
	int screen_done;	/* the screen's pieces are back (laid out) */
	unsigned long laid;	/* when it was last laid out */
	long bytes, done_bytes;
	char from[TR_LANG];	/* the language the Gateway found */
};

static int has_letter(const char *s, long n)
{
	long i;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char) s[i];
		if (c >= 0x80 || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'))
			return 1;
	}
	return 0;
}

/* written right to left (webgate's list) */
static int rtl_lang(const char *code)
{
	static const char *const rtl[] = { "he", "ar", "fa", "ur", "yi", "ps", "dv" };
	unsigned i;
	for (i = 0; i < sizeof rtl / sizeof rtl[0]; i++)
		if (!strcmp(code, rtl[i]))
			return 1;
	return 0;
}

static int blank(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* a subtree left as it is */
static int kept_as_is(const dnode *n)
{
	const char *t;
	if (n->type != DN_ELEMENT)
		return 0;
	if (n->ns != DN_NS_HTML)
		return 1;	/* SVG, MathML */
	switch (n->tag) {
	case T_SCRIPT: case T_STYLE: case T_TEXTAREA: case T_PRE: case T_TEMPLATE:
	case T_TITLE: case T_SELECT: case T_HEAD:
		return 1;
	}
	t = dn_attr(n, "translate");
	return t != NULL && !strcmp(t, "no");
}

static int32_t place_of(const dnode *n)
{
	for (; n != NULL; n = n->parent)
		if (n->y >= 0)
			return n->y;
	return 0;
}

static int add_unit(struct xlate *x, dnode *n, const char *s, long len)
{
	XUNIT *u;
	long i, start = x->words.len;
	int sp = 0;
	if (!has_letter(s, len))
		return 1;
	if ((x->nu & 255) == 0) {
		XUNIT *m = realloc(x->u, (x->nu + 256) * sizeof *m);
		if (m == NULL)
			return 0;
		x->u = m;
	}
	u = &x->u[x->nu];
	u->n = n;
	u->y = n != NULL ? place_of(n) : -1;	/* (the title: first) */
	u->lead = blank((unsigned char) s[0]);
	u->trail = blank((unsigned char) s[len - 1]);
	u->done = 0;
	for (i = 0; i < len; i++) {
		if (blank((unsigned char) s[i])) {
			sp = x->words.len > start;
			continue;
		}
		if (sp && !sb_addc(&x->words, ' '))
			return 0;
		sp = 0;
		if (!sb_addc(&x->words, s[i]))
			return 0;
	}
	u->off = start;
	u->len = x->words.len - start;
	if (u->len == 0 || u->len > TR_PIECE - 16) {
		x->words.len = start;	/* (none, or more than a request carries: as it is) */
		return 1;
	}
	x->bytes += u->len;
	x->nu++;
	x->left++;
	return 1;
}

static int collect(struct xlate *x, dnode *n)
{
	dnode *c;
	for (c = n->first; c != NULL; c = c->next) {
		if (c->type == DN_TEXT) {
			if (c->len && !add_unit(x, c, c->text, c->len))
				return 0;
		} else if (c->type == DN_ELEMENT && !kept_as_is(c)) {
			if (!collect(x, c))
				return 0;
		}
	}
	return 1;
}

void br_translate_stop(browser *b)
{
	struct xlate *x = b->xl;
	if (x == NULL)
		return;
	free(x->u);
	sb_free(&x->words);
	free(x);
	b->xl = NULL;
}

int br_translate_start(browser *b, const char *host, unsigned short port)
{
	struct xlate *x;
	br_translate_stop(b);
	if (b->translate == NULL || b->d == NULL || b->d->root == NULL)
		return 0;
	br_translation_runs++;
	if ((x = calloc(1, sizeof *x)) == NULL)
		return 0;
	x->d = b->d;
	x->shows = b->shows;
	str_copy(x->host, host, sizeof x->host);
	x->port = port;
	str_copy(x->to, b->translate, sizeof x->to);
	sb_init(&x->words);
	if (!add_unit(x, NULL, b->title, (long) strlen(b->title)) || !collect(x, b->d->root)) {
		b->xl = x;
		br_translate_stop(b);
		return 0;
	}
	x->laid = pf_ms();
	b->xl = x;
	if (x->left == 0) {
		br_translate_stop(b);
		return 0;
	}
	return 1;
}

/* is it still the page it was begun for? (else forgotten) */
static struct xlate *current(browser *b)
{
	struct xlate *x = b->xl;
	if (x != NULL && (x->d != b->d || x->shows != b->shows)) {
		br_translate_stop(b);
		x = NULL;
	}
	return x;
}

static int on_screen(const XUNIT *u, int32_t top, int32_t height)
{
	return u->n == NULL || (u->y >= top && u->y < top + height);
}

int br_translate_pending(browser *b, int32_t top, int32_t height, int screen_only)
{
	struct xlate *x = current(b);
	int i;
	if (x == NULL || x->left == 0)
		return 0;
	if (!screen_only)
		return 1;
	for (i = 0; i < x->nu; i++)
		if (!x->u[i].done && on_screen(&x->u[i], top, height))
			return 1;
	return 0;
}

/* the translation put in the node (or the title) */
static int put(browser *b, XUNIT *u, const char *s, long n)
{
	char *t;
	long len;
	while (n > 0 && blank((unsigned char) *s))
		s++, n--;
	while (n > 0 && blank((unsigned char) s[n - 1]))
		n--;
	if (n == 0)
		return 1;	/* (nothing came: as it was) */
	b->tr_shows = b->shows;
	if (u->n == NULL) {
		long m = n < (long) sizeof b->title - 1 ? n : (long) sizeof b->title - 1;
		memcpy(b->title, s, m);
		b->title[m] = 0;
		str_copy(b->d->title, b->title, sizeof b->d->title);
		return 1;
	}
	len = u->lead + n + u->trail;
	if ((t = doc_alloc(b->d, len + 1)) == NULL)
		return 0;
	if (u->lead)
		t[0] = ' ';
	memcpy(t + u->lead, s, n);
	if (u->trail)
		t[u->lead + n] = ' ';
	t[len] = 0;
	u->n->text = t;
	u->n->len = u->n->cap = (uint32_t) len;
	return 1;
}

/* the answer's paragraphs, one a unit: blank lines between them */
static int split_answer(browser *b, struct xlate *x, int *idx, int k, const char *s, long n)
{
	int i = 0;
	long p = 0;
	while (i < k) {
		long q = p, e;
		while (q < n && !(s[q] == '\n' && (q + 1 < n && (s[q + 1] == '\n' ||
			  (s[q + 1] == '\r' && q + 2 < n && s[q + 2] == '\n')))))
			q++;
		if (i == k - 1)
			q = n;
		else if (q >= n)
			return 0;	/* fewer than were asked: none of them put in */
		e = q;
		if (!put(b, &x->u[idx[i]], s + p, e - p))
			return -1;
		while (q < n && (s[q] == '\n' || s[q] == '\r'))
			q++;
		p = q;
		i++;
	}
	return 1;
}

int br_translate_step(browser *b, int32_t top, int32_t height, char *err, int errlen)
{
	struct xlate *x = current(b);
	SBUF req, out;
	int idx[1024], k = 0, i, screen = 0, same = 0, r;
	long size = 0;
	char from[TR_LANG];
	err[0] = 0;
	if (x == NULL || x->left == 0)
		return 0;
	br_translation_runs++;
	/* the screen's first, then the rest in the page's order */
	for (i = 0; i < x->nu; i++)
		if (!x->u[i].done && on_screen(&x->u[i], top, height)) {
			screen = 1;
			break;
		}
	sb_init(&req);
	for (i = 0; i < x->nu && k < (int) (sizeof idx / sizeof idx[0]); i++) {
		XUNIT *u = &x->u[i];
		if (u->done || (screen && !on_screen(u, top, height)))
			continue;
		if (k > 0 && size + 2 + u->len > TR_PIECE_DEFAULT)
			break;
		if ((k > 0 && !sb_add(&req, "\n\n", 2)) || !sb_add(&req, x->words.s + u->off, u->len)) {
			sb_free(&req);
			str_copy(err, "out of memory", errlen);
			return -1;
		}
		size += (k > 0 ? 2 : 0) + u->len;
		idx[k++] = i;
	}
	sb_init(&out);
	r = translate_piece(x->host, x->port, x->to, x->from[0] ? x->from : NULL, req.s, req.len, &out,
			    from, &same, err, errlen);
	sb_free(&req);
	if (!r) {
		sb_free(&out);
		return -1;
	}
	if (!x->from[0] && from[0])
		str_copy(x->from, from, sizeof x->from);
	r = same ? 1 : split_answer(b, x, idx, k, out.s != NULL ? out.s : "", out.len);
	sb_free(&out);
	if (r < 0) {
		str_copy(err, "out of memory", errlen);
		return -1;
	}
	for (i = 0; i < k; i++) {
		x->u[idx[i]].done = 1;
		x->done_bytes += x->u[idx[i]].len;
	}
	x->left -= k;
	if (r > 0 && !same)
		x->changed = 1;
	/* laid out again: the screen's when they are all back, then at most
	   every XL_RELAYOUT_MS, and at the end */
	if (x->changed && ((!x->screen_done && !br_translate_pending(b, top, height, 1)) || x->left == 0 ||
			   (long) (pf_ms() - x->laid) >= XL_RELAYOUT_MS)) {
		int restyled = 0;
		x->screen_done = 1;
		x->changed = 0;
		x->laid = pf_ms();
		if (!x->dir_set && b->d->html != NULL) {
			/* the page's direction and language are the translation's
			   (as webgate sets them): a Hebrew paragraph right to left,
			   against the right edge, its Latin words and numbers in
			   their own direction */
			x->dir_set = 1;
			if (!dn_set_attr(b->d, b->d->html, "dir", rtl_lang(x->to) ? "rtl" : "ltr", 3) ||
			    !dn_set_attr(b->d, b->d->html, "lang", x->to, strlen(x->to))) {
				str_copy(err, "out of memory", errlen);
				return -1;
			}
			restyled = 1;
		}
		if (restyled ? !br_restyle(b) : !br_relayout(b, b->width)) {
			str_copy(err, "out of memory", errlen);
			return -1;
		}
		return 2;
	}
	return 1;
}

void br_translate_progress(browser *b, long *done, long *total, char *from, int fromlen)
{
	struct xlate *x = current(b);
	*done = x != NULL ? x->done_bytes : 0;
	*total = x != NULL ? x->bytes : 0;
	if (from != NULL)
		str_copy(from, x != NULL ? x->from : "", fromlen);
}

#else	/* the ST edition: no translation (frozen, DESIGN.md) */

int br_translate_start(browser *b, const char *host, unsigned short port) { return 0; }
int br_translate_pending(browser *b, int32_t top, int32_t height, int screen_only) { return 0; }
int br_translate_step(browser *b, int32_t top, int32_t height, char *err, int errlen) { err[0] = 0; return 0; }
void br_translate_stop(browser *b) { }
void br_translate_progress(browser *b, long *done, long *total, char *from, int fromlen)
{
	*done = *total = 0;
	if (from != NULL && fromlen > 0)
		from[0] = 0;
}

#endif
