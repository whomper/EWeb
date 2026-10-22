/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rftest - a refresh's content (<meta http-equiv=refresh>, the Refresh
 * header) as src/doc/doc.c parses it (the HTML Standard's shared
 * declarative refresh steps): the Web Platform Tests' cases
 * (html/semantics/document-metadata/the-meta-element/pragma-directives/
 * attr-meta-http-equiv-refresh/parsing.html), each address resolved as
 * the test resolves it; then which <meta>s of a page count (each that
 * is one, in the order the parser put them in, as its content was then:
 * not in a template, in <noscript> only without scripts, a <base> before
 * it or not); then random contents under the sanitizers.
 * Prints each failure and how many passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "html.h"
#include "url.h"

static int failed, passed;

static void result(const char *name, int ok, const char *got)
{
	if (ok) {
		passed++;
		return;
	}
	failed++;
	fprintf(stderr, "rftest: %s failed: %s\n", name, got);
}

#define PAGE "http://example.com/dir/refresh.html"
#define NONE NULL	/* not a refresh */
static const char self[] = "(the page)";
#define SELF self	/* a refresh of the page itself */

/* the Web Platform Tests' cases: the content, then its time and the
   address as the test writes it (unresolved), or NONE */
static const struct {
	const char *in;
	unsigned long secs;
	const char *url;
} wpt[] = {
	{ "", 0, NONE },
	{ "1", 1, SELF },
	{ "1 ", 1, SELF },
	{ "1\t", 1, SELF },
	{ "1\r", 1, SELF },
	{ "1\n", 1, SELF },
	{ "1\f", 1, SELF },
	{ "1;", 1, SELF },
	{ "1,", 1, SELF },
	{ "1; url=foo", 1, "foo" },
	{ "1, url=foo", 1, "foo" },
	{ "1 url=foo", 1, "foo" },
	{ "1;\turl=foo", 1, "foo" },
	{ "1,\turl=foo", 1, "foo" },
	{ "1\turl=foo", 1, "foo" },
	{ "1;\rurl=foo", 1, "foo" },
	{ "1,\rurl=foo", 1, "foo" },
	{ "1\rurl=foo", 1, "foo" },
	{ "1;\nurl=foo", 1, "foo" },
	{ "1,\nurl=foo", 1, "foo" },
	{ "1\nurl=foo", 1, "foo" },
	{ "1;\furl=foo", 1, "foo" },
	{ "1,\furl=foo", 1, "foo" },
	{ "1\furl=foo", 1, "foo" },
	{ "1url=foo", 0, NONE },
	{ "1x;url=foo", 0, NONE },
	{ "1 x;url=foo", 1, "x;url=foo" },
	{ "1;;url=foo", 1, ";url=foo" },
	{ "  1  ;  url  =  foo", 1, "foo" },
	{ "  1  ,  url  =  foo", 1, "foo" },
	{ "  1  ;  foo", 1, "foo" },
	{ "  1  ,  foo", 1, "foo" },
	{ "  1  url  =  foo", 1, "foo" },
	{ "1; url=foo ", 1, "foo" },
	{ "1; url=f\to\no", 1, "foo" },
	{ "1; url=\"foo\"bar", 1, "foo" },
	{ "1; url='foo'bar", 1, "foo" },
	{ "1; url=\"foo'bar", 1, "foo'bar" },
	{ "1; url foo", 1, "url foo" },
	{ "1; urlfoo", 1, "urlfoo" },
	{ "1; urfoo", 1, "urfoo" },
	{ "1; ufoo", 1, "ufoo" },
	{ "1; \"foo\"bar", 1, "foo" },
	{ "; foo", 0, NONE },
	{ ";foo", 0, NONE },
	{ ", foo", 0, NONE },
	{ ",foo", 0, NONE },
	{ "foo", 0, NONE },
	{ "+1; url=foo", 0, NONE },
	{ "-1; url=foo", 0, NONE },
	{ "+0; url=foo", 0, NONE },
	{ "-0; url=foo", 0, NONE },
	{ "0; url=foo", 0, "foo" },
	{ "+1; foo", 0, NONE },
	{ "-1; foo", 0, NONE },
	{ "+0; foo", 0, NONE },
	{ "-0; foo", 0, NONE },
	{ "0; foo", 0, "foo" },
	{ "+1", 0, NONE },
	{ "-1", 0, NONE },
	{ "+0", 0, NONE },
	{ "-0", 0, NONE },
	{ "0", 0, SELF },
	{ "1.9; url=foo", 1, "foo" },
	{ "1.9..5.; url=foo", 1, "foo" },
	{ ".9; url=foo", 0, "foo" },
	{ "0.9; url=foo", 0, "foo" },
	{ "0...9; url=foo", 0, "foo" },
	{ "0...; url=foo", 0, "foo" },
	{ "1e0; url=foo", 0, NONE },
	{ "1e1; url=foo", 0, NONE },
	{ "10e-1; url=foo", 0, NONE },
	{ "-0.1; url=foo", 0, NONE },
	/* and EWeb's own: the case of "URL", an address after a quote left
	   open, an empty one (the base), a time too long */
	{ "5;URL='/next.html'", 5, "/next.html" },
	{ "5; Url = \"https://example.org/a b\"", 5, "https://example.org/a b" },
	{ "0; url='", 0, "" },
	{ "0; url=", 0, "" },
	{ "99999999999999999999; url=x", DOC_REFRESH_MAX, "x" },
};

