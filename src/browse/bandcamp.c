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
#include "url.h"

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

/* a, kept, the newest first (the same page's replaced) */
static void keep(bc_album *a)
{
	int i, k = BC_KEPT - 1;
	for (i = 0; i < BC_KEPT; i++)
		if (kept[i] != NULL && strcmp(kept[i]->page, a->page) == 0)
			k = i;
	free(kept[k]);
	for (i = k; i > 0; i--)
		kept[i] = kept[i - 1];
	kept[0] = a;
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
	a->album = 1;
	keep(a);
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
			if (a->artist[0]) {
				sb_adds(out, a->artist);
				sb_adds(out, " - ");
			}
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

/* ---------------------------------------------------------------- a page's audio */

/* the place after "key" and its colon (spaces allowed, as JSON-LD has
   them) at or after p, before end; NULL none */
static const char *ld_key(const char *p, const char *end, const char *key)
{
	size_t k = strlen(key);
	for (; p + k + 2 < end; p++)
		if (*p == '"' && memcmp(p + 1, key, k) == 0 && p[k + 1] == '"') {
			const char *q = p + k + 2;
			while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
				q++;
			if (q < end && *q == ':') {
				for (q++; q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'); q++)
					;
				return q;
			}
		}
	return NULL;
}

/* an ISO 8601 duration (PT3M20S, PT1H2M) in seconds; 0 none */
static long iso_secs(const char *s)
{
	long n = 0, v = 0;
	if (*s != 'P')
		return 0;
	for (s++; *s && *s != '"'; s++)
		if (*s >= '0' && *s <= '9')
			v = v * 10 + (*s - '0');
		else if (*s == 'H')
			n += v * 3600, v = 0;
		else if (*s == 'M')
			n += v * 60, v = 0;
		else if (*s == 'S')
			n += v, v = 0;
		else if (*s == '.')
			while (s[1] >= '0' && s[1] <= '9')
				s++;
	return n;
}

/* how EPlay takes an address: 1 it plays it (MP3, WAV, AIFF, an .m3u),
   2 audio it doesn't (Ogg, Opus, AAC, M4A, FLAC, HLS: the Gateway's), 0 not
   audio by its name (its type, when given, says: type) */
static int au_kind(const char *u, const char *type)
{
	static const char *const yes[] = { ".mp3", ".wav", ".aif", ".aiff", ".m3u" };
	static const char *const other[] = { ".ogg", ".oga", ".opus", ".aac", ".m4a", ".flac", ".m3u8", ".pls" };
	const char *e = u + strcspn(u, "?#");
	size_t i;
	for (i = 0; i < sizeof yes / sizeof yes[0]; i++) {
		size_t k = strlen(yes[i]);
		if ((size_t) (e - u) > k && strncasecmp(e - k, yes[i], k) == 0)
			return 1;
	}
	for (i = 0; i < sizeof other / sizeof other[0]; i++) {
		size_t k = strlen(other[i]);
		if ((size_t) (e - u) > k && strncasecmp(e - k, other[i], k) == 0)
			return 2;
	}
	if (type != NULL && strncasecmp(type, "audio/", 6) == 0)
		return strncasecmp(type + 6, "mpeg", 4) == 0 || strncasecmp(type + 6, "mp3", 3) == 0 ||
		       strncasecmp(type + 6, "wav", 3) == 0 || strncasecmp(type + 6, "x-wav", 5) == 0 ||
		       strncasecmp(type + 6, "aiff", 4) == 0 || strncasecmp(type + 6, "x-aiff", 6) == 0 ? 1 : 2;
	return 0;
}

/* an address from the page (its HTML's entities undone: &amp;) joined to
   the page's, into out */
static int au_join(const URL *base, const char *s, size_t n, char *out, int size)
{
	char raw[1024], *o = raw;
	static URL u;		/* (4 KB: not on the stack) */
	size_t i;
	for (i = 0; i < n && o < raw + sizeof raw - 1; i++) {
		if (s[i] == '&' && i + 4 < n && memcmp(s + i, "&amp;", 5) == 0) {
			*o++ = '&';
			i += 4;
		} else {
			*o++ = s[i];
		}
	}
	*o = 0;
	if (!url_parse(&u, raw, base) || !u.special || (int) strlen(u.href) >= size)
		return 0;
	memcpy(out, u.href, strlen(u.href) + 1);
	return 1;
}

/* a track to the list: its address once; kind 2 counted, not taken */
static void au_add(bc_album *a, const char *url, int kind, const char *title, long secs)
{
	int i;
	if (kind == 0 || !url[0])
		return;
	for (i = 0; i < a->n; i++)
		if (strcmp(a->t[i].url, url) == 0)
			return;
	if (kind == 2) {
		a->others++;
		return;
	}
	if (a->n >= BC_TRACKS)
		return;
	snprintf(a->t[a->n].url, sizeof a->t[a->n].url, "%s", url);
	snprintf(a->t[a->n].title, sizeof a->t[a->n].title, "%s", title && title[0] ? title : "");
	if (!a->t[a->n].title[0]) {
		/* (its file's name, without its folder or its query) */
		const char *e = url + strcspn(url, "?#"), *b = e;
		while (b > url && b[-1] != '/')
			b--;
		snprintf(a->t[a->n].title, sizeof a->t[a->n].title, "%.*s", (int) (e - b), b);
	}
	a->t[a->n].seconds = secs;
	a->n++;
}

/* an attribute's value in the tag from p to e: where and how long; NULL none */
static const char *tag_attr(const char *p, const char *e, const char *name, size_t *len)
{
	size_t k = strlen(name);
	for (; p + k + 1 < e; p++)
		if ((p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') && strncasecmp(p, name, k) == 0 &&
		    p[k] == '=') {
			const char *v = p + k + 1, *ve;
			char q = *v == '"' || *v == '\'' ? *v++ : 0;
			for (ve = v; ve < e && (q ? *ve != q : *ve != ' ' && *ve != '>'); ve++)
				;
			*len = (size_t) (ve - v);
			return v;
		}
	return NULL;
}

/* the schema.org recordings in a JSON-LD block, from p to e */
static void au_ld(bc_album *a, const URL *base, const char *p, const char *e)
{
	const char *t = ld_key(p, e, "@type"), *v;
	int album = t != NULL && (strncmp(t, "\"MusicAlbum\"", 12) == 0 || strncmp(t, "\"MusicPlaylist\"", 15) == 0);
	if (t == NULL || (!album && strncmp(t, "\"MusicRecording\"", 16) != 0))
		return;
	if (album) {
		if ((v = ld_key(p, e, "name")) != NULL && *v == '"' && !a->title[0])
			json_str(v, a->title, sizeof a->title);
		if ((v = ld_key(p, e, "byArtist")) != NULL && (v = ld_key(v, e, "name")) != NULL && *v == '"' && !a->artist[0])
			json_str(v, a->artist, sizeof a->artist);
	}
	/* each recording: its name, its duration, its audio's address */
	for (v = p; (v = ld_key(v, e, "@type")) != NULL; v++) {
		const char *o, *oe, *f;
		char name[128], url[1024], u[512];
		long secs = 0;
		if (strncmp(v, "\"MusicRecording\"", 16) != 0)
			continue;
		for (o = v; o > p && *o != '{'; o--)
			;
		if (*o != '{' || (oe = json_end(o)) == NULL || oe > e)
			continue;
		name[0] = url[0] = 0;
		if ((f = ld_key(o, oe, "name")) != NULL && *f == '"')
			json_str(f, name, sizeof name);
		if ((f = ld_key(o, oe, "duration")) != NULL && *f == '"')
			secs = iso_secs(f + 1);
		if ((f = ld_key(o, oe, "contentUrl")) != NULL && *f == '"')
			json_str(f, url, sizeof url);
		else if ((f = ld_key(o, oe, "audio")) != NULL && *f == '"')
			json_str(f, url, sizeof url);
		if (url[0] && au_join(base, url, strlen(url), u, sizeof u)) {
			char ty[40];
			ty[0] = 0;
			if ((f = ld_key(o, oe, "encodingFormat")) != NULL && *f == '"')
				json_str(f, ty, sizeof ty);
			au_add(a, u, au_kind(u, ty[0] ? ty : NULL), name, secs);
		}
		v = oe - 1;
	}
	if (album && a->n > 0)
		a->album = 1;
}

/* 1 when the page may have audio: "audio", "Audio" (og:audio, <audio>,
   audio/..., AudioObject), "Music" (schema.org's) or ".mp3", ".wav",
   ".aif", ".m3u" in it; the rest of the page's scan only then (memchr's
   pace: every page is looked at) */
static int au_maybe(const char *data, long len)
{
	const char *p = data, *end = data + len;
	while (p < end && (p = memchr(p, 'u', (size_t) (end - p))) != NULL) {
		if (p >= data + 1 && p + 3 < end && (p[-1] == 'a' || p[-1] == 'A') && p[1] == 'd' && p[2] == 'i' &&
		    p[3] == 'o')
			return 1;
		if (p >= data + 1 && p + 3 < end && p[-1] == 'M' && p[1] == 's' && p[2] == 'i' && p[3] == 'c')
			return 1;
		p++;
	}
	for (p = data; p < end && (p = memchr(p, '.', (size_t) (end - p))) != NULL; p++)
		if (p + 3 < end && ((p[1] == 'm' && p[2] == 'p' && p[3] == '3') || (p[1] == 'w' && p[2] == 'a' && p[3] == 'v') ||
				    (p[1] == 'a' && p[2] == 'i' && p[3] == 'f') || (p[1] == 'm' && p[2] == '3' && p[3] == 'u') ||
				    (p[1] == 'M' && p[2] == 'P' && p[3] == '3')))
			return 1;
	return 0;
}

bc_album *au_find(const char *url, const char *data, long len)
{
	static URL base;	/* (4 KB: not on the stack) */
	const char *p, *end = data + len;
	bc_album *a;
	if (bc_page(url))
		return bc_read(url, data, len);
	if (!au_maybe(data, len))
		return NULL;
	if (!url_parse(&base, url, NULL))
		return NULL;
	a = calloc(1, sizeof *a);
	if (a == NULL)
		return NULL;
	snprintf(a->page, sizeof a->page, "%s", url);
	for (p = data; p < end; p++) {
		const char *te;
		size_t n;
		if (*p != '<')
			continue;
		for (te = p; te < end && *te != '>'; te++)
			;
		if (te >= end)
			break;
		if (strncasecmp(p, "<script", 7) == 0) {
			const char *ty = tag_attr(p + 1, te, "type", &n), *se;
			for (se = te; se + 9 <= end && strncasecmp(se, "</script", 8) != 0; se++)
				;
			if (ty != NULL && n == 19 && strncasecmp(ty, "application/ld+json", 19) == 0)
				au_ld(a, &base, te + 1, se);
			p = se;
		} else if (strncasecmp(p, "<meta", 5) == 0) {
			const char *pr = tag_attr(p + 1, te, "property", &n), *c;
			size_t cn;
			char u[512];
			if (pr != NULL && ((n == 8 && strncasecmp(pr, "og:audio", 8) == 0) ||
					   (n == 12 && strncasecmp(pr, "og:audio:url", 12) == 0) ||
					   (n == 19 && strncasecmp(pr, "og:audio:secure_url", 19) == 0)) &&
			    (c = tag_attr(p + 1, te, "content", &cn)) != NULL && au_join(&base, c, cn, u, sizeof u))
				au_add(a, u, au_kind(u, "audio/"), NULL, 0);
		} else if (strncasecmp(p, "<audio", 6) == 0 || strncasecmp(p, "<source", 7) == 0) {
			const char *s = tag_attr(p + 1, te, "src", &n), *ty;
			size_t tn;
			char u[512], tb[40];
			tb[0] = 0;
			if ((ty = tag_attr(p + 1, te, "type", &tn)) != NULL)
				snprintf(tb, sizeof tb, "%.*s", (int) tn, ty);
			if (s != NULL && au_join(&base, s, n, u, sizeof u) &&
			    (p[1] == 'a' || p[1] == 'A' || au_kind(u, tb[0] ? tb : NULL)))
				au_add(a, u, au_kind(u, tb[0] ? tb : "audio/"), NULL, 0);
		} else if ((p[1] == 'a' || p[1] == 'A') && (p[2] == ' ' || p[2] == '\t' || p[2] == '\n')) {
			const char *h = tag_attr(p + 1, te, "href", &n), *ty, *ae, *q;
			size_t tn;
			char u[512], tb[40], text[128];
			int k, o = 0;
			tb[0] = 0;
			if ((ty = tag_attr(p + 1, te, "type", &tn)) != NULL)
				snprintf(tb, sizeof tb, "%.*s", (int) tn, ty);
			if (h == NULL || !au_join(&base, h, n, u, sizeof u) || (k = au_kind(u, tb[0] ? tb : NULL)) == 0)
				continue;
			/* its words, the tags in them left out */
			for (ae = te + 1; ae + 4 <= end && strncasecmp(ae, "</a", 3) != 0; ae++)
				;
			for (q = te + 1; q < ae && o < (int) sizeof text - 1; q++) {
				if (*q == '<') {
					while (q < ae && *q != '>')
						q++;
					continue;
				}
				if (*q == '\n' || *q == '\r' || *q == '\t' || (*q == ' ' && (o == 0 || text[o - 1] == ' ')))
					continue;
				text[o++] = *q;
			}
			while (o > 0 && text[o - 1] == ' ')
				o--;
			text[o] = 0;
			au_add(a, u, k, text, 0);
		}
		p = te;
	}
	if (a->n == 0) {
		free(a);
		return NULL;
	}
	{
		static int next_id = 1000000;
		a->id = ++next_id;
	}
	keep(a);
	return a;
}

int au_bar(SBUF *out, const bc_album *a, const char *data, long len)
{
	const char *p, *end = data + len, *te;
	char line[256];
	for (p = data; p + 5 < end; p++)
		if (*p == '<' && strncasecmp(p, "<body", 5) == 0 && (p[5] == '>' || p[5] == ' ' || p[5] == '\t' || p[5] == '\n'))
			break;
	if (p + 5 >= end)
		return 0;
	for (te = p; te < end && *te != '>'; te++)
		;
	if (te >= end)
		return 0;
	sb_add(out, data, te + 1 - data);
	snprintf(line, sizeof line,
		 "<div style=\"margin:0;padding:4px 8px;background:#fbeee2;color:#1b1714;font:13px sans-serif;"
		 "border-bottom:1px solid #b87333\">%d audio %s on this page &middot; "
		 "<a href=\"about:play?a=%d&amp;from=0\" style=\"color:#b87333\">Play in EPlay</a></div>",
		 a->n, a->n == 1 ? "track" : "tracks", a->id);
	sb_adds(out, line);
	sb_add(out, te + 1, end - (te + 1));
	return 1;
}

int au_playable(const char *url)
{
	return au_kind(url, NULL) == 1;
}

bc_album *au_one(const char *url, const char *title)
{
	bc_album *a = calloc(1, sizeof *a);
	static int next_id = 2000000;
	if (a == NULL)
		return NULL;
	snprintf(a->page, sizeof a->page, "%s", url);
	au_add(a, url, 1, title, 0);
	a->id = ++next_id;
	keep(a);
	return a;
}

