/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cookie.c - cookies (cookie.h), after RFC 6265bis: the Set-Cookie
 * parsing algorithm (5.6, with the cookie-date algorithm of 5.1.1), the
 * storage model (5.7: domains, paths, Secure, the __Secure- and __Host-
 * prefixes, SameSite) and the Cookie header (5.8). Without the Public
 * Suffix List a site is the last two labels of its host, three under the
 * second-level suffixes listed below (co.il, co.uk...).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cookie.h"
#include "plat.h"
#include "secret.h"
#include "tls.h"

enum { SS_DEFAULT, SS_LAX, SS_STRICT, SS_NONE };

typedef struct ck ck;
struct ck {
	ck *next;
	char *name, *value, *domain, *path;
	long long expiry;		/* 0: only while EWeb runs */
	long long created, used;
	unsigned long seq;		/* the order they were made in: created is in seconds */
	unsigned char host_only, secure, http_only, same;
};

static ck *all;
static unsigned long seq;
static long count;
static int changed;			/* lasting cookies changed since the file was read */
static int loaded;			/* the file was read */
static char file[256];
static char keyfile[256];		/* EWEB.KEY: "" none */
static int key_state;			/* 0 not read yet, 1 there, -1 none to be had */

/* the key, read (or made) the first time it is needed: 1 if there is one */
static int have_key(void)
{
	if (keyfile[0] == 0)
		return 0;
	if (key_state == 0)
		key_state = secret_init(keyfile) ? 1 : -1;
	return key_state > 0;
}

/* ---------------------------------------------------------------- time */

static long long days_from_civil(long y, int m, int d)
{
	long era;
	unsigned long yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned long) (y - era * 400);
	doy = (153UL * (unsigned long) (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned long) d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (long long) era * 146097 + (long long) doe - 719468;
}

static long long clock_now(void)
{
	PFTIME t;
	pf_now(&t);
	return days_from_civil(t.year, t.mon, t.day) * 86400LL + t.hour * 3600L + t.min * 60L + t.sec -
	       tls_tz_minutes * 60LL;
}

long long (*ck_now)(void) = clock_now;

