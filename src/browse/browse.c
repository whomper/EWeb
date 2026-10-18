/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * browse.c - EWeb's browsing (browse.h): an address to a page laid out,
 * through the network (src/net), the document (src/doc), the CSS engine
 * (src/css) and the layout (src/layout).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browse.h"
#include "http.h"
#include "conn.h"
#include "plat.h"
#include "util.h"

#ifndef EWEB_VERSION
#define EWEB_VERSION ""
#endif

long br_cache_max = 4L << 20;

enum { NAV_NEW, NAV_HISTORY, NAV_RELOAD };

static void say(browser *b, const char *fmt, const char *arg)
{
	char buf[200];
	if (b->say == NULL)
		return;
	snprintf(buf, sizeof buf, fmt, arg);
	b->say(b->ud, buf);
}

/* ---------------------------------------------------------------- the cache */
/* pages and stylesheets as they came, newest first, for Back and Forward
   and for the next page of a site (its stylesheets again) */

typedef struct centry centry;
struct centry {
	centry *next;
	char *key;		/* the address asked for */
	char *url;		/* where it came from, after redirects */
	char *data;
	long len;
	char mime[64], charset[40], security[96];
};

static centry *cache;
static long cache_used;

static void centry_free(centry *e)
{
	free(e->key);
	free(e->url);
	free(e->data);
	free(e);
}

void br_cache_clear(void)
{
	while (cache != NULL) {
		centry *e = cache;
		cache = e->next;
		centry_free(e);
	}
	cache_used = 0;
}

static centry *cache_get(const char *key)
{
	centry *e, *prev = NULL;
	for (e = cache; e != NULL; prev = e, e = e->next)
		if (strcmp(e->key, key) == 0) {
			if (prev != NULL) {	/* to the front: used last */
				prev->next = e->next;
				e->next = cache;
				cache = e;
			}
			return e;
		}
	return NULL;
}

static void cache_put(const char *key, const char *url, const char *data, long len, const char *mime,
		      const char *charset, const char *security)
{
	centry *e, **pp;
	long size = len + (long) strlen(key) + (long) strlen(url) + (long) sizeof *e;
	if (size > br_cache_max / 4)
		return;
	for (pp = &cache; *pp != NULL; pp = &(*pp)->next)
		if (strcmp((*pp)->key, key) == 0) {
			e = *pp;
			*pp = e->next;
			cache_used -= e->len + (long) strlen(e->key) + (long) strlen(e->url) + (long) sizeof *e;
			centry_free(e);
			break;
		}
	e = calloc(1, sizeof *e);
	if (e == NULL)
		return;
	e->key = str_ndup(key, (long) strlen(key));
	e->url = str_ndup(url, (long) strlen(url));
	e->data = malloc(len + 1);
	if (e->key == NULL || e->url == NULL || e->data == NULL) {
		centry_free(e);
		return;
	}
	memcpy(e->data, data, len);
	e->data[len] = 0;
	e->len = len;
	str_copy(e->mime, mime, sizeof e->mime);
	str_copy(e->charset, charset, sizeof e->charset);
	str_copy(e->security, security, sizeof e->security);
	e->next = cache;
	cache = e;
	cache_used += size;
	/* the oldest go */
	while (cache_used > br_cache_max) {
		centry *last = cache, *before = NULL;
		while (last->next != NULL) {
			before = last;
			last = last->next;
		}
		if (before == NULL)
			break;
		before->next = NULL;
		cache_used -= last->len + (long) strlen(last->key) + (long) strlen(last->url) + (long) sizeof *last;
		centry_free(last);
	}
}

/* ---------------------------------------------------------------- addresses */

static int is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int has_scheme(const char *s, const char *scheme)
{
	size_t n = strlen(scheme);
	size_t i;
	for (i = 0; i < n; i++)
		if ((s[i] | 0x20) != scheme[i])
			return 0;
	return s[n] == ':';
}

