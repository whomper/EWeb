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
#include "bookmark.h"
#include "form.h"
#include "html.h"
#include "img.h"
#include "js.h"
#include "font.h"
#include "pic.h"
#include "http.h"
#include "conn.h"
#include "cookie.h"
#include "dcache.h"
#include "plat.h"
#include "util.h"

#ifndef EWEB_VERSION
#define EWEB_VERSION ""
#endif

long br_cache_max = 4L << 20;

/* NAV_REPLACE: a new page in place of the one shown in the history (a
   refresh: the HTML Standard's historyHandling "replace"); NAV_REFRESH:
   the page shown again for its refresh, fetched anew as Reload does but
   its stylesheets as any page's (Chrome's "reload": the page validated,
   not what it uses) */
enum { NAV_NEW, NAV_HISTORY, NAV_RELOAD, NAV_REPLACE, NAV_REFRESH };

/* the load's phase, for the front end's ribbon */
static void phase(browser *b, int ph)
{
	if (b->phase != NULL)
		b->phase(b->ud, ph);
}

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

/* href resolved against base (a page's address, or its <base>), its
   query in enc, the page's encoding (the HTML Standard's "encoding-parsing
   a URL", 2.4.2: NULL UTF-8, as a stylesheet's url()s are) */
static int resolve_enc(const char *base, const char *href, char *out, int size, const URL_ENC *enc)
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
			ok = url_parse_enc(u, href, bu, enc);
		else
			ok = url_parse_enc(u, href, NULL, enc);
		if (ok)
			str_copy(out, u->href, size);
	}
	free(u);
	free(bu);
	return ok;
}

static int resolve(const char *base, const char *href, char *out, int size)
{
	return resolve_enc(base, href, out, size, NULL);
}

/* the encoding a document's addresses put their query in: its own, NULL
   for UTF-8 (and UTF-16, whose output encoding that is). One table is
   kept (from the first page in one: none for UTF-8 alone), made again
   when a page in another comes. */