/* the cookie-date algorithm: 1 and *t, or 0 */
int ck_date(const char *s, long long *t)
{
	static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
	int hh = -1, mi = 0, ss = 0, day = -1, mon = -1, year = -1;
	const char *p = s;
	while (*p) {
		const char *b;
		size_t n;
		unsigned char c;
		/* delimiters: %x09, %x20-2F, %x3B-40, %x5B-60, %x7B-7E */
		while ((c = (unsigned char) *p) != 0 &&
		       (c == 0x09 || (c >= 0x20 && c <= 0x2f) || (c >= 0x3b && c <= 0x40) || (c >= 0x5b && c <= 0x60) ||
			(c >= 0x7b && c <= 0x7e)))
			p++;
		b = p;
		while ((c = (unsigned char) *p) != 0 &&
		       !(c == 0x09 || (c >= 0x20 && c <= 0x2f) || (c >= 0x3b && c <= 0x40) || (c >= 0x5b && c <= 0x60) ||
			 (c >= 0x7b && c <= 0x7e)))
			p++;
		n = (size_t) (p - b);
		if (n == 0)
			continue;
		if (hh < 0) {
			/* 1*2DIGIT ":" 1*2DIGIT ":" 1*2DIGIT ( non-digit *OCTET ) */
			int v[3], k = 0;
			const char *q = b;
			for (k = 0; k < 3; k++) {
				int dgt = 0;
				v[k] = 0;
				while (q < p && *q >= '0' && *q <= '9' && dgt < 3) {
					v[k] = v[k] * 10 + (*q++ - '0');
					dgt++;
				}
				if (dgt < 1 || dgt > 2)
					break;
				if (k < 2) {
					if (q >= p || *q != ':')
						break;
					q++;
				}
			}
			if (k == 3 && (q >= p || !(*q >= '0' && *q <= '9'))) {
				hh = v[0];
				mi = v[1];
				ss = v[2];
				continue;
			}
		}
		if (day < 0 && b[0] >= '0' && b[0] <= '9' && (n < 2 || !(b[1] >= '0' && b[1] <= '9') || n < 3 ||
							     !(b[2] >= '0' && b[2] <= '9'))) {
			day = b[0] - '0';
			if (n >= 2 && b[1] >= '0' && b[1] <= '9')
				day = day * 10 + (b[1] - '0');
			continue;
		}
		if (mon < 0 && n >= 3) {
			char m3[4];
			const char *f;
			int k;
			for (k = 0; k < 3; k++)
				m3[k] = (char) (b[k] >= 'A' && b[k] <= 'Z' ? b[k] + 32 : b[k]);
			m3[3] = 0;
			if ((f = strstr(months, m3)) != NULL && (f - months) % 3 == 0) {
				mon = (int) (f - months) / 3 + 1;
				continue;
			}
		}
		if (year < 0 && n >= 2 && b[0] >= '0' && b[0] <= '9' && b[1] >= '0' && b[1] <= '9') {
			int k = 0, v = 0;
			while (k < (int) n && k < 5 && b[k] >= '0' && b[k] <= '9')
				v = v * 10 + (b[k++] - '0');
			if (k <= 4) {
				year = v;
				continue;
			}
		}
	}
	if (year >= 70 && year <= 99)
		year += 1900;
	else if (year >= 0 && year <= 69)
		year += 2000;
	if (hh < 0 || day < 1 || day > 31 || mon < 0 || year < 1601 || hh > 23 || mi > 59 || ss > 59)
		return 0;
	/* a day the month has */
	{
		static const int len[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
		if (day > len[mon - 1] || (mon == 2 && day == 29 && !leap))
			return 0;
	}
	*t = days_from_civil(year, mon, day) * 86400LL + hh * 3600L + mi * 60L + ss;
	return 1;
}

/* ---------------------------------------------------------------- sites and domains */

/* second-level public suffixes (the Public Suffix List's commonest, and
   a few hosts that give each user a site of their own) */
static const char *const suffixes[] = {
	"co.il", "org.il", "ac.il", "gov.il", "net.il", "muni.il", "k12.il", "idf.il",
	"co.uk", "org.uk", "ac.uk", "gov.uk", "ltd.uk", "plc.uk", "me.uk", "net.uk",
	"com.au", "net.au", "org.au", "edu.au", "gov.au", "co.nz", "org.nz", "net.nz",
	"co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp", "co.za", "co.in", "co.kr",
	"com.br", "com.ar", "com.mx", "com.tr", "com.cn", "com.tw", "com.hk", "com.sg",
	"github.io", "gitlab.io", "blogspot.com", "herokuapp.com", "netlify.app", "vercel.app",
	"pages.dev", "workers.dev", "neocities.org", "sourceforge.net", NULL
};

static int is_ip(const char *h)
{
	const char *p;
	if (strchr(h, ':') != NULL)
		return 1;
	for (p = h; *p; p++)
		if (!((*p >= '0' && *p <= '9') || *p == '.'))
			return 0;
	return *h != 0;
}

/* a public suffix: one label, or one of the list */
static int is_public(const char *d)
{
	int i;
	if (strchr(d, '.') == NULL)
		return 1;
	for (i = 0; suffixes[i] != NULL; i++)
		if (strcmp(d, suffixes[i]) == 0)
			return 1;
	return 0;
}

/* the host's site: its last two labels, three under a listed suffix */
static const char *site_of(const char *h)
{
	const char *last1 = NULL, *last2 = NULL, *q, *two;
	int i;
	if (is_ip(h))
		return h;
	for (q = h; *q; q++)
		if (*q == '.') {
			last2 = last1;
			last1 = q;
		}
	if (last1 == NULL)
		return h;
	two = last2 ? last2 + 1 : h;
	for (i = 0; suffixes[i] != NULL; i++)
		if (strcmp(two, suffixes[i]) == 0) {
			/* the label before them too */
			if (two == h)
				return h;
			q = two - 1;
			while (q > h && q[-1] != '.')
				q--;
			return q;
		}
	return two;
}

int ck_same_site(const char *a, const char *b)
{
	return strcmp(site_of(a), site_of(b)) == 0;
}

/* host is domain, or ends in "." domain (and isn't an address) */
static int domain_match(const char *host, const char *domain)
{
	size_t h = strlen(host), d = strlen(domain);
	if (strcmp(host, domain) == 0)
		return 1;
	return h > d && host[h - d - 1] == '.' && strcmp(host + h - d, domain) == 0 && !is_ip(host);
}

static int path_match(const char *req, const char *path)
{
	size_t n = strlen(path);
	if (strcmp(req, path) == 0)
		return 1;
	return strncmp(req, path, n) == 0 && (path[n - 1] == '/' || req[n] == '/');
}

/* the request's path, and the default path (5.1.4) */
static void req_path(const URL *u, char *out, int size)
{
	int n = u->query - u->path;
	if (n <= 0 || u->href[u->path] != '/') {
		str_copy(out, "/", size);
		return;
	}
	if (n >= size)
		n = size - 1;
	memcpy(out, u->href + u->path, (size_t) n);
	out[n] = 0;
}

static void default_path(const URL *u, char *out, int size)
{
	char *s;
	req_path(u, out, size);
	s = strrchr(out, '/');
	if (s == out)
		out[1] = 0;
	else
		*s = 0;
}

/* ---------------------------------------------------------------- the store */

static void ck_free(ck *c)
{
	free(c->name);
	free(c->value);
	free(c->domain);
	free(c->path);
	free(c);
}

/* c out of the list (pp: where it is) */
static void drop(ck **pp)
{
	ck *c = *pp;
	*pp = c->next;
	if (c->expiry)
		changed = 1;
	ck_free(c);
	count--;
}

static char *dup(const char *s, size_t n)
{
	char *d = malloc(n + 1);
	if (d != NULL) {
		memcpy(d, s, n);
		d[n] = 0;
	}
	return d;
}

static void ensure(void)
{
	char *d;
	long len;
	if (loaded)
		return;
	loaded = 1;
	if (file[0] == 0 || (d = pf_load(file, &len)) == NULL)
		return;
	ck_load(d, len);
	free(d);
	changed = 0;
}

/* the expired go; then, past the limits, the least recently used */
static void trim(const char *domain)
{
	long long now = ck_now();
	ck **pp, **old;
	long n;
	for (pp = &all; *pp != NULL;)
		if ((*pp)->expiry && (*pp)->expiry <= now)
			drop(pp);
		else
			pp = &(*pp)->next;
	for (;;) {
		n = 0;
		old = NULL;
		for (pp = &all; *pp != NULL; pp = &(*pp)->next)
			if (domain == NULL || strcmp((*pp)->domain, domain) == 0) {
				n++;
				if (old == NULL || (*pp)->used < (*old)->used)
					old = pp;
			}
		if (old == NULL || n <= (domain ? CK_PER_DOMAIN : CK_MAX))
			return;
		drop(old);
	}
}

static int is_ctl(unsigned char c)
{
	return (c < 0x20 && c != 0x09) || c == 0x7f;
}

static void trim_ws(const char **b, const char **e)
{
	while (*b < *e && (**b == ' ' || **b == '\t'))
		(*b)++;
	while (*e > *b && ((*e)[-1] == ' ' || (*e)[-1] == '\t'))
		(*e)--;
}

static int ieq(const char *a, size_t n, const char *b)
{
	size_t i;
	if (strlen(b) != n)
		return 0;
	for (i = 0; i < n; i++)
		if ((a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i]) != b[i])
			return 0;
	return 1;
}

