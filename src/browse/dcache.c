/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dcache.c - EWeb's disk cache (dcache.h): the index in memory, the
 * answers in one pack on the disk.
 *
 * EWEB.IDX, text: "EWDC 1 GEN PAKLEN\n", then a line for each answer,
 * "OFF LEN STORED FRESH USED\tMIME\tCHARSET\tSECURITY\tETAG\tLASTMOD\tKEY\tURL\n"
 * (times in UTC seconds since 1970). EWEB.PAK: "EWDC" and GEN in eight
 * hex digits, CR LF twice (16 bytes), then the answers' bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cookie.h"
#include "dcache.h"
#include "plat.h"
#include "util.h"

#define HEAD 16			/* the pack's first bytes */

typedef struct {
	char *key, *url, *etag, *lastmod;
	char mime[64], charset[40], security[96];
	long long stored, fresh, used;	/* fresh: until when */
	long off, len;			/* off -1: in mem, not yet in the pack */
	char *mem;
	unsigned long hash;
} DCE;

static long long now_utc(void)
{
	return ck_now();
}

long long (*dc_now)(void) = now_utc;

static char dir[256];
static long max_size;
static DCE *ent;
static int nent, cap;
static int loaded;
static unsigned long gen;
static long pak_len, dead, pending;
static long idx_len;		/* the pack's length the index read was of; -1 none */
static int checked;		/* the pack's head checked against the index this run */
static int pak = -1;
static int changed;

static void flush(void);

static void path_of(char *out, int size, const char *name)
{
	snprintf(out, (size_t) size, "%s%c%s", dir, PF_SEP, name);
}

static unsigned long hash_of(const char *s)
{
	unsigned long h = 2166136261UL;
	for (; *s; s++)
		h = ((h ^ (unsigned char) *s) * 16777619UL) & 0xFFFFFFFFUL;
	return h;
}

static void ent_free(DCE *e)
{
	free(e->key);
	free(e->url);
	free(e->etag);
	free(e->lastmod);
	free(e->mem);
}

static void forget_all(void)
{
	int i;
	for (i = 0; i < nent; i++)
		ent_free(&ent[i]);
	free(ent);
	ent = NULL;
	nent = cap = 0;
	pak_len = dead = pending = 0;
}

static void close_pak(void)
{
	if (pak >= 0)
		pf_close(pak);
	pak = -1;
}

static void new_gen(void)
{
	gen = ((unsigned long) dc_now() * 2654435761UL + gen + 1) & 0xFFFFFFFFUL;
}

void dc_clear(void)
{
	char p[300];
	close_pak();
	forget_all();
	if (dir[0]) {
		path_of(p, sizeof p, "EWEB.PAK");
		pf_remove(p);
		path_of(p, sizeof p, "EWEB.IDX");
		pf_remove(p);
	}
	new_gen();
	loaded = 1;
	checked = 1;		/* there is no pack now */
	idx_len = -1;
	changed = 0;
}

void dc_dir(const char *d, long max)
{
	close_pak();
	forget_all();
	str_copy(dir, d, sizeof dir);
	max_size = max;
	loaded = 0;
	changed = 0;
}

static DCE *find(const char *key)
{
	unsigned long h = hash_of(key);
	int i;
	for (i = 0; i < nent; i++)
		if (ent[i].hash == h && strcmp(ent[i].key, key) == 0)
			return &ent[i];
	return NULL;
}

static void drop(DCE *e)
{
	if (e->off >= 0)
		dead += e->len;
	else
		pending -= e->len;
	ent_free(e);
	*e = ent[--nent];
	changed = 1;
}

static DCE *add(const char *key, const char *url, const char *etag, const char *lastmod)
{
	DCE *e;
	if (nent == cap) {
		int c = cap ? cap * 2 : 64;
		DCE *n = realloc(ent, (size_t) c * sizeof *n);
		if (n == NULL)
			return NULL;
		ent = n;
		cap = c;
	}
	e = &ent[nent];
	memset(e, 0, sizeof *e);
	e->key = str_ndup(key, (long) strlen(key));
	e->url = str_ndup(url, (long) strlen(url));
	e->etag = str_ndup(etag, (long) strlen(etag));
	e->lastmod = str_ndup(lastmod, (long) strlen(lastmod));
	if (e->key == NULL || e->url == NULL || e->etag == NULL || e->lastmod == NULL) {
		ent_free(e);
		return NULL;
	}
	e->hash = hash_of(key);
	nent++;
	return e;
}

/* a number at *p (the freestanding Atari build has no sscanf), and *p
   past it and the space after it; 1 when there was one */
