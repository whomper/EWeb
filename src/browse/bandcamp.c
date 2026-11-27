/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bandcamp.c - a Bandcamp album's or track's page as EWeb's own (decided
 * 27 Nov 2026: "Album to EPlay"): its cover, its title and artist, and
 * its tracks, each with Play, and Play for the whole album; EPlay plays
 * them (the front end hands it an .M3U: bc_m3u). Bandcamp's own page plays
 * with scripts far past EWeb's; its data is in the page itself, the
 * data-tralbum attribute (JSON, its quotes as &quot;): "artist",
 * "current" {"title"}, "trackinfo" [{"title", "duration", "file"
 * {"mp3-128": the stream's address}}]. The stream addresses carry a token
 * that lasts some hours: an album played much later is loaded again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bandcamp.h"

/* the albums last shown, the newest first: a Play on a page kept for Back
   finds its own (each by its page's address) */
#define BC_KEPT 4
static bc_album *kept[BC_KEPT];

/* the attribute's value, &quot; &amp; &lt; &gt; &#39; undone, as a new
   string; NULL: none */
static char *attr_value(const char *data, long len, const char *name)
{
	const char *p, *e, *end = data + len;
	char *out, *o;
	size_t nl = strlen(name);
	for (p = data; p + nl + 2 < end; p++)
		if (memcmp(p, name, nl) == 0 && p[nl] == '=' && p[nl + 1] == '"')
			break;
	if (p + nl + 2 >= end)
		return NULL;
	p += nl + 2;
	for (e = p; e < end && *e != '"'; e++)
		;
	out = malloc((size_t) (e - p) + 1);
	if (out == NULL)
		return NULL;
	for (o = out; p < e;) {
		if (*p == '&') {
			static const struct { const char *s; char c; } ent[] = {
				{ "&quot;", '"' }, { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
				{ "&#39;", '\'' }, { "&#x27;", '\'' }, { "&apos;", '\'' } };
			size_t i;
			for (i = 0; i < sizeof ent / sizeof ent[0]; i++) {
				size_t k = strlen(ent[i].s);
				if ((size_t) (e - p) >= k && memcmp(p, ent[i].s, k) == 0) {
					*o++ = ent[i].c;
					p += k;
					break;
				}
			}
			if (i < sizeof ent / sizeof ent[0])
				continue;
		}
		*o++ = *p++;
	}
	*o = 0;
	return out;
}

/* a JSON string at p (its opening quote) into out, UTF-8, \u escapes
   undone (a surrogate pair too); the place after it, or NULL */
static const char *json_str(const char *p, char *out, int size)
{
	int n = 0;
	if (*p != '"')
		return NULL;
	for (p++; *p && *p != '"'; p++) {
		unsigned long c = (unsigned char) *p;
		int raw = 1;	/* (a byte as it is: UTF-8 passes through) */
		if (c == '\\') {
			p++;
			switch (*p) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = 8; break;
			case 'f': c = 12; break;
			case 'u': {
				int i;
				raw = 0;
				c = 0;
				for (i = 1; i <= 4; i++) {
					char h = p[i];
					if (h == 0)
						return NULL;
					c = c * 16 + (unsigned long) (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
				}
				p += 4;
				if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
					unsigned long d = 0;
					for (i = 3; i <= 6 && p[i]; i++)
						d = d * 16 + (unsigned long) (p[i] <= '9' ? p[i] - '0' : (p[i] | 32) - 'a' + 10);
					if (d >= 0xDC00 && d < 0xE000) {
						c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
						p += 6;
					}
				}
				break;
			}
			case 0: return NULL;
			default: c = (unsigned char) *p; break;
			}
		}
		/* (UTF-8 out, a byte at a time: the 2-, 3- and 4-byte forms) */
		{
			char b[4];
			int k = 0, i;
			if (raw || c < 0x80)
				b[k++] = (char) c;
			else if (c < 0x800)
				b[k++] = (char) (0xC0 | (c >> 6)), b[k++] = (char) (0x80 | (c & 63));
			else if (c < 0x10000)
				b[k++] = (char) (0xE0 | (c >> 12)), b[k++] = (char) (0x80 | ((c >> 6) & 63)),
				b[k++] = (char) (0x80 | (c & 63));
			else
				b[k++] = (char) (0xF0 | (c >> 18)), b[k++] = (char) (0x80 | ((c >> 12) & 63)),
				b[k++] = (char) (0x80 | ((c >> 6) & 63)), b[k++] = (char) (0x80 | (c & 63));
			for (i = 0; i < k; i++)
				if (n < size - 1)
					out[n++] = b[i];
		}
	}
	out[n] = 0;
	return *p == '"' ? p + 1 : NULL;
}

/* the value of "key": a string, at or after p and before end */
static const char *json_key(const char *p, const char *end, const char *key)
{
	char want[48];
	size_t k = (size_t) snprintf(want, sizeof want, "\"%s\":", key);
	for (; p && p + k <= end; p++)
		if (memcmp(p, want, k) == 0)
			return p + k;
	return NULL;
}

/* the end of the JSON value (object or array) at p: its closing brace or
   bracket's next place, strings passed over */
static const char *json_end(const char *p)
{
	int depth = 0;
	for (; *p; p++) {
		if (*p == '"') {
			for (p++; *p && *p != '"'; p++)
				if (*p == '\\' && p[1])
					p++;
			if (!*p)
				return NULL;
		} else if (*p == '{' || *p == '[') {
			depth++;
		} else if (*p == '}' || *p == ']') {
			if (--depth == 0)
				return p + 1;
		}
	}
	return NULL;
}

int bc_page(const char *url)
{
	const char *h = strstr(url, "://"), *s;
	if (h == NULL || strncmp(url, "https://", 8) != 0)
		return 0;
	h += 3;
	s = strchr(h, '/');
	if (s == NULL || s - h < 14 || memcmp(s - 13, ".bandcamp.com", 13) != 0 ||
	    (strncmp(s, "/album/", 7) != 0 && strncmp(s, "/track/", 7) != 0))
		return 0;
	return 1;
}

bc_album *bc_read(const char *url, const char *data, long len)
{
	char *j = attr_value(data, len, "data-tralbum");
	const char *p, *end, *tr, *te;
	bc_album *a;
	if (j == NULL)
		return NULL;
	a = calloc(1, sizeof *a);
	if (a == NULL) {
		free(j);
		return NULL;
	}
	snprintf(a->page, sizeof a->page, "%s", url);
	{
		static int next_id;
		a->id = ++next_id;
	}
	end = j + strlen(j);
	/* (the first "artist" with a name: "current" has its own, null) */
	for (p = j; (p = json_key(p, end, "artist")) != NULL && *p != '"';)
		;
	if (p != NULL)
		json_str(p, a->artist, sizeof a->artist);
	if ((p = json_key(j, end, "current")) != NULL && (p = json_key(p, end, "title")) != NULL)
		json_str(p, a->title, sizeof a->title);
	tr = json_key(j, end, "trackinfo");
	te = tr != NULL && *tr == '[' ? json_end(tr) : NULL;
	for (p = tr ? tr + 1 : NULL; p != NULL && te != NULL && p < te && a->n < BC_TRACKS;) {
		const char *o = strchr(p, '{'), *oe, *v;
		bc_track *t;
		if (o == NULL || o >= te || (oe = json_end(o)) == NULL)
			break;
		t = &a->t[a->n];
		/* (the track's own fields: "file" comes after "title"; a
		   nested object's "title" isn't one of them) */
		if ((v = json_key(o, oe, "title")) != NULL)
			json_str(v, t->title, sizeof t->title);
		if ((v = json_key(o, oe, "duration")) != NULL)
			t->seconds = atol(v);
		if ((v = json_key(o, oe, "mp3-128")) != NULL)
			json_str(v, t->url, sizeof t->url);
		if (t->title[0])
			a->n++;
		p = oe;
	}
	free(j);
	if (a->n == 0) {
		free(a);
		return NULL;
	}
	/* kept, the newest first (the same page's replaced) */
	{
		int i, k = BC_KEPT - 1;
		for (i = 0; i < BC_KEPT; i++)
			if (kept[i] != NULL && strcmp(kept[i]->page, url) == 0)
				k = i;
		free(kept[k]);
		for (i = k; i > 0; i--)
			kept[i] = kept[i - 1];
		kept[0] = a;
	}
	return a;
}

const bc_album *bc_find(int id)
{
	int i;
	for (i = 0; i < BC_KEPT; i++)
		if (kept[i] != NULL && kept[i]->id == id)
			return kept[i];
	return NULL;
}

int bc_m3u(const bc_album *a, int from, SBUF *out)
{
	int i, n = 0;
	sb_adds(out, "#EXTM3U\r\n");
	for (i = from < 0 ? 0 : from; i < a->n; i++) {
		if (!a->t[i].url[0])
			continue;	/* (not streamed: a track to buy only) */
		{
			char n2[40];
			snprintf(n2, sizeof n2, "#EXTINF:%ld,", a->t[i].seconds > 0 ? a->t[i].seconds : -1L);
			sb_adds(out, n2);	/* (sb_printf cuts long lines) */
			sb_adds(out, a->artist);
			sb_adds(out, " - ");
			sb_adds(out, a->t[i].title);
			sb_adds(out, "\r\n");
		}
		sb_adds(out, a->t[i].url);
		sb_adds(out, "\r\n");
		n++;
	}
	return n;
}

static void esc_html(SBUF *s, const char *t)
{
	for (; *t; t++) {
		if (*t == '<')
			sb_adds(s, "&lt;");
		else if (*t == '>')
			sb_adds(s, "&gt;");
		else if (*t == '&')
			sb_adds(s, "&amp;");
		else if (*t == '"')
			sb_adds(s, "&quot;");
		else
			sb_add(s, t, 1);
	}
}

void bc_html(SBUF *s, const bc_album *a, const char *cover, const char *style)
{
	char line[192];
	int i;
	sb_adds(s, "<!DOCTYPE html><html><head><meta charset=utf-8><title>");
	esc_html(s, a->title[0] ? a->title : "A Bandcamp album");
	sb_adds(s, "</title>");
	sb_adds(s, style);
	sb_adds(s, "<style>.trk td{padding:2px 8px 2px 0}.trk .len{color:#888}</style>");
	sb_adds(s, "</head><body><h1>");
	esc_html(s, a->title[0] ? a->title : "A Bandcamp album");
	sb_adds(s, "</h1>");
	if (a->artist[0]) {
		sb_adds(s, "<p>by ");
		esc_html(s, a->artist);
		sb_adds(s, "</p>");
	}
	sb_adds(s, "<p class=addr>");
	esc_html(s, a->page);
	sb_adds(s, "</p>");
	if (cover != NULL && cover[0]) {
		sb_adds(s, "<p><img src=\"");
		esc_html(s, cover);
		sb_adds(s, "\" width=300 height=300 alt=\"\"></p>");
	}
	snprintf(line, sizeof line, "<p><a href=\"about:play?a=%d&amp;from=0\">Play the album</a> in EPlay</p>",
		 a->id);
	sb_adds(s, line);
	sb_adds(s, "<table class=trk>");
	for (i = 0; i < a->n; i++) {
		snprintf(line, sizeof line, "<tr><td>%d.</td><td>", i + 1);
		sb_adds(s, line);
		esc_html(s, a->t[i].title);
		if (a->t[i].seconds > 0) {
			snprintf(line, sizeof line, "</td><td class=len>%ld:%02ld", a->t[i].seconds / 60,
				 a->t[i].seconds % 60);
			sb_adds(s, line);
		} else {
			sb_adds(s, "</td><td class=len>");
		}
		if (a->t[i].url[0]) {
			snprintf(line, sizeof line, "</td><td><a href=\"about:play?a=%d&amp;from=%d\">Play</a>", a->id, i);
			sb_adds(s, line);
		} else {
			sb_adds(s, "</td><td class=len>(not streamed)");
		}
		sb_adds(s, "</td></tr>");
	}
	sb_adds(s, "</table><p class=small>EPlay plays the tracks from the one chosen on, as Bandcamp's own "
		   "player streams them (128 kbit/s MP3). Bandcamp's own page is made by scripts far bigger than "
		   "EWeb runs; this page of EWeb's stands in for it.</p></body></html>");
}