static int iprefix(const char *s, const char *p)
{
	for (; *p; s++, p++)
		if ((*s >= 'A' && *s <= 'Z' ? *s + 32 : *s) != *p)
			return 0;
	return 1;
}

void ck_set(const URL *u, const char *line, const CKCTX *x)
{
	const char *p, *nb, *ne, *vb, *ve, *semi;
	char host[256], dom[256], path[1024], site[256];
	long long now = ck_now(), expires = 0, maxage = 0;
	int has_expires = 0, has_maxage = 0, has_domain = 0, has_path = 0, secure = 0, http_only = 0;
	int same = SS_DEFAULT;
	ck *c, **pp;

	if (!u->special || u->hostlen == 0)
		return;
	url_host(u, host, sizeof host);
	if (x != NULL && !x->top && x->site != NULL) {
		URL s;
		if (!url_parse(&s, x->site, NULL) || s.hostlen == 0)
			return;
		url_host(&s, site, sizeof site);
		if (!ck_same_site(host, site))
			return;		/* another site's picture or stylesheet: no cookies */
	}
	for (p = line; *p; p++)
		if (is_ctl((unsigned char) *p))
			return;
	semi = strchr(line, ';');
	nb = line;
	ne = semi ? semi : line + strlen(line);
	vb = memchr(nb, '=', (size_t) (ne - nb));
	if (vb == NULL) {
		/* no '=': a value with no name */
		vb = nb;
		ve = ne;
		ne = nb;
	} else {
		ve = ne;
		ne = vb++;
	}
	trim_ws(&nb, &ne);
	trim_ws(&vb, &ve);
	if ((ne == nb && ve == vb) || (ne - nb) + (ve - vb) > 4096 || memchr(nb, '\t', (size_t) (ne - nb)) ||
	    memchr(vb, '\t', (size_t) (ve - vb)))
		return;
	default_path(u, path, sizeof path);
	dom[0] = 0;
	/* the attributes: the last of each kind counts */
	for (p = semi; p != NULL && *p == ';';) {
		const char *ab = p + 1, *ae, *eq, *avb, *ave;
		semi = strchr(ab, ';');
		ae = semi ? semi : ab + strlen(ab);
		p = semi;
		eq = memchr(ab, '=', (size_t) (ae - ab));
		avb = eq ? eq + 1 : ae;
		ave = ae;
		if (eq)
			ae = eq;
		trim_ws(&ab, &ae);
		trim_ws(&avb, &ave);
		if (ave - avb > 1024)
			continue;
		if (ieq(ab, (size_t) (ae - ab), "expires")) {
			char t[1025];
			long long e;
			memcpy(t, avb, (size_t) (ave - avb));
			t[ave - avb] = 0;
			if (ck_date(t, &e)) {
				expires = e;
				has_expires = 1;
			}
		} else if (ieq(ab, (size_t) (ae - ab), "max-age")) {
			const char *q = avb;
			long long v = 0;
			int neg = 0;
			if (q < ave && *q == '-') {
				neg = 1;
				q++;
			}
			if (q == ave || !(*q >= '0' && *q <= '9'))
				continue;
			while (q < ave && *q >= '0' && *q <= '9') {
				if (v < 1000000000000LL)
					v = v * 10 + (*q - '0');
				q++;
			}
			if (q != ave)
				continue;
			maxage = neg ? -1 : v;
			has_maxage = 1;
		} else if (ieq(ab, (size_t) (ae - ab), "domain")) {
			size_t n;
			if (ave == avb)
				continue;
			if (*avb == '.')
				avb++;
			n = (size_t) (ave - avb);
			if (n >= sizeof dom)
				continue;
			for (n = 0; avb + n < ave; n++)
				dom[n] = (char) (avb[n] >= 'A' && avb[n] <= 'Z' ? avb[n] + 32 : avb[n]);
			dom[n] = 0;
			has_domain = dom[0] != 0;
		} else if (ieq(ab, (size_t) (ae - ab), "path")) {
			has_path = 1;
			/* a tab would break the file's lines: as no path */
			if (ave == avb || *avb != '/' || memchr(avb, '\t', (size_t) (ave - avb)) != NULL) {
				default_path(u, path, sizeof path);
			} else {
				size_t n = (size_t) (ave - avb);
				if (n >= sizeof path)
					n = sizeof path - 1;
				memcpy(path, avb, n);
				path[n] = 0;
			}
		} else if (ieq(ab, (size_t) (ae - ab), "secure")) {
			secure = 1;
		} else if (ieq(ab, (size_t) (ae - ab), "httponly")) {
			http_only = 1;
		} else if (ieq(ab, (size_t) (ae - ab), "samesite")) {
			same = ieq(avb, (size_t) (ave - avb), "none") ? SS_NONE
			       : ieq(avb, (size_t) (ave - avb), "strict") ? SS_STRICT
			       : ieq(avb, (size_t) (ave - avb), "lax") ? SS_LAX : SS_DEFAULT;
		}
	}
	/* the storage model */
	if (has_domain) {
		if (is_ip(host) ? strcmp(dom, host) != 0 : !domain_match(host, dom))
			return;
		if (is_public(dom)) {
			if (strcmp(dom, host) != 0)
				return;
			has_domain = 0;		/* a host that is a suffix: its own cookie */
		}
	}
	if (secure && !u->tls)
		return;
	if (same == SS_NONE && !secure)
		return;
	if (iprefix(nb, "__secure-") && !secure)
		return;
	if (iprefix(nb, "__host-") && (!secure || has_domain || !has_path || strcmp(path, "/") != 0))
		return;
	ensure();
	/* a plain page may not put a cookie over a secure one */
	if (!secure && !u->tls)
		for (c = all; c != NULL; c = c->next)
			if (c->secure && strlen(c->name) == (size_t) (ne - nb) && memcmp(c->name, nb, (size_t) (ne - nb)) == 0 &&
			    (domain_match(c->domain, has_domain ? dom : host) ||
			     domain_match(has_domain ? dom : host, c->domain)) && path_match(path, c->path))
				return;
	c = calloc(1, sizeof *c);
	if (c == NULL)
		return;
	c->name = dup(nb, (size_t) (ne - nb));
	c->value = dup(vb, (size_t) (ve - vb));
	c->domain = dup(has_domain ? dom : host, strlen(has_domain ? dom : host));
	c->path = dup(path, strlen(path));
	if (c->name == NULL || c->value == NULL || c->domain == NULL || c->path == NULL) {
		ck_free(c);
		return;
	}
	c->host_only = (unsigned char) !has_domain;
	c->secure = (unsigned char) secure;
	c->http_only = (unsigned char) http_only;
	if (x != NULL && x->script && http_only) {
		ck_free(c);		/* a script can't set an HttpOnly cookie */
		return;
	}
	c->same = (unsigned char) same;
	c->created = c->used = now;
	c->seq = ++seq;
	if (has_maxage)
		c->expiry = maxage <= 0 ? 1 : now + maxage;
	else if (has_expires)
		c->expiry = expires > 0 ? expires : 1;
	/* the one it replaces keeps its age */
	for (pp = &all; *pp != NULL; pp = &(*pp)->next) {
		ck *o = *pp;
		if (strcmp(o->name, c->name) == 0 && strcmp(o->domain, c->domain) == 0 && o->host_only == c->host_only &&
		    strcmp(o->path, c->path) == 0) {
			if (x != NULL && x->script && o->http_only) {
				ck_free(c);	/* nor replace one */
				return;
			}
			c->created = o->created;
			c->seq = o->seq;
			if (!o->expiry && !c->expiry)
				drop(pp);
			else if (o->expiry && c->expiry > now && o->expiry == c->expiry && strcmp(o->value, c->value) == 0 &&
				 o->secure == c->secure && o->same == c->same && o->http_only == c->http_only) {
				o->used = now;		/* the same again: nothing to write */
				ck_free(c);
				return;
			} else
				drop(pp);
			break;
		}
	}
	if (c->expiry && c->expiry <= now) {
		ck_free(c);		/* expired: it only deleted the old one */
		return;
	}
	c->next = all;
	all = c;
	count++;
	if (c->expiry)
		changed = 1;
	trim(c->domain);
	if (count > CK_MAX)
		trim(NULL);
}

