/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bookmark.c - EWeb's bookmarks (bookmark.h): Netscape's bookmark file,
 * read and written.
 */
#include <stdlib.h>
#include <string.h>

#include "bookmark.h"
#include "plat.h"
#include "util.h"

BM *bm_list;
int bm_n;
unsigned bm_changes;
static int bm_cap;
static char bm_path[256];

void bm_free(void)
{
	int i;
	for (i = 0; i < bm_n; i++) {
		free(bm_list[i].url);
		free(bm_list[i].title);
	}
	free(bm_list);
	bm_list = NULL;
	bm_n = bm_cap = 0;
	bm_changes++;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int space(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

/* s (to end) starts with word, in any case */
static int starts(const char *s, const char *end, const char *word)
{
	for (; *word; s++, word++)
		if (s >= end || lower((unsigned char) *s) != *word)
			return 0;
	return 1;
}

static void put_utf8(SBUF *b, unsigned long c)
{
	if (c == 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
		c = 0xFFFD;
	if (c < 0x80) {
		sb_addc(b, (char) c);
	} else if (c < 0x800) {
		sb_addc(b, (char) (0xC0 | (c >> 6)));
		sb_addc(b, (char) (0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		sb_addc(b, (char) (0xE0 | (c >> 12)));
		sb_addc(b, (char) (0x80 | ((c >> 6) & 0x3F)));
		sb_addc(b, (char) (0x80 | (c & 0x3F)));
	} else {
		sb_addc(b, (char) (0xF0 | (c >> 18)));
		sb_addc(b, (char) (0x80 | ((c >> 12) & 0x3F)));
		sb_addc(b, (char) (0x80 | ((c >> 6) & 0x3F)));
		sb_addc(b, (char) (0x80 | (c & 0x3F)));
	}
}

/* s to end as text: the character references these files have decoded,
   tags left out, white space made one space and trimmed */
static char *text_of(const char *s, const char *end)
{
	static const struct {
		const char *name;
		char c;
	} named[] = { { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' } };
	SBUF b;
	int gap = 0;
	size_t i;
	sb_init(&b);
	while (s < end) {
		unsigned char c = (unsigned char) *s;
		if (c == '<') {
			while (s < end && *s != '>')
				s++;
			s++;
			continue;
		}
		if (space(c) || c < 0x20) {	/* control characters (a NUL too) as white space */
			gap = 1;
			s++;
			continue;
		}
		if (c == '&' && s + 1 < end && s[1] == '#') {
			const char *p = s + 2;
			unsigned long v = 0;
			int hex = p < end && (*p == 'x' || *p == 'X'), n = 0;
			if (hex)
				p++;
			for (; p < end && n < 8; p++, n++) {
				int d = *p >= '0' && *p <= '9' ? *p - '0' :
					hex && lower((unsigned char) *p) >= 'a' && lower((unsigned char) *p) <= 'f' ?
					lower((unsigned char) *p) - 'a' + 10 : -1;
				if (d < 0)
					break;
				v = v * (hex ? 16 : 10) + (unsigned long) d;
			}
			if (n > 0) {
				s = p < end && *p == ';' ? p + 1 : p;
				if (v <= 0x20 || v == 0x7F || (v >= 0x80 && v < 0xA0)) {
					gap = 1;	/* a space or a control character: white space */
					continue;
				}
				if (gap && b.len > 0)
					sb_addc(&b, ' ');
				gap = 0;
				put_utf8(&b, v);
				continue;
			}
		}
		if (c == '&' && starts(s + 1, end, "nbsp;")) {
			gap = 1;
			s += 6;
			continue;
		}
		if (gap && b.len > 0)
			sb_addc(&b, ' ');
		gap = 0;
		if (c == '&') {
			for (i = 0; i < sizeof named / sizeof named[0]; i++)
				if (starts(s + 1, end, named[i].name)) {
					sb_addc(&b, named[i].c);
					s += 1 + strlen(named[i].name);
					break;
				}
			if (i < sizeof named / sizeof named[0])
				continue;
		}
		sb_addc(&b, (char) c);
		s++;
	}
	return sb_steal(&b);
}

static int add(const char *url, long ulen, const char *t, const char *tend)
{
	char *u, *title;
	if (bm_n == bm_cap) {
		int cap = bm_cap ? bm_cap * 2 : 32;
		BM *l = realloc(bm_list, (size_t) cap * sizeof *l);
		if (l == NULL)
			return 0;
		bm_list = l;
		bm_cap = cap;
	}
	u = text_of(url, url + ulen);
	title = text_of(t, tend);
	/* no address, a bookmarklet or Firefox's place: query: left out */
	if (u == NULL || title == NULL || u[0] == 0 || starts(u, u + strlen(u), "javascript:") ||
	    starts(u, u + strlen(u), "place:")) {
		free(u);
		free(title);
		return u != NULL && title != NULL;	/* none: left out */
	}
	bm_list[bm_n].url = u;
	bm_list[bm_n].title = title;
	bm_n++;
	bm_changes++;
	return 1;
}

int bm_parse(const char *data, long len)
{
	const char *p = data, *end = data + len;
	bm_free();
	while (p < end) {
		const char *href = NULL, *t;
		long hlen = 0;
		if (*p != '<' || !starts(p + 1, end, "a") || p + 2 >= end || !space((unsigned char) p[2])) {
			p++;
			continue;
		}
		/* the tag's attributes: its href */
		for (p += 2; p < end && *p != '>';) {
			if (space((unsigned char) *p)) {
				p++;
				continue;
			}
			if (starts(p, end, "href") && p + 4 < end && (p[4] == '=' || space((unsigned char) p[4]))) {
				p += 4;
				while (p < end && space((unsigned char) *p))
					p++;
				if (p < end && *p == '=') {
					p++;
					while (p < end && space((unsigned char) *p))
						p++;
					if (p < end && (*p == '"' || *p == '\'')) {
						char q = *p++;
						href = p;
						while (p < end && *p != q)
							p++;
						hlen = p - href;
						if (p < end)
							p++;
					} else {
						href = p;
						while (p < end && !space((unsigned char) *p) && *p != '>')
							p++;
						hlen = p - href;
					}
				}
				continue;
			}
			/* another attribute: its name, then its value if any */
			while (p < end && !space((unsigned char) *p) && *p != '>' && *p != '=')
				p++;
			if (p < end && *p == '=') {
				p++;
				if (p < end && (*p == '"' || *p == '\'')) {
					char q = *p++;
					while (p < end && *p != q)
						p++;
					if (p < end)
						p++;
				} else {
					while (p < end && !space((unsigned char) *p) && *p != '>')
						p++;
				}
			}
		}
		if (p >= end)
			break;
		t = ++p;
		while (p < end && !(*p == '<' && starts(p + 1, end, "/a")))
			p++;
		if (href != NULL && !add(href, hlen, t, p))
			return 0;
	}
	return 1;
}

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

char *bm_write(long *len)
{
	SBUF s;
	int i;
	sb_init(&s);
	sb_adds(&s, "<!DOCTYPE NETSCAPE-Bookmark-file-1>\r\n"
		"<!-- EWeb's bookmarks: read and written by EWEB.PRG, and by other browsers'"
		" bookmark import and export. -->\r\n"
		"<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\r\n"
		"<TITLE>Bookmarks</TITLE>\r\n<H1>Bookmarks</H1>\r\n<DL><p>\r\n");
	for (i = 0; i < bm_n; i++) {
		sb_adds(&s, "    <DT><A HREF=\"");
		esc(&s, bm_list[i].url);
		sb_adds(&s, "\">");
		esc(&s, bm_list[i].title);
		sb_adds(&s, "</A>\r\n");
	}
	sb_adds(&s, "</DL><p>\r\n");
	*len = s.len;
	return sb_steal(&s);
}

/* the list on the disk at once (6.5); 1 written (or nowhere to) */
static int save(void)
{
	long len;
	char *d;
	int ok;
	if (bm_path[0] == 0)
		return 1;
	d = bm_write(&len);
	if (d == NULL)
		return 0;
	ok = pf_save(bm_path, d, len) == 0;
	free(d);
	return ok;
}

void bm_file(const char *path)
{
	long len;
	char *d;
	str_copy(bm_path, path, sizeof bm_path);
	d = pf_load(path, &len);
	bm_free();
	if (d != NULL)
		bm_parse(d, len);
	free(d);
}

int bm_find(const char *url)
{
	int i;
	for (i = 0; i < bm_n; i++)
		if (strcmp(bm_list[i].url, url) == 0)
			return i;
	return -1;
}

int bm_add(const char *url, const char *title)
{
	int n = bm_n;
	if (bm_find(url) >= 0)
		return -1;
	/* through the file's own escaping: what is kept is what is read */
	{
		SBUF u, t;
		int ok;
		sb_init(&u);
		sb_init(&t);
		esc(&u, url);
		esc(&t, title);
		ok = u.s != NULL && add(u.s, u.len, t.s ? t.s : "", t.s ? t.s + t.len : "");
		sb_free(&u);
		sb_free(&t);
		if (!ok || bm_n == n)
			return 0;
	}
	if (!save()) {
		bm_n--;
		free(bm_list[bm_n].url);
		free(bm_list[bm_n].title);
		return 0;
	}
	return 1;
}

int bm_remove(int i)
{
	BM gone;
	if (i < 0 || i >= bm_n)
		return 0;
	gone = bm_list[i];
	memmove(bm_list + i, bm_list + i + 1, (size_t) (bm_n - i - 1) * sizeof *bm_list);
	bm_n--;
	if (!save()) {
		memmove(bm_list + i + 1, bm_list + i, (size_t) (bm_n - i) * sizeof *bm_list);
		bm_list[i] = gone;
		bm_n++;
		return 0;
	}
	free(gone.url);
	free(gone.title);
	bm_changes++;
	return 1;
}