static int read_num(char **p, long long *v)
{
	char *s = *p;
	int neg = *s == '-', n = 0;
	*v = 0;
	if (neg)
		s++;
	for (; *s >= '0' && *s <= '9' && n < 19; s++, n++)
		*v = *v * 10 + (*s - '0');
	if (neg)
		*v = -*v;
	while (*s == ' ')
		s++;
	*p = s;
	return n > 0;
}

/* the next tab-separated field of *p, cut off */
static char *field(char **p)
{
	char *s = *p, *t = strchr(s, '\t');
	if (t != NULL) {
		*t = 0;
		*p = t + 1;
	} else {
		*p = s + strlen(s);
	}
	return s;
}

/* the pack open: 1, or 0 (there is none). The first time a run opens
   it, its head and length are checked against the index; a pack that
   isn't the index's is let go, with what the index said was in it. */
static int open_pak(int make)
{
	char p[300], head[HEAD + 1], want[HEAD + 1];
	path_of(p, sizeof p, "EWEB.PAK");
	if (pak >= 0)
		return 1;
	if (!checked || pak_len > 0)	/* else there is none to open (no index, or emptied) */
		pak = pf_open(p, PF_UPDATE);
	if (pak >= 0 && !checked) {
		long size = pf_open_size(pak);
		snprintf(want, sizeof want, "EWDC%08lx\r\n\r\n", gen);
		head[HEAD] = 0;
		checked = 1;
		if (idx_len < HEAD || size < idx_len || pf_read(pak, head, HEAD) != HEAD || strcmp(head, want) != 0) {
			int i;
			close_pak();
			pf_remove(p);
			for (i = nent - 1; i >= 0; i--)
				if (ent[i].off >= 0)
					drop(&ent[i]);
			dead = 0;
		} else {
			pak_len = size;
			dead += size - idx_len;	/* added after the index was written (a reset) */
		}
	}
	if (pak < 0 && make) {
		new_gen();		/* a new pack: what the index says now is all there is */
		pf_mkdir(dir);
		pak = pf_open(p, PF_WRITE);
		snprintf(head, sizeof head, "EWDC%08lx\r\n\r\n", gen);
		if (pak >= 0 && pf_write(pak, head, HEAD) != HEAD) {
			close_pak();
			pf_remove(p);
		}
		checked = 1;
		pak_len = HEAD;
		dead = 0;
	}
	return pak >= 0;
}

/* the index, read the first time the cache is needed (6.5: once) */
static void load(void)
{
	char p[300], *d, *s, *nl;
	long len;
	unsigned long g = 0;
	long plen = 0;
	long long v;
	if (loaded)
		return;
	loaded = 1;
	checked = 0;
	idx_len = -1;
	new_gen();
	if (dir[0] == 0)
		return;
	path_of(p, sizeof p, "EWEB.IDX");
	/* read in one open, without pf_load's look for an unfinished safe
	   save: an index lost that way only empties the cache */
	d = NULL;
	{
		int h = pf_open(p, PF_READ);
		if (h >= 0) {
			len = pf_open_size(h);
			d = len >= 0 ? malloc((size_t) len + 1) : NULL;
			if (d != NULL && pf_read(h, d, len) == len) {
				d[len] = 0;
			} else {
				free(d);
				d = NULL;
			}
			pf_close(h);
		}
	}
	s = d != NULL && strncmp(d, "EWDC 1 ", 7) == 0 ? d + 7 : NULL;
	if (s != NULL) {
		g = strtoul(s, &s, 16);
		while (*s == ' ')
			s++;
	}
	if (s == NULL || !read_num(&s, &v) || (nl = strchr(d, '\n')) == NULL) {
		free(d);	/* none: a pack there can't be read, and is written over */
		checked = 1;
		return;
	}
	gen = g;
	plen = idx_len = (long) v;
	for (s = nl + 1; *s; s = nl + 1) {
		long long o, n, st, fr, us;
		long off;
		char *rest = s, *mime, *cs, *sec, *etag, *lm, *key, *url;
		DCE *e;
		nl = strchr(s, '\n');
		if (nl == NULL)
			break;
		*nl = 0;
		if (!read_num(&rest, &o) || !read_num(&rest, &n) || !read_num(&rest, &st) || !read_num(&rest, &fr) ||
		    !read_num(&rest, &us) || *rest != '\t')
			continue;
		rest++;
		off = (long) o;
		mime = field(&rest);
		cs = field(&rest);
		sec = field(&rest);
		etag = field(&rest);
		lm = field(&rest);
		key = field(&rest);
		url = field(&rest);
		if (off < HEAD || n < 0 || off + n > plen || !key[0] || find(key) != NULL)
			continue;
		e = add(key, url[0] ? url : key, etag, lm);
		if (e == NULL)
			break;
		str_copy(e->mime, mime, sizeof e->mime);
		str_copy(e->charset, cs, sizeof e->charset);
		str_copy(e->security, sec, sizeof e->security);
		e->stored = st;
		e->fresh = fr;
		e->used = us;
		e->off = off;
		e->len = (long) n;
	}
	free(d);
}

