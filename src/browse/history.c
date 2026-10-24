/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* history.c - the pages EWeb has shown (history.h): an array, oldest
   first, a table of their addresses' hashes for the links' question and
   one of their origins' for a quick no, made again when the array
   changes (a few thousand words, no strings) */
#include <stdlib.h>
#include <string.h>

#include "history.h"
#include "plat.h"
#include "util.h"

#define HG_MAX 1000
#ifdef EW_ST
#define HG_BYTES 98304L			/* and their addresses and titles at most (a 4 MB ST) */
#else
#define HG_BYTES 262144L
#endif
#define HG_TAB 2048			/* the table: a power of two over twice HG_MAX */
#define HG_OTAB 2048			/* the origins' */

unsigned long hg_changes;

static HG list[HG_MAX];
static int n;
static short tab[HG_TAB];		/* 1 + index into list, 0 empty */
static unsigned long otab[HG_OTAB];	/* the origins' hashes, 0 empty */
static char path[256];
static int loaded, dirty;
static long bytes;			/* their addresses' and titles' */

/* djb2: a shift and two adds a byte (a 68030's multiply takes 44 cycles) */
static unsigned long hash_of(const char *url, long len)
{
	unsigned long h = 5381;
	long i;
	for (i = 0; i < len; i++)
		h = ((h << 5) + h + (unsigned char) url[i]) & 0xFFFFFFFFUL;
	return h;
}

/* the origin of an http or https address: the length of "https://host"
   (with its port, if any), else 0 */
static long origin_len(const char *url)
{
	long k = !strncmp(url, "http://", 7) ? 7 : !strncmp(url, "https://", 8) ? 8 : 0;
	return k ? k + (long) strcspn(url + k, "/?#") : 0;
}

static int keepable(const char *url, long len);

/* an origin's hash as otab keeps it (never 0) */
static unsigned long ohash_of(const char *o, long len)
{
	return hash_of(o, len) | 1;
}

static void retab(void)
{
	int i;
	memset(tab, 0, sizeof tab);
	memset(otab, 0, sizeof otab);
	for (i = 0; i < n; i++) {
		unsigned k = (unsigned) list[i].hash & (HG_TAB - 1);
		while (tab[k])
			k = (k + 1) & (HG_TAB - 1);
		tab[k] = (short) (i + 1);
		if (list[i].ohash) {
			k = (unsigned) list[i].ohash & (HG_OTAB - 1);
			while (otab[k] && otab[k] != list[i].ohash)
				k = (k + 1) & (HG_OTAB - 1);
			otab[k] = list[i].ohash;
		}
	}
}

/* the one for url (len bytes of it), or -1 */
static int find(const char *url, long len, unsigned long h)
{
	unsigned k = (unsigned) h & (HG_TAB - 1);
	while (tab[k]) {
		const HG *e = &list[tab[k] - 1];
		if (e->hash == h && (long) strlen(e->url) == len && memcmp(e->url, url, (size_t) len) == 0)
			return tab[k] - 1;
		k = (k + 1) & (HG_TAB - 1);
	}
	return -1;
}

/* the table made again with the newest of each address only (a file
   written by hand may have one twice) */
static void dedupe(void)
{
	int i, j = 0;
	memset(tab, 0, sizeof tab);
	for (i = n - 1; i >= 0; i--) {
		unsigned k = (unsigned) list[i].hash & (HG_TAB - 1);
		if (find(list[i].url, (long) strlen(list[i].url), list[i].hash) >= 0) {
			bytes -= (long) strlen(list[i].url) + (long) strlen(list[i].title) + 2;
			free(list[i].url);
			free(list[i].title);
			list[i].url = NULL;
			continue;
		}
		while (tab[k])
			k = (k + 1) & (HG_TAB - 1);
		tab[k] = (short) (i + 1);
	}
	for (i = 0; i < n; i++)
		if (list[i].url != NULL)
			list[j++] = list[i];
	n = j;
	retab();
}

static void drop(int i)
{
	bytes -= (long) strlen(list[i].url) + (long) strlen(list[i].title) + 2;
	free(list[i].url);
	free(list[i].title);
	memmove(&list[i], &list[i + 1], (size_t) (n - i - 1) * sizeof list[0]);
	n--;
}

/* at the end; the oldest go when there is no room. 0 if out of memory */
static int append(const char *url, long ulen, const char *title, long tlen, long long when)
{
	char *u, *t;
	while (n > 0 && (n == HG_MAX || bytes + ulen + tlen + 2 > HG_BYTES))
		drop(0);
	u = malloc((size_t) ulen + 1);
	t = malloc((size_t) tlen + 1);
	if (u == NULL || t == NULL) {
		free(u);
		free(t);
		return 0;
	}
	memcpy(u, url, (size_t) ulen);
	u[ulen] = 0;
	memcpy(t, title, (size_t) tlen);
	t[tlen] = 0;
	bytes += ulen + tlen + 2;
	list[n].url = u;
	list[n].title = t;
	list[n].when = when;
	list[n].hash = hash_of(u, ulen);
	list[n].ohash = origin_len(u) ? ohash_of(u, origin_len(u)) : 0;
	n++;
	return 1;
}

void hg_free(void)
{
	while (n > 0)
		drop(n - 1);
	memset(tab, 0, sizeof tab);
	memset(otab, 0, sizeof otab);
}

static void load(void)
{
	long len;
	char *d;
	if (loaded)
		return;
	loaded = 1;
	if (path[0] == 0 || (d = pf_load(path, &len)) == NULL)
		return;
	hg_parse(d, len);
	free(d);
}

