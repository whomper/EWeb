/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * form.c - a page's forms (form.h), after the HTML Standard: the form
 * a control belongs to (its form attribute, the form the parser had
 * open, else the form around it), what it holds, and a form's
 * submission (constructing the entry list, then the
 * application/x-www-form-urlencoded, multipart/form-data or text/plain
 * encoding, in the page's charset).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/codec.h>

#include "form.h"

static int lower_eq(const char *a, const char *b)
{
	if (a == NULL)
		return 0;
	for (; *a && *b; a++, b++) {
		char c = *a >= 'A' && *a <= 'Z' ? (char) (*a + 32) : *a;
		if (c != *b)
			return 0;
	}
	return *a == 0 && *b == 0;
}

static int is_html(const dnode *n, int tag)
{
	return n->type == DN_ELEMENT && n->ns == DN_NS_HTML && n->tag == tag;
}

/* the element after n in document order, below top */
static dnode *next_node(dnode *n, const dnode *top, int into)
{
	if (into && n->first != NULL)
		return n->first;
	while (n != NULL && n != top) {
		if (n->next != NULL)
			return n->next;
		n = n->parent;
	}
	return NULL;
}

/* the text of n's descendants, as it is */
static void text_into(const dnode *n, char **s, long *len, long *cap)
{
	const dnode *k;
	for (k = n->first; k != NULL; k = k->next) {
		if (k->type == DN_TEXT && k->len > 0) {
			if (*len + (long) k->len + 1 > *cap) {
				long nc = (*len + (long) k->len + 1) * 2;
				char *t = realloc(*s, (size_t) nc);
				if (t == NULL)
					return;
				*s = t;
				*cap = nc;
			}
			memcpy(*s + *len, k->text, k->len);
			*len += k->len;
			(*s)[*len] = 0;
		} else if (k->type == DN_ELEMENT) {
			text_into(k, s, len, cap);
		}
	}
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* the bytes of the UTF-8 letter that starts with c */
static int u8len(unsigned char c)
{
	return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}

/* s with its white space collapsed and stripped, into out (whole
   letters only) */
static void collapse(const char *s, long n, char *out, int size)
{
	int o = 0, sp = 0;
	long i = 0;
	while (i < n) {
		int k;
		if (is_space(s[i])) {
			sp = o > 0;
			i++;
			continue;
		}
		k = u8len((unsigned char) s[i]);
		if (i + k > n)
			k = (int) (n - i);
		if (o + sp + k > size - 1)
			break;
		if (sp)
			out[o++] = ' ';
		sp = 0;
		memcpy(out + o, s + i, (size_t) k);
		o += k;
		i += k;
	}
	out[o] = 0;
}

void form_option_text(const dnode *o, char *out, int size)
{
	const char *l = dn_attr(o, "label");
	char *s = NULL;
	long len = 0, cap = 0;
	if (l != NULL && *l) {
		collapse(l, (long) strlen(l), out, size);
		return;
	}
	text_into(o, &s, &len, &cap);
	collapse(s ? s : "", len, out, size);
	free(s);
}

/* ---------------------------------------------------------------- the controls */

static int kind_of(dnode *n)
{
	const char *t = dn_attr(n, "type");
	switch (n->tag) {
	case T_TEXTAREA:
		return FC_TEXTAREA;
	case T_SELECT:
		return FC_SELECT;
	case T_BUTTON:
		if (lower_eq(t, "reset"))
			return FC_RESET;
		if (lower_eq(t, "button"))
			return FC_BUTTON;
		return FC_SUBMIT;
	}
	if (t == NULL)
		return FC_TEXT;
	if (lower_eq(t, "password"))
		return FC_PASSWORD;
	if (lower_eq(t, "hidden"))
		return FC_HIDDEN;
	if (lower_eq(t, "checkbox"))
		return FC_CHECKBOX;
	if (lower_eq(t, "radio"))
		return FC_RADIO;
	if (lower_eq(t, "submit"))
		return FC_SUBMIT;
	if (lower_eq(t, "reset"))
		return FC_RESET;
	if (lower_eq(t, "button"))
		return FC_BUTTON;
	if (lower_eq(t, "image"))
		return FC_IMAGE;
	if (lower_eq(t, "file"))
		return FC_FILE;
	return FC_TEXT;		/* text, search, email, url, tel, number, date... and the unknown */
}

int form_typed(const fctl *c)
{
	return c->kind == FC_TEXT || c->kind == FC_PASSWORD || c->kind == FC_TEXTAREA;
}

static int room(fctl *c, long n)
{
	if (c->len + n + 1 > c->cap) {
		long nc = (c->len + n + 1) * 2;
		char *t;
		if (nc < 32)
			nc = 32;
		t = realloc(c->v, (size_t) nc);
		if (t == NULL)
			return 0;
		c->v = t;
		c->cap = nc;
	}
	return 1;
}

/* the value as the page gives it: a field's without line breaks, a
   textarea's with \n for its line breaks */
static void default_value(fctl *c)
{
	const char *v;
	long i, o;
	c->len = 0;
	if (c->kind == FC_TEXTAREA) {
		char *s = NULL;
		long len = 0, cap = 0;
		text_into(c->n, &s, &len, &cap);
		if (s != NULL && room(c, len))
			memcpy(c->v, s, (size_t) len);
		free(s);
		c->len = s != NULL ? len : 0;
	} else if ((v = dn_attr(c->n, "value")) != NULL) {
		long n = (long) strlen(v);
		if (room(c, n)) {
			memcpy(c->v, v, (size_t) n);
			c->len = n;
		}
	}
	if (c->v == NULL && !room(c, 0))
		return;
	/* CR LF and CR to LF; a field loses them */
	for (i = o = 0; i < c->len; i++) {
		char ch = c->v[i];
		if (ch == '\r') {
			if (i + 1 < c->len && c->v[i + 1] == '\n')
				continue;
			ch = '\n';
		}
		if (ch == '\n' && c->kind != FC_TEXTAREA && c->kind != FC_HIDDEN)
			continue;
		c->v[o++] = ch;
	}
	c->len = o;
	c->v[o] = 0;
	c->cur = c->len;
	c->first = 0;
}

/* an option disabled, or in a disabled optgroup */
static int opt_disabled(const dnode *o)
{
	return dn_attr(o, "disabled") != NULL ||
	       (o->parent != NULL && is_html(o->parent, T_OPTGROUP) && dn_attr(o->parent, "disabled") != NULL);
}

/* a select's options: its own and its optgroups' */
static void options(fctl *c)
{
	dnode *o;
	int n = 0, i, last = -1;
	for (o = c->n->first; o != NULL; o = o->next) {
		dnode *k;
		if (is_html(o, T_OPTION))
			n++;
		else if (o->type == DN_ELEMENT)
			for (k = o->first; k != NULL; k = k->next)
				if (is_html(k, T_OPTION))
					n++;
	}
	c->opt = n ? malloc((size_t) n * sizeof *c->opt) : NULL;
	c->sel = n ? calloc((size_t) n, 1) : NULL;
	if (n && (c->opt == NULL || c->sel == NULL)) {
		free(c->opt);
		free(c->sel);
		c->opt = NULL;
		c->sel = NULL;
		return;
	}
	for (o = c->n->first; o != NULL; o = o->next) {
		dnode *k;
		if (is_html(o, T_OPTION))
			c->opt[c->nopt++] = o;
		else if (o->type == DN_ELEMENT)
			for (k = o->first; k != NULL; k = k->next)
				if (is_html(k, T_OPTION))
					c->opt[c->nopt++] = k;
	}
	for (i = 0; i < c->nopt; i++)
		if (dn_attr(c->opt[i], "selected") != NULL) {
			c->sel[i] = 1;
			if (!c->multiple && last >= 0)
				c->sel[last] = 0;
			last = i;
		}
	if (!c->multiple && last < 0) {
		const char *sz = dn_attr(c->n, "size");
		if (sz == NULL || atoi(sz) <= 1)
			for (i = 0; i < c->nopt; i++)
				if (!opt_disabled(c->opt[i])) {
					c->sel[i] = 1;
					break;
				}
	}
}

static void defaults(fctl *c)
{
	c->checked = dn_attr(c->n, "checked") != NULL;
	if (c->kind == FC_SELECT) {
		free(c->opt);
		free(c->sel);
		c->opt = NULL;
		c->sel = NULL;
		c->nopt = 0;
		options(c);
	} else {
		default_value(c);
	}
}

/* only the last of a group ticked stays ticked, as when the parser
   puts them in one by one */
static void radio_groups(doc *d)
{
	fctl *c, *k;
	for (c = d->ctls; c != NULL; c = c->next) {
		const char *name = dn_attr(c->n, "name");
		if (c->kind != FC_RADIO || !c->checked || name == NULL || *name == 0)
			continue;
		for (k = d->ctls; k != c; k = k->next)
			if (k->kind == FC_RADIO && k->checked && k->form == c->form && dn_attr(k->n, "name") &&
			    strcmp(dn_attr(k->n, "name"), name) == 0)
				k->checked = 0;
	}
}

/* in a disabled fieldset, and not in its first legend */
static int in_disabled_fieldset(const dnode *n)
{
	const dnode *p, *child = n;
	for (p = n->parent; p != NULL; child = p, p = p->parent) {
		const dnode *l;
		if (!is_html(p, T_FIELDSET) || dn_attr(p, "disabled") == NULL)
			continue;
		for (l = p->first; l != NULL; l = l->next)
			if (is_html(l, T_LEGEND))
				break;
		if (l != NULL && l == child)
			continue;
		return 1;
	}
	return 0;
}

int form_init(doc *d)
{
	dnode *n, **forms = NULL;
	fctl **tail = &d->ctls, **old = NULL, *c;
	int nforms = 0, cap = 0, ok = 1, nold = 0, i;

	if (d->root == NULL)
		return 1;
	/* again, after a script changed the tree: the controls still in it
	   keep their state, those gone are let go */
	for (c = d->ctls; c != NULL; c = c->next)
		nold++;
	if (nold > 0) {
		if ((old = malloc((size_t) nold * sizeof *old)) == NULL)
			return 0;
		for (nold = 0, c = d->ctls; c != NULL; c = c->next) {
			c->gone = 1;
			old[nold++] = c;
		}
	}
	d->ctls = NULL;
	/* the forms first: a control may name one further down */
	for (n = d->root; n != NULL; n = next_node(n, d->root, !is_html(n, T_TEMPLATE)))
		if (is_html(n, T_FORM)) {
			if (nforms == cap) {
				dnode **t = realloc(forms, (size_t) (cap = cap ? cap * 2 : 8) * sizeof *forms);
				if (t == NULL) {
					free(forms);
					return 0;
				}
				forms = t;
			}
			forms[nforms++] = n;
		}
	for (n = d->root; n != NULL; n = next_node(n, d->root, !is_html(n, T_TEMPLATE))) {
		const char *fa;
		if (n->type != DN_ELEMENT || n->ns != DN_NS_HTML ||
		    (n->tag != T_INPUT && n->tag != T_BUTTON && n->tag != T_SELECT && n->tag != T_TEXTAREA))
			continue;
		if ((c = n->ctl) != NULL) {
			c->gone = 0;
			*tail = c;
			tail = &c->next;
			continue;
		}
		c = calloc(1, sizeof *c);
		if (c == NULL) {
			ok = 0;
			break;
		}
		c->n = n;
		c->kind = (uint8_t) kind_of(n);
		c->maxlen = dn_attr(n, "maxlength") ? atol(dn_attr(n, "maxlength")) : -1;
		if (c->maxlen < 0 || c->maxlen > 524288)
			c->maxlen = -1;
		c->disabled = dn_attr(n, "disabled") != NULL || in_disabled_fieldset(n);
		c->readonly = dn_attr(n, "readonly") != NULL;
		c->multiple = n->tag == T_SELECT && dn_attr(n, "multiple") != NULL;
		/* its form */
		if ((fa = dn_attr(n, "form")) != NULL) {
			for (i = 0; i < nforms; i++)
				if (dn_attr(forms[i], "id") != NULL && strcmp(dn_attr(forms[i], "id"), fa) == 0)
					break;
			c->form = i < nforms ? forms[i] : NULL;
		} else {
			dnode *p;
			for (i = 0; i < nforms && n->owner != NULL; i++)
				if (forms[i] == n->owner)
					break;
			if (n->owner != NULL && i < nforms) {
				c->form = n->owner;
			} else {
				for (p = n->parent; p != NULL && !is_html(p, T_FORM); p = p->parent)
					;
				c->form = p;
			}
		}
		defaults(c);
		n->ctl = c;
		*tail = c;
		tail = &c->next;
	}
	*tail = NULL;
	free(forms);
	if (old == NULL) {
		radio_groups(d);
		return ok;
	}
	for (i = 0; i < nold; i++)
		if (old[i]->gone) {
			c = old[i];
			if (c->n->ctl == c)
				c->n->ctl = NULL;
			free(c->v);
			free(c->opt);
			free(c->sel);
			free(c);
		}
	free(old);
	return ok;
}

void form_free(doc *d)
{
	fctl *c, *next;
	for (c = d->ctls; c != NULL; c = next) {
		next = c->next;
		c->n->ctl = NULL;
		free(c->v);
		free(c->opt);
		free(c->sel);
		free(c);
	}
	d->ctls = NULL;
}

void form_reset(doc *d, dnode *f)
{
	fctl *c;
	for (c = d->ctls; c != NULL; c = c->next)
		if (c->form == f)
			defaults(c);
	radio_groups(d);
}

/* ---------------------------------------------------------------- what a control shows */

static void put(char *out, int size, int *o, const char *s, long n)
{
	if (*o + n > size - 1) {
		n = size - 1 - *o;
		/* not in the middle of a letter */
		while (n > 0 && ((unsigned char) s[n] & 0xc0) == 0x80)
			n--;
	}
	if (n <= 0)
		return;
	memcpy(out + *o, s, (size_t) n);
	*o += (int) n;
	out[*o] = 0;
}

void form_label(const fctl *c, char *out, int size)
{
	const char *v;
	int o = 0, i;
	out[0] = 0;
	switch (c->kind) {
	case FC_PASSWORD: {
		long k;
		for (k = 0; k < c->len; k++)
			if (((unsigned char) c->v[k] & 0xc0) != 0x80)
				put(out, size, &o, "\xe2\x80\xa2", 3);	/* a bullet a letter */
		return;
	}
	case FC_CHECKBOX:
		put(out, size, &o, c->checked ? "\xe2\x9c\x93" : " ", c->checked ? 3 : 1);
		return;
	case FC_RADIO:
		put(out, size, &o, c->checked ? "\xe2\x97\x8f" : "\xe2\x97\x8b", 3);
		return;
	case FC_SUBMIT:
	case FC_RESET:
	case FC_BUTTON:
		if (c->n->tag == T_BUTTON) {
			char *s = NULL;
			long len = 0, cap = 0;
			text_into(c->n, &s, &len, &cap);
			collapse(s ? s : "", len, out, size);
			free(s);
			return;
		}
		v = dn_attr(c->n, "value");
		if (v == NULL)
			v = c->kind == FC_SUBMIT ? "Submit" : c->kind == FC_RESET ? "Reset" : "";
		collapse(v, (long) strlen(v), out, size);
		return;
	case FC_IMAGE:
		v = dn_attr(c->n, "alt");
		if (v == NULL || *v == 0)
			v = dn_attr(c->n, "value");
		if (v == NULL || *v == 0)
			v = "Submit";
		collapse(v, (long) strlen(v), out, size);
		return;
	case FC_FILE:
		put(out, size, &o, "No file", 7);
		return;
	case FC_SELECT: {
		int any = 0;
		for (i = 0; i < c->nopt; i++)
			if (c->sel[i]) {
				char t[200];
				form_option_text(c->opt[i], t, sizeof t);
				if (any)
					put(out, size, &o, ", ", 2);
				put(out, size, &o, t, (long) strlen(t));
				any = 1;
				if (!c->multiple)
					break;
			}
		return;
	}
	default:
		put(out, size, &o, c->v ? c->v : "", c->len);
	}
}

const char *form_placeholder(const fctl *c)
{
	if (!form_typed(c) || c->len > 0)
		return NULL;
	return dn_attr(c->n, "placeholder");
}

/* ---------------------------------------------------------------- editing */

static long chars(const char *s, long n)
{
	long i, k = 0;
	for (i = 0; i < n; i++)
		if (((unsigned char) s[i] & 0xc0) != 0x80)
			k++;
	return k;
}

int form_insert(fctl *c, const char *s, long n)
{
	long i, o, k;
	char *t;
	if (c->disabled || c->readonly || !form_typed(c) || n <= 0)
		return 0;
	t = malloc((size_t) n);
	if (t == NULL)
		return 0;
	/* a field takes no line breaks; CR LF and CR become LF */
	for (i = o = 0; i < n; i++) {
		char ch = s[i];
		if (ch == '\r') {
			if (i + 1 < n && s[i + 1] == '\n')
				continue;
			ch = '\n';
		}
		if (ch == '\n' && c->kind != FC_TEXTAREA)
			continue;
		t[o++] = ch;
	}
	if (c->maxlen >= 0) {
		/* at most maxlength letters: what is too much is cut, whole letters */
		long have = chars(c->v, c->len), j;
		for (j = 0, k = 0; j < o; j++) {
			if (((unsigned char) t[j] & 0xc0) != 0x80) {
				if (have + k >= c->maxlen)
					break;
				k++;
			}
		}
		o = j;
	}
	if (o == 0 || !room(c, o)) {
		free(t);
		return 0;
	}
	memmove(c->v + c->cur + o, c->v + c->cur, (size_t) (c->len - c->cur));
	memcpy(c->v + c->cur, t, (size_t) o);
	c->len += o;
	c->cur += o;
	c->v[c->len] = 0;
	free(t);
	return 1;
}

int form_backspace(fctl *c)
{
	long k;
	if (c->disabled || c->readonly || !form_typed(c) || c->cur == 0)
		return 0;
	k = c->cur - 1;
	while (k > 0 && ((unsigned char) c->v[k] & 0xc0) == 0x80)
		k--;
	memmove(c->v + k, c->v + c->cur, (size_t) (c->len - c->cur + 1));
	c->len -= c->cur - k;
	c->cur = k;
	return 1;
}

int form_delete(fctl *c)
{
	long k;
	if (c->disabled || c->readonly || !form_typed(c) || c->cur >= c->len)
		return 0;
	k = c->cur + 1;
	while (k < c->len && ((unsigned char) c->v[k] & 0xc0) == 0x80)
		k++;
	memmove(c->v + c->cur, c->v + k, (size_t) (c->len - k + 1));
	c->len -= k - c->cur;
	return 1;
}

void form_set(fctl *c, const char *s, long n)
{
	c->len = 0;
	c->cur = 0;
	if (c->v != NULL)
		c->v[0] = 0;
	if (form_typed(c)) {
		int ro = c->readonly;
		c->readonly = 0;
		form_insert(c, s, n);
		c->readonly = (uint8_t) ro;
	} else if (room(c, n)) {
		memcpy(c->v, s, (size_t) n);
		c->len = n;
		c->v[n] = 0;
	}
	c->first = 0;
}

void form_check(doc *d, fctl *c)
{
	fctl *k;
	const char *name;
	if (c->disabled)
		return;
	if (c->kind == FC_CHECKBOX) {
		c->checked = !c->checked;
		return;
	}
	if (c->kind != FC_RADIO)
		return;
	c->checked = 1;
	name = dn_attr(c->n, "name");
	if (name == NULL || *name == 0)
		return;
	for (k = d->ctls; k != NULL; k = k->next)
		if (k != c && k->kind == FC_RADIO && k->form == c->form && dn_attr(k->n, "name") &&
		    strcmp(dn_attr(k->n, "name"), name) == 0)
			k->checked = 0;
}

void form_choose(fctl *c, int i)
{
	if (c->kind != FC_SELECT || c->disabled || i < 0 || i >= c->nopt || opt_disabled(c->opt[i]))
		return;
	if (c->multiple) {
		c->sel[i] = !c->sel[i];
	} else {
		memset(c->sel, 0, (size_t) c->nopt);
		c->sel[i] = 1;
	}
}

fctl *form_next(doc *d, fctl *c, int dir)
{
	fctl *k, *prev = NULL;
	if (dir > 0) {
		for (k = c ? c->next : d->ctls; k != NULL; k = k->next)
			if (k->kind != FC_HIDDEN && !k->disabled)
				return k;
		return NULL;
	}
	for (k = d->ctls; k != NULL && k != c; k = k->next)
		if (k->kind != FC_HIDDEN && !k->disabled)
			prev = k;
	return prev;
}

/* ---------------------------------------------------------------- the submission */

fctl *form_default(doc *d, fctl *c, int *send)
{
	fctl *k;
	int fields = 0;
	*send = 0;
	if (c == NULL || c->form == NULL)
		return NULL;
	for (k = d->ctls; k != NULL; k = k->next) {
		if (k->form != c->form)
			continue;
		if (k->kind == FC_SUBMIT || k->kind == FC_IMAGE) {
			*send = !k->disabled;
			return k->disabled ? NULL : k;
		}
		if (k->kind == FC_TEXT || k->kind == FC_PASSWORD)
			fields++;
	}
	*send = fields == 1;
	return NULL;
}

/* the page's charset for a form: one byte a letter, each byte's letter
   (from libparserutils' own tables), else UTF-8 */
typedef struct {
	int utf8;
	char name[40];
	uint32_t uni[256];
} CSET;

static void cset_init(CSET *cs, const doc *d, const dnode *form)
{
	parserutils_charset_codec *codec = NULL;
	const char *ac = dn_attr(form, "accept-charset");
	int i;
	memset(cs, 0, sizeof *cs);
	cs->utf8 = 1;
	strcpy(cs->name, "UTF-8");
	if (ac != NULL && (strstr(ac, "utf-8") || strstr(ac, "UTF-8") || strstr(ac, "Utf-8")))
		return;
	if (d->charset[0] == 0 || lower_eq(d->charset, "utf-8") ||
	    parserutils_charset_codec_create(d->charset, &codec) != PARSERUTILS_OK)
		return;
	for (i = 0; i < 256; i++) {
		uint8_t in = (uint8_t) i, outb[16];
		const uint8_t *src = &in;
		uint8_t *dst = outb;
		size_t sl = 1, dl = sizeof outb;
		cs->uni[i] = 0xfffd;
		parserutils_charset_codec_reset(codec);
		if (parserutils_charset_codec_decode(codec, &src, &sl, &dst, &dl) == PARSERUTILS_OK &&
		    sizeof outb - dl == 4) {
			uint32_t u = (uint32_t) outb[0] << 24 | (uint32_t) outb[1] << 16 | (uint32_t) outb[2] << 8 | outb[3];
			cs->uni[i] = u;
		} else if (i < 0x80) {
			/* a multibyte charset: UTF-8 instead */
			parserutils_charset_codec_destroy(codec);
			return;
		}
	}
	parserutils_charset_codec_destroy(codec);
	for (i = 0; i < 0x80; i++)
		if (cs->uni[i] != (uint32_t) i)
			return;		/* not ASCII below: UTF-8 */
	cs->utf8 = 0;
	snprintf(cs->name, sizeof cs->name, "%s", d->charset);
}

static int sb_room(char **s, long *len, long *cap, long n)
{
	if (*len + n + 1 > *cap) {
		long nc = (*len + n + 1) * 2;
		char *t = realloc(*s, (size_t) nc);
		if (t == NULL)
			return 0;
		*s = t;
		*cap = nc;
	}
	return 1;
}

typedef struct {
	char *s;
	long len, cap;
	int ok;
} OUT;

static void out_add(OUT *o, const char *s, long n)
{
	if (!o->ok || !sb_room(&o->s, &o->len, &o->cap, n)) {
		o->ok = 0;
		return;
	}
	memcpy(o->s + o->len, s, (size_t) n);
	o->len += n;
	o->s[o->len] = 0;
}

static void out_adds(OUT *o, const char *s)
{
	out_add(o, s, (long) strlen(s));
}

/* UTF-8 text in the form's charset, line breaks as CR LF; a letter the
   charset lacks as &#N; (as browsers send it) */
static void encode(OUT *o, const CSET *cs, const char *s, long n)
{
	long i = 0;
	while (i < n) {
		unsigned char ch = (unsigned char) s[i];
		uint32_t u;
		int k, len;
		if (ch == '\r' || ch == '\n') {
			out_add(o, "\r\n", 2);
			i += ch == '\r' && i + 1 < n && s[i + 1] == '\n' ? 2 : 1;
			continue;
		}
		if (ch < 0x80) {
			out_add(o, s + i, 1);
			i++;
			continue;
		}
		len = u8len(ch);
		if (i + len > n)
			len = (int) (n - i);
		if (cs->utf8) {
			out_add(o, s + i, len);
			i += len;
			continue;
		}
		u = len == 1 ? ch : ch & (0x7f >> len);
		for (k = 1; k < len; k++)
			u = u << 6 | ((unsigned char) s[i + k] & 0x3f);
		i += len;
		for (k = 0x80; k < 256; k++)
			if (cs->uni[k] == u)
				break;
		if (k < 256) {
			char b = (char) k;
			out_add(o, &b, 1);
		} else {
			char t[16];
			snprintf(t, sizeof t, "&#%lu;", (unsigned long) u);
			out_adds(o, t);
		}
	}
}

/* application/x-www-form-urlencoded's byte serializer */
static void urlencode(OUT *o, const char *s, long n)
{
	static const char hex[] = "0123456789ABCDEF";
	long i;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char) s[i];
		if (c == ' ') {
			out_add(o, "+", 1);
		} else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			   c == '*' || c == '-' || c == '.' || c == '_') {
			out_add(o, (const char *) &c, 1);
		} else {
			char t[3] = { '%', hex[c >> 4], hex[c & 15] };
			out_add(o, t, 3);
		}
	}
}