/* ---------------------------------------------------------------- HTTP's rules */

/* every header line called name, their values joined with ", " */
static void header(const char *h, const char *name, char *out, int size)
{
	size_t n = strlen(name);
	out[0] = 0;
	while (h != NULL && *h) {
		const char *nl = strchr(h, '\n'), *e = nl ? nl : h + strlen(h);
		size_t i;
		for (i = 0; i < n && h + i < e; i++)
			if ((h[i] | 0x20) != (name[i] | 0x20))
				break;
		if (i == n && h + n < e && h[n] == ':') {
			const char *v = h + n + 1;
			size_t k = strlen(out);
			while (v < e && (*v == ' ' || *v == '\t'))
				v++;
			while (e > v && (e[-1] == '\r' || e[-1] == ' '))
				e--;
			if (k > 0 && (int) k + 2 < size) {
				strcpy(out + k, ", ");
				k += 2;
			}
			if ((int) (k + (size_t) (e - v)) < size) {
				memcpy(out + k, v, (size_t) (e - v));
				out[k + (size_t) (e - v)] = 0;
			}
		}
		h = nl ? nl + 1 : NULL;
	}
}

/* a Cache-Control directive: 1 (and its number in *v when there is one) */
static int directive(const char *cc, const char *name, long *v)
{
	size_t n = strlen(name);
	const char *p = cc;
	while (*p) {
		size_t i;
		while (*p == ' ' || *p == ',')
			p++;
		for (i = 0; i < n; i++)
			if ((p[i] | 0x20) != name[i])
				break;
		if (i == n && (p[n] == 0 || p[n] == ',' || p[n] == ' ' || p[n] == '=')) {
			if (v != NULL) {
				const char *q = p + n;
				*v = -1;
				if (*q == '=') {
					q++;
					if (*q == '"')
						q++;
					if (*q >= '0' && *q <= '9')
						*v = strtol(q, NULL, 10);
				}
			}
			return 1;
		}
		while (*p && *p != ',')
			p++;
	}
	return 0;
}

/* how long an answer stays fresh, seconds, from its headers (RFC 9111,
   4.2): max-age, else Expires less Date, else a tenth of the time since
   Last-Modified, a day at most; less its Age. -1: not to be kept. */
static long long lifetime(const char *h)
{
	char cc[256], v[128];
	long n;
	long long date, t, life = 0;
	header(h, "Cache-Control", cc, sizeof cc);
	if (directive(cc, "no-store", NULL))
		return -1;
	header(h, "Vary", v, sizeof v);
	if (strchr(v, '*') != NULL)
		return -1;
	header(h, "Date", v, sizeof v);
	if (!ck_date(v, &date))
		date = dc_now();
	if (directive(cc, "no-cache", NULL)) {
		life = 0;
	} else if (directive(cc, "max-age", &n) && n >= 0) {
		life = n;
	} else if (header(h, "Expires", v, sizeof v), v[0]) {
		life = ck_date(v, &t) && t > date ? t - date : 0;
	} else if (header(h, "Last-Modified", v, sizeof v), v[0] && ck_date(v, &t) && t < date) {
		life = (date - t) / 10;
		if (life > 86400)
			life = 86400;
	}
	header(h, "Age", v, sizeof v);
	if (v[0] >= '0' && v[0] <= '9')
		life -= strtol(v, NULL, 10);
	if (life > 31536000)		/* a year at most */
		life = 31536000;
	return life > 0 ? life : 0;
}