int ck_get(const URL *u, const CKCTX *x, SBUF *out)
{
	char host[256], path[2048], site[256];
	long long now = ck_now();
	int same = 1, n = 0, i, j;
	ck *c, **pp, **list;

	if (!u->special || u->hostlen == 0)
		return 0;
	url_host(u, host, sizeof host);
	if (x != NULL && x->site != NULL) {
		URL s;
		if (url_parse(&s, x->site, NULL) && s.hostlen != 0) {
			url_host(&s, site, sizeof site);
			same = ck_same_site(host, site);
		}
	}
	if (x != NULL && !x->top && !same)
		return 0;		/* another site's picture or stylesheet */
	ensure();
	req_path(u, path, sizeof path);
	for (pp = &all; *pp != NULL;)
		if ((*pp)->expiry && (*pp)->expiry <= now)
			drop(pp);
		else
			pp = &(*pp)->next;
	list = malloc((size_t) (count + 1) * sizeof *list);
	if (list == NULL)
		return 0;
	for (c = all; c != NULL; c = c->next) {
		if (!(c->host_only ? strcmp(host, c->domain) == 0 : domain_match(host, c->domain)) ||
		    !path_match(path, c->path) || (c->secure && !u->tls) || (x != NULL && x->script && c->http_only))
			continue;
		if (c->same == SS_STRICT && !same)
			continue;
		if ((c->same == SS_LAX || c->same == SS_DEFAULT) && !same && !(x != NULL && x->top && x->safe))
			continue;
		list[n++] = c;
	}
	/* longer paths first, then the older */
	for (i = 1; i < n; i++)
		for (j = i; j > 0; j--) {
			ck *a = list[j - 1], *b = list[j];
			size_t la = strlen(a->path), lb = strlen(b->path);
			if (la > lb || (la == lb && (a->created < b->created ||
						     (a->created == b->created && a->seq <= b->seq))))
				break;
			list[j - 1] = b;
			list[j] = a;
		}
	for (i = 0; i < n; i++) {
		if (i > 0)
			sb_adds(out, "; ");
		if (list[i]->name[0]) {
			sb_adds(out, list[i]->name);
			sb_addc(out, '=');
		}
		sb_adds(out, list[i]->value);
		list[i]->used = now;
	}
	free(list);
	return n > 0;
}