void hg_file(const char *p)
{
	str_copy(path, p != NULL ? p : "", sizeof path);
	hg_free();
	loaded = dirty = 0;
}

int hg_parse(const char *data, long len)
{
	const char *s = data, *end = data + len;
	int ok = 1;
	hg_free();
	while (s < end) {
		const char *e = memchr(s, '\n', (size_t) (end - s)), *t1, *t2;
		long long when = 0;
		if (e == NULL)
			e = end;
		t1 = memchr(s, '\t', (size_t) (e - s));
		t2 = t1 != NULL ? memchr(t1 + 1, '\t', (size_t) (e - t1 - 1)) : NULL;
		if (*s != '#' && t1 != NULL && t2 != NULL && keepable(t1 + 1, (long) (t2 - t1 - 1))) {
			const char *p, *te = e, *z;
			for (p = s; p < t1 && p < s + 18 && *p >= '0' && *p <= '9'; p++)
				when = when * 10 + (*p - '0');
			z = memchr(t2 + 1, 0, (size_t) (te - t2 - 1));
			if (z != NULL)
				te = z;		/* (a title is a C string) */
			while (te > t2 + 1 && te[-1] == '\r')
				te--;
			if (!append(t1 + 1, (long) (t2 - t1 - 1), t2 + 1, (long) (te - t2 - 1), when)) {
				ok = 0;
				break;
			}
		}
		s = e + 1;
	}
	dedupe();
	return ok;
}

char *hg_write(long *len)
{
	SBUF s;
	int i;
	sb_init(&s);
	sb_adds(&s, "# EWeb's history, oldest first: when (UTC seconds since 1970), address, title\n");
	for (i = 0; i < n; i++) {
		char w[24];
		long long v = list[i].when;
		int k = (int) sizeof w - 1;
		w[k] = 0;
		do {
			w[--k] = (char) ('0' + v % 10);
			v /= 10;
		} while (v > 0 && k > 0);
		sb_adds(&s, w + k);
		sb_addc(&s, '\t');
		sb_adds(&s, list[i].url);
		sb_addc(&s, '\t');
		sb_adds(&s, list[i].title);
		sb_addc(&s, '\n');
	}
	*len = s.len;
	return sb_steal(&s);
}

/* an address kept (len long: in a file being read, not NUL-ended): one
   with a control character (a tab or a line's end would make lines of
   the file) or a user's name and password in it isn't */
static int keepable(const char *url, long len)
{
	long i, host = len >= 8 && !strncmp(url, "https://", 8) ? 8 : len >= 7 && !strncmp(url, "http://", 7) ? 7 : len;
	if (len == 0 || len > 2000)
		return 0;
	for (i = 0; i < len; i++) {
		if ((unsigned char) url[i] < 0x20 || url[i] == 0x7f)
			return 0;
		if (i >= host && (url[i] == '/' || url[i] == '?' || url[i] == '#'))
			host = len;	/* (past the authority) */
		else if (i >= host && url[i] == '@')
			return 0;
	}
	return 1;
}

void hg_add(const char *url, const char *title, long long when)
{
	long ulen = (long) strlen(url), tlen;
	char t[256];
	int i, ok;
	load();
	if (!keepable(url, ulen))
		return;
	/* a title on one line, without the file's tabs, and not cut inside
	   a UTF-8 character */
	str_copy(t, title != NULL ? title : "", sizeof t);
	for (tlen = 0; t[tlen]; tlen++)
		if (t[tlen] == '\t' || t[tlen] == '\n' || t[tlen] == '\r')
			t[tlen] = ' ';
	if (tlen == (long) sizeof t - 1) {
		long k = tlen;
		unsigned char c;
		while (k > 0 && (t[k - 1] & 0xC0) == 0x80)
			k--;
		c = k > 0 ? (unsigned char) t[k - 1] : 0;
		if (c >= 0xC0 && tlen - (k - 1) < (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2))
			tlen = k - 1;	/* its last character cut short: it goes */
	}
	i = find(url, ulen, hash_of(url, ulen));
	if (i >= 0)
		drop(i);	/* (it goes to the end, as the newest) */
	ok = append(url, ulen, t, tlen, when);
	if (ok || i >= 0)
		dirty = 1;
	if (i < 0 ? ok : !ok)
		hg_changes++;	/* one more visited, or (out of memory) one fewer */
	retab();
}

int hg_visited(const char *url)
{
	/* djb2 as hash_of, and the length, in one pass (a page's links come
	   here one by one) */
	unsigned long h = 5381;
	const char *p;
	load();
	if (n == 0)
		return 0;
	for (p = url; *p; p++)
		h = ((h << 5) + h + (unsigned char) *p) & 0xFFFFFFFFUL;
	return find(url, (long) (p - url), h) >= 0;
}

int hg_origin(const char *origin, long len)
{
	unsigned long h = ohash_of(origin, len);
	unsigned k = (unsigned) h & (HG_OTAB - 1);
	load();
	while (otab[k] && otab[k] != h)
		k = (k + 1) & (HG_OTAB - 1);
	return otab[k] != 0;
}

int hg_count(void)
{
	load();
	return n;
}

const HG *hg_get(int i)
{
	return i >= 0 && i < n ? &list[i] : NULL;
}

void hg_clear(void)
{
	load();
	if (n == 0)
		return;
	hg_free();
	dirty = 1;
	hg_changes++;
}

int hg_save(void)
{
	long len;
	char *d;
	int ok;
	if (!dirty || path[0] == 0)
		return 1;
	d = hg_write(&len);
	if (d == NULL)
		return 0;
	ok = pf_save(path, d, len) == 0;
	free(d);
	if (ok)
		dirty = 0;
	return ok;
}