void dc_put(const char *key, const char *url, const char *headers, const char *mime, const char *charset,
	    const char *security, const char *data, long len)
{
	char etag[96], lm[48], sc[8];
	long long life;
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	e = find(key);
	if (e != NULL)
		drop(e);
	life = lifetime(headers);
	header(headers, "ETag", etag, sizeof etag);
	header(headers, "Last-Modified", lm, sizeof lm);
	header(headers, "Set-Cookie", sc, sizeof sc);
	/* not kept: what HTTP says not to keep, what would never be fresh
	   and can't be asked about, one person's answer (it sets a cookie),
	   and what would take an eighth of the cache */
	if (life < 0 || (life == 0 && !etag[0] && !lm[0]) || sc[0] || len > max_size / 8 ||
	    strchr(etag, '\t') || strchr(lm, '\t') || strchr(key, '\t') || strchr(url, '\t') || strchr(security, '\t') ||
	    strchr(key, '\n') || strchr(url, '\n'))
		return;
	e = add(key, url, etag, lm);
	if (e == NULL)
		return;
	e->mem = malloc((size_t) len + 1);
	if (e->mem == NULL) {
		ent_free(e);
		nent--;
		return;
	}
	memcpy(e->mem, data, (size_t) len);
	str_copy(e->mime, mime, sizeof e->mime);
	str_copy(e->charset, charset, sizeof e->charset);
	str_copy(e->security, security, sizeof e->security);
	e->stored = e->used = dc_now();
	e->fresh = e->stored + life;
	e->off = -1;
	e->len = len;
	pending += len;
	changed = 1;
	if (pending >= max_size / 16)	/* the buffer is full: into the pack now */
		flush();
}

void dc_fresh(const char *key, const char *headers)
{
	char v[96];
	long long life;
	DCE *e;
	if (dir[0] == 0)
		return;
	load();
	e = find(key);
	if (e == NULL)
		return;
	life = lifetime(headers);
	if (life < 0) {
		drop(e);
		return;
	}
	e->stored = e->used = dc_now();
	e->fresh = e->stored + life;
	header(headers, "ETag", v, sizeof v);
	if (v[0] && !strchr(v, '\t')) {
		char *t = str_ndup(v, (long) strlen(v));
		if (t != NULL) {
			free(e->etag);
			e->etag = t;
		}
	}
	changed = 1;
}

int dc_get(const char *key, DC_GOT *g)
{
	DCE *e;
	char *d;
	if (dir[0] == 0)
		return 0;
	load();
	e = find(key);
	if (e == NULL)
		return 0;
	d = malloc((size_t) e->len + 1);
	if (d == NULL)
		return 0;
	if (e->off < 0) {
		memcpy(d, e->mem, (size_t) e->len);
	} else if (!open_pak(0) || (e = find(key)) == NULL) {	/* a pack that wasn't the index's */
		free(d);
		return 0;
	} else if (pf_seek(pak, e->off) != e->off || pf_read(pak, d, e->len) != e->len) {
		free(d);
		drop(e);	/* the pack lost it */
		return 0;
	}
	d[e->len] = 0;
	memset(g, 0, sizeof *g);
	g->data = d;
	g->len = e->len;
	str_copy(g->url, e->url, sizeof g->url);
	str_copy(g->mime, e->mime, sizeof g->mime);
	str_copy(g->charset, e->charset, sizeof g->charset);
	str_copy(g->security, e->security, sizeof g->security);
	e->used = dc_now();
	g->fresh = e->used < e->fresh;
	if (e->etag[0])
		snprintf(g->validate, sizeof g->validate, "If-None-Match: %.150s\r\n", e->etag);
	else if (e->lastmod[0])
		snprintf(g->validate, sizeof g->validate, "If-Modified-Since: %.60s\r\n", e->lastmod);
	changed = 1;
	return 1;
}

/* ---------------------------------------------------------------- the pack */

/* what has gathered, into the pack in one write at its end */
static void flush(void)
{
	if (pending > 0 && !open_pak(1)) {
		int i;
		for (i = nent - 1; i >= 0; i--)	/* no pack to be had: let them go */
			if (ent[i].off < 0)
				drop(&ent[i]);
		pending = 0;
	} else if (pending > 0) {
		/* what the job brought, in one write at the pack's end */
		char *buf = malloc((size_t) pending);
		long at = 0;
		int i;
		if (buf != NULL) {
			for (i = 0; i < nent; i++)
				if (ent[i].off < 0) {
					memcpy(buf + at, ent[i].mem, (size_t) ent[i].len);
					at += ent[i].len;
				}
		}
		if (buf != NULL && pf_seek(pak, pak_len) == pak_len && pf_write(pak, buf, at) == at) {
			for (i = 0; i < nent; i++)
				if (ent[i].off < 0) {
					ent[i].off = pak_len;
					pak_len += ent[i].len;
					free(ent[i].mem);
					ent[i].mem = NULL;
				}
		} else {
			for (i = nent - 1; i >= 0; i--)	/* the disk wouldn't take it */
				if (ent[i].off < 0)
					drop(&ent[i]);
		}
		free(buf);
		pending = 0;
	}
}