/* an address resolved against the page, as the test's iframe does */
static void resolve(const char *href, char *out, size_t size)
{
	static URL base, u;
	url_parse(&base, PAGE, NULL);
	if (url_parse(&u, href, &base))
		snprintf(out, size, "%s", u.href);
	else
		snprintf(out, size, "(failure)");
}

static void parsing(void)
{
	size_t i;
	for (i = 0; i < sizeof wpt / sizeof wpt[0]; i++) {
		unsigned long secs = 12345;
		const char *u = (const char *) 1;
		size_t ul = 7;
		char name[80], got[600], want[600], href[300];
		int r = doc_refresh_parse(wpt[i].in, &secs, &u, &ul);
		snprintf(name, sizeof name, "parse %zu \"%.40s\"", i, wpt[i].in);
		if (wpt[i].url == NONE) {
			snprintf(got, sizeof got, "%s", r ? "a refresh" : "none");
			result(name, !r, got);
			continue;
		}
		if (!r) {
			result(name, 0, "none");
			continue;
		}
		if (u == NULL) {
			snprintf(got, sizeof got, "%s", PAGE);
		} else {
			snprintf(href, sizeof href, "%.*s", (int) ul, u);
			resolve(href, got, sizeof got);
		}
		if (wpt[i].url == SELF)
			snprintf(want, sizeof want, "%s", PAGE);
		else
			resolve(wpt[i].url, want, sizeof want);
		result(name, secs == wpt[i].secs && strcmp(got, want) == 0 && (u != NULL || wpt[i].url == SELF), got);
	}
}

static ec_media media = { 640, 400, 16 };

/* the <meta>s noted, as "CONTENT base N" with " | " between ("" none) */
static void noted(const doc *d, char *out, size_t size)
{
	const drefresh *r;
	size_t n = 0;
	out[0] = 0;
	for (r = d != NULL ? d->refresh : NULL; r != NULL && n < size; r = r->next)
		n += (size_t) snprintf(out + n, size - n, "%s%s base %d", n ? " | " : "", r->content, r->based);
}

/* the <meta>s a page's refresh may come from */
static void meta(const char *name, const char *html, int scripting, const char *want)
{
	html_opts o;
	doc *d;
	char got[600];
	memset(&o, 0, sizeof o);
	o.scripting = scripting;
	d = doc_parse_opts(html, strlen(html), &o, &media);
	if (d == NULL) {
		result(name, 0, "out of memory");
		return;
	}
	noted(d, got, sizeof got);
	result(name, strcmp(got, want) == 0, got);
	doc_free(d);
}