static const URL_ENC *doc_enc(const doc *d)
{
	static char name[sizeof ((doc *) 0)->charset];
	static URL_ENC *tab;
	static int have;
	if (d == NULL || d->charset[0] == 0 || strcmp(d->charset, "UTF-8") == 0)
		return NULL;
	if (strcmp(name, d->charset) != 0) {
		if (tab == NULL && (tab = malloc(sizeof *tab)) == NULL)
			return NULL;
		str_copy(name, d->charset, sizeof name);
		have = html_enc_table(name, tab->hi);
	}
	return have ? tab : NULL;
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

static void host_of(const char *url, char *out, int size);

/* a bookmark's mark in its Remove link, so that a list shown before
   another was removed can't remove the wrong one */
static unsigned long bm_mark(int i)
{
	const char *u = bm_list[i].url;
	unsigned long h = 2166136261UL;
	for (; *u; u++)
		h = ((h ^ (unsigned char) *u) * 16777619UL) & 0xFFFFFFFFUL;
	return h;
}

/* about:bookmarks: each with its address's host and a Remove link */
static void bookmarks_page(SBUF *s)
{
	int i;
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8><title>Bookmarks</title>");
	sb_adds(s, own_style);
	sb_adds(s, "<style>.rm{font-size:13px;color:#a00}</style></head><body><h1>Bookmarks</h1>");
	if (bm_n == 0)
		sb_adds(s, "<p>None yet: Bookmarks &gt; Add bookmark (^D) keeps the page shown here, and in "
			   "the Bookmarks menu.</p>");
	else
		sb_adds(s, "<ul>");
	for (i = 0; i < bm_n; i++) {
		char host[128], mark[96];
		host_of(bm_list[i].url, host, sizeof host);
		sb_adds(s, "<li><a href=\"");
		esc(s, bm_list[i].url);
		sb_adds(s, "\">");
		esc(s, bm_list[i].title[0] ? bm_list[i].title : bm_list[i].url);
		sb_adds(s, "</a> <span class=small>");
		esc(s, host);
		snprintf(mark, sizeof mark, "</span> <a class=rm href=\"about:bookmarks?remove=%d-%08lx\">", i, bm_mark(i));
		sb_adds(s, mark);
		sb_adds(s, "Remove</a></li>");
	}
	if (bm_n > 0)
		sb_adds(s, "</ul>");
	sb_adds(s, "<p class=small>They are kept in BOOKMARK.HTM, next to EWEB.PRG: a file other browsers "
		   "import and export.</p></body></html>");
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

/* ---------------------------------------------------------------- fetching */

typedef struct {
	char *data;		/* malloc'ed, NUL-terminated */
	long len;
	char url[4096];		/* where it came from */
	char mime[64], charset[40], security[96];
	int status;		/* 0: a file or the cache */
	char err[200];		/* when nothing came */
	int nomem;		/* and that for want of memory */
	int stale;		/* from the cache, out of date: the server couldn't be reached */
	char refresh[2048];	/* a page's Refresh header, "" none */
	/* a page's Content-Disposition (RFC 6266): the caches would lose
	   what it says (so it isn't kept: they keep no headers), it says
	   attachment (to save, not to show), and the file name it gives
	   (UTF-8, "" none) */
	int disposed, attach;
	char name[256];
} FETCH;

#ifndef EW_ST
static void js_host_of(browser *b, js_host *h);
/* the document whose scripts run as it is built (b->d is the page
   before it then), NULL: the page shown's */
static const doc *js_doc;
#endif
static int js_after(browser *b, int *load_r);
static int load_post(browser *b, const char *url, int mode, const FSUBMIT *post);
static int load(browser *b, const char *url, int mode);

#ifndef EW_ST
static int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
#endif

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

/* cookie.c's for the HTTP client's requests */
static void cookie_in(const HTTPREQ *rq, const URL *u, const char *method, const char *value)
{
	CKCTX x;
	x.top = rq->top;
	x.safe = !strcmp(method, "GET") || !strcmp(method, "HEAD");
	x.site = rq->site;
	x.script = 0;
	ck_set(u, value, &x);
}

static void cookie_out(const HTTPREQ *rq, const URL *u, const char *method, SBUF *req)
{
	CKCTX x;
	SBUF v;
	x.top = rq->top;
	x.safe = !strcmp(method, "GET") || !strcmp(method, "HEAD");
	x.site = rq->site;
	x.script = 0;
	sb_init(&v);
	if (ck_get(u, &x, &v) && v.s != NULL) {
		sb_adds(req, "Cookie: ");
		sb_adds(req, v.s);	/* not sb_printf: it cuts long lines */
		sb_adds(req, "\r\n");
	}
	sb_free(&v);
}

/* 1: f->data has what came (a page, or a server's error page); 0: none,
   f->err says why (conn_stop: stopped). accept NULL: a page, else a
   stylesheet or a picture of the page site (its address; a page's: the
   page it was found on, NULL when typed, for cookies and Referer). post:
   a form sent with POST (its answer isn't kept in the cache), else NULL */
/* for the next fetch only: more header lines (each with CRLF), and the
   types whose answer isn't wanted (HTTPREQ.refuse); NULL: none */
static const char *fetch_more, *fetch_refuse;
/* for the disk cache: the request's variant (the pens asked for), after
   the address, or NULL */
static const char *fetch_variant;
/* requests under way: GEM's messages come meanwhile (pf_idle), and a
   window sized then doesn't fetch the page's sheets inside one
   (br_relayout) */
static int net_busy;

/* does name (s to e: a file's, or an address's path) end in ending */
static int ends_in(const char *s, const char *e, const char *ending)
{
	size_t n = strlen(ending);
	return (size_t) (e - s) >= n && strncasecmp(e - n, ending, n) == 0;
}

/* is an answer a gzip file, to be saved as it came rather than unpacked
   (what browsers do): Content-Encoding gzip with a gzip type, or with a
   name (name: Content-Disposition's, else the address's) ending in .gz
   or .tgz */
static int gzip_file(const HTTPRESP *rs, const char *name)
{
	static const char *const types[] = { "application/gzip", "application/x-gzip", "application/x-tar+gzip" };
	const char *s = name, *e;
	size_t i;
	if (rs->encoding != 1)
		return 0;
	for (i = 0; i < sizeof types / sizeof types[0]; i++)
		if (strcmp(rs->mime, types[i]) == 0)
			return 1;
	if (*s)
		e = s + strlen(s);
	else {
		s = rs->url.href + rs->url.path;
		e = rs->url.href + rs->url.query;
	}
	return ends_in(s, e, ".gz") || ends_in(s, e, ".tgz");
}

/* fetch's HTTPREQ.head for a page: its Content-Disposition, and a gzip
   file that is to be saved (one it says to, or of a type EWeb doesn't
   show) kept as it came */
static int page_head(void *ud, HTTPRESP *rs)
{
	FETCH *f = ud;
	const char *v = http_header_at(rs, "Content-Disposition");
	int t = v != NULL ? http_disposition(v, f->name, sizeof f->name) : -1;
	/* shown as a page whatever its name (a picture may yet be saved:
	   one EWeb can't decode) */
	int page = rs->mime[0] == 0 || strncmp(rs->mime, "text/", 5) == 0 ||
		   strcmp(rs->mime, "application/xhtml+xml") == 0;
	f->attach = t == 1;
	/* not kept when the caches, which keep no headers, would lose what
	   it says: attachment, or the name of a file to be saved ("inline"
	   on a page is kept, as without the header) */
	f->disposed = f->attach || (t == 0 && f->name[0] && !page);
	if ((f->attach || !(page || strncmp(rs->mime, "image/", 6) == 0)) && gzip_file(rs, f->name))
		rs->packed = 1;
	return 1;
}

static int fetch(browser *b, const char *url, const char *site, int fresh, const char *accept, long maxbody,
		 int keep, const FSUBMIT *post, FETCH *f)
{
	memset(f, 0, sizeof *f);
	str_copy(f->url, url, sizeof f->url);
	if (has_scheme(url, "file")) {
		char path[512];
		file_path(url, path, sizeof path);
		f->data = pf_load(path, &f->len);
		if (f->data == NULL) {
			/* there, and as much memory couldn't be had: short of it
			   (not a file that can't be read) */
			long size = pf_size(path);
			void *room = size >= 0 ? malloc((size_t) size + 1) : NULL;
			f->nomem = size >= 0 && room == NULL;
			free(room);
			snprintf(f->err, sizeof f->err, f->nomem ? "There isn't the memory to read %.150s." :
				 "There is no file %.150s, or it can't be read.", path);
			return 0;
		}
		/* a text file by its name, as a server would say: the rest are
		   read as pages ("") */
		{
			size_t n = strlen(path);
			if (n > 4 && path[n - 4] == '.' && (path[n - 3] | 32) == 't' && (path[n - 2] | 32) == 'x' &&
			    (path[n - 1] | 32) == 't')
				str_copy(f->mime, "text/plain", sizeof f->mime);
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
	if (fresh != 1) {
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
		HTTPRESP *rs;
		SBUF body;
		DC_GOT got;
		char key[4096], *both = NULL;
		int ok, have = 0, cached = 0;
		/* the disk cache (dcache.h), by the address without its fragment:
		   fresh, or any age for Back and Forward, it is used as it is;
		   else the server is asked whether it changed */
		str_copy(key, url, sizeof key);
		key[strcspn(key, "#")] = 0;
		if (fetch_variant != NULL && strlen(key) + strlen(fetch_variant) + 2 < sizeof key) {
			strcat(key, "#");	/* no address has one: the fragment is cut off */
			strcat(key, fetch_variant);
		}
		/* a page through the Pi's browser: a snapshot of it as it is now,
		   never kept (webgate says no-store), so not looked for either */
		int snap = b->snapshot && accept == NULL && post == NULL && http_gateway[0];
		if (post == NULL && !snap && dc_get(key, &got)) {
			/* fresh (immutable: even for Reload, RFC 8246), or any age
			   for Back and Forward */
			if ((fresh != 1 && (got.fresh || fresh == 2)) || (fresh == 1 && got.fresh && got.immutable)) {
				f->data = got.data;
				f->len = got.len;
				str_copy(f->url, got.url, sizeof f->url);
				str_copy(f->mime, got.mime, sizeof f->mime);
				str_copy(f->charset, got.charset, sizeof f->charset);
				str_copy(f->security, got.security, sizeof f->security);
				f->status = 200;
				if (keep)
					cache_put(url, f->url, f->data, f->len, f->mime, f->charset, f->security);
				return 1;
			}
			cached = 1;	/* kept for when the server can't be reached */
			if (got.validate[0])
				have = 1;
		}
		rs = malloc(sizeof *rs);
		if (rs == NULL) {
			if (cached)
				free(got.data);
			snprintf(f->err, sizeof f->err, "Out of memory.");
			f->nomem = 1;
			return 0;
		}
		memset(&rq, 0, sizeof rq);
		rq.url = url;
		rq.headers = have ? got.validate : snap ? "X-Gateway-Snapshot: 1\r\n" : NULL;
		if (fetch_more != NULL) {
			both = malloc(strlen(rq.headers ? rq.headers : "") + strlen(fetch_more) + 1);
			if (both != NULL) {
				strcpy(both, rq.headers ? rq.headers : "");
				strcat(both, fetch_more);
				rq.headers = both;
			}
		}
		rq.refuse = fetch_refuse;
		rq.accept = accept;
		if (accept == NULL) {
			rq.head = page_head;
			rq.ud = f;
		}
		if (post != NULL) {
			rq.method = "POST";
			rq.body = post->body;
			rq.bodylen = post->len;
			rq.ctype = post->ctype;
		}
		rq.maxbody = maxbody;
		rq.top = accept == NULL;
		rq.site = site != NULL && (has_scheme(site, "http") || has_scheme(site, "https")) ? site : NULL;
		rq.referrer = rq.site;
		sb_init(&body);
		{
			unsigned long t0 = pf_ms(), ms;
			net_busy++;
			ok = http_request(&rq, rs, &body, f->err, sizeof f->err);
			net_busy--;
			f->nomem = !ok && strstr(f->err, "memory") != NULL;
			ms = pf_ms() - t0;
			/* how fast answers come, from one big enough to tell */
			if (ok && body.len >= 8192 && ms > 0)
				b->rate = (long) ((int64_t) body.len * 1000 / (int64_t) ms);
		}
		free(both);
		if (ok && have && rs->status == 304) {
			/* unchanged: what is kept, fresh again */
			dc_fresh(key, rs->headers.s ? rs->headers.s : "");
			sb_free(&body);
			f->data = got.data;
			f->len = got.len;
			str_copy(f->url, got.url, sizeof f->url);
			str_copy(f->mime, got.mime, sizeof f->mime);
			str_copy(f->charset, got.charset, sizeof f->charset);
			str_copy(f->security, got.security, sizeof f->security);
			f->status = 200;
			if (keep)
				cache_put(url, f->url, f->data, f->len, f->mime, f->charset, f->security);
			http_resp_free(rs);
			free(rs);
			return 1;
		}
		if (!ok && cached && got.stale_ok && !conn_stop && post == NULL) {
			/* no answer: what is kept, said to be out of date (RFC 9111,
			   4.2.4: a cache may when it is cut off) */
			sb_free(&body);
			f->data = got.data;
			f->len = got.len;
			str_copy(f->url, got.url, sizeof f->url);
			str_copy(f->mime, got.mime, sizeof f->mime);
			str_copy(f->charset, got.charset, sizeof f->charset);
			str_copy(f->security, got.security, sizeof f->security);
			f->status = 200;
			f->stale = 1;
			http_resp_free(rs);
			free(rs);
			return 1;
		}
		if (cached)
			free(got.data);
		if (ok && post != NULL && rs->status < 400)
			dc_drop(key);	/* what was kept for it has changed (RFC 9111, 4.4) */
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
			/* a page's Refresh header (one too long for the buffer is
			   dropped: its address cut short would be another) */
			if (accept == NULL && http_header(rs, "Refresh", f->refresh, sizeof f->refresh) != NULL &&
			    strlen(f->refresh) == sizeof f->refresh - 1)
				f->refresh[0] = 0;
			/* an answer whose Content-Disposition says attachment, or
			   names a file to save, isn't kept, here or on the disk:
			   from the cache it would have neither its name nor
			   "attachment" */
			if (f->status == 200 && post == NULL && !f->disposed)
				dc_put(key, f->url, rs->headers.s ? rs->headers.s : "", f->mime, f->charset, f->security,
				       f->data, f->len);
			else if (f->disposed)
				dc_drop(key);
			/* a page with a Refresh header isn't kept, here or on the disk
			   (dc_put): from the cache it wouldn't go on */
			if (f->status == 200 && keep && !f->refresh[0] && !f->disposed) {
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
	int quiet;		/* not a load: nothing said on the status line */
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
	if (!resolve_enc(sh->base, href, url, sizeof url, doc_enc(sh->d)))
		return;
	f = malloc(sizeof *f);
	if (f == NULL)
		return;
	sh->n++;
	if (!sh->quiet) {
		char host[128], msg[200];
		host_of(url, host, sizeof host);
		snprintf(msg, sizeof msg, "%s: stylesheet %d...", host, sh->n);
		phase(sh->b, BRPH_STYLES);
		say(sh->b, "%s", msg);
	}
	if (fetch(sh->b, url, sh->base, sh->fresh, "text/css,*/*;q=0.1", 2L << 20, 1, NULL, f) &&
	    (f->status == 0 || (f->status >= 200 && f->status < 300)))
		/* its url()s from where it came */
		ec_sheet_at(sh->d->css, f->data, (size_t) f->len, EC_ORIGIN_AUTHOR,
			    f->url[0] ? f->url : url);
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

/* the page's sheets into its CSS context, in the cascade's order: the
   UA's (when it isn't on the base), the one that hides <noscript> when
   scripts run, the page's in document order (doc_sheets: a <link>'s
   fetched, fresh as fetch() takes it) and the user's (extra_css). What
   they hang on is kept with the document: the widths their media queries
   hold at as they did (at another, br_relayout adds them again) and, with
   scripts, which they were (doc_sheets_key: a script that changes them,
   they are added again) */
static void add_sheets(browser *b, doc *d, const char *base, int fresh, int quiet)
{
	SHEETS sh;
	int stop = conn_stop;
	doc_add_ua(d);
	if (d->scripting)
		ec_sheet(d->css, "noscript { display: none !important }", 37, EC_ORIGIN_UA);
	sh.b = b;
	sh.d = d;
	sh.base = base;
	sh.fresh = fresh;
	sh.n = 0;
	sh.quiet = quiet;
	doc_sheets(d, sheet_cb, &sh);
	if (b->extra_css != NULL)
		ec_sheet(d->css, b->extra_css, strlen(b->extra_css), EC_ORIGIN_AUTHOR);
	ec_media_span(d->css, &d->span_lo, &d->span_hi);
	if (conn_stop && !stop) {
		/* stopped while its sheets came: at any other width they are
		   taken again */
		ec_media m = media_of(b);
		d->span_lo = d->span_hi = m.width;
	}
}

/* the page's sheets being added again: its styles are gone meanwhile,
   and a window sized as a sheet comes (GEM's messages come in pf_idle)
   isn't laid out then (br_relayout) */
static int resheeting;

static int unkeep_all(browser *b);

/* the page's sheets added again: for the screen as it is now (another
   width), or as a script changed them. Those fetched before come from
   the cache, those that hold now for the first time over the network.
   What they took is given back first (ec_drop_sheets), so they take what
   they took; when they don't fit even so (a small machine), the pages kept for
   Back go and they are added once more. by_script: a script changed
   them, else the window was sized; the log says which */
static void readd_sheets(browser *b, doc *d, const char *base, int fresh, int quiet, int by_script)
{
	ec_media m = media_of(b);
	int tries;
	resheeting++;
	for (tries = 0; tries < 2; tries++) {
		ec_drop_sheets(d->css);
		ec_set_media(d->css, &m);
		add_sheets(b, d, base, fresh, quiet);
		if (!ec_sheets_short(d->css) || conn_stop || tries > 0 || !unkeep_all(b))
			break;
	}
	if (b->log != NULL) {
		char line[100];
		if (by_script)
			snprintf(line, sizeof line, "sheets added again: a script changed them");
		else
			snprintf(line, sizeof line, "sheets added again: the window %ld CSS px wide", (long) m.width);
		if (ec_sheets_short(d->css))
			str_copy(line + strlen(line), ", short of memory", (long) (sizeof line - strlen(line)));
		b->log(b->ud, line);
	}
	resheeting--;
}

/* ---------------------------------------------------------------- pictures in the layout */

/* the address the page's pictures are resolved against, while it is
   laid out, and the page (its encoding, for their queries) */
static const char *lay_base;
static const doc *lay_doc;
/* the page is fetched anew for its refresh (NAV_REFRESH): its pictures
   are asked for again, as their HTTP freshness says, as browsers do for
   a document made anew (a camera's, a weather map, sent no-store) */
static int lay_recheck;

/* (a url() of the page's CSS: its query in UTF-8, as an external
   sheet's, which the CSS engine has made absolute already; an <img>'s in
   the page's encoding) */
static void *image_cb(void *ud, const char *src, int css, int32_t *w, int32_t *h)
{
	browser *b = ud;
	char url[4096];
	ew_img *im;
	if (!b->pictures || lay_base == NULL ||
	    !resolve_enc(lay_base, src, url, sizeof url, css ? NULL : doc_enc(lay_doc)))
		return NULL;
	if (!has_scheme(url, "http") && !has_scheme(url, "https") && !has_scheme(url, "file") &&
	    !has_scheme(url, "data"))
		return NULL;
	im = img_get(url);
	if (im == NULL)
		return NULL;
	if (lay_recheck)
		img_recheck(im);
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
	o.text = b->text;
	o.image = image_cb;
	o.ud = b;
	lay_base = base;
	lay_doc = d;
	p = layout_page(d, &o);
	lay_base = NULL;
	lay_doc = NULL;
	page_imgs(p, 1);
	return p;
}

/* the address a document's links and pictures are resolved against */
static void base_of(const browser *b, const doc *d, const char *url, char *out, int size)
{
	if (d == NULL || d->base[0] == 0 || !resolve_enc(url, d->base, out, size, doc_enc(d)))
		str_copy(out, url, size);
}

static void unkeep(BR_ENTRY *e);

/* the pages kept for Back given back: 1 when there was one */
static int unkeep_all(browser *b)
{
	int i, freed = 0;
	for (i = 0; i < b->nhist; i++)
		if (b->hist[i].p != NULL) {
			unkeep(&b->hist[i]);
			freed = 1;
		}
	return freed;
}

/* all that, and the pictures not in use and the fonts' glyphs (read
   again when drawn), but not the page shown; 1 when a kept page went */
static int shed(browser *b)
{
	int freed = unkeep_all(b);
	img_clear();
	font_drop();
	return freed;
}

/* memory for a page that didn't fit (a small machine): the pages kept for
   Back and the one shown go, and the pictures not in use; 1 when
   something was given back */
static int make_room(browser *b)
{
	int freed = 0;
	if (b->p != NULL) {
		pg_free(b->p);
		doc_free(b->d);
		b->p = NULL;
		b->d = NULL;
		freed = 1;
	}
	return shed(b) || freed;
}

static int build1(browser *b, const char *data, long len, const char *charset, const char *url, int fresh,
		  doc **dout, page **pout);

/* the next build is a text file's (text/plain): its bytes are the text */
static int build_plain;

/* the source made into a page (doc styled and laid out): 1, or 0 when
   out of memory or stopped while fetching its sheets. Out of memory, the
   pages kept go and it is tried once more. charset: the encoding chosen
   by hand, else the HTTP answer's, else NULL (the page's own) */
static int build(browser *b, const char *data, long len, const char *charset, const char *url, int fresh,
		 doc **dout, page **pout)
{
	if (build1(b, data, len, charset, url, fresh, dout, pout))
		return 1;
	if (conn_stop || !make_room(b))
		return 0;
	return build1(b, data, len, charset, url, fresh, dout, pout);
}

static int build1(browser *b, const char *data, long len, const char *charset, const char *url, int fresh,
		  doc **dout, page **pout)
{
	ec_media m = media_of(b);
	doc *d;
	page *p;
	char base[4096];

	unsigned long t = pf_ms(), t_sheets;
	int scripts = br_scripts_for(b, url), again = 0;
	html_opts o;

	b->note[0] = 0;
#ifndef EW_ST
parse:
#endif
	phase(b, BRPH_BUILD);
	say(b, "%s", "Reading the page...");
	memset(&o, 0, sizeof o);
	o.charset = charset && *charset ? charset : NULL;
	o.fallback = br_fallback(url);
	o.plaintext = build_plain;
	o.scripting = scripts;
	d = doc_parse_opts(data, (size_t) len, &o, &m);
	if (d == NULL)
		return 0;
	ec_set_resolver(d->css, resolve);
	b->ms_parse = pf_ms() - t;
	if (d->base[0] == 0 || !resolve_enc(url, d->base, base, sizeof base, doc_enc(d)))
		str_copy(base, url, sizeof base);
	t_sheets = pf_ms();
	/* <noscript> is shown when scripts aren't run (the HTML Standard's
	   rendering: hidden only with scripting): d->scripting */
	add_sheets(b, d, base, again ? 0 : fresh, 0);
	t = pf_ms();
	b->ms_fetch += t - t_sheets;		/* the stylesheets came meanwhile */
#ifndef EW_ST
	if (scripts && !conn_stop && doc_has_scripts(d) && !js_available()) {
		/* EWEBJS.OVL couldn't be loaded (only a page with scripts has it
		   loaded): the page as with its scripts off, and the note says why */
		snprintf(b->note, sizeof b->note, "Its scripts didn't run: EWEBJS.OVL %s", js_problem());
		doc_free(d);
		scripts = 0;
		again = 1;
		t = pf_ms();
		goto parse;
	}
	if (scripts && !conn_stop) {
		js_host h;
		int r;
		unsigned long ms;
		long bytes;
		say(b, "%s", "Running the page's scripts...");
		js_host_of(b, &h);
		js_doc = d;
		r = js_page(d, url, base, &h);
		js_doc = NULL;
		js_stats(d, &ms, &bytes);
		if (r == JS_TOO_MUCH || r == JS_NO_MEMORY) {
			/* over the budget: the page as it is without them */
			snprintf(b->note, sizeof b->note, "%s",
				 r == JS_TOO_MUCH ? "Its scripts were too much for EWeb: shown without them." :
				 "No memory for its scripts: shown without them.");
			if (b->log != NULL) {
				char line[120];
				snprintf(line, sizeof line, "scripts too much ms %lu bytes %ld", ms, bytes);
				b->log(b->ud, line);
			}
			doc_free(d);
			scripts = 0;
			again = 1;
			t = pf_ms();
			goto parse;
		}
		if (r == JS_DONE) {
			if (b->log != NULL) {
				char line[120];
				snprintf(line, sizeof line, "scripts ran ms %lu bytes %ld", ms, bytes);
				b->log(b->ud, line);
			}
			doc_refresh(d);
			js_changed(d);
			/* its scripts changed its sheets (a handler that made a
			   <link>'s media all or its rel stylesheet as it loaded,
			   a <style> added): taken again, those fetched from the
			   cache */
			if (doc_sheets_key(d) != d->sheets_key)
				readd_sheets(b, d, base, fresh == 2 ? 2 : 0, 0, 1);
		}
		t = pf_ms();
	}
#endif
	phase(b, BRPH_BUILD);
	say(b, "%s", "Laying the page out...");
	if (doc_select(d) < 0) {
		/* out of memory styling it (a small machine): as when laying it out,
		   not a page with no styles, which lays out to nothing */
		doc_free(d);
		return 0;
	}
	b->ms_style = pf_ms() - t;
	t = pf_ms();
	p = lay(b, d, base);
	b->ms_layout = pf_ms() - t;
	if (p == NULL) {
		doc_free(d);
		return 0;
	}
	js_set_page(d, p);
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
	free(e->charset);
	free(e->src);
	e->p = NULL;
	e->d = NULL;
	e->security = NULL;
	e->charset = NULL;
	e->src = NULL;
	e->src_len = 0;
}

/* what a page laid out keeps, roughly: its tree, styles and display list
   (about eight times its source, on the page set), and the source */
static long cost(long src_len)
{
	return 9 * src_len + 65536L;
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
		free(b->src);
	} else {
		e = &b->hist[b->at];
		unkeep(e);
		e->d = b->d;
		e->p = b->p;
		e->src = b->src;
		e->src_len = b->src_len;
		str_copy(e->mime, b->mime, sizeof e->mime);
		e->status = b->status;
		e->security = str_ndup(b->security, (long) strlen(b->security));
		e->charset = b->charset[0] ? str_ndup(b->charset, (long) strlen(b->charset)) : NULL;
	}
	b->d = NULL;
	b->p = NULL;
	b->src = NULL;
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
		free(b->src);
		b->src = NULL;
	}
	b->d = d;
	b->p = p;
	b->shows++;
	js_set_page(d, p);
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

/* the page shown's encoding (browse.h): the HTTP answer's charset, the
   one chosen by hand, whether it may be read in another; kept in its
   history entry too, where Reload, Back and Forward find the choice */
static void enc_set(browser *b, const char *charset, const char *enc, int encodable)
{
	char cs[sizeof b->charset];
	int i = 0;
	str_copy(cs, charset, sizeof cs);	/* (either may be b's own) */
	if (encodable)
		for (i = br_nencodings; i > 0 && strcmp(br_encodings[i - 1].name, enc) != 0; i--)
			;
	memcpy(b->charset, cs, sizeof cs);
	str_copy(b->enc, i ? br_encodings[i - 1].name : "", sizeof b->enc);
	b->encodable = encodable;
	if (b->cur >= 0 && b->cur < b->nhist) {
		b->hist[b->cur].enc = (uint8_t) i;
		b->hist[b->cur].encodable = (uint8_t) encodable;
	}
}

static void toggled(void *ud, dnode *k);
static int relay(browser *b);

/* where url's #fragment is on the page shown, or -1 (the top). A closed
   details it is in is opened first and the page laid out again (b->relaid
   then): the HTML Standard's scroll to the fragment (7.4.6.4), its
   ancestor details revealing algorithm (4.11.1). Out of memory even
   without the page shown (relay), that is gone (b->relaid, and a note) */
static int32_t anchor_y(browser *b, const char *url)
{
	const char *fr = fragment(url);
	dnode *n;
	if (b->p == NULL || *fr == 0)
		return -1;
	n = page_anchor_node(b->p, fr);
	if (n != NULL && dn_folded(n) && dn_reveal(b->d, n, toggled, b) > 0) {
		b->relaid = 1;
		if (!relay(b)) {
			snprintf(b->note, sizeof b->note, "%s", "EWeb hasn't the memory to lay the page out again.");
			return -1;
		}
	}
	return page_anchor(b->p, fr);
}

/* a page of EWeb's own, shown in place of url */
static int own_page(browser *b, const char *url, SBUF *src, int mode)
{
	doc *d;
	page *p;
	if (!build(b, src->s, src->len, "utf-8", url, 0, &d, &p))
		return BR_STOPPED;
	show(b, d, p, url, "", 0, mode == NAV_NEW || mode == NAV_HISTORY);
	b->src_len = src->len;
	b->src = sb_steal(src);
	str_copy(b->mime, "text/html", sizeof b->mime);
	if (mode == NAV_NEW)
		hist_push(b, url);
	else if (mode == NAV_REPLACE)
		hist_set_url(b, url);
	b->at = b->cur;
	enc_set(b, "", "", 0);
	keep_trim(b);
	b->scroll_to = mode == NAV_NEW || mode == NAV_REPLACE ? -1 : b->hist[b->cur].y;
	return BR_SHOWN;
}

/* a picture opened on its own: kept as the page's picture, and a page
   that shows it, its name and size the title. anew: the page fetched
   anew for its refresh (a camera's picture with a Refresh header), so
   what came is the picture, changed or not */
static int picture_page(SBUF *s, FETCH *f, int anew)
{
	ew_img *im = img_get(f->url);
	const char *name = strrchr(f->url, '/');
	char t[160];
	long w, h;
	if (im == NULL || !pic_size((const unsigned char *) f->data, f->len, &w, &h))
		return 0;
	if (anew)
		img_recheck(im);	/* (unchanged: as it was decoded) */
	if (img_state(im) != IMG_HERE || img_stale(im)) {
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

/* ---------------------------------------------------------------- refresh */
/* the HTML Standard's shared declarative refresh steps (4.2.5.3): the
   Refresh header's value, else the content of the first <meta
   http-equiv=refresh> the parser put in, parsed by doc_refresh_parse */

static int refreshing;		/* the load is a refresh's (br_refresh_go) */

/* what the status line says at rest once refreshes at once in a row
   are stopped (refresh_arm), until the next load */
static const char loop_note[] = "This page sends EWeb on at once again and again: it stays here.";

void br_refresh_cancel(browser *b)
{
	if (b->refresh != NULL && b->refresh_s > 0)
		b->note[0] = 0;		/* its "Going on to ..." */
	free(b->refresh);
	b->refresh = NULL;
	b->refresh_pics = 0;
	b->refresh_loop = 0;
}

long br_refresh_due(const browser *b)
{
	long ms;
	if (b->refresh == NULL)
		return -1;
	if (b->refresh_pics)	/* not counting yet: its pictures come */
		return b->refresh_s > 0 ? (long) b->refresh_s * 1000 : 1;
	ms = (long) (b->refresh_at - pf_ms());
	return ms > 0 ? ms : 0;
}

/* the page's pictures are in (br_pictures_pending): a refresh of the
   page itself counts its seconds from now */
static void refresh_count(browser *b)
{
	if (b->refresh_pics) {
		b->refresh_pics = 0;
		b->refresh_at = pf_ms() + b->refresh_s * 1000;
	}
}

int br_refresh_note(const browser *b, char *out, int size)
{
	long ms = br_refresh_due(b);
	unsigned long s;
	char in[24];
	if (b->refresh_loop) {
		snprintf(out, size, "%s", loop_note);
		return 1;
	}
	if (ms < 0 || b->refresh_s == 0)
		return 0;	/* none, or at once: nothing to say */
	s = b->refresh_pics ? b->refresh_s : ((unsigned long) ms + 999) / 1000;
	if (s >= 120)
		snprintf(in, sizeof in, "%lu min", (s + 30) / 60);
	else
		snprintf(in, sizeof in, "%lu s", s);
	if (same_but_fragment(b->refresh, b->url)) {
		if (*fragment(b->refresh))
			snprintf(out, size, "This page goes to #%.24s in %s: Esc stops it.", fragment(b->refresh), in);
		else
			snprintf(out, size, "This page loads again in %s: Esc stops it.", in);
	} else {
		char host[128];
		const char *name = strrchr(b->refresh, '/');
		if (has_scheme(b->refresh, "file") && name != NULL)
			str_copy(host, name + 1, sizeof host);
		else
			host_of(b->refresh, host, sizeof host);
		snprintf(out, size, "Going on to %.48s in %.10s: Esc stays here.", host, in);
	}
	return 1;
}

/* one refresh (the header's value, a meta's content) for the page
   shown, its address resolved against base: 0 when it isn't one (its
   address doesn't parse, or is javascript:), so the next may be. (This
   and refresh_arm are kept out of load_post: inlined, their 8 KB of
   addresses sat on the stack under the whole load, and the ST edition's
   48 KB ran out under a stylesheet fetched through the gateway.) */
static __attribute__((noinline)) int refresh_set(browser *b, const char *value, const char *base)
{
	unsigned long secs;
	const char *u;
	size_t n;
	char to[4096];
	if (value == NULL || !doc_refresh_parse(value, &secs, &u, &n))
		return 0;
	if (u == NULL) {
		str_copy(to, b->url, sizeof to);	/* the page itself */
	} else {
		char *href = str_ndup(u, (long) n);
		int ok = href != NULL && resolve_enc(base, href, to, sizeof to, doc_enc(b->d));
		free(href);
		if (!ok || has_scheme(to, "javascript"))
			return 0;
	}
	/* one EWeb doesn't follow on its own (mailto:, data:, a file from a
	   page on the web): the page stays, as the standard lets it ("do
	   nothing") */
	if (!has_scheme(to, "http") && !has_scheme(to, "https") &&
	    !(has_scheme(to, "file") && has_scheme(b->url, "file")))
		return 1;
	b->refresh = str_ndup(to, (long) strlen(to));
	b->refresh_s = secs;
	b->refresh_at = pf_ms() + secs * 1000;
	/* another page: its seconds count from now, the pictures of the
	   page left not waited for (one that goes on at once fetches none);
	   the page itself, or a place on it: from when its pictures are in,
	   as the standard says ("completely loaded"), so that a page that
	   refreshes itself shows them each time */
	b->refresh_pics = b->refresh != NULL && same_but_fragment(to, b->url);
	return 1;
}

/* after a page is shown: its refresh, if it has one; hdr its Refresh
   header ("" none), which the document is made with, before any meta */
static __attribute__((noinline)) void refresh_arm(browser *b, const char *hdr)
{
	char base[4096];
	doc *d = b->d;
	const drefresh *m;
	int found = hdr[0] && refresh_set(b, hdr, b->url);
	for (m = d != NULL ? d->refresh : NULL; !found && m != NULL; m = m->next) {
		/* as the parser put it in, against the base the page had then */
		if (m->based)
			base_of(b, d, b->url, base, sizeof base);
		else
			str_copy(base, b->url, sizeof base);
		found = refresh_set(b, m->content, base);
	}
	if (b->refresh == NULL)
		return;
	if (b->refresh_s == 0 && b->refresh_run >= BR_REFRESH_RUN) {
		/* at once, again and again (a page that refreshes itself, two
		   that send each other on): browsers go on for ever. Said at
		   rest, so it comes back after the pictures' lines */
		br_refresh_cancel(b);
		b->refresh_loop = 1;
		snprintf(b->note, sizeof b->note, "%s", loop_note);
		return;
	}
	br_refresh_note(b, b->note, sizeof b->note);
}

int br_refresh_go(browser *b)
{
	char *to = b->refresh;
	int r;
	if (to == NULL)
		return BR_STOPPED;
	b->refresh = NULL;
	b->refresh_pics = 0;
	if (b->refresh_s > 0)
		b->note[0] = 0;		/* its "Going on to ..." */
	b->refresh_run = b->refresh_s == 0 ? b->refresh_run + 1 : 0;
	if (*fragment(to) && same_but_fragment(to, b->url)) {
		/* a place on the page: scrolled to, the page staying, its
		   address in the history changed (the standard's navigation to
		   a fragment, "replace"; Chrome's too) */
		hist_set_url(b, to);
		str_copy(b->url, to, sizeof b->url);
		b->scroll_to = anchor_y(b, to);
		free(to);
		return BR_SAME;
	}
	refreshing = 1;
	/* the page itself is asked for anew (NAV_REFRESH: as Chrome does, a
	   page that refreshes itself is there for that), its stylesheets
	   as any page's; another takes its place in the history */
	r = load(b, to, same_but_fragment(to, b->url) ? NAV_REFRESH : NAV_REPLACE);
	refreshing = 0;
	free(to);
	return r;
}

/* a file to save (BR_DOWNLOAD): what came, kept in b->dl; f goes */
static int download(browser *b, FETCH *f)
{
	br_dl_free(b);
	b->dl.data = f->data;
	b->dl.len = f->len;
	str_copy(b->dl.url, f->url, sizeof b->dl.url);
	str_copy(b->dl.mime, f->mime, sizeof b->dl.mime);
	str_copy(b->dl.name, f->name, sizeof b->dl.name);
	free(f);
	return BR_DOWNLOAD;
}

/* post: a form sent with POST to url, else NULL */
static int load_post(browser *b, const char *url, int mode, const FSUBMIT *post)
{
	FETCH *f;
	SBUF src;
	doc *d;
	page *p;
	int r, encodable = 0, gone = 0;
	char host[128], enc[sizeof b->enc];

	conn_stop = 0;
	b->elsewhere[0] = 0;
	br_refresh_cancel(b);		/* any load: the page shown's refresh goes */
	if (!refreshing)
		b->refresh_run = 0;
	if (has_scheme(url, "javascript") || has_scheme(url, "data"))
		return BR_STOPPED;	/* nothing EWeb can do with it */
	if (has_scheme(url, "mailto")) {
		str_copy(b->elsewhere, url, sizeof b->elsewhere);
		return BR_ELSEWHERE;
	}
	if (b->can_open != NULL && !b->can_open(b->ud, url))
		return BR_STOPPED;	/* the page shown stays */
	/* the encoding chosen by hand for the page (View > Encoding): kept
	   when it comes again (Reload, a refresh of itself, Back, Forward),
	   not for another */
	str_copy(enc, mode == NAV_NEW || mode == NAV_REPLACE || b->cur < 0 || b->hist[b->cur].enc == 0 ? "" :
		 br_encodings[b->hist[b->cur].enc - 1].name, sizeof enc);
	/* a place on the page shown */
	if (mode == NAV_NEW && post == NULL && b->p != NULL && fragment(url)[0] && same_but_fragment(url, b->url)) {
		hist_push(b, url);
		enc_set(b, b->charset, b->enc, b->encodable);	/* the same page */
		str_copy(b->url, url, sizeof b->url);
		b->relaid = 0;
		b->note[0] = 0;
		b->scroll_to = anchor_y(b, url);
		return BR_SAME;
	}
	sb_init(&src);
	if (has_scheme(url, "about")) {
		if (strcmp(url + 6, "blank") == 0) {
			sb_adds(&src, "<html><head><title></title></head><body></body></html>");
		} else if (strncmp(url + 6, "bookmarks", 9) == 0) {
			/* ?remove=I-MARK: that one gone, the list shown again in
			   its place (Back doesn't remove it twice) */
			const char *q = strstr(url, "?remove=");
			if (q != NULL) {
				char *e;
				long i = strtol(q + 8, &e, 10);
				if (*e == '-' && i >= 0 && i < bm_n && strtoul(e + 1, NULL, 16) == bm_mark((int) i))
					bm_remove((int) i);
				if (b->cur >= 0 && strncmp(b->hist[b->cur].url, "about:bookmarks", 15) == 0)
					mode = NAV_RELOAD;
			}
			url = "about:bookmarks";
			bookmarks_page(&src);
		} else {
			start_page(&src);
		}
		r = own_page(b, url, &src, mode);
		sb_free(&src);
		return r;
	}
	f = malloc(sizeof *f);
	if (f == NULL)
		return BR_STOPPED;
	host_of(url, host, sizeof host);
	phase(b, BRPH_CONNECT);
	if (has_scheme(url, "file"))
		say(b, "%s", "Reading the file...");
	else
		say(b, "Opening %s...", host);
	b->ms_fetch = pf_ms();
	r = fetch(b, url, b->typed ? NULL : b->url,
		  mode == NAV_RELOAD || mode == NAV_REFRESH ? 1 : mode == NAV_HISTORY ? 2 : 0, NULL, 0, 1, post, f);
	if (!r && f->nomem && !conn_stop && post == NULL && make_room(b)) {
		/* it didn't fit beside the pages kept and the one shown (a page
		   loaded again on a small machine): they go, and once more (not
		   a form sent, which the server may have had) */
		gone = b->p == NULL;
		r = fetch(b, url, b->typed ? NULL : b->url,
			  mode == NAV_RELOAD || mode == NAV_REFRESH ? 1 : mode == NAV_HISTORY ? 2 : 0, NULL, 0, 1, post, f);
	}
	b->ms_fetch = pf_ms() - b->ms_fetch;
	if (!r) {
		if (conn_stop && !gone) {
			conn_stop = 0;
			free(f);
			return BR_STOPPED;
		}
		if (conn_stop) {
			/* stopped once the page shown had gone for the memory: it
			   can't stay, so this says so in its place */
			conn_stop = 0;
			snprintf(f->err, sizeof f->err, "%s", "Stopped.");
		}
		error_page(&src, url, "EWeb can't open this page", f->err,
			   has_scheme(url, "file") ? NULL :
			   "Check the address and the network (STinG), then try again (^R).");
		free(f);
		r = own_page(b, url, &src, mode);
		sb_free(&src);
		return r;
	}
	/* what it is: a file its server says to save (RFC 6266, 4.2: not on
	   an error's page, which is shown, as tc2231 has it), else by its type */
	if (f->attach && f->status >= 200 && f->status < 300) {
		sb_free(&src);
		return download(b, f);
	} else if (f->mime[0] == 0 || strcmp(f->mime, "text/html") == 0 || strcmp(f->mime, "application/xhtml+xml") == 0) {
		lay_recheck = mode == NAV_REFRESH;
		r = build(b, f->data, f->len, enc[0] ? enc : f->charset, f->url, mode == NAV_RELOAD, &d, &p);
		lay_recheck = 0;
		encodable = 1;
	} else if (strncmp(f->mime, "text/", 5) == 0) {
		/* a text file: sniffed as a page is, but for a <meta> */
		build_plain = 1;
		r = build(b, f->data, f->len, enc[0] ? enc : f->charset, f->url, 0, &d, &p);
		build_plain = 0;
		encodable = 1;
	} else if (b->pictures && (pic_kind((const unsigned char *) f->data, f->len) == PIC_JPEG ||
				   pic_kind((const unsigned char *) f->data, f->len) == PIC_PNG ||
				   pic_kind((const unsigned char *) f->data, f->len) == PIC_GIF) &&
		   picture_page(&src, f, mode == NAV_REFRESH)) {
		/* a picture on its own: a page that shows it */
		r = build(b, src.s, src.len, "utf-8", f->url, 0, &d, &p);
	} else if (!has_scheme(f->url, "file") && f->status >= 200 && f->status < 300) {
		/* a file to save: the page shown stays */
		sb_free(&src);
		return download(b, f);
	} else {
		char why[200];
		snprintf(why, sizeof why, "This is %s (%ld bytes): EWeb shows web pages, text and "
			 "JPEG, PNG and GIF pictures; other files it saves.", f->mime, f->len);
		error_page(&src, f->url, "EWeb can't show this", why, NULL);
		f->refresh[0] = 0;	/* EWeb's own page in its place goes nowhere */
		r = build(b, src.s, src.len, "utf-8", f->url, 0, &d, &p);
	}
	sb_free(&src);
	if (!r && !conn_stop) {
		/* no memory for it, even with the kept pages gone: said on a
		   page of EWeb's own, which is small */
		free(f->data);
		free(f);
		error_page(&src, url, "EWeb can't show this page",
			   "There isn't enough memory for it on this Atari.", NULL);
		r = own_page(b, url, &src, mode);
		sb_free(&src);
		return r;
	}
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
		show(b, d, p, shown, f->security, f->status, mode == NAV_NEW || mode == NAV_HISTORY);
		if (f->stale && b->note[0] == 0)
			snprintf(b->note, sizeof b->note, "%s",
				 "From the cache: the server couldn't be reached, so it may be out of date.");
		b->src_len = f->len;
		b->src = f->data;
		f->data = NULL;
		str_copy(b->mime, f->mime, sizeof b->mime);
		if (mode == NAV_NEW)
			hist_push(b, shown);
		else
			hist_set_url(b, shown);
		b->at = b->cur;
		enc_set(b, f->charset, enc, encodable);
		keep_trim(b);
		/* a page loaded anew: its refresh. Back and Forward leave it be,
		   as the standard lets them (a page that goes on at once would
		   otherwise make Back go nowhere) */
		if (mode != NAV_HISTORY)
			refresh_arm(b, f->refresh);
	}
	if (mode == NAV_NEW || mode == NAV_REPLACE)
		b->scroll_to = anchor_y(b, b->url);
	else
		b->scroll_to = b->hist[b->cur].y;
	free(f->data);
	free(f);
	if (b->d != NULL && b->d->js != NULL && js_navigation(b->d, NULL, NULL, NULL) != NULL) {
		/* its scripts sent the browser on (location = ...) */
		int lr;
		if (js_after(b, &lr) == BRJ_LOAD)
			return lr;
	}
	return BR_SHOWN;
}

static int load(browser *b, const char *url, int mode)
{
	int r = load_post(b, url, mode, NULL);
	dc_job_end();		/* the page and its stylesheets: into the pack */
	return r;
}

/* ---------------------------------------------------------------- encodings */

/* ISO-8859-1 and ISO-8859-9 are the Encoding Standard's labels of
   windows-1252 and windows-1254, which are theirs with more letters */
const BR_ENC br_encodings[] = {
	{ "Unicode (UTF-8)", "UTF-8" },
	{ "Western (Windows-1252, ISO-8859-1)", "windows-1252" },
	{ "Western (ISO-8859-15)", "ISO-8859-15" },
	{ "Hebrew (Windows-1255)", "windows-1255" },
	{ "Hebrew, logical (ISO-8859-8-I)", "ISO-8859-8-I" },
	{ "Hebrew, visual (ISO-8859-8)", "ISO-8859-8" },
	{ "Central European (Windows-1250)", "windows-1250" },
	{ "Central European (ISO-8859-2)", "ISO-8859-2" },
	{ "Turkish (Windows-1254, ISO-8859-9)", "windows-1254" },
	{ "Baltic (Windows-1257)", "windows-1257" },
	{ "Baltic (ISO-8859-13)", "ISO-8859-13" },
	{ "Baltic (ISO-8859-4)", "ISO-8859-4" },
	{ "Nordic (ISO-8859-10)", "ISO-8859-10" },
	{ "South European (ISO-8859-3)", "ISO-8859-3" }
};
const int br_nencodings = (int) (sizeof br_encodings / sizeof br_encodings[0]);

const char *br_encoding_label(const char *name)
{
	int i;
	for (i = 0; i < br_nencodings; i++)
		if (strcmp(br_encodings[i].name, name) == 0)
			return br_encodings[i].label;
	return name;
}

const char *br_fallback(const char *url)
{
	char host[256], *h;
	size_t n;
	if (!has_scheme(url, "http") && !has_scheme(url, "https"))
		return "windows-1252";
	host_of(url, host, sizeof host);
	h = strrchr(host, '@');		/* user:password@ */
	h = h != NULL ? h + 1 : host;
	if (*h != '[')
		h[strcspn(h, ":")] = 0;	/* the port */
	n = strlen(h);
	if (n > 0 && h[n - 1] == '.')	/* "example.co.il." is example.co.il */
		h[--n] = 0;
	/* Hebrew's only, for now: the other locales' (Cyrillic, Greek,
	   Arabic...) have letters EWEB.FNT hasn't got */
	if (n > 3 && h[n - 3] == '.' && (h[n - 2] | 32) == 'i' && (h[n - 1] | 32) == 'l')
		return "windows-1255";
	return "windows-1252";
}

int br_encoding(browser *b, const char *name)
{
	char security[96], *src = b->src, *url;
	const char *canon = name[0] ? html_encoding(name) : "";
	long len = b->src_len;
	doc *d;
	page *p;
	int r;
	if (canon != NULL && canon[0] && strcmp(br_encoding_label(canon), canon) == 0)
		canon = NULL;		/* not one of br_encodings */
	if (!b->encodable || src == NULL || b->p == NULL || b->cur < 0 || canon == NULL)
		return BR_STOPPED;
	/* (the address on the heap: not on the stack under the whole build,
	   which the ST edition's 48 KB must hold) */
	if ((url = str_ndup(b->url, (long) strlen(b->url))) == NULL)
		return BR_STOPPED;
	conn_stop = 0;
	str_copy(security, b->security, sizeof security);
	b->ms_fetch = 0;
	b->src = NULL;		/* show() lets the page shown go: this is its source */
	/* read again from what came, its stylesheets from the cache of any
	   age (as Back takes them): nothing is fetched anew */
	build_plain = strncmp(b->mime, "text/", 5) == 0 && strcmp(b->mime, "text/html") != 0;
	r = build(b, src, len, canon[0] ? canon : b->charset, url, 2, &d, &p);
	build_plain = 0;
	if (r && conn_stop) {
		pg_free(p);
		doc_free(d);
		r = 0;
	}
	if (!r) {
		conn_stop = 0;
		if (b->p != NULL) {	/* stopped: the page stays as it was */
			b->src = src;
			free(url);
			return BR_STOPPED;
		}
		/* no memory for it, even with the kept pages gone (the page
		   shown went too): said on a page of EWeb's own */
		free(src);
		{
			SBUF s;
			sb_init(&s);
			error_page(&s, url, "EWeb can't show this page", "There isn't enough memory for it on this Atari.",
				   NULL);
			r = own_page(b, url, &s, NAV_RELOAD);
			sb_free(&s);
		}
		free(url);
		return r;
	}
	show(b, d, p, url, security, b->status, 0);
	free(url);
	b->src = src;
	b->src_len = len;
	enc_set(b, b->charset, canon, 1);
	b->scroll_to = b->hist[b->cur].y;
	if (b->d->js != NULL && js_navigation(b->d, NULL, NULL, NULL) != NULL) {
		/* its scripts sent the browser on (location = ...) */
		int lr;
		if (js_after(b, &lr) == BRJ_LOAD)
			return lr;
	}
	return BR_SHOWN;
}

/* ---------------------------------------------------------------- what the front end asks */

/* a long decode keeps the window alive, and Esc stops it: GEM's turn
   ten times a second, and then without waiting for its timer (each
   turn waited a clock tick, 20 ms; a progressive JPEG on the ST polled
   hundreds of times, most of its 13.7 s) */
static int picture_poll(void)
{
	static unsigned long last;
	unsigned long now = pf_ms();
	if (pf_idle && now - last >= 100) {
		last = now;
		pf_idle_quick = 1;
		pf_idle();
		pf_idle_quick = 0;
	}
	return !conn_stop;
}

void br_init(browser *b, int32_t width, int32_t zoom)
{
	pic_poll = picture_poll;
	http_cookie_in = cookie_in;
	http_cookie_out = cookie_out;
	memset(b, 0, sizeof *b);
	b->width = width;
	b->zoom = zoom > 0 ? zoom : 1024;
	b->text = 1024;
	b->cur = -1;
	b->at = -1;
	b->scroll_to = -1;
	b->search = "";
	b->pictures = 1;
	b->pens = -1;
}

void br_free(browser *b)
{
	int i;
	if (b->p != NULL)
		pg_free(b->p);
	if (b->d != NULL)
		doc_free(b->d);
	free(b->src);
	br_dl_free(b);
	br_refresh_cancel(b);
	b->p = NULL;
	b->d = NULL;
	b->src = NULL;
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
	int r;
	typed_url(b, typed, url, sizeof url);
	if (url[0] == 0)
		return BR_STOPPED;
	b->typed = 1;		/* from no page: no Referer, a site's own cookies */
	r = load(b, url, NAV_NEW);
	b->typed = 0;
	return r;
}

void br_link_url(const browser *b, int link, char *out, int size)
{
	char base[4096];
	out[0] = 0;
	if (b->p == NULL || link < 0 || link >= b->p->nlinks)
		return;
	base_of(b, b->d, b->url, base, sizeof base);
	if (!resolve_enc(base, b->p->links[link], out, size, doc_enc(b->d)))
		str_copy(out, b->p->links[link], size);
}

const char *br_link_download(const browser *b, int link)
{
	if (b->p == NULL || link < 0 || link >= b->p->nlinks)
		return NULL;
	return page_link_download(b->p, link);
}

/* are two addresses of the same origin (the HTML Standard's: scheme,
   host and port; http and https only, the rest have none to share) */
static int same_origin(const char *a, const char *c)
{
	URL *u = malloc(2 * sizeof *u);
	int r = u != NULL && url_parse(&u[0], a, NULL) && url_parse(&u[1], c, NULL) && u[0].special &&
		u[1].special && u[0].tls == u[1].tls && u[0].port == u[1].port && u[0].hostlen == u[1].hostlen &&
		memcmp(u[0].href + u[0].host, u[1].href + u[1].host, u[0].hostlen) == 0;
	free(u);
	return r;
}

/* a link to url with a download attribute (dl, NULL none): the HTML
   Standard's "download the hyperlink" (4.6.5) in place of following it,
   for a link to the page's own site: 1, and b->dl says what to save
   (BR_SAVE). One to another site is followed as a link, as browsers do
   (its server may still say attachment), and so is one too long for dl */
static int link_to_save(browser *b, const char *url, const char *dl)
{
	if (dl == NULL || strlen(url) >= sizeof b->dl.url || !same_origin(url, b->url))
		return 0;
	br_dl_free(b);
	str_copy(b->dl.url, url, sizeof b->dl.url);
	str_copy(b->dl.name, dl, sizeof b->dl.name);
	return 1;
}

int br_follow(browser *b, int link)
{
	char url[4096];
	br_link_url(b, link, url, sizeof url);
	if (url[0] == 0)
		return BR_STOPPED;
	if (link_to_save(b, url, br_link_download(b, link)))
		return BR_SAVE;
	if (has_scheme(url, "javascript")) {
		int lr, r = br_js_url(b, url, &lr);
		if (r == BRJ_LOAD)
			return lr;
		if (r == BRJ_DRAWN) {
			b->scroll_to = b->cur >= 0 ? b->hist[b->cur].y : -1;
			return BR_SHOWN;
		}
		return BR_STOPPED;
	}
	return load(b, url, NAV_NEW);
}

int br_submit(browser *b, fctl *from, fctl *submitter, int32_t x, int32_t y)
{
	FSUBMIT s;
	char base[4096], url[4096];
	int r;
	if (b->d == NULL)
		return BR_STOPPED;
	{
		/* the form's submit event: a script may send it itself, or not */
		fctl *c = submitter ? submitter : from;
		if (c != NULL && c->form != NULL && b->d->js != NULL && js_event(b->d, c->form, "submit")) {
			int load;
			if (js_after(b, &load) == BRJ_LOAD)
				return load;
			return BR_STOPPED;
		}
	}
	if (!form_submit(b->d, from, submitter, x, y, &s))
		return BR_STOPPED;
	base_of(b, b->d, b->url, base, sizeof base);
	/* no action: the page's own address, without its fragment */
	if (s.action[0] == 0) {
		str_copy(url, base, sizeof url);
		url[strcspn(url, "#")] = 0;
	} else if (!resolve_enc(base, s.action, url, sizeof url, doc_enc(b->d))) {
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
		dc_job_end();
	}
	form_submit_free(&s);
	return r;
}

#ifndef EW_ST
/* ---------------------------------------------------------------- scripts */

int br_scripts_for(const browser *b, const char *url)
{
	char host[128];
	const char *s;
	size_t hl;
	if (!b->scripts || !js_built || b->script_sites == NULL)
		return 0;
	if (has_scheme(url, "file"))
		str_copy(host, "file", sizeof host);
	else if (has_scheme(url, "http") || has_scheme(url, "https"))
		host_of(url, host, sizeof host);
	else
		return 0;
	host[strcspn(host, ":")] = 0;
	hl = strlen(host);
	for (s = b->script_sites; *s;) {
		const char *w;
		size_t i;
		while (*s == ' ' || *s == ',')
			s++;
		w = s;
		while (*s && *s != ' ' && *s != ',')
			s++;
		if (s - w == 1 && *w == '*')
			return 1;
		if ((size_t) (s - w) != hl)
			continue;
		for (i = 0; i < hl; i++)
			if (((w[i] >= 'A' && w[i] <= 'Z') ? w[i] + 32 : w[i]) !=
			    ((host[i] >= 'A' && host[i] <= 'Z') ? host[i] + 32 : host[i]))
				break;
		if (i == hl)
			return 1;
	}
	return 0;
}

static int jh_fetch(void *ud, const char *site, const char *url, const char *method, const char *body, long blen,
		    const char *ctype, char **data, long *len, int *status, char *mime, int msize)
{
	browser *b = ud;
	FETCH *f = malloc(sizeof *f);
	FSUBMIT post;
	int r;
	if (f == NULL)
		return 0;
	memset(&post, 0, sizeof post);
	if (strcmp(method, "POST") == 0) {
		post.post = 1;
		post.body = (char *) body;
		post.len = blen;
		str_copy(post.ctype, ctype ? ctype : "text/plain", sizeof post.ctype);
	}
	r = fetch(b, url, site, 0, "*/*", 2L << 20, !post.post, post.post ? &post : NULL, f);
	if (r) {
		*data = f->data;
		*len = f->len;
		*status = f->status;
		str_copy(mime, f->mime, msize);
	} else {
		free(f->data);
	}
	free(f);
	return r;
}

static void jh_resolve(void *ud, const char *base, const char *href, char *out, int size, int utf8)
{
	browser *b = ud;
	if (!resolve_enc(base, href, out, size, utf8 ? NULL : doc_enc(js_doc != NULL ? js_doc : b->d)))
		out[0] = 0;
}

static void jh_cookie_get(void *ud, const char *url, char *out, int size)
{
	URL *u = malloc(sizeof *u);
	CKCTX x;
	SBUF v;
	(void) ud;
	out[0] = 0;
	if (u == NULL)
		return;
	x.top = 1;
	x.safe = 1;
	x.site = url;
	x.script = 1;
	sb_init(&v);
	if (url_parse(u, url, NULL) && ck_get(u, &x, &v) && v.s != NULL)
		str_copy(out, v.s, size);
	sb_free(&v);
	free(u);
}

static void jh_cookie_set(void *ud, const char *url, const char *line)
{
	URL *u = malloc(sizeof *u);
	CKCTX x;
	(void) ud;
	if (u == NULL)
		return;
	x.top = 1;
	x.safe = 1;
	x.site = url;
	x.script = 1;
	if (url_parse(u, url, NULL))
		ck_set(u, line, &x);
	free(u);
}

static int jh_ask(void *ud, int kind, const char *text, char *out, int size)
{
	browser *b = ud;
	return b->ask != NULL ? b->ask(b->ud, kind, text, out, size) : 1;
}

static void jh_log(void *ud, const char *line)
{
	browser *b = ud;
	if (b->log != NULL)
		b->log(b->ud, line);
}

static void jh_console(void *ud, const char *line)
{
	browser *b = ud;
	if (b->console != NULL)
		b->console(b->ud, line);
}

static void js_host_of(browser *b, js_host *h)
{
	h->fetch = jh_fetch;
	h->resolve = jh_resolve;
	h->cookie_get = jh_cookie_get;
	h->cookie_set = jh_cookie_set;
	h->ask = jh_ask;
	h->log = jh_log;
	h->console = jh_console;
	h->ud = b;
}

/* the page shown styled and laid out again after a script changed it */
static int restyle(browser *b)
{
	char base[4096];
	doc *d = b->d;
	page *p;
	doc_refresh(d);
	base_of(b, d, b->url, base, sizeof base);
	if (doc_sheets_key(d) != d->sheets_key)
		readd_sheets(b, d, base, 0, 1, 1);	/* it changed the page's sheets */
	else
		ec_unstyle(d->css);
	if (doc_select(d) < 0)
		return 0;		/* (out of memory: as laying it out) */
	p = lay(b, d, base);
	if (p == NULL)
		return 0;
	pg_free(b->p);
	b->p = p;
	js_set_page(d, p);
	shown_imgs(b);
	str_copy(b->title, d->title, sizeof b->title);
	return 1;
}

/* what the page's scripts did: a new address, or a changed page */
static int js_after(browser *b, int *load_r)
{
	static int depth;
	const char *to, *post, *ctype;
	long len;
	if (b->d == NULL || b->d->js == NULL)
		return BRJ_NONE;
	if ((to = js_navigation(b->d, &post, &len, &ctype)) != NULL && depth < 4) {
		char *u = str_ndup(to, (long) strlen(to));
		FSUBMIT s;
		/* a link with a download attribute it clicked (a.click()):
		   saved, as a click on the link is */
		if (u != NULL && link_to_save(b, u, js_navigation_download(b->d))) {
			js_navigation_done(b->d);
			free(u);
			*load_r = BR_SAVE;
			return BRJ_LOAD;
		}
		memset(&s, 0, sizeof s);
		if (post != NULL) {
			s.post = 1;
			s.body = str_ndup(post, len);
			s.len = len;
			str_copy(s.ctype, ctype, sizeof s.ctype);
		}
		js_navigation_done(b->d);
		if (u == NULL) {
			free(s.body);
			return BRJ_NONE;
		}
		depth++;
		if (!strcmp(u, "about:back"))
			*load_r = br_back(b);
		else if (!strcmp(u, "about:forward"))
			*load_r = br_forward(b);
		else if (s.post) {
			*load_r = load_post(b, u, NAV_NEW, &s);
			dc_job_end();
		} else {
			*load_r = load(b, u, NAV_NEW);
		}
		depth--;
		free(s.body);
		free(u);
		return BRJ_LOAD;
	}
	if (js_navigation(b->d, NULL, NULL, NULL) != NULL)
		js_navigation_done(b->d);	/* sent on and on: no further */
	if (js_changed(b->d) && restyle(b))
		return BRJ_DRAWN;
	return BRJ_NONE;
}

long br_js_due(const browser *b)
{
	return b->d != NULL ? js_due(b->d) : -1;
}

int br_js_step(browser *b, int *load_r)
{
	if (b->d == NULL || b->d->js == NULL)
		return BRJ_NONE;
	js_run_timers(b->d);
	return js_after(b, load_r);
}

int br_js_click(browser *b, int32_t x, int32_t y, int *cancelled, int *load_r)
{
	*cancelled = 0;
#ifndef EW_ST
	if (b->d != NULL && b->d->js != NULL && b->p != NULL) {
		dnode *n = page_node_at(b->p, x, y);
		if (n != NULL)
			*cancelled = js_event(b->d, n, "click");
		return js_after(b, load_r);
	}
#else
	(void) x, (void) y;
#endif
	(void) load_r;
	return BRJ_NONE;
}

int br_js_event(browser *b, fctl *c, const char *type, int *cancelled, int *load_r)
{
	*cancelled = 0;
	if (b->d == NULL || b->d->js == NULL || c == NULL)
		return BRJ_NONE;
	*cancelled = js_event(b->d, c->n, type);
	return js_after(b, load_r);
}

int br_js_url(browser *b, const char *url, int *load_r)
{
	char *code;
	long n;
	if (b->d == NULL || b->d->js == NULL || !has_scheme(url, "javascript"))
		return BRJ_NONE;
	/* its code, %-escapes undone */
	url += 11;
	n = (long) strlen(url);
	if ((code = malloc((size_t) n + 1)) == NULL)
		return BRJ_NONE;
	{
		long i, k = 0;
		for (i = 0; i < n; i++) {
			int a, c;
			if (url[i] == '%' && i + 2 < n && (a = hexval(url[i + 1])) >= 0 && (c = hexval(url[i + 2])) >= 0) {
				code[k++] = (char) (a * 16 + c);
				i += 2;
			} else
				code[k++] = url[i];
		}
		code[k] = 0;
	}
	js_run(b->d, code);
	free(code);
	return js_after(b, load_r);
}

#else	/* the ST edition: no scripts */

int br_scripts_for(const browser *b, const char *url)
{
	(void) b, (void) url;
	return 0;
}

long br_js_due(const browser *b)
{
	(void) b;
	return -1;
}

int br_js_step(browser *b, int *load_r)
{
	(void) b, (void) load_r;
	return BRJ_NONE;
}

int br_js_click(browser *b, int32_t x, int32_t y, int *cancelled, int *load_r)
{
	(void) b, (void) x, (void) y, (void) load_r;
	*cancelled = 0;
	return BRJ_NONE;
}

int br_js_event(browser *b, fctl *c, const char *type, int *cancelled, int *load_r)
{
	(void) b, (void) c, (void) type, (void) load_r;
	*cancelled = 0;
	return BRJ_NONE;
}

int br_js_url(browser *b, const char *url, int *load_r)
{
	(void) b, (void) url, (void) load_r;
	return BRJ_NONE;
}

static int js_after(browser *b, int *load_r)
{
	(void) b, (void) load_r;
	return BRJ_NONE;
}
#endif

static int go_to(browser *b, int i)
{
	unsigned long shows;
	int old = b->cur, r;
	if (i < 0 || i >= b->nhist)
		return BR_STOPPED;
	br_refresh_cancel(b);
	b->refresh_run = 0;
	if (b->p != NULL && same_but_fragment(b->hist[i].url, b->url)) {
		b->cur = i;
		enc_set(b, b->charset, b->enc, b->encodable);	/* the same page */
		str_copy(b->url, b->hist[i].url, sizeof b->url);
		b->scroll_to = b->hist[i].y;
		b->relaid = 0;
		b->note[0] = 0;
		return BR_SAME;
	}
	if (b->hist[i].p != NULL && b->hist[i].p->opt.zoom == b->zoom && b->hist[i].p->opt.text == b->text) {
		/* kept: shown at once, laid out again only for another width */
		BR_ENTRY *e = &b->hist[i];
		doc *d = e->d;
		page *p = e->p;
		int status = e->status;
		long src_len = e->src_len;
		char *src = e->src;
		char security[96], cs[sizeof b->charset];
		str_copy(security, e->security ? e->security : "", sizeof security);
		str_copy(cs, e->charset ? e->charset : "", sizeof cs);
		e->d = NULL;
		e->p = NULL;
		e->src = NULL;
		unkeep(e);
		show(b, d, p, e->url, security, status, 1);
		b->src_len = src_len;
		b->src = src;
		str_copy(b->mime, e->mime, sizeof b->mime);
		b->note[0] = 0;		/* (the last page built's) */
		b->cur = i;
		b->at = i;
		enc_set(b, cs, e->enc ? br_encodings[e->enc - 1].name : "", e->encodable);
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
	shows = b->shows;
	r = load(b, b->hist[i].url, NAV_HISTORY);
	if ((r == BR_STOPPED || r == BR_DOWNLOAD) && b->shows == shows)
		b->cur = old;	/* (not when its page was shown, and its script went on) */
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
	int32_t w;
	b->width = width;
	if (b->d == NULL)
		return 1;
	if (resheeting) {
		/* sized as the page's sheets are added again (a sheet coming):
		   laid out at this width after them, as for a picture's size;
		   for one of br_relayout's own, at once (below) */
		b->pic_relayout = 1;
		return 1;
	}
	base_of(b, b->d, b->url, base, sizeof base);
	do {
		/* a media query the page's sheets have (theirs, or an @media in
		   them) goes the other way at this width: its sheets are added
		   again (Media Queries 4, 2), those that hold now fetched (the
		   window's messages go on meanwhile: another resize is done
		   after) */
		ec_media m;
		w = b->width;
		m = media_of(b);
		if (m.width >= b->d->span_lo && m.width <= b->d->span_hi) {
			ec_set_media(b->d->css, &m);	/* <source media>, sizes */
		} else if (net_busy) {
			/* sized while a request is under way (a picture's): not
			   another inside it; its sheets after it, as for a
			   picture's size (br_pictures_step) */
			ec_set_media(b->d->css, &m);
			b->pic_relayout = 1;
		} else {
			int stop = conn_stop;
			conn_stop = 0;
			readd_sheets(b, b->d, base, 0, 1, 0);
			doc_select(b->d);
			conn_stop = stop;
		}
	} while (b->width != w);
	p = lay(b, b->d, base);
	if (p == NULL)
		return 0;
	pg_free(b->p);
	b->p = p;
	js_set_page(b->d, p);
	shown_imgs(b);
	return 1;
}

/* (the page's scripts get a toggle event, after: js_toggled) */
static void toggled(void *ud, dnode *k)
{
	browser *b = ud;
	if (b->d->js != NULL)
		js_toggled(b->d, k);
}

/* the page shown styled again (details[open] > summary's triangle, and
   the page's own [open] rules), as after a script, and laid out again;
   its tree is as it was. Out of memory (a small machine), what a page that
   doesn't fit gives back goes, as for build: what shed gives back, and it
   is tried once more; then the page shown itself (its tree kept), and it
   is tried a last time. 0 when it still doesn't fit: b->p is then NULL */
static int relay(browser *b)
{
	char base[4096];
	doc *d = b->d;
	page *p = NULL;
	int k;
	base_of(b, d, b->url, base, sizeof base);
	for (k = 0; k < 3 && p == NULL; k++) {
		if (k == 1) {
			shed(b);
		} else if (k == 2) {
			js_set_page(d, NULL);
			pg_free(b->p);
			b->p = NULL;
		}
		ec_unstyle(d->css);
		if (doc_select(d) >= 0)
			p = lay(b, d, base);
	}
	if (p == NULL)
		return 0;
	pg_free(b->p);
	b->p = p;
	js_set_page(d, p);
	shown_imgs(b);
	return 1;
}

int br_details(browser *b, dnode *det)
{
	if (b->d == NULL || det == NULL || !dn_details_toggle(b->d, det, toggled, b))
		return 0;
	return relay(b);
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
	static const char *dsp;		/* where JPEGs are transformed, as last logged */
	char line[600];
	int32_t w, h;
	if (b->log == NULL)
		return;
	img_size(im, &w, &h);
	snprintf(line, sizeof line, "picture %.300s %s %ldx%ld fetch %lu decode %lu",
		 strncmp(img_url(im), "data:", 5) == 0 ? "data:" : img_url(im), what, (long) w, (long) h,
		 ms_fetch, ms_decode);
	b->log(b->ud, line);
	if (dsp != pic_dsp_state() && strcmp(pic_dsp_state(), "-") != 0) {
		/* the first JPEG, or the DSP's check failed */
		dsp = pic_dsp_state();
		snprintf(line, sizeof line, "JPEGs transformed on the %s", dsp);
		b->log(b->ud, line);
	}
}

/* a picture still to do at item d: to fetch, or to decode at its size */
static int to_do(const browser *b, const dl_item *d)
{
	ew_img *im = d->img;
	int st;
	if (d->type != DL_IMAGE)
		return 0;
	st = img_state(im);
	/* to fetch, or to fetch again (img_recheck), unless Esc stopped them */
	if ((st == IMG_WAITING || img_stale(im)) && !b->pic_stopped)
		return 1;
	return st == IMG_HERE && !(d->flags & DLF_ALT) && !img_tried(im, DL_PIC_W(d), DL_PIC_H(d));
}

/* the picture is to be fetched now (to_do's first case) */
static int to_fetch(const browser *b, const ew_img *im)
{
	return img_state(im) == IMG_WAITING || (img_stale(im) && !b->pic_stopped);
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
	dc_job_end();		/* the page's pictures have come: into the pack */
	refresh_count(b);	/* (the page loaded, as the standard has it) */
	return 0;
}

void br_pictures_stop(browser *b)
{
	b->pic_stopped = 1;
	dc_job_end();
}

/* pictures still to fetch, each once */
static int waiting(const browser *b)
{
	long i, k;
	int n = 0;
	for (i = 0; i < b->p->nitems; i++) {
		const dl_item *d = &b->p->items[i];
		if (d->type != DL_IMAGE || !to_fetch(b, d->img))
			continue;
		for (k = 0; k < i; k++)
			if (b->p->items[k].type == DL_IMAGE && b->p->items[k].img == d->img)
				break;
		n += k == i;
	}
	return n;
}

/* a picture of a kind EWeb can't show, by its address's ending (WebP,
   AVIF, SVG...): not fetched on its own (the Pi's gateway makes them
   JPEG or PNG) */
static int unshowable(const char *url)
{
	static const char *const ends[] = { ".webp", ".avif", ".svg", ".svgz", ".jxl", ".heic", ".heif", ".tif",
					    ".tiff" };
	size_t n = strcspn(url, "?#"), i;
	for (i = 0; i < sizeof ends / sizeof ends[0]; i++) {
		size_t k = strlen(ends[i]);
		if (n > k && !strncasecmp(url + n - k, ends[i], k))
			return 1;
	}
	return 0;
}

/* the kinds of answer a picture's request leaves unread: those EWeb
   can't show (a page of HTML too, a site's "not found") */
static const char picture_refuse[] = "image/webp image/avif image/svg+xml image/jxl image/heic image/heif "
				     "image/tiff text/";

/* pictures through the Pi's gateway as the screen's pens (webgate's
   aerapens: the Atari's own decoder and dither, on the Pi)? The ST
   edition always (a picture is tens of seconds on a 68000, its pens a
   moment), and a 68030 without the DSP; with the DSP when the answers
   come at 30 KB/s or more (16 colours or fewer) or 45 (256): below
   that, the JPEG is quicker (DESIGN.md, Pens from the Pi) */
static int pens_wanted(const browser *b)
{
	if (!http_gateway[0] || img_pal == NULL || b->pens == 0)
		return 0;
	if (b->pens > 0)
		return 1;
#ifdef EW_ST
	return 1;
#else
	return pic_dsp == NULL || b->rate >= (img_pal->n <= 16 ? 30L : 45L) * 1024;
#endif
}

/* the colours sent to the Pi (webgate keeps them by their CRC), for the
   pens it makes: 1 if it took them */
static int send_colours(browser *b)
{
	FSUBMIT post;
	FETCH *f = malloc(sizeof *f);
	char rgb[768];
	int ok;
	if (f == NULL || img_pal == NULL) {
		free(f);
		return 0;
	}
	memset(&post, 0, sizeof post);
	memcpy(rgb, img_pal->rgb, (size_t) img_pal->n * 3);
	post.post = 1;
	post.body = rgb;
	post.len = (long) img_pal->n * 3;
	str_copy(post.ctype, "application/octet-stream", sizeof post.ctype);
	ok = fetch(b, "http://webgate/palette", NULL, 1, "*/*", 4096, 0, &post, f) && f->status == 204;
	free(f->data);
	free(f);
	return ok;
}

/* the size a picture is shown at, when the layout knows it without the
   picture (its width and height said): 1 and w, h; 0 */
static int shown_size(const browser *b, const ew_img *im, int32_t *w, int32_t *h)
{
	long i;
	for (i = 0; i < b->p->nitems; i++) {
		const dl_item *d = &b->p->items[i];
		if (d->type != DL_IMAGE || d->img != im || (d->flags & DLF_ALT))
			continue;
		if (d->flags & DLF_NOSIZE)
			return 0;
		*w = DL_PIC_W(d);
		*h = DL_PIC_H(d);
		return *w > 0 && *h > 0 && *w <= 4096 && *h <= 4096;
	}
	return 0;
}

/* the file of a picture: from the network, a file or a data: address.
   Through the Pi's gateway, the picture is asked for as the page can
   show it: no wider than the page (webgate makes a bigger one smaller,
   and WebP and the like JPEG or PNG, saying the picture's own size in
   it: img.c), or as the screen's pens (pens_wanted) at the size it is
   shown (rw x rh when it is to be asked for again at that size, else
   the layout's when it knows it, else no wider than the page) */
static int fetch_picture(browser *b, ew_img *im, int32_t rw, int32_t rh)
{
	const char *url = img_url(im);
	char *data;
	long len = 0;
	if (!http_gateway[0] && !has_scheme(url, "file") && unshowable(url)) {
		b->pic_done++;
		return 0;
	}
	if (has_scheme(url, "data")) {
		data = data_url(url, &len);
	} else {
		FETCH *f = malloc(sizeof *f);
		char host[128], msg[200], fit[160], variant[64];
		long max = img_max / 2 < (4L << 20) ? img_max / 2 : (4L << 20);
		int ok, pens;
		if (f == NULL)
			return 0;
		host_of(url, host, sizeof host);
		if (b->pic_n == 0)
			b->pic_n = waiting(b) + b->pic_done;
		snprintf(msg, sizeof msg, "Pictures: %d of %d from %s...", b->pic_done + 1,
			 b->pic_n > b->pic_done ? b->pic_n : b->pic_done + 1, has_scheme(url, "file") ? "the disk" : host);
		say(b, "%s", msg);
		pens = !has_scheme(url, "file") && pens_wanted(b);
		if (pens) {
			int32_t w = rw, h = rh;
			char size[32] = "";
			if ((w > 0 && h > 0) || shown_size(b, im, &w, &h))
				snprintf(size, sizeof size, "size=%ldx%ld; ", (long) w, (long) h);
			snprintf(fit, sizeof fit, "X-Gateway-Picture: fit=%ld; %spens=%d; pal=%08lx; ed=%s\r\n",
				 (long) (b->width > 0 ? b->width : 640), size, img_pal->n, img_pal_crc(),
#ifdef EW_ST
				 "tj"
#else
				 "lj"
#endif
				 );
			if (size[0])
				snprintf(variant, sizeof variant, "pens=%d,%08lx,%s", img_pal->n, img_pal_crc(), size + 5);
			else
				snprintf(variant, sizeof variant, "pens=%d,%08lx,fit%ld", img_pal->n, img_pal_crc(),
					 (long) (b->width > 0 ? b->width : 640));
			variant[strcspn(variant, ";")] = 0;
		} else {
			snprintf(fit, sizeof fit, "X-Gateway-Picture: fit=%ld\r\n", (long) (b->width > 0 ? b->width : 640));
		}
		fetch_more = http_gateway[0] ? fit : NULL;
		fetch_variant = pens ? variant : NULL;
		fetch_refuse = picture_refuse;
		ok = fetch(b, url, b->url, 0, "image/jpeg,image/png,image/gif;q=0.9", max, 0, NULL, f);
		if (ok && pens && f->status == 409 && !conn_stop) {
			/* the Pi hasn't the screen's colours: sent, then asked again */
			free(f->data);
			f->data = NULL;
			fetch_more = fetch_variant = fetch_refuse = NULL;
			if (send_colours(b)) {
				fetch_more = fit;
				fetch_variant = variant;
				fetch_refuse = picture_refuse;
				ok = fetch(b, url, b->url, 0, "image/jpeg,image/png,image/gif;q=0.9", max, 0, NULL, f);
			} else {
				ok = 0;
			}
		}
		fetch_more = fetch_refuse = fetch_variant = NULL;
		if (!ok || (f->status != 0 && (f->status < 200 || f->status >= 300))) {
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
	if (to_fetch(b, im)) {
		int again = img_stale(im);
		int r = fetch_picture(b, im, 0, 0);
		if (r < 0) {
			img_waiting(im);
			return BRP_STOPPED;
		}
		ms_fetch = pf_ms() - t0;
		if (r == 0 && again && img_state(im) == IMG_HERE) {
			/* asked for again, it didn't come (the server couldn't
			   be reached): shown as it was */
			img_kept(im);
			log_img(b, im, "kept (it didn't come again)", ms_fetch, 0);
		} else if (r == 0) {
			img_failed(im);
			snprintf(what, sizeof what, "failed (%s)", img_why(im));
			log_img(b, im, what, ms_fetch, 0);
		} else if (r == 2) {
			log_img(b, im, "unchanged", ms_fetch, 0);	/* asked for again: as it was decoded */
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
		if (img_refetch(im, DL_PIC_W(d), DL_PIC_H(d)) && img_refetched(im) < 4) {
			/* pens from the Pi for another size or screen: asked for
			   at this size, for this screen */
			int r = fetch_picture(b, im, DL_PIC_W(d), DL_PIC_H(d));
			if (r < 0)
				return BRP_STOPPED;
			b->pic_done--;	/* the same picture */
			if (r == 0) {
				img_failed(im);
				snprintf(what, sizeof what, "failed (%s)", img_why(im));
				log_img(b, im, what, 0, 0);
			}
		}
		if (!img_tried(im, DL_PIC_W(d), DL_PIC_H(d)) && img_state(im) == IMG_HERE) {
			unsigned long t = pf_ms();
			int32_t w = DL_PIC_W(d), h = DL_PIC_H(d);
			int ok;
			if (!fetched)
				say(b, "%s", "Pictures: decoding...");
			ok = img_decode(im, w, h);
			if (!ok && conn_stop)
				return BRP_STOPPED;
			if (b->p != p0)
				return BRP_LAYOUT;	/* laid out again meanwhile (the window sized) */
			ms_decode += pf_ms() - t;
			snprintf(what, sizeof what, "%s at %ldx%ld", ok ? (img_is_pens(im) ? "pens from the Pi" :
							       img_from_cache(im) ? "from the cache" : "decoded") :
				 img_why(im), (long) w, (long) h);
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

int br_bookmark(browser *b)
{
	if (b->cur < 0 || b->url[0] == 0 || has_scheme(b->url, "about"))
		return 0;
	return bm_add(b->url, b->title);
}

/* ---------------------------------------------------------------- saving */

static int name_char(int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static int to_upper(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

/* n characters of s that a TOS name may hold, in capitals, to out (max
   of them); in an address (pct), '%' and the two digits after it left
   out */
static int name_part(const char *s, long n, int pct, char *out, int max)
{
	int k = 0;
	long i;
	for (i = 0; i < n && k < max; i++) {
		int c = to_upper((unsigned char) s[i]);
		if (c == '%' && pct && i + 2 < n) {
			i += 2;
			continue;
		}
		if (name_char(c))
			out[k++] = (char) c;
	}
	out[k] = 0;
	return k;
}

/* s to e (an address's last part, pct, or a name given) as a TOS name's
   base (8) and ending (5, cut to 3 later), from its last dot; ".tar.gz"
   is ".TGZ" */
static void split_name(const char *s, const char *e, int pct, char *base, char *ext)
{
	const char *dot;
	for (dot = e; dot > s && dot[-1] != '.'; dot--)
		;
	if (dot > s) {
		const char *b = dot - 1;
		name_part(dot, e - dot, pct, ext, 5);
		if (strcmp(ext, "GZ") == 0 && ends_in(s, b, ".tar")) {
			b -= 4;
			strcpy(ext, "TGZ");
		}
		name_part(s, b - s, pct, base, 8);
	} else {
		name_part(s, e - s, pct, base, 8);
		ext[0] = 0;
	}
}

void br_file_name(const char *url, const char *given, const char *mime, char *out, int size)
{
	static const char *const by_mime[][2] = {
		{ "text/html", "HTM" }, { "application/xhtml+xml", "HTM" }, { "text/plain", "TXT" },
		{ "text/css", "CSS" }, { "image/jpeg", "JPG" }, { "image/png", "PNG" }, { "image/gif", "GIF" },
		{ "application/zip", "ZIP" }, { "application/pdf", "PDF" }, { "application/gzip", "GZ" },
		{ "application/x-gzip", "GZ" }, { "application/x-tar+gzip", "TGZ" }, { "application/x-tar", "TAR" }
	};
	static const char *const pages[] = { "HTML", "HTM", "XHTML", "SHTML", "PHP", "ASP", "ASPX", "JSP" };
	const char *p = strstr(url, "://"), *e, *seg;
	char base[9], ext[6], kext[6];
	int page = strcmp(mime, "text/html") == 0 || strcmp(mime, "application/xhtml+xml") == 0;
	int kept = 0, script = 0;	/* the ending is a given name's; the address's is a script's */
	size_t i;

	base[0] = ext[0] = kext[0] = 0;
	if (given != NULL && *given) {
		/* the name given: its last part only (RFC 6266, 4.3), its ending kept */
		const char *g = given;
		e = given + strlen(given);
		for (seg = given; seg < e; seg++)
			if (*seg == '/' || *seg == '\\')
				g = seg + 1;
		split_name(g, e, 0, base, kext);
		kept = kext[0] != 0;
	}
	if (base[0] == 0) {
		/* the address's path's last part, without its query and fragment */
		p = p != NULL ? p + 3 : url;
		e = p + strcspn(p, "?#");
		seg = e;
		while (seg > p && seg[-1] != '/')
			seg--;
		if (seg == p)		/* the host alone */
			seg = e;
		split_name(seg, e, 1, base, ext);
		if (!kept)
			for (i = 0; i < sizeof pages / sizeof pages[0]; i++)
				if (strcmp(ext, pages[i]) == 0) {
					/* a script's ending: a page's, or not the file's */
					if (mime[0] == 0)
						page = 1;
					script = !page;
				}
	}
	if (kept) {
		strcpy(ext, kext);
		for (i = 0; page && i < sizeof pages / sizeof pages[0]; i++)
			if (strcmp(ext, pages[i]) == 0)
				strcpy(ext, "HTM");
	} else if (page) {
		strcpy(ext, "HTM");
	} else if (ext[0] == 0 || script) {
		ext[0] = 0;
		for (i = 0; i < sizeof by_mime / sizeof by_mime[0]; i++)
			if (strcmp(mime, by_mime[i][0]) == 0)
				strcpy(ext, by_mime[i][1]);
		if (mime[0] == 0 && base[0] == 0) {	/* an address ending in / is a page */
			page = 1;
			strcpy(ext, "HTM");
		}
	}
	if (strcmp(ext, "JPEG") == 0)
		strcpy(ext, "JPG");
	ext[3] = 0;
	snprintf(out, (size_t) size, "%s%s%s", base[0] ? base : page ? "INDEX" : "DOWNLOAD", ext[0] ? "." : "", ext);
}

int br_save_page(browser *b, const char *path)
{
	return b->src != NULL && pf_save(path, b->src, b->src_len) == 0;
}

void br_dl_free(browser *b)
{
	free(b->dl.data);
	memset(&b->dl, 0, sizeof b->dl);
}

int br_dl_save(browser *b, const char *path)
{
	int r = b->dl.data != NULL && pf_save(path, b->dl.data, b->dl.len) == 0;
	br_dl_free(b);
	return r;
}

long br_save_buf = 256L << 10, br_saved;

typedef struct {
	browser *b;
	int h;			/* the file, once the answer has begun (-1 before) */
	char *buf;
	long n, size;
	unsigned long said;
	int failed;		/* the disk wouldn't take it: 1 a write, 2 the file itself */
	int cancelled;		/* save_as said no */
	int asked;		/* save_as was asked, the answer held unread meanwhile */
	const char *given;	/* the link's download attribute, or NULL */
	char *path;
	int pathsize;
} SAVING;

static int save_out(SAVING *s)
{
	if (s->n > 0 && pf_write(s->h, s->buf, s->n) != s->n) {
		s->failed = 1;
		return 0;
	}
	s->n = 0;
	return 1;
}

static int save_sink(void *ud, const char *data, long n)
{
	SAVING *s = ud;
	while (n > 0) {
		long k = s->size - s->n < n ? s->size - s->n : n;
		memcpy(s->buf + s->n, data, (size_t) k);
		s->n += k;
		data += k;
		n -= k;
		br_saved += k;
		if (s->n == s->size && !save_out(s))
			return 0;
	}
	if (s->b->say != NULL && pf_ms() - s->said >= 500) {
		char t[64];
		s->said = pf_ms();
		snprintf(t, sizeof t, "Saving... %ld KB", br_saved >> 10);
		s->b->say(s->b->ud, t);
	}
	return 1;
}

/* br_save_url's HTTPREQ.head: what came named (the HTML Standard's "get
   the suggested filename", 4.6.5: Content-Disposition's when it says
   attachment, else the download attribute's for an answer from the page's
   own site, else Content-Disposition's inline one), where it goes asked
   of the front end, and the file made */
static int save_head(void *ud, HTTPRESP *rs)
{
	SAVING *s = ud;
	browser *b = s->b;
	const char *v = http_header_at(rs, "Content-Disposition");
	char cd[256], name[16];
	int t = -1;
	cd[0] = 0;
	if (v != NULL)
		t = http_disposition(v, cd, sizeof cd);
	str_copy(b->dl.url, rs->url.href, sizeof b->dl.url);
	str_copy(b->dl.mime, rs->mime, sizeof b->dl.mime);
	if (!(t == 1 && cd[0]) && s->given != NULL && *s->given && same_origin(rs->url.href, b->url))
		str_copy(b->dl.name, s->given, sizeof b->dl.name);
	else
		str_copy(b->dl.name, cd, sizeof b->dl.name);
	br_file_name(b->dl.url, b->dl.name, b->dl.mime, name, sizeof name);
	if (s->path[0] == 0) {
		if (b->save_as == NULL || !b->save_as(b->ud, name, s->path, s->pathsize)) {
			s->cancelled = 1;
			return 0;
		}
		s->asked = 1;
	}
	s->h = pf_open(s->path, PF_WRITE);
	if (s->h < 0) {
		s->failed = 2;
		return 0;
	}
	return 1;
}

int br_save_url(browser *b, const char *url, const char *given, char *path, int size, char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP *rs;
	SBUF body;
	SAVING s;
	char host[128];
	int ok, tries = 0, made = 0;

	br_saved = 0;
	err[0] = 0;
	conn_stop = 0;
	/* what is being saved, until the answer says more */
	br_dl_free(b);
	str_copy(b->dl.url, url, sizeof b->dl.url);
	str_copy(b->dl.name, given != NULL ? given : "", sizeof b->dl.name);
	if (!has_scheme(url, "http") && !has_scheme(url, "https")) {
		snprintf(err, (size_t) errlen, "EWeb saves files from http and https addresses.");
		return BRS_FAILED;
	}
	memset(&s, 0, sizeof s);
	s.b = b;
	s.h = -1;
	s.given = given;
	s.path = path;
	s.pathsize = size;
	s.size = br_save_buf > 8192 ? br_save_buf : 8192;
	while ((s.buf = malloc((size_t) s.size)) == NULL && s.size > 8192)
		s.size /= 2;
	rs = malloc(sizeof *rs);
	if (s.buf == NULL || rs == NULL) {
		free(s.buf);
		free(rs);
		snprintf(err, (size_t) errlen, "Out of memory.");
		return BRS_FAILED;
	}
	host_of(url, host, sizeof host);
	say(b, "Opening %s...", host);
	memset(&rq, 0, sizeof rq);
	rq.url = url;
	rq.sink = save_sink;
	rq.head = save_head;
	rq.ud = &s;
	rq.top = 1;		/* a link followed, for its cookies */
	rq.site = has_scheme(b->url, "http") || has_scheme(b->url, "https") ? b->url : NULL;
	rq.referrer = rq.site;
	sb_init(&body);
	for (;;) {
		net_busy++;
		ok = http_request(&rq, rs, &body, err, errlen);
		net_busy--;
		/* lost once the file selector had been up, the answer unread
		   meanwhile: its server may have given up waiting (nginx and
		   Apache end a connection that takes nothing for 60 s), so it is
		   asked for once more, into the file chosen */
		if (ok || !s.asked || s.h < 0 || s.failed || conn_stop || tries++)
			break;
		pf_close(s.h);
		s.h = -1;
		made = 1;
		s.asked = 0;
		s.n = 0;
		br_saved = 0;
		err[0] = 0;
		http_resp_free(rs);
		sb_free(&body);
		sb_init(&body);
		say(b, "Opening %s again...", host);
	}
	if (ok && (rs->status < 200 || rs->status >= 300)) {
		snprintf(err, (size_t) errlen, "The server answered %d %.60s.", rs->status, rs->reason);
		ok = 0;
	}
	if (ok && s.h >= 0)
		ok = save_out(&s);
	if (s.failed == 2)
		snprintf(err, (size_t) errlen, "EWeb can't write %.150s.", path);
	else if (s.failed)
		snprintf(err, (size_t) errlen, "The disk is full, or %.150s can't be written.", path);
	if (s.h >= 0)
		pf_close(s.h);
	sb_free(&body);
	http_resp_free(rs);
	free(rs);
	free(s.buf);
	if (!ok) {
		if (s.h >= 0 || made)
			pf_remove(path);
		if (s.cancelled)
			return BRS_CANCELLED;
		if (conn_stop) {
			conn_stop = 0;
			return BRS_STOPPED;
		}
		if (err[0] == 0)
			snprintf(err, (size_t) errlen, "Nothing came.");
		return BRS_FAILED;
	}
	return BRS_SAVED;
}