/* the entry list */
typedef struct {
	char *name, *value;	/* UTF-8 */
	int file;
} ENTRY;

typedef struct {
	ENTRY *e;
	int n, cap;
	int ok;
} ENTRIES;

static void entry(ENTRIES *l, const char *name, long nn, const char *value, long vn, int file)
{
	ENTRY *e;
	if (!l->ok)
		return;
	if (l->n == l->cap) {
		ENTRY *t = realloc(l->e, (size_t) (l->cap = l->cap ? l->cap * 2 : 16) * sizeof *t);
		if (t == NULL) {
			l->ok = 0;
			return;
		}
		l->e = t;
	}
	e = &l->e[l->n];
	e->name = malloc((size_t) nn + 1);
	e->value = malloc((size_t) vn + 1);
	if (e->name == NULL || e->value == NULL) {
		free(e->name);
		free(e->value);
		l->ok = 0;
		return;
	}
	memcpy(e->name, name, (size_t) nn);
	e->name[nn] = 0;
	memcpy(e->value, value, (size_t) vn);
	e->value[vn] = 0;
	e->file = file;
	l->n++;
}

static void entry_s(ENTRIES *l, const char *name, const char *value, int file)
{
	entry(l, name, (long) strlen(name), value, (long) strlen(value), file);
}