void dc_job_end(void)
{
	close_pak();
}

static int by_use(const void *a, const void *b)
{
	const DCE *x = a, *y = b;
	return x->used > y->used ? -1 : x->used < y->used;
}

static int by_place(const void *a, const void *b)
{
	const DCE *x = a, *y = b;
	return x->off < y->off ? -1 : x->off > y->off;
}

/* the pack written again with the answers used last, three quarters of
   its size at most: read and written in big pieces (6.5) */
static void compact(void)
{
	char from[300], to[300], head[HEAD + 1];
	long keep = 0, at, piece = 256L << 10, used = 0;
	int i, n = 0, out, in, ok = 1;
	char *buf;
	qsort(ent, (size_t) nent, sizeof *ent, by_use);
	while (n < nent && keep + ent[n].len <= max_size / 4 * 3)
		keep += ent[n++].len;
	for (i = n; i < nent; i++)
		ent_free(&ent[i]);
	nent = n;
	qsort(ent, (size_t) nent, sizeof *ent, by_place);
	while ((buf = malloc((size_t) piece)) == NULL && piece > 8192)
		piece /= 2;
	path_of(from, sizeof from, "EWEB.PAK");
	path_of(to, sizeof to, "EWEB.NEW");
	in = pf_open(from, PF_READ);
	out = buf != NULL && in >= 0 ? pf_open(to, PF_WRITE) : -1;
	if (out < 0) {
		free(buf);
		if (in >= 0)
			pf_close(in);
		dc_clear();
		return;
	}
	new_gen();
	snprintf(head, sizeof head, "EWDC%08lx\r\n\r\n", gen);
	memcpy(buf, head, HEAD);
	used = HEAD;
	at = HEAD;
	for (i = 0; i < nent && ok; i++) {
		long left = ent[i].len, src = ent[i].off;
		ent[i].off = at;
		at += left;
		while (left > 0 && ok) {
			long k = piece - used < left ? piece - used : left;
			ok = pf_seek(in, src) == src && pf_read(in, buf + used, k) == k;
			used += k;
			src += k;
			left -= k;
			if (used == piece) {
				ok = ok && pf_write(out, buf, used) == used;
				used = 0;
			}
		}
	}
	ok = ok && (used == 0 || pf_write(out, buf, used) == used);
	pf_close(in);
	pf_close(out);
	free(buf);
	if (!ok || pf_remove(from) != 0 || pf_rename(to, from) != 0) {
		pf_remove(to);
		dc_clear();
		return;
	}
	pak_len = at;
	dead = 0;
	changed = 1;
}

void dc_close(void)
{
	SBUF s;
	char p[300];
	int i;
	flush();
	close_pak();
	if (dir[0] == 0 || !loaded) {
		forget_all();
		return;
	}
	if (nent > 0 && (pak_len > max_size || dead > pak_len / 2))
		compact();
	if (changed && nent > 0) {
		sb_init(&s);
		sb_printf(&s, "EWDC 1 %08lx %ld\n", gen, pak_len);
		for (i = 0; i < nent; i++) {
			DCE *e = &ent[i];
			/* times as long: the Atari's printf has no %lld (2038 is
			   far enough for a cache) */
			sb_printf(&s, "%ld %ld %ld %ld %ld\t", e->off, e->len, (long) e->stored, (long) e->fresh,
				  (long) e->used);
			sb_adds(&s, e->mime);
			sb_addc(&s, '\t');
			sb_adds(&s, e->charset);
			sb_addc(&s, '\t');
			sb_adds(&s, e->security);
			sb_addc(&s, '\t');
			sb_adds(&s, e->etag);
			sb_addc(&s, '\t');
			sb_adds(&s, e->lastmod);
			sb_addc(&s, '\t');
			sb_adds(&s, e->key);
			sb_addc(&s, '\t');
			sb_adds(&s, e->url);
			sb_addc(&s, '\n');
		}
		path_of(p, sizeof p, "EWEB.IDX");
		if (s.s == NULL || pf_save(p, s.s, s.len) != 0)
			dc_clear();
		sb_free(&s);
	} else if (nent == 0 && changed) {
		dc_clear();
	}
	forget_all();
	loaded = 0;
	changed = 0;
}

long dc_count(long *bytes)
{
	long b = 0;
	int i;
	load();
	for (i = 0; i < nent; i++)
		b += ent[i].len;
	if (bytes != NULL)
		*bytes = b;
	return nent;
}