/* ---------------------------------------------------------------- the file */

/* 64-bit numbers as text and back (the Atari's printf has no %lld) */
static char *ll_str(long long v, char *buf)
{
	char t[24];
	int n = 0, neg = v < 0;
	unsigned long long u = neg ? (unsigned long long) -v : (unsigned long long) v;
	do
		t[n++] = (char) ('0' + (int) (u % 10));
	while ((u /= 10) != 0);
	if (neg)
		*buf++ = '-';
	while (n > 0)
		*buf++ = t[--n];
	*buf = 0;
	return buf;
}

static long long ll_parse(const char *s)
{
	long long v = 0;
	int neg = *s == '-';
	if (neg)
		s++;
	while (*s >= '0' && *s <= '9' && v < 100000000000000LL)
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

void ck_file(const char *path, const char *key)
{
	str_copy(file, path ? path : "", sizeof file);
	str_copy(keyfile, key ? key : "", sizeof keyfile);
	key_state = 0;
	loaded = 0;
}

/* an encrypted value's room (taken when needed: 8 KB kept for the rest
   otherwise) */
#define ENC_SIZE (sizeof SECRET_TAG + 2 * (6 + SECRET_MAX) + 1)

char *ck_text(long *len)
{
	SBUF s;
	long long now = ck_now();
	ck *c;
	char *enc = NULL;
	sb_init(&s);
	sb_adds(&s, "# EWeb's cookies, kept by EWeb: domain, host only, path, secure, expires (UTC seconds\n"
		    "# since 1970), name, value, HttpOnly, SameSite, created\n");
	for (c = all; c != NULL; c = c->next)
		if (c->expiry > now) {
			char e[24], k[24];
			/* without a key, a value that starts as an encrypted one
			   does would be read back as one (and lost): not kept */
			if (!(keyfile[0] && c->value[0] && have_key()) &&
			    strncmp(c->value, SECRET_TAG, strlen(SECRET_TAG)) == 0)
				continue;
			ll_str(c->expiry, e);
			ll_str(c->created, k);
			/* piece by piece: sb_printf cuts long lines */
			sb_adds(&s, c->domain);
			sb_printf(&s, "\t%d\t", c->host_only);
			sb_adds(&s, c->path);
			sb_printf(&s, "\t%d\t%s\t", c->secure, e);
			sb_adds(&s, c->name);
			sb_addc(&s, '\t');
			if (keyfile[0] && c->value[0] && have_key()) {
				/* encrypted, as the apps keep passwords (without a key
				   secret.c leaves it as it is and says so) */
				if (enc == NULL && (enc = malloc(ENC_SIZE)) == NULL) {
					sb_free(&s);	/* never written as it is */
					return NULL;
				}
				secret_encode(c->value, enc, ENC_SIZE);
				sb_adds(&s, enc);
			} else {
				sb_adds(&s, c->value);
			}
			sb_printf(&s, "\t%d\t%d\t%s\n", c->http_only, c->same, k);
		}
	free(enc);
	*len = s.len;
	return sb_steal(&s);
}

void ck_load(const char *s, long len)
{
	const char *p = s, *e = s + len;
	long long now = ck_now();
	ck **tail = &all;
	char *dec = NULL;	/* a value decrypted: taken when one is */
	while (*tail != NULL)
		tail = &(*tail)->next;
	/* in the file's order, after those already here */
	while (p < e) {
		const char *nl = memchr(p, '\n', (size_t) (e - p)), *f[10], *q;
		const char *end = nl ? nl : e;
		int k = 0;
		ck *c;
		if (*p != '#') {
			f[k++] = p;
			for (q = p; q < end && k < 10; q++)
				if (*q == '\t')
					f[k++] = q + 1;
		}
		if (k == 10) {
			size_t n[10];
			int i;
			for (i = 0; i < 10; i++)
				n[i] = (size_t) ((i < 9 ? f[i + 1] - 1 : end) - f[i]);
			c = calloc(1, sizeof *c);
			if (c == NULL) {
				free(dec);
				return;
			}
			c->domain = dup(f[0], n[0]);
			c->host_only = (unsigned char) (f[1][0] == '1');
			c->path = dup(f[2], n[2]);
			c->secure = (unsigned char) (f[3][0] == '1');
			c->expiry = ll_parse(f[4]);
			c->name = dup(f[5], n[5]);
			c->value = dup(f[6], n[6]);
			if (c->value != NULL && strncmp(c->value, SECRET_TAG, strlen(SECRET_TAG)) == 0) {
				/* encrypted: read with the key, else the cookie goes */
				char *v = c->value;
				if (dec == NULL)
					dec = malloc(SECRET_MAX + 1);
				c->value = dec != NULL && have_key() && secret_decode(v, dec, SECRET_MAX + 1) ?
					   dup(dec, strlen(dec)) : NULL;
				free(v);
			}
			c->http_only = (unsigned char) (f[7][0] == '1');
			c->same = (unsigned char) (f[8][0] - '0');
			c->created = c->used = ll_parse(f[9]);
			c->seq = ++seq;
			if (c->domain == NULL || c->path == NULL || c->name == NULL || c->value == NULL || c->expiry <= now ||
			    c->same > SS_NONE || c->path[0] != '/') {
				ck_free(c);
			} else {
				*tail = c;
				tail = &c->next;
				count++;
			}
		}
		p = nl ? nl + 1 : e;
	}
	free(dec);
	trim(NULL);
	loaded = 1;
}

int ck_save(void)
{
	char *t;
	long len;
	int ok;
	ck *c;
	if (!changed || file[0] == 0)
		return 1;
	for (c = all; c != NULL && !c->expiry; c = c->next)
		;
	if (c == NULL) {
		/* none that last: no file */
		changed = 0;
		return pf_remove(file) == 0 || !pf_exists(file);
	}
	t = ck_text(&len);
	if (t == NULL)
		return 0;
	ok = pf_save(file, t, len);
	free(t);
	if (ok)
		changed = 0;
	return ok;
}

void ck_clear(void)
{
	while (all != NULL) {
		ck *c = all;
		all = c->next;
		ck_free(c);
	}
	count = 0;
	changed = 0;
	loaded = 1;
	if (file[0])
		pf_remove(file);
}

long ck_count(void)
{
	ensure();
	return count;
}