/* the direction of a field's text, for dirname: its first strong letter */
static const char *dir_of(const fctl *c)
{
	long i;
	for (i = 0; i < c->len; i++) {
		unsigned char ch = (unsigned char) c->v[i];
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
			return "ltr";
		if (ch == 0xd6 || ch == 0xd7)	/* U+0580-U+05FF: Hebrew */
			return "rtl";
	}
	return "ltr";
}

static void entries(doc *d, dnode *form, fctl *submitter, int32_t x, int32_t y, const CSET *cs, ENTRIES *l)
{
	fctl *c;
	for (c = d->ctls; c != NULL; c = c->next) {
		const char *name = dn_attr(c->n, "name"), *v;
		if (c->form != form || c->disabled)
			continue;
		if ((c->kind == FC_SUBMIT || c->kind == FC_IMAGE) && c != submitter)
			continue;
		if (c->kind == FC_RESET || c->kind == FC_BUTTON)
			continue;
		if ((c->kind == FC_CHECKBOX || c->kind == FC_RADIO) && !c->checked)
			continue;
		if (c->kind == FC_IMAGE) {
			char nx[300], ny[300], vx[16], vy[16];
			if (name != NULL && *name) {
				snprintf(nx, sizeof nx, "%.250s.x", name);
				snprintf(ny, sizeof ny, "%.250s.y", name);
			} else {
				strcpy(nx, "x");
				strcpy(ny, "y");
			}
			snprintf(vx, sizeof vx, "%ld", (long) x);
			snprintf(vy, sizeof vy, "%ld", (long) y);
			entry_s(l, nx, vx, 0);
			entry_s(l, ny, vy, 0);
			continue;
		}
		if (name == NULL || *name == 0)
			continue;
		switch (c->kind) {
		case FC_SELECT: {
			int i;
			for (i = 0; i < c->nopt; i++) {
				char t[1024];
				if (!c->sel[i] || opt_disabled(c->opt[i]))
					continue;
				v = dn_attr(c->opt[i], "value");
				if (v == NULL) {
					char *s = NULL;
					long len = 0, cap = 0;
					text_into(c->opt[i], &s, &len, &cap);
					collapse(s ? s : "", len, t, sizeof t);
					free(s);
					v = t;
				}
				entry_s(l, name, v, 0);
			}
			break;
		}
		case FC_CHECKBOX:
		case FC_RADIO:
			v = dn_attr(c->n, "value");
			entry_s(l, name, v ? v : "on", 0);
			break;
		case FC_FILE:
			entry_s(l, name, "", 1);
			break;
		case FC_SUBMIT:
			v = dn_attr(c->n, "value");
			entry_s(l, name, v ? v : "", 0);
			break;
		case FC_HIDDEN:
			if (lower_eq(name, "_charset_") && dn_attr(c->n, "value") == NULL) {
				entry_s(l, name, cs->name, 0);
				break;
			}
			/* fall through */
		default:
			entry(l, name, (long) strlen(name), c->v ? c->v : "", c->len, 0);
			if ((c->kind == FC_TEXT || c->kind == FC_TEXTAREA) && (v = dn_attr(c->n, "dirname")) != NULL && *v)
				entry_s(l, v, dir_of(c), 0);
		}
	}
}

