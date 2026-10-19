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
#include "form.h"
#include "img.h"
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
		   "</ul><p class=small>EWeb shows text, links, lists, tables, pictures (JPEG, PNG, GIF) "
		   "and forms (Tab goes from field to field, Return sends). Esc stops a page that is loading, "
		   "and its pictures; Undo goes back.</p>"
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
   f->err says why (conn_stop: stopped). post: a form sent with POST
   (its answer isn't kept in the cache), else NULL */
static int fetch(browser *b, const char *url, int fresh, const char *accept, long maxbody, int keep,
		 const FSUBMIT *post, FETCH *f)
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
	if (post != NULL) {
		fresh = 1;
		keep = 0;
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
		if (post != NULL) {
			rq.method = "POST";
			rq.body = post->body;
			rq.bodylen = post->len;
			rq.ctype = post->ctype;
		}
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
			if (f->status == 200 && keep) {
				cache_put(url, f->url, f->data, f->len, f->mime, f->charset, f->security);
				/* after a redirect, under where it came from too: the
				   history keeps that address, and Back asks for it */
				if (strcmp(url, f->url) != 0)
					cache_put(f->url, f->url, f->data, f->len, f->mime, f->charset, f->security);
			}
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
	if (fetch(sh->b, url, sh->fresh, "text/css,*/*;q=0.1", 2L << 20, 1, NULL, f) &&
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

/* ---------------------------------------------------------------- pictures in the layout */

/* the address the page's pictures are resolved against, while it is
   laid out */
static const char *lay_base;

static void *image_cb(void *ud, const char *src, int32_t *w, int32_t *h)
{
	browser *b = ud;
	char url[4096];
	ew_img *im;
	if (!b->pictures || lay_base == NULL || !resolve(lay_base, src, url, sizeof url))
		return NULL;
	if (!has_scheme(url, "http") && !has_scheme(url, "https") && !has_scheme(url, "file") &&
	    !has_scheme(url, "data"))
		return NULL;
	im = img_get(url);
	if (im == NULL)
		return NULL;
	img_size(im, w, h);
	return im;
}

static unsigned long ref_serial;

/* each picture a page shows, once: d +1 when it is laid out, -1 when it
   goes */
static void page_imgs(page *p, int d)
{
	long i;
	ew_img *last = NULL;
	if (p == NULL)
		return;
	ref_serial++;
	for (i = 0; i < p->nitems; i++) {
		dl_item *it = &p->items[i];
		long k;
		if (it->type != DL_IMAGE || it->img == last)
			continue;
		/* once a page: earlier items with the same picture */
		for (k = 0; k < i; k++)
			if (p->items[k].type == DL_IMAGE && p->items[k].img == it->img)
				break;
		if (k < i)
			continue;
		last = it->img;
		img_ref(it->img, d);
	}
}

static void pg_free(page *p)
{
	page_imgs(p, -1);
	page_free(p);
}

/* the page shown's pictures are those the trimming keeps */
static void shown_imgs(const browser *b)
{
	long i;
	img_current(NULL);
	if (b->p == NULL)
		return;
	for (i = 0; i < b->p->nitems; i++)
		if (b->p->items[i].type == DL_IMAGE)
			img_current(b->p->items[i].img);
}

/* the document laid out at the browser's width and zoom, its pictures
   resolved against base */
static page *lay(browser *b, doc *d, const char *base)
{
	LAYOUT_OPT o;
	page *p;
	memset(&o, 0, sizeof o);
	o.width = b->width;
	o.zoom = b->zoom;
	o.image = image_cb;
	o.ud = b;
	lay_base = base;
	p = layout_page(d, &o);
	lay_base = NULL;
	page_imgs(p, 1);
	return p;
}

/* the address a document's links and pictures are resolved against */
static void base_of(const browser *b, const doc *d, const char *url, char *out, int size)
{
	if (d == NULL || d->base[0] == 0 || !resolve(url, d->base, out, size))
		str_copy(out, url, size);
}

/* the source made into a page (doc styled and laid out): 1, or 0 when
   out of memory or stopped while fetching its sheets */
static int build(browser *b, const char *data, long len, const char *charset, const char *url, int fresh,
		 doc **dout, page **pout)
{
	ec_media m = media_of(b);
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
	p = lay(b, d, base);
	b->ms_layout = pf_ms() - t;
	if (p == NULL) {
		doc_free(d);
		return 0;
	}
	*dout = d;
	*pout = p;
	return 1;
}

/* ---------------------------------------------------------------- pages kept for Back */

long br_keep_max = 16L << 20;

static void unkeep(BR_ENTRY *e)
{
	if (e->p != NULL)
		pg_free(e->p);
	if (e->d != NULL)
		doc_free(e->d);
	free(e->security);
	e->p = NULL;
	e->d = NULL;
	e->security = NULL;
	e->src_len = 0;
}

/* what a page laid out keeps, roughly: its tree, styles and display list
   (about eight times its source, on the page set) */
static long cost(long src_len)
{
	return 8 * src_len + 65536L;
}

/* the kept pages within br_keep_max: those farthest from the one shown go
   first */
static void keep_trim(browser *b)
{
	for (;;) {
		long total = 0;
		int i, far = -1, fd = -1;
		for (i = 0; i < b->nhist; i++) {
			if (b->hist[i].p == NULL)
				continue;
			total += cost(b->hist[i].src_len);
			if ((i > b->cur ? i - b->cur : b->cur - i) > fd) {
				fd = i > b->cur ? i - b->cur : b->cur - i;
				far = i;
			}
		}
		if (total <= br_keep_max || far < 0)
			return;
		unkeep(&b->hist[far]);
	}
}

/* the page shown goes to its history entry, to come back at once */
static void keep_shown(browser *b)
{
	BR_ENTRY *e;
	if (b->p == NULL || b->at < 0 || b->at >= b->nhist || cost(b->src_len) > br_keep_max) {
		if (b->p != NULL)
			pg_free(b->p);
		if (b->d != NULL)
			doc_free(b->d);
	} else {
		e = &b->hist[b->at];
		unkeep(e);
		e->d = b->d;
		e->p = b->p;
		e->src_len = b->src_len;
		e->status = b->status;
		e->security = str_ndup(b->security, (long) strlen(b->security));
	}
	b->d = NULL;
	b->p = NULL;
	b->at = -1;
}

/* keep: the page shown stays in its history entry (a new page, or one
   from the history); else it goes (a reload) */
static void show(browser *b, doc *d, page *p, const char *url, const char *security, int status, int keep)
{
	if (keep) {
		keep_shown(b);
	} else {
		if (b->p != NULL)
			pg_free(b->p);
		if (b->d != NULL)
			doc_free(b->d);
	}
	b->d = d;
	b->p = p;
	b->pic_stopped = 0;
	b->pic_relayout = 0;
	b->pic_n = b->pic_done = 0;
	shown_imgs(b);
	img_trim();
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
	for (i = b->cur + 1; i < b->nhist; i++) {
		free(b->hist[i].url);
		unkeep(&b->hist[i]);
	}
	b->nhist = b->cur + 1;
	if (b->nhist == BR_HISTORY) {
		free(b->hist[0].url);
		unkeep(&b->hist[0]);
		memmove(b->hist, b->hist + 1, (BR_HISTORY - 1) * sizeof b->hist[0]);
		b->nhist--;
		b->cur--;
	}
	memset(&b->hist[b->nhist], 0, sizeof b->hist[0]);
	b->hist[b->nhist].url = u;
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
	show(b, d, p, url, "", 0, mode != NAV_RELOAD);
	b->src_len = src->len;
	if (mode == NAV_NEW)
		hist_push(b, url);
	b->at = b->cur;
	keep_trim(b);
	b->scroll_to = mode == NAV_NEW ? -1 : b->hist[b->cur].y;
	return BR_SHOWN;
}

/* a picture opened on its own: kept as the page's picture, and a page
   that shows it, its name and size the title */
static int picture_page(SBUF *s, FETCH *f)
{
	ew_img *im = img_get(f->url);
	const char *name = strrchr(f->url, '/');
	char t[160];
	long w, h;
	if (im == NULL || !pic_size((const unsigned char *) f->data, f->len, &w, &h))
		return 0;
	if (img_state(im) != IMG_HERE) {
		char *copy = malloc((size_t) f->len);
		if (copy == NULL)
			return 0;
		memcpy(copy, f->data, (size_t) f->len);
		if (!img_set_file(im, copy, f->len))
			return 0;
	}
	snprintf(t, sizeof t, "%.100s (%s, %ld x %ld)", name && name[1] ? name + 1 : f->url,
		 pic_kind_name(pic_kind((const unsigned char *) f->data, f->len)), w, h);
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8><title>");
	esc(s, t);
	sb_adds(s, "</title><style>body{margin:8px;background:#fff}img{max-width:100%}</style></head><body><img src=\"");
	esc(s, f->url);
	sb_adds(s, "\" alt=\"\"></body></html>");
	return 1;
}

/* post: a form sent with POST to url, else NULL */
static int load_post(browser *b, const char *url, int mode, const FSUBMIT *post)
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
	if (mode == NAV_NEW && post == NULL && b->p != NULL && fragment(url)[0] && same_but_fragment(url, b->url)) {
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
	r = fetch(b, url, mode == NAV_RELOAD, NULL, 0, 1, post, f);
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
	} else if (b->pictures && (pic_kind((const unsigned char *) f->data, f->len) == PIC_JPEG ||
				   pic_kind((const unsigned char *) f->data, f->len) == PIC_PNG ||
				   pic_kind((const unsigned char *) f->data, f->len) == PIC_GIF) &&
		   picture_page(&src, f)) {
		/* a picture on its own: a page that shows it */
		r = build(b, src.s, src.len, "utf-8", f->url, 0, &d, &p);
	} else {
		char why[200];
		snprintf(why, sizeof why, "This is %s (%ld bytes): EWeb shows web pages, text and "
			 "JPEG, PNG and GIF pictures, not other files yet.", f->mime, f->len);
		error_page(&src, f->url, "EWeb can't show this", why, NULL);
		r = build(b, src.s, src.len, "utf-8", f->url, 0, &d, &p);
	}
	sb_free(&src);
	if (!r || conn_stop) {
		if (r) {
			pg_free(p);
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
		show(b, d, p, shown, f->security, f->status, mode != NAV_RELOAD);
		b->src_len = f->len;
		if (mode == NAV_NEW)
			hist_push(b, shown);
		else
			hist_set_url(b, shown);
		b->at = b->cur;
		keep_trim(b);
	}
	if (mode == NAV_NEW)
		b->scroll_to = anchor_y(b, b->url);
	else
		b->scroll_to = b->hist[b->cur].y;
	free(f->data);
	free(f);
	return BR_SHOWN;
}

static int load(browser *b, const char *url, int mode)
{
	return load_post(b, url, mode, NULL);
}

/* ---------------------------------------------------------------- what the front end asks */

/* a long decode keeps the window alive, and Esc stops it */
static int picture_poll(void)
{
	if (pf_idle)
		pf_idle();
	return !conn_stop;
}

void br_init(browser *b, int32_t width, int32_t zoom)
{
	pic_poll = picture_poll;
	memset(b, 0, sizeof *b);
	b->width = width;
	b->zoom = zoom > 0 ? zoom : 1024;
	b->cur = -1;
	b->at = -1;
	b->scroll_to = -1;
	b->search = "";
	b->pictures = 1;
}

void br_free(browser *b)
{
	int i;
	if (b->p != NULL)
		pg_free(b->p);
	if (b->d != NULL)
		doc_free(b->d);
	b->p = NULL;
	b->d = NULL;
	for (i = 0; i < b->nhist; i++) {
		free(b->hist[i].url);
		unkeep(&b->hist[i]);
	}
	b->nhist = 0;
	b->cur = -1;
	b->at = -1;
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
	base_of(b, b->d, b->url, base, sizeof base);
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

int br_submit(browser *b, fctl *from, fctl *submitter, int32_t x, int32_t y)
{
	FSUBMIT s;
	char base[4096], url[4096];
	int r;
	if (b->d == NULL || !form_submit(b->d, from, submitter, x, y, &s))
		return BR_STOPPED;
	base_of(b, b->d, b->url, base, sizeof base);
	/* no action: the page's own address, without its fragment */
	if (s.action[0] == 0) {
		str_copy(url, base, sizeof url);
		url[strcspn(url, "#")] = 0;
	} else if (!resolve(base, s.action, url, sizeof url)) {
		form_submit_free(&s);
		return BR_STOPPED;
	}
	if (!s.post || !(has_scheme(url, "http") || has_scheme(url, "https"))) {
		/* the entries as the address's query, its fragment kept */
		char frag[1024], *h = strchr(url, '#');
		size_t q;
		frag[0] = 0;
		if (h != NULL) {
			str_copy(frag, h, sizeof frag);
			*h = 0;
		}
		url[strcspn(url, "?")] = 0;
		q = strlen(url);
		if (q + 1 + (size_t) s.len + strlen(frag) + 1 > sizeof url) {
			form_submit_free(&s);
			return BR_STOPPED;	/* too long for an address */
		}
		url[q] = '?';
		memcpy(url + q + 1, s.body, (size_t) s.len);
		strcpy(url + q + 1 + s.len, frag);
		r = load(b, url, NAV_NEW);
	} else {
		r = load_post(b, url, NAV_NEW, &s);
	}
	form_submit_free(&s);
	return r;
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
	if (b->hist[i].p != NULL && b->hist[i].p->opt.zoom == b->zoom) {
		/* kept: shown at once, laid out again only for another width */
		BR_ENTRY *e = &b->hist[i];
		doc *d = e->d;
		page *p = e->p;
		int status = e->status;
		long src_len = e->src_len;
		char security[96];
		str_copy(security, e->security ? e->security : "", sizeof security);
		e->d = NULL;
		e->p = NULL;
		unkeep(e);
		show(b, d, p, e->url, security, status, 1);
		b->src_len = src_len;
		b->cur = i;
		b->at = i;
		b->ms_fetch = b->ms_parse = b->ms_style = b->ms_layout = 0;
		if (p->opt.width != b->width) {
			unsigned long t = pf_ms();
			br_relayout(b, b->width);
			b->ms_layout = pf_ms() - t;
		}
		keep_trim(b);
		b->scroll_to = e->y;
		return BR_SHOWN;
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
	char base[4096];
	page *p;
	b->width = width;
	if (b->d == NULL)
		return 1;
	base_of(b, b->d, b->url, base, sizeof base);
	p = lay(b, b->d, base);
	if (p == NULL)
		return 0;
	pg_free(b->p);
	b->p = p;
	shown_imgs(b);
	return 1;
}

/* ---------------------------------------------------------------- the page's pictures */

static int b64(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+' || c == '-')
		return 62;
	if (c == '/' || c == '_')
		return 63;
	return -1;
}

/* the bytes of a data: address, base64 or percent-escaped (malloc'ed);
   NULL if it isn't one */
static char *data_url(const char *url, long *len)
{
	const char *comma = strchr(url, ','), *p;
	char *out, *o;
	int base64;
	if (!has_scheme(url, "data") || comma == NULL)
		return NULL;
	base64 = comma - url >= 7 && strncmp(comma - 7, ";base64", 7) == 0;
	out = malloc(strlen(comma) + 1);
	if (out == NULL)
		return NULL;
	o = out;
	if (base64) {
		unsigned long acc = 0;
		int bits = 0, v;
		for (p = comma + 1; *p; p++) {
			if (*p == '%' && p[1] == '3' && (p[2] == 'D' || p[2] == 'd'))
				break;	/* =, escaped */
			if ((v = b64((unsigned char) *p)) < 0)
				continue;	/* = and white space */
			acc = (acc << 6) | (unsigned long) v;
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				*o++ = (char) (acc >> bits);
			}
		}
	} else {
		for (p = comma + 1; *p; p++) {
			if (*p == '%' && p[1] && p[2]) {
				char h[3] = { p[1], p[2], 0 };
				*o++ = (char) strtol(h, NULL, 16);
				p += 2;
			} else {
				*o++ = *p;
			}
		}
	}
	*len = (long) (o - out);
	return out;
}

static void log_img(browser *b, ew_img *im, const char *what, unsigned long ms_fetch, unsigned long ms_decode)
{
	char line[600];
	int32_t w, h;
	if (b->log == NULL)
		return;
	img_size(im, &w, &h);
	snprintf(line, sizeof line, "picture %.300s %s %ldx%ld fetch %lu decode %lu",
		 strncmp(img_url(im), "data:", 5) == 0 ? "data:" : img_url(im), what, (long) w, (long) h,
		 ms_fetch, ms_decode);
	b->log(b->ud, line);
}

/* a picture still to do at item d: to fetch, or to decode at its size */
static int to_do(const browser *b, const dl_item *d)
{
	ew_img *im = d->img;
	int st;
	if (d->type != DL_IMAGE)
		return 0;
	st = img_state(im);
	if (st == IMG_WAITING)
		return !b->pic_stopped;
	return st == IMG_HERE && !(d->flags & DLF_ALT) && !img_tried(im, d->w, d->h);
}

/* the item to work on next: in the window first, then below, then above */
static long next_item(const browser *b, int32_t top, int32_t height)
{
	long i, best = -1;
	unsigned long bd = 0xffffffffUL;
	for (i = 0; i < b->p->nitems; i++) {
		const dl_item *d = &b->p->items[i];
		unsigned long dist;
		if (!to_do(b, d))
			continue;
		if (d->y + d->h > top && d->y < top + height)
			dist = 0;
		else if (d->y >= top + height)
			dist = 1 + (unsigned long) (d->y - (top + height));
		else
			dist = 0x40000000UL + (unsigned long) (top - (d->y + d->h));
		if (dist < bd) {
			bd = dist;
			best = i;
			if (dist == 0)
				break;
		}
	}
	return best;
}

int br_pictures_pending(browser *b)
{
	long i;
	if (b->p == NULL)
		return 0;
	if (b->pic_relayout)
		return 1;
	for (i = 0; i < b->p->nitems; i++)
		if (to_do(b, &b->p->items[i]))
			return 1;
	return 0;
}

void br_pictures_stop(browser *b)
{
	b->pic_stopped = 1;
}

/* pictures still to fetch, each once */
static int waiting(const browser *b)
{
	long i, k;
	int n = 0;
	for (i = 0; i < b->p->nitems; i++) {
		const dl_item *d = &b->p->items[i];
		if (d->type != DL_IMAGE || img_state(d->img) != IMG_WAITING)
			continue;
		for (k = 0; k < i; k++)
			if (b->p->items[k].type == DL_IMAGE && b->p->items[k].img == d->img)
				break;
		n += k == i;
	}
	return n;
}

/* the file of a picture: from the network, a file or a data: address */
static int fetch_picture(browser *b, ew_img *im)
{
	const char *url = img_url(im);
	char *data;
	long len = 0;
	if (has_scheme(url, "data")) {
		data = data_url(url, &len);
	} else {
		FETCH *f = malloc(sizeof *f);
		char host[128], msg[200];
		long max = img_max / 2 < (4L << 20) ? img_max / 2 : (4L << 20);
		if (f == NULL)
			return 0;
		host_of(url, host, sizeof host);
		if (b->pic_n == 0)
			b->pic_n = waiting(b) + b->pic_done;
		snprintf(msg, sizeof msg, "Pictures: %d of %d from %s...", b->pic_done + 1,
			 b->pic_n > b->pic_done ? b->pic_n : b->pic_done + 1, has_scheme(url, "file") ? "the disk" : host);
		say(b, "%s", msg);
		if (!fetch(b, url, 0, "image/jpeg,image/png,image/gif;q=0.9", max, 0, NULL, f) ||
		    (f->status != 0 && (f->status < 200 || f->status >= 300))) {
			free(f->data);
			free(f);
			if (conn_stop)
				return -1;
			b->pic_done++;
			return 0;
		}
		b->pic_done++;
		data = f->data;
		len = f->len;
		free(f);
	}
	if (data == NULL)
		return 0;
	return img_set_file(im, data, len);
}

int br_pictures_step(browser *b, int32_t top, int32_t height, int32_t *y0, int32_t *y1)
{
	long k, i;
	ew_img *im;
	int32_t iw, ih, lo = 0x7fffffff, hi = -1;
	int fetched = 0;
	unsigned long t0 = pf_ms(), ms_fetch = 0, ms_decode = 0;
	char what[120];
	*y0 = *y1 = 0;
	if (b->p == NULL)
		return BRP_NONE;
	conn_stop = 0;
	k = next_item(b, top, height);
	if (k < 0) {
		if (!b->pic_relayout)
			return BRP_NONE;
		goto relayout;
	}
	im = b->p->items[k].img;
	if (img_state(im) == IMG_WAITING) {
		int r = fetch_picture(b, im);
		if (r < 0) {
			img_waiting(im);
			return BRP_STOPPED;
		}
		ms_fetch = pf_ms() - t0;
		if (r == 0) {
			img_failed(im);
			snprintf(what, sizeof what, "failed (%s)", img_why(im));
			log_img(b, im, what, ms_fetch, 0);
		}
		fetched = 1;
		/* laid out with a size it hasn't: laid out again */
		img_size(im, &iw, &ih);
		for (i = 0; i < b->p->nitems && iw > 0; i++) {
			const dl_item *d = &b->p->items[i];
			int64_t a, c;
			if (d->type != DL_IMAGE || d->img != im || !(d->flags & DLF_NOSIZE))
				continue;
			a = (int64_t) iw * d->h;
			c = (int64_t) ih * d->w;
			if ((d->flags & DLF_ALT) || (a > c ? a - c : c - a) > (iw > ih ? iw : ih))
				b->pic_relayout = 1;
		}
	}
	if (b->pic_relayout && (pf_ms() - b->pic_laid >= 2000 || waiting(b) == 0))
		goto relayout;
	/* decoded at each size it is shown at; what to draw again */
	for (i = 0; i < b->p->nitems; i++) {
		const dl_item *d = &b->p->items[i];
		const page *p0 = b->p;
		if (d->type != DL_IMAGE || d->img != im || (d->flags & DLF_ALT))
			continue;
		if (!img_tried(im, d->w, d->h) && img_state(im) == IMG_HERE) {
			unsigned long t = pf_ms();
			int32_t w = d->w, h = d->h;
			int ok;
			if (!fetched)
				say(b, "%s", "Pictures: decoding...");
			ok = img_decode(im, w, h);
			if (!ok && conn_stop)
				return BRP_STOPPED;
			if (b->p != p0)
				return BRP_LAYOUT;	/* laid out again meanwhile (the window sized) */
			ms_decode += pf_ms() - t;
			snprintf(what, sizeof what, "%s at %ldx%ld", ok ? "decoded" : img_why(im), (long) w, (long) h);
			log_img(b, im, what, ms_fetch, pf_ms() - t);
		}
		if (d->y < lo)
			lo = d->y;
		if (d->y + d->h > hi)
			hi = d->y + d->h;
	}
	img_trim();
	if (hi > lo) {
		*y0 = lo;
		*y1 = hi;
	}
	return BRP_DRAWN;
relayout:
	b->pic_relayout = 0;
	b->pic_laid = pf_ms();
	if (!br_relayout(b, b->width))
		return BRP_STOPPED;
	return BRP_LAYOUT;
}