/* a scheme before ':' (letters, digits, + - .), not a drive letter */
static int scheme_len(const char *s)
{
	int i = 0;
	if (!is_alpha(s[0]))
		return 0;
	while (is_alpha(s[i]) || (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' || s[i] == '.')
		i++;
	return s[i] == ':' && i > 1 ? i : 0;
}

/* file:///C:/DIR/PAGE.HTM -> C:\DIR\PAGE.HTM (on the Atari), percent
   escapes undone */
static void file_path(const char *url, char *out, int size)
{
	const char *p = url + 5;
	int o = 0;
	if (p[0] == '/' && p[1] == '/')
		p += 2;
	if (p[0] == '/' && is_alpha(p[1]) && (p[2] == ':' || p[2] == '|'))
		p++;		/* /C:/... */
	for (; *p && *p != '#' && *p != '?' && o < size - 1; p++) {
		char c = *p;
		if (c == '%' && p[1] && p[2]) {
			char h[3] = { p[1], p[2], 0 };
			c = (char) strtol(h, NULL, 16);
			p += 2;
		} else if (c == '|' && o == 1) {
			c = ':';
		}
#ifdef EW_TOS
		if (c == '/')
			c = '\\';
#endif
		out[o++] = c;
	}
	out[o] = 0;
}

/* a link on a file: page, made absolute (the URL parser takes http and
   https only) */
static void file_resolve(const char *base, const char *href, char *out, int size)
{
	char *q, *s;
	if (scheme_len(href) > 0) {
		str_copy(out, href, size);
		return;
	}
	if (href[0] == '#') {
		str_copy(out, base, size);
		q = strchr(out, '#');
		if (q != NULL)
			*q = 0;
		if ((long) strlen(out) + (long) strlen(href) < size)
			strcat(out, href);
		return;
	}
	if (href[0] == '/' && href[1] == '/') {
		snprintf(out, size, "file:%s", href);
		return;
	}
	str_copy(out, base, size);
	q = strpbrk(out + 5, "?#");
	if (q != NULL)
		*q = 0;
	if (href[0] == '/') {
		/* from the root: file:///C:/ keeps its drive */
		char *r = out + 7;
		if (r[0] == '/' && is_alpha(r[1]) && r[2] == ':')
			r[3] = 0;
		else
			*r = 0;
	} else {
		q = strrchr(out, '/');
		if (q != NULL)
			q[1] = 0;
	}
	if ((long) strlen(out) + (long) strlen(href) >= size)
		return;
	strcat(out, href);
	/* "x/../" and "./" worked out */
	s = out + 8;
	while ((q = strstr(s, "/./")) != NULL)
		memmove(q, q + 2, strlen(q + 2) + 1);
	while ((q = strstr(s, "/../")) != NULL) {
		char *p = q;
		while (p > s && p[-1] != '/')
			p--;
		if (p <= s) {
			memmove(q, q + 3, strlen(q + 3) + 1);
			continue;
		}
		memmove(p, q + 4, strlen(q + 4) + 1);
	}
}

/* href resolved against base (a page's address, or its <base>) */
static int resolve(const char *base, const char *href, char *out, int size)
{
	URL *u, *bu;
	int ok = 0;
	while (*href == ' ' || *href == '\t' || *href == '\n' || *href == '\r')
		href++;
	if (has_scheme(base, "file") || (scheme_len(href) > 0 && has_scheme(href, "file"))) {
		file_resolve(base, href, out, size);
		return 1;
	}
	if (scheme_len(href) > 0 && !has_scheme(href, "http") && !has_scheme(href, "https")) {
		str_copy(out, href, size);
		return 1;
	}
	u = malloc(sizeof *u);
	bu = malloc(sizeof *bu);
	if (u != NULL && bu != NULL) {
		if (url_parse(bu, base, NULL))
			ok = url_parse(u, href, bu);
		else
			ok = url_parse(u, href, NULL);
		if (ok)
			str_copy(out, u->href, size);
	}
	free(u);
	free(bu);
	return ok;
}

/* the page's address without its fragment, and the fragment ("" none) */
static const char *fragment(const char *url)
{
	const char *h = strchr(url, '#');
	return h ? h + 1 : "";
}

static int same_but_fragment(const char *a, const char *b)
{
	size_t na = strcspn(a, "#"), nb = strcspn(b, "#");
	return na == nb && memcmp(a, b, na) == 0;
}

/* what was typed, as an address */
static void typed_url(const browser *b, const char *t, char *out, int size)
{
	char buf[1024];
	size_t n;
	int i;
	while (*t == ' ')
		t++;
	str_copy(buf, t, sizeof buf);
	n = strlen(buf);
	while (n > 0 && buf[n - 1] == ' ')
		buf[--n] = 0;
	/* a file: C:\DIR\PAGE.HTM, or /home/... on this computer */
	if (is_alpha(buf[0]) && buf[1] == ':' && (buf[2] == '\\' || buf[2] == '/' || buf[2] == 0)) {
		int o = snprintf(out, size, "file:///");
		for (i = 0; buf[i] && o < size - 1; i++)
			out[o++] = buf[i] == '\\' ? '/' : buf[i];
		out[o] = 0;
		return;
	}
	if (buf[0] == '/') {
		snprintf(out, size, "file://%s", buf);
		return;
	}
	if (strstr(buf, "://") != NULL || has_scheme(buf, "about") || has_scheme(buf, "mailto") ||
	    has_scheme(buf, "file") || has_scheme(buf, "javascript") || has_scheme(buf, "data")) {
		str_copy(out, buf, size);
		return;
	}
	/* words: a search, where there is a search engine */
	if (b->search != NULL && b->search[0] &&
	    (strchr(buf, ' ') != NULL || (strchr(buf, '.') == NULL && strncmp(buf, "localhost", 9) != 0 &&
					   strchr(buf, ':') == NULL))) {
		char q[1024];
		const char *pc = strstr(b->search, "%s");
		int o = 0;
		for (i = 0; buf[i] && o < (int) sizeof q - 4; i++) {
			unsigned char c = (unsigned char) buf[i];
			if (c == ' ') {
				q[o++] = '+';
			} else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				   c == '-' || c == '_' || c == '.' || c == '~') {
				q[o++] = (char) c;
			} else {
				sprintf(q + o, "%%%02X", c);
				o += 3;
			}
		}
		q[o] = 0;
		if (pc != NULL)
			snprintf(out, size, "%.*s%s%s", (int) (pc - b->search), b->search, q, pc + 2);
		else
			snprintf(out, size, "%s%s", b->search, q);
		return;
	}
	snprintf(out, size, "http://%s", buf);
}

/* ---------------------------------------------------------------- EWeb's own pages */

static void esc(SBUF *s, const char *t)
{
	for (; *t; t++) {
		switch (*t) {
		case '&': sb_adds(s, "&amp;"); break;
		case '<': sb_adds(s, "&lt;"); break;
		case '>': sb_adds(s, "&gt;"); break;
		case '"': sb_adds(s, "&quot;"); break;
		default: sb_addc(s, *t);
		}
	}
}

static const char own_style[] =
	"<style>body{font-family:sans-serif;margin:24px 32px;color:#222;background:#fff}"
	"h1{font-size:22px;margin:0 0 12px}p{margin:8px 0}.addr{color:#555}"
	"ul{margin:8px 0}li{margin:3px 0}.small{font-size:13px;color:#555}</style>";

static void start_page(SBUF *s)
{
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8><title>Start</title>");
	sb_adds(s, own_style);
	sb_adds(s, "</head><body><h1>EWeb ");
	sb_adds(s, EWEB_VERSION);
	sb_adds(s, "</h1><p>Type an address above and press Return, or words to search for.</p>"
		   "<p>Pages that read well on an Atari:</p><ul>"
		   "<li><a href=\"http://frogfind.com/\">FrogFind</a>: a search engine for vintage computers</li>"
		   "<li><a href=\"http://68k.news/\">68k.news</a>: the news, as plain pages</li>"
		   "<li><a href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\">The first web page</a>, "
		   "CERN, 1991</li>"
		   "<li><a href=\"https://en.wikipedia.org/wiki/Atari_Falcon\">Atari Falcon</a> on Wikipedia</li>"
		   "</ul><p class=small>This first EWeb shows text, links, lists and tables; pictures and "
		   "forms come in later versions. Esc stops a page that is loading; Undo goes back.</p>"
		   "</body></html>");
}

static void error_page(SBUF *s, const char *url, const char *what, const char *why, const char *todo)
{
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8><title>");
	esc(s, what);
	sb_adds(s, "</title>");
	sb_adds(s, own_style);
	sb_adds(s, "</head><body><h1>");
	esc(s, what);
	sb_adds(s, "</h1><p class=addr>");
	esc(s, url);
	sb_adds(s, "</p><p>");
	esc(s, why);
	sb_adds(s, "</p>");
	if (todo != NULL && *todo) {
		sb_adds(s, "<p>");
		esc(s, todo);
		sb_adds(s, "</p>");
	}
	sb_adds(s, "</body></html>");
}

/* a text file as a page: its lines as they are */
static void text_page(SBUF *s, const char *data, long len)
{
	long i;
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8></head><body><pre>");
	for (i = 0; i < len; i++) {
		switch (data[i]) {
		case '&': sb_adds(s, "&amp;"); break;
		case '<': sb_adds(s, "&lt;"); break;
		case '>': sb_adds(s, "&gt;"); break;
		default: sb_addc(s, data[i]);
		}
	}
	sb_adds(s, "</pre></body></html>");
}

/* ---------------------------------------------------------------- fetching */

typedef struct {
	char *data;		/* malloc'ed, NUL-terminated */
	long len;
	char url[4096];		/* where it came from */
	char mime[64], charset[40], security[96];
	int status;		/* 0: a file or the cache */
	char err[200];		/* when nothing came */
} FETCH;

static void host_of(const char *url, char *out, int size)
{
	const char *h = strstr(url, "://");
	size_t n;
	h = h ? h + 3 : url;
	n = strcspn(h, "/?#");
	if ((int) n >= size)
		n = size - 1;
	memcpy(out, h, n);
	out[n] = 0;
}

/* 1: f->data has what came (a page, or a server's error page); 0: none,
   f->err says why (conn_stop: stopped) */
static int fetch(browser *b, const char *url, int fresh, const char *accept, long maxbody, FETCH *f)
{
	memset(f, 0, sizeof *f);
	str_copy(f->url, url, sizeof f->url);
	if (has_scheme(url, "file")) {
		char path[512];
		file_path(url, path, sizeof path);
		f->data = pf_load(path, &f->len);
		if (f->data == NULL) {
			snprintf(f->err, sizeof f->err, "There is no file %.150s, or it can't be read.", path);
			return 0;
		}
		return 1;
	}
	if (!has_scheme(url, "http") && !has_scheme(url, "https")) {
		snprintf(f->err, sizeof f->err, "EWeb opens http and https addresses, and files.");
		return 0;
	}
	if (!fresh) {
		centry *e = cache_get(url);
		if (e != NULL) {
			f->data = malloc(e->len + 1);
			if (f->data != NULL) {
				memcpy(f->data, e->data, e->len + 1);
				f->len = e->len;
				str_copy(f->url, e->url, sizeof f->url);
				str_copy(f->mime, e->mime, sizeof f->mime);
				str_copy(f->charset, e->charset, sizeof f->charset);
				str_copy(f->security, e->security, sizeof f->security);
				f->status = 200;
				return 1;
			}
		}
	}
	{
		HTTPREQ rq;
		HTTPRESP *rs = malloc(sizeof *rs);
		SBUF body;
		int ok;
		if (rs == NULL) {
			snprintf(f->err, sizeof f->err, "Out of memory.");
			return 0;
		}
		memset(&rq, 0, sizeof rq);
		rq.url = url;
		rq.accept = accept;
		rq.maxbody = maxbody;
		rq.referrer = b->url[0] && !has_scheme(b->url, "file") && !has_scheme(b->url, "about") ? b->url : NULL;
		sb_init(&body);
		ok = http_request(&rq, rs, &body, f->err, sizeof f->err);
		if (ok) {
			f->status = rs->status;
			str_copy(f->url, rs->url.href, sizeof f->url);
			str_copy(f->mime, rs->mime, sizeof f->mime);
			str_copy(f->charset, rs->charset, sizeof f->charset);
			str_copy(f->security, rs->security, sizeof f->security);
			f->len = body.len;
			f->data = sb_steal(&body);
			if (f->data == NULL) {
				f->data = malloc(1);
				if (f->data != NULL)
					f->data[0] = 0;
				f->len = 0;
			}
			if (f->status == 200)
				cache_put(url, f->url, f->data, f->len, f->mime, f->charset, f->security);
		}
		sb_free(&body);
		http_resp_free(rs);
		free(rs);
		return ok && f->data != NULL;
	}
}

/* ---------------------------------------------------------------- building the page */

typedef struct {
	browser *b;
	doc *d;
	const char *base;
	int fresh;
	int n;
} SHEETS;

static void sheet_cb(void *ud, int is_link, const char *s, size_t len)
{
	SHEETS *sh = ud;
	char href[1024], url[2048];
	FETCH *f;
	if (!is_link) {
		ec_sheet(sh->d->css, s, len, EC_ORIGIN_AUTHOR);
		return;
	}
	if (conn_stop || len >= sizeof href)
		return;
	memcpy(href, s, len);
	href[len] = 0;
	if (!resolve(sh->base, href, url, sizeof url))
		return;
	f = malloc(sizeof *f);
	if (f == NULL)
		return;
	sh->n++;
	{
		char host[128], msg[200];
		host_of(url, host, sizeof host);
		snprintf(msg, sizeof msg, "%s: stylesheet %d...", host, sh->n);
		say(sh->b, "%s", msg);
	}
	if (fetch(sh->b, url, sh->fresh, "text/css,*/*;q=0.1", 2L << 20, f) &&
	    (f->status == 0 || (f->status >= 200 && f->status < 300)))
		ec_sheet(sh->d->css, f->data, (size_t) f->len, EC_ORIGIN_AUTHOR);
	free(f->data);
	free(f);
}

static ec_media media_of(const browser *b)
{
	ec_media m;
	m.width = (int32_t) ((int64_t) b->width * 1024 / b->zoom);
	m.height = (int32_t) ((int64_t) 400 * 1024 / b->zoom);
	m.font_size = 16;
	return m;
}

/* the source made into a page (doc styled and laid out): 1, or 0 when
   out of memory or stopped while fetching its sheets */
static int build(browser *b, const char *data, long len, const char *charset, const char *url, int fresh,
		 doc **dout, page **pout)
{
	ec_media m = media_of(b);
	LAYOUT_OPT o;
	doc *d;
	page *p;
	char base[4096];
	SHEETS sh;

	unsigned long t = pf_ms(), t_sheets;

	say(b, "%s", "Reading the page...");
	d = doc_parse(data, (size_t) len, charset && *charset ? charset : NULL, &m);
	if (d == NULL)
		return 0;
	doc_add_ua(d);
	b->ms_parse = pf_ms() - t;
	if (d->base[0] == 0 || !resolve(url, d->base, base, sizeof base))
		str_copy(base, url, sizeof base);
	sh.b = b;
	sh.d = d;
	sh.base = base;
	sh.fresh = fresh;
	sh.n = 0;
	t_sheets = pf_ms();
	doc_sheets(d, sheet_cb, &sh);
	if (b->extra_css != NULL)
		ec_sheet(d->css, b->extra_css, strlen(b->extra_css), EC_ORIGIN_AUTHOR);
	t = pf_ms();
	b->ms_fetch += t - t_sheets;		/* the stylesheets came meanwhile */
	say(b, "%s", "Laying the page out...");
	doc_select(d);
	b->ms_style = pf_ms() - t;
	t = pf_ms();
	memset(&o, 0, sizeof o);
	o.width = b->width;
	o.zoom = b->zoom;
	p = layout_page(d, &o);
	b->ms_layout = pf_ms() - t;
	if (p == NULL) {
		doc_free(d);
		return 0;
	}
	*dout = d;
	*pout = p;
	return 1;
}

static void show(browser *b, doc *d, page *p, const char *url, const char *security, int status)
{
	if (b->p != NULL)
		page_free(b->p);
	if (b->d != NULL)
		doc_free(b->d);
	b->d = d;
	b->p = p;
	str_copy(b->url, url, sizeof b->url);
	str_copy(b->title, d->title, sizeof b->title);
	str_copy(b->security, security ? security : "", sizeof b->security);
	b->status = status;
}

static void hist_push(browser *b, const char *url)
{
	int i;
	char *u = str_ndup(url, (long) strlen(url));
	if (u == NULL)
		return;
	/* what was ahead goes */
	for (i = b->cur + 1; i < b->nhist; i++)
		free(b->hist[i].url);
	b->nhist = b->cur + 1;
	if (b->nhist == BR_HISTORY) {
		free(b->hist[0].url);
		memmove(b->hist, b->hist + 1, (BR_HISTORY - 1) * sizeof b->hist[0]);
		b->nhist--;
	}
	b->hist[b->nhist].url = u;
	b->hist[b->nhist].y = 0;
	b->cur = b->nhist++;
}

static void hist_set_url(browser *b, const char *url)
{
	char *u;
	if (b->cur < 0 || strcmp(b->hist[b->cur].url, url) == 0)
		return;
	u = str_ndup(url, (long) strlen(url));
	if (u == NULL)
		return;
	free(b->hist[b->cur].url);
	b->hist[b->cur].url = u;
}

static int32_t anchor_y(const browser *b, const char *url)
{
	const char *fr = fragment(url);
	int32_t y;
	if (b->p == NULL || *fr == 0)
		return -1;
	y = page_anchor(b->p, fr);
	return y;
}

/* a page of EWeb's own, shown in place of url */
static int own_page(browser *b, const char *url, SBUF *src, int mode)
{
	doc *d;
	page *p;
	if (!build(b, src->s, src->len, "utf-8", url, 0, &d, &p))
		return BR_STOPPED;
	show(b, d, p, url, "", 0);
	if (mode == NAV_NEW)
		hist_push(b, url);
	b->scroll_to = mode == NAV_NEW ? -1 : b->hist[b->cur].y;
	return BR_SHOWN;
}

static int load(browser *b, const char *url, int mode)
{
	FETCH *f;
	SBUF src;
	doc *d;
	page *p;
	int r;
	char host[128];

	conn_stop = 0;
	b->elsewhere[0] = 0;
	if (has_scheme(url, "javascript") || has_scheme(url, "data"))
		return BR_STOPPED;	/* nothing EWeb can do with it */
	if (has_scheme(url, "mailto")) {
		str_copy(b->elsewhere, url, sizeof b->elsewhere);
		return BR_ELSEWHERE;
	}
	/* a place on the page shown */
	if (mode == NAV_NEW && b->p != NULL && fragment(url)[0] && same_but_fragment(url, b->url)) {
		hist_push(b, url);
		str_copy(b->url, url, sizeof b->url);
		b->scroll_to = anchor_y(b, url);
		return BR_SAME;
	}
	sb_init(&src);
	if (has_scheme(url, "about")) {
		if (strcmp(url + 6, "blank") == 0)
			sb_adds(&src, "<html><head><title></title></head><body></body></html>");
		else
			start_page(&src);
		r = own_page(b, url, &src, mode);
		sb_free(&src);
		return r;
	}
	f = malloc(sizeof *f);
	if (f == NULL)
		return BR_STOPPED;
	host_of(url, host, sizeof host);
	if (has_scheme(url, "file"))
		say(b, "%s", "Reading the file...");
	else
		say(b, "Opening %s...", host);
	b->ms_fetch = pf_ms();
	r = fetch(b, url, mode == NAV_RELOAD, NULL, 0, f);
	b->ms_fetch = pf_ms() - b->ms_fetch;
	if (!r) {
		if (conn_stop) {
			conn_stop = 0;
			free(f);
			return BR_STOPPED;
		}
		error_page(&src, url, "EWeb can't open this page", f->err,
			   has_scheme(url, "file") ? NULL :
			   "Check the address and the network (STinG), then try again (^R).");
		free(f);
		r = own_page(b, url, &src, mode);
		sb_free(&src);
		return r;
	}
	/* what it is */
	if (f->mime[0] == 0 || strcmp(f->mime, "text/html") == 0 || strcmp(f->mime, "application/xhtml+xml") == 0) {
		r = build(b, f->data, f->len, f->charset, f->url, mode == NAV_RELOAD, &d, &p);
	} else if (strncmp(f->mime, "text/", 5) == 0) {
		text_page(&src, f->data, f->len);
		r = build(b, src.s, src.len, f->charset[0] ? f->charset : "windows-1252", f->url, 0, &d, &p);
	} else {
		char why[200];
		snprintf(why, sizeof why, "This is %s (%ld bytes): EWeb shows web pages and text, "
			 "not pictures or other files yet.", f->mime, f->len);
		error_page(&src, f->url, "EWeb can't show this", why, NULL);
		r = build(b, src.s, src.len, "utf-8", f->url, 0, &d, &p);
	}
	sb_free(&src);
	if (!r || conn_stop) {
		if (r) {
			page_free(p);
			doc_free(d);
		}
		conn_stop = 0;
		free(f->data);
		free(f);
		return BR_STOPPED;
	}
	{
		/* the address after redirects, with the fragment asked for */
		char shown[4096];
		const char *fr = fragment(url);
		str_copy(shown, f->url, sizeof shown);
		if (*fr && strchr(shown, '#') == NULL && (long) (strlen(shown) + strlen(fr) + 2) < (long) sizeof shown) {
			strcat(shown, "#");
			strcat(shown, fr);
		}
		show(b, d, p, shown, f->security, f->status);
		if (mode == NAV_NEW)
			hist_push(b, shown);
		else
			hist_set_url(b, shown);
	}
	if (mode == NAV_NEW)
		b->scroll_to = anchor_y(b, b->url);
	else
		b->scroll_to = b->hist[b->cur].y;
	free(f->data);
	free(f);
	return BR_SHOWN;
}

/* ---------------------------------------------------------------- what the front end asks */

void br_init(browser *b, int32_t width, int32_t zoom)
{
	memset(b, 0, sizeof *b);
	b->width = width;
	b->zoom = zoom > 0 ? zoom : 1024;
	b->cur = -1;
	b->scroll_to = -1;
	b->search = "";
}

void br_free(browser *b)
{
	int i;
	if (b->p != NULL)
		page_free(b->p);
	if (b->d != NULL)
		doc_free(b->d);
	b->p = NULL;
	b->d = NULL;
	for (i = 0; i < b->nhist; i++)
		free(b->hist[i].url);
	b->nhist = 0;
	b->cur = -1;
}

int br_go(browser *b, const char *typed)
{
	char url[4096];
	typed_url(b, typed, url, sizeof url);
	if (url[0] == 0)
		return BR_STOPPED;
	return load(b, url, NAV_NEW);
}

void br_link_url(const browser *b, int link, char *out, int size)
{
	char base[4096];
	out[0] = 0;
	if (b->p == NULL || link < 0 || link >= b->p->nlinks)
		return;
	if (b->d == NULL || b->d->base[0] == 0 || !resolve(b->url, b->d->base, base, sizeof base))
		str_copy(base, b->url, sizeof base);
	if (!resolve(base, b->p->links[link], out, size))
		str_copy(out, b->p->links[link], size);
}

int br_follow(browser *b, int link)
{
	char url[4096];
	br_link_url(b, link, url, sizeof url);
	if (url[0] == 0)
		return BR_STOPPED;
	return load(b, url, NAV_NEW);
}

static int go_to(browser *b, int i)
{
	int old = b->cur, r;
	if (i < 0 || i >= b->nhist)
		return BR_STOPPED;
	if (b->p != NULL && same_but_fragment(b->hist[i].url, b->url)) {
		b->cur = i;
		str_copy(b->url, b->hist[i].url, sizeof b->url);
		b->scroll_to = b->hist[i].y;
		return BR_SAME;
	}
	b->cur = i;
	r = load(b, b->hist[i].url, NAV_HISTORY);
	if (r == BR_STOPPED)
		b->cur = old;
	return r;
}

int br_back(browser *b)
{
	return go_to(b, b->cur - 1);
}

int br_forward(browser *b)
{
	return go_to(b, b->cur + 1);
}

int br_reload(browser *b)
{
	if (b->cur < 0)
		return BR_STOPPED;
	return load(b, b->hist[b->cur].url, NAV_RELOAD);
}

int br_can_back(const browser *b)
{
	return b->cur > 0;
}

int br_can_forward(const browser *b)
{
	return b->cur >= 0 && b->cur < b->nhist - 1;
}

void br_scrolled(browser *b, int32_t y)
{
	if (b->cur >= 0)
		b->hist[b->cur].y = y;
}

int br_relayout(browser *b, int32_t width)
{
	LAYOUT_OPT o;
	page *p;
	b->width = width;
	if (b->d == NULL)
		return 1;
	memset(&o, 0, sizeof o);
	o.width = width;
	o.zoom = b->zoom;
	p = layout_page(b->d, &o);
	if (p == NULL)
		return 0;
	page_free(b->p);
	b->p = p;
	return 1;
}