static unsigned long boundary_n;

int form_submit(doc *d, fctl *from, fctl *submitter, int32_t x, int32_t y, FSUBMIT *s)
{
	dnode *form = submitter ? submitter->form : from ? from->form : NULL;
	const char *m, *a, *e;
	ENTRIES l;
	OUT o;
	CSET *cs;
	int i;

	memset(s, 0, sizeof *s);
	if (form == NULL || (submitter != NULL && submitter->disabled))
		return 0;
	if (submitter != NULL && submitter->kind != FC_SUBMIT && submitter->kind != FC_IMAGE)
		submitter = NULL;
	m = submitter && dn_attr(submitter->n, "formmethod") ? dn_attr(submitter->n, "formmethod")
							       : dn_attr(form, "method");
	if (lower_eq(m, "dialog"))
		return 0;
	s->post = lower_eq(m, "post");
	a = submitter && dn_attr(submitter->n, "formaction") ? dn_attr(submitter->n, "formaction")
							       : dn_attr(form, "action");
	e = submitter && dn_attr(submitter->n, "formenctype") ? dn_attr(submitter->n, "formenctype")
								: dn_attr(form, "enctype");
	s->enctype = lower_eq(e, "multipart/form-data") ? FS_MULTIPART : lower_eq(e, "text/plain") ? FS_PLAIN
												 : FS_URLENCODED;
	if (!s->post)
		s->enctype = FS_URLENCODED;	/* a GET puts it in the address */
	s->action = malloc(a ? strlen(a) + 1 : 1);
	cs = malloc(sizeof *cs);
	if (s->action == NULL || cs == NULL) {
		free(s->action);
		free(cs);
		s->action = NULL;
		return 0;
	}
	/* the action without white space around it */
	if (a != NULL) {
		size_t n;
		while (is_space(*a))
			a++;
		n = strlen(a);
		while (n > 0 && is_space(a[n - 1]))
			n--;
		memcpy(s->action, a, n);
		s->action[n] = 0;
	} else {
		s->action[0] = 0;
	}
	cset_init(cs, d, form);
	memset(&l, 0, sizeof l);
	l.ok = 1;
	entries(d, form, submitter, x, y, cs, &l);
	memset(&o, 0, sizeof o);
	o.ok = l.ok;
	out_add(&o, "", 0);
	if (s->enctype == FS_MULTIPART) {
		char b[48];
		OUT t;
		boundary_n = boundary_n * 1103515245UL + 12345UL + (unsigned long) (size_t) d;
		snprintf(b, sizeof b, "----EWebFormBoundary%08lx%04x", boundary_n & 0xffffffffUL, l.n & 0xffff);
		snprintf(s->ctype, sizeof s->ctype, "multipart/form-data; boundary=%s", b);
		for (i = 0; i < l.n; i++) {
			const char *p;
			out_adds(&o, "--");
			out_adds(&o, b);
			out_adds(&o, "\r\nContent-Disposition: form-data; name=\"");
			/* a name's line breaks and quotes escaped, as browsers do */
			memset(&t, 0, sizeof t);
			t.ok = 1;
			for (p = l.e[i].name; *p; p++) {
				if (*p == '\n')
					out_adds(&t, "%0A");
				else if (*p == '\r')
					out_adds(&t, "%0D");
				else if (*p == '"')
					out_adds(&t, "%22");
				else
					out_add(&t, p, 1);
			}
			encode(&o, cs, t.s ? t.s : "", t.len);
			if (!t.ok)
				o.ok = 0;
			free(t.s);
			out_adds(&o, "\"");
			if (l.e[i].file)
				out_adds(&o, "; filename=\"\"\r\nContent-Type: application/octet-stream");
			out_adds(&o, "\r\n\r\n");
			encode(&o, cs, l.e[i].value, (long) strlen(l.e[i].value));
			out_adds(&o, "\r\n");
		}
		out_adds(&o, "--");
		out_adds(&o, b);
		out_adds(&o, "--\r\n");
	} else {
		OUT t;
		for (i = 0; i < l.n; i++) {
			memset(&t, 0, sizeof t);
			t.ok = 1;
			if (s->enctype == FS_PLAIN) {
				encode(&o, cs, l.e[i].name, (long) strlen(l.e[i].name));
				out_add(&o, "=", 1);
				encode(&o, cs, l.e[i].value, (long) strlen(l.e[i].value));
				out_add(&o, "\r\n", 2);
				continue;
			}
			if (i > 0)
				out_add(&o, "&", 1);
			encode(&t, cs, l.e[i].name, (long) strlen(l.e[i].name));
			urlencode(&o, t.s ? t.s : "", t.len);
			out_add(&o, "=", 1);
			t.len = 0;
			encode(&t, cs, l.e[i].value, (long) strlen(l.e[i].value));
			urlencode(&o, t.s ? t.s : "", t.len);
			if (!t.ok)
				o.ok = 0;
			free(t.s);
		}
		if (s->post)
			strcpy(s->ctype, s->enctype == FS_PLAIN ? "text/plain" : "application/x-www-form-urlencoded");
	}
	for (i = 0; i < l.n; i++) {
		free(l.e[i].name);
		free(l.e[i].value);
	}
	free(l.e);
	free(cs);
	if (!o.ok) {
		free(o.s);
		free(s->action);
		s->action = NULL;
		return 0;
	}
	s->body = o.s;
	s->len = o.len;
	return 1;
}

void form_submit_free(FSUBMIT *s)
{
	free(s->action);
	free(s->body);
	s->action = NULL;
	s->body = NULL;
}