static void metas(void)
{
	html_opts o;
	doc *d;
	static char late[1200], many[1200];
	char got[600];
	int i;
	size_t n;
	static const char head[] = "<meta http-equiv=refresh content='1; url=a'><title>",
			  tail[] = "</title><meta charset=iso-8859-8><p>\xe0</p>";
	meta("a meta in the head", "<title>t</title><meta http-equiv=refresh content='3; url=a'>", 0, "3; url=a base 0");
	meta("http-equiv in any case", "<meta HTTP-EQUIV=ReFresh content=4>", 0, "4 base 0");
	meta("each that is one", "<meta http-equiv=refresh content=''><meta http-equiv=refresh content=x>"
	     "<meta http-equiv=refresh><meta http-equiv=refresh content='2; url=b'>"
	     "<meta http-equiv=refresh content='3; url=c'>", 0, "2; url=b base 0 | 3; url=c base 0");
	meta("not another http-equiv", "<meta http-equiv=refreshx content=1><meta name=refresh content=1>", 0, "");
	meta("in the body too", "<body><p>x<meta http-equiv=refresh content='1;u'>", 0, "1;u base 0");
	meta("not in a template", "<template><meta http-equiv=refresh content=1></template>", 0, "");
	meta("in noscript, scripts off", "<head><noscript><meta http-equiv=refresh content='0; url=n'></noscript>",
	     0, "0; url=n base 0");
	meta("in noscript, scripts on", "<head><noscript><meta http-equiv=refresh content='0; url=n'></noscript>",
	     1, "");
	meta("in the body's noscript, scripts on", "<body><noscript><meta http-equiv=refresh content=0></noscript>",
	     1, "");
	meta("a base before", "<base href=/x/><meta http-equiv=refresh content='1; url=a'>", 0, "1; url=a base 1");
	meta("a base after", "<meta http-equiv=refresh content='1; url=a'><base href=/x/>", 0, "1; url=a base 0");
	meta("a base without href", "<base target=x><meta http-equiv=refresh content='1; url=a'>", 0, "1; url=a base 0");
	meta("a template's base", "<template><base href=/x/></template><meta http-equiv=refresh content='1; url=a'>",
	     0, "1; url=a base 0");
	meta("after a template", "<template><meta http-equiv=refresh content=1></template>"
	     "<meta http-equiv=refresh content=2>", 0, "2 base 0");
	/* the order put in: a table puts the second before itself (foster
	   parenting), so it is first in the tree, but the first is first */
	meta("in the order put in, not the tree's", "<table><tr><td><meta http-equiv=refresh content='0; url=javascript:x'>"
	     "</td></tr><meta http-equiv=refresh content='0; url=foster'></table>", 0,
	     "0; url=javascript:x base 0 | 0; url=foster base 0");
	meta("the first can't be used, a base between", "<meta http-equiv=refresh content='0; url=javascript:x()'><p>"
	     "<meta http-equiv=refresh content=junk><base href=/b/><meta http-equiv=refresh content='0; url=two'>", 0,
	     "0; url=javascript:x() base 0 | 0; url=two base 1");
	/* at most DOC_REFRESH_METAS */
	for (i = 0, n = 0; i < DOC_REFRESH_METAS + 4; i++)
		n += (size_t) snprintf(many + n, sizeof many - n, "<meta http-equiv=refresh content=%d>", i);
	memset(&o, 0, sizeof o);
	d = doc_parse_opts(many, n, &o, &media);
	noted(d, got, sizeof got);
	{
		const drefresh *r;
		int k = 0;
		for (r = d != NULL ? d->refresh : NULL; r != NULL; r = r->next)
			k++;
		result("at most DOC_REFRESH_METAS", d != NULL && k == DOC_REFRESH_METAS && !strncmp(got, "0 base 0 | 1 ", 13),
		       got);
	}
	doc_free(d);
	/* a <meta charset> past the prescan's 1024 bytes, which starts the
	   parse again: the meta of the tree that stays, once */
	memset(&o, 0, sizeof o);
	memset(late, 'x', sizeof late);
	memcpy(late, head, sizeof head - 1);
	memcpy(late + sizeof late - sizeof tail, tail, sizeof tail);
	d = doc_parse_opts(late, strlen(late), &o, &media);
	noted(d, got, sizeof got);
	result("a parse begun again", d != NULL && !strcmp(got, "1; url=a base 0") && !strcmp(d->charset, "ISO-8859-8"),
	       d != NULL ? got : "out of memory");
	/* a <frameset> takes the body away, and the meta in it: it still
	   counts (the steps ran when it went in) */
	meta("a meta taken out of the tree", "<p><meta http-equiv=refresh content=1><frameset>", 0, "1 base 0");
	/* innerHTML's (a fragment's) doesn't count, nor clears the page's */
	html_fragment(d, "div", DN_NS_HTML, "<meta http-equiv=refresh content=9>", 35, 0);
	noted(d, got, sizeof got);
	result("a fragment's", d != NULL && !strcmp(got, "1; url=a base 0"), got);
	/* a script changes the content after: the meta as it was put in */
	if (d != NULL && d->refresh != NULL) {
		dnode *m = d->root;
		while (m != NULL && !(m->type == DN_ELEMENT && m->tag == T_META))
			m = m->first != NULL ? m->first : m->next;
		if (m != NULL)
			dn_set_attr(d, m, "content", "0; url=javascript:void 0", 24);
		noted(d, got, sizeof got);
		result("its content changed after", m != NULL && !strcmp(dn_attr(m, "content"), "0; url=javascript:void 0") &&
		       !strcmp(got, "1; url=a base 0"), got);
	}
	doc_free(d);
}

/* contents made of the characters that matter, under the sanitizers */
static void random_contents(void)
{
	static const char abc[] = "0123456789.;, \t\n\r\furlURL='\"x/:#";
	unsigned long seed = 7;
	char in[40];
	int i, k;
	for (i = 0; i < 200000; i++) {
		unsigned long secs;
		const char *u;
		size_t ul;
		int n;
		seed = seed * 1103515245UL + 12345UL;
		n = (int) ((seed >> 16) % sizeof in);
		for (k = 0; k < n; k++) {
			seed = seed * 1103515245UL + 12345UL;
			in[k] = abc[(seed >> 16) % (sizeof abc - 1)];
		}
		in[n] = 0;
		if (doc_refresh_parse(in, &secs, &u, &ul) &&
		    (secs > DOC_REFRESH_MAX || (u != NULL && (u < in || u + ul > in + n)))) {
			result("random", 0, in);
			return;
		}
	}
	passed++;
}

int main(void)
{
	parsing();
	metas();
	random_contents();
	fprintf(stderr, "rftest: %d passed, %d failed\n", passed, failed);
	return failed != 0;
}
