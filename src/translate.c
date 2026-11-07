/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * translate.c - the client of the Gateway's translation service; see translate.h
 * and standards/TRANSLATION-SERVICE.md.
 */
#include <stdlib.h>
#include <string.h>
#include "conn.h"
#include "charset.h"
#include "translate.h"

#define TR_TIMEOUT_MS 60000L	/* a piece of 2 KB takes seconds on the Gateway; the service answers 504 beyond this */
#define TR_HDRMAX     40	/* header lines read before giving up */

/* ---- the pure parts ---- */

int translate_code_ok(const char *code)
{
	int i = 0, n;
	if (!code)
		return 0;
	while (code[i] >= 'a' && code[i] <= 'z')
		i++;
	if (i < 2 || i > 3)
		return 0;
	if (!code[i])
		return 1;
	if (code[i++] != '-')
		return 0;
	n = 0;
	while ((code[i] >= 'a' && code[i] <= 'z') || (code[i] >= 'A' && code[i] <= 'Z') ||
	       (code[i] >= '0' && code[i] <= '9')) {
		i++;
		n++;
	}
	return code[i] == 0 && n >= 2 && n <= 4;
}

/* a paragraph break ends at i: two line ends, a CR allowed before each */
static int para_end(const char *s, long i)
{
	if (i < 2 || s[i - 1] != '\n')
		return 0;
	if (s[i - 2] == '\n')
		return 1;
	return s[i - 2] == '\r' && i >= 3 && s[i - 3] == '\n';
}

long translate_split(const char *s, long n, long max)
{
	long lim, i;
	if (n <= 0)
		return 0;
	if (n <= max)
		return n;
	lim = max;
	while (lim > 0 && ((unsigned char)s[lim] & 0xC0) == 0x80)
		lim--;			/* the cut would fall inside a character */
	for (i = lim; i >= 2; i--)
		if (para_end(s, i))
			return i;
	for (i = lim; i >= 2; i--)	/* the end of a sentence: . ! ? and then a space or a line end */
		if ((s[i - 1] == ' ' || s[i - 1] == '\n') && (s[i - 2] == '.' || s[i - 2] == '!' || s[i - 2] == '?'))
			return i;
	for (i = lim; i >= 1; i--)
		if (s[i - 1] == '\n')
			return i;
	for (i = lim; i >= 1; i--)
		if (s[i - 1] == ' ')
			return i;
	if (lim > 0)
		return lim;
	/* a continuation byte at the very start: one whole character */
	for (i = 1; i < n && ((unsigned char)s[i] & 0xC0) == 0x80; i++)
		;
	return i;
}

/* a host that can stand in a Host: line */
static int host_ok(const char *h)
{
	if (!h || !h[0] || strlen(h) > 100)
		return 0;
	for (; *h; h++)
		if ((unsigned char)*h <= ' ' || (unsigned char)*h >= 127)
			return 0;
	return 1;
}

int translate_request(SBUF *out, const char *host, const char *to, const char *from,
		      const char *body, long n)
{
	int has_from = from && from[0];
	if (!host_ok(host) || !translate_code_ok(to) || (has_from && !translate_code_ok(from)) || n < 0)
		return 0;
	sb_reset(out);
	if (!sb_printf(out, "POST /translate?to=%s%s%s HTTP/1.0\r\nHost: %s\r\n"
			    "Content-Type: text/plain; charset=utf-8\r\nContent-Length: %ld\r\n"
			    "Connection: close\r\n\r\n",
		       to, has_from ? "&from=" : "", has_from ? from : "", host, n))
		return 0;
	return sb_add(out, body, n);
}

int translate_status_code(const char *line)
{
	int code = 0, i;
	if (!line || strncmp(line, "HTTP/1.", 7) || (line[7] != '0' && line[7] != '1') || line[8] != ' ')
		return 0;
	for (i = 9; i < 12; i++) {
		if (line[i] < '0' || line[i] > '9')
			return 0;
		code = code * 10 + (line[i] - '0');
	}
	return line[12] == 0 || line[12] == ' ' ? code : 0;
}

const char *translate_refusal(int code)
{
	switch (code) {
	case 400: return "the Gateway didn't understand the request";
	case 413: return "the text is too long for the Gateway";
	case 422: return "that language pair isn't installed on the Gateway";
	case 503: return "the translation engine on the Gateway is loading or busy";
	case 504: return "the Gateway took too long to translate";
	}
	return "the Gateway refused the request";
}

/* is s[0..n) well-formed UTF-8 (no overlong forms, no surrogates)? */
static int utf8_ok(const unsigned char *s, long n)
{
	long i = 0;
	while (i < n) {
		unsigned char c = s[i];
		int k;
		unsigned long u;
		if (c < 0x80) {
			i++;
			continue;
		}
		if (c >= 0xC2 && c <= 0xDF) {
			k = 1;
			u = c & 0x1F;
		} else if (c >= 0xE0 && c <= 0xEF) {
			k = 2;
			u = c & 0x0F;
		} else if (c >= 0xF0 && c <= 0xF4) {
			k = 3;
			u = c & 0x07;
		} else
			return 0;
		if (i + k >= n)
			return 0;
		{
			int j;
			for (j = 1; j <= k; j++) {
				if ((s[i + j] & 0xC0) != 0x80)
					return 0;
				u = (u << 6) | (s[i + j] & 0x3F);
			}
		}
		if ((k == 2 && u < 0x800) || (k == 3 && u < 0x10000) || u > 0x10FFFF ||
		    (u >= 0xD800 && u <= 0xDFFF))
			return 0;
		i += k + 1;
	}
	return 1;
}

static long count_paras(const char *s, long n)
{
	long i, k = 1;
	for (i = 2; i <= n; i++)
		if (para_end(s, i) && i < n)
			k++;
	return k;
}

/* ---- the network ---- */

static int ieq(const char *a, const char *b)	/* b lower case, a up to its end */
{
	for (; *b; a++, b++) {
		char c = *a;
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		if (c != *b)
			return 0;
	}
	return 1;
}

static void put_err(char *err, int errlen, const char *s)
{
	if (err && errlen > 0)
		str_copy(err, s, errlen);
}

/* one request, one answer. Returns the status code (0 if there was no
   usable answer, err says why); the body of a 200 is appended to body (at
   most maxbody bytes); from_hdr (TR_LANG) gets X-Translate-From. */
static int http(const char *host, unsigned short port, const SBUF *req, const char *what,
		SBUF *body, long maxbody, char *from_hdr, int *same, char *err, int errlen)
{
	CONN *c;
	SBUF line;
	long clen = -1;
	int code, h;
	char msg[160];

	if (from_hdr)
		from_hdr[0] = 0;
	if (same)
		*same = 0;
	c = conn_open("TRANSLATE", host, port, 4096, SEC_PLAIN, msg, sizeof(msg));
	if (!c) {
		put_err(err, errlen, msg);
		return 0;
	}
	c->timeout_ms = TR_TIMEOUT_MS;
	conn_log("TRANSLATE", " >> ", what, (long)strlen(what));	/* never the text */
	if (!conn_write(c, req->s, req->len)) {
		put_err(err, errlen, c->err[0] ? c->err : "couldn't send to the Gateway");
		conn_close(c);
		return 0;
	}
	sb_init(&line);
	if (conn_getline(c, &line) < 0) {
		put_err(err, errlen, c->err[0] ? c->err : "no answer from the Gateway");
		sb_free(&line);
		conn_close(c);
		return 0;
	}
	code = translate_status_code(line.s);
	if (!code) {
		put_err(err, errlen, "that isn't the translation service");
		sb_free(&line);
		conn_close(c);
		return 0;
	}
	for (h = 0; h < TR_HDRMAX; h++) {
		if (conn_getline(c, &line) < 0) {
			put_err(err, errlen, c->err[0] ? c->err : "the Gateway stopped answering");
			sb_free(&line);
			conn_close(c);
			return 0;
		}
		if (!line.len)
			break;
		if (ieq(line.s, "content-length:"))
			clen = atol(line.s + 15);
		else if (same && ieq(line.s, "x-translate-same:")) {
			const char *v = line.s + 17;
			while (*v == ' ')
				v++;
			*same = *v == '1';
		} else if (from_hdr && ieq(line.s, "x-translate-from:")) {
			const char *v = line.s + 17;
			while (*v == ' ')
				v++;
			if (translate_code_ok(v) && strlen(v) < TR_LANG)
				strcpy(from_hdr, v);
		}
	}
	sb_free(&line);
	if (h == TR_HDRMAX) {
		put_err(err, errlen, "that isn't the translation service");
		conn_close(c);
		return 0;
	}
	if (code != 200) {
		put_err(err, errlen, translate_refusal(code));
		conn_close(c);
		return code;
	}
	if (clen > maxbody) {
		put_err(err, errlen, "the Gateway's answer is longer than the protocol allows");
		conn_close(c);
		return 0;
	}
	if (clen >= 0) {
		if (clen > 0 && !conn_getbytes(c, body, clen)) {
			put_err(err, errlen, c->err[0] ? c->err : "the Gateway's answer broke off");
			conn_close(c);
			return 0;
		}
	} else {
		char tmp[512];
		long r;
		while ((r = conn_read(c, tmp, sizeof(tmp))) > 0) {
			if (body->len + r > maxbody) {
				put_err(err, errlen, "the Gateway's answer is longer than the protocol allows");
				conn_close(c);
				return 0;
			}
			if (!sb_add(body, tmp, r)) {
				put_err(err, errlen, "out of memory");
				conn_close(c);
				return 0;
			}
		}
		if (r < 0) {
			put_err(err, errlen, c->err[0] ? c->err : "the Gateway's answer broke off");
			conn_close(c);
			return 0;
		}
	}
	conn_close(c);
	return 200;
}

static int get(const char *host, unsigned short port, const char *path, SBUF *body, long maxbody,
	       char *err, int errlen)
{
	SBUF req;
	int code;
	if (!host_ok(host)) {
		put_err(err, errlen, "no address for the Gateway");
		return 0;
	}
	sb_init(&req);
	if (!sb_printf(&req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host)) {
		put_err(err, errlen, "out of memory");
		sb_free(&req);
		return 0;
	}
	code = http(host, port, &req, path, body, maxbody, NULL, NULL, err, errlen);
	sb_free(&req);
	return code == 200;
}

int translate_status(const char *host, unsigned short port, char *info, long infomax,
		     char *err, int errlen)
{
	SBUF b;
	long i, k = 0;
	int ok;
	sb_init(&b);
	ok = get(host, port, "/status", &b, 1024, err, errlen);
	if (ok && (b.len < 14 || strncmp(b.s, "translategate ", 14))) {
		put_err(err, errlen, "that isn't the translation service");
		ok = 0;
	}
	if (ok && info && infomax > 0) {
		for (i = 0; i < b.len && k < infomax - 3; i++) {	/* the lines as one, joined by ", " */
			if (b.s[i] == '\r')
				continue;
			if (b.s[i] == '\n') {
				if (i + 1 < b.len) {
					info[k++] = ',';
					info[k++] = ' ';
				}
				continue;
			}
			info[k++] = b.s[i];
		}
		info[k] = 0;
	}
	sb_free(&b);
	return ok;
}

int translate_languages(const char *host, unsigned short port, SBUF *out, char *err, int errlen)
{
	long start = out->len;
	if (!get(host, port, "/languages", out, 16384, err, errlen)) {
		out->len = start;
		return 0;
	}
	if (!utf8_ok((const unsigned char *)out->s + start, out->len - start)) {
		out->len = start;
		put_err(err, errlen, "the Gateway's list of languages isn't text");
		return 0;
	}
	return 1;
}

int translate_piece(const char *host, unsigned short port, const char *to, const char *from,
		    const char *utf8, long n, SBUF *out, char *from_used, int *same, char *err, int errlen)
{
	SBUF req, what;
	char fr[TR_LANG];
	long start = out->len;
	int code, same_ans = 0;

	if (from_used)
		from_used[0] = 0;
	if (same)
		*same = 0;
	if (n <= 0 || n > TR_PIECE) {
		put_err(err, errlen, "nothing to translate");
		return 0;
	}
	sb_init(&req);
	if (!translate_request(&req, host, to, from, utf8, n)) {
		put_err(err, errlen, "bad language or address for the Gateway");
		sb_free(&req);
		return 0;
	}
	sb_init(&what);
	sb_printf(&what, "%s>%s, %ld bytes", from && from[0] ? from : "auto", to, n);
	code = http(host, port, &req, what.s ? what.s : "", out, 4 * n + 64, fr, &same_ans, err, errlen);
	sb_free(&req);
	sb_free(&what);
	if (code != 200)
		return 0;
	if (same_ans) {
		/* already in the target language: the Gateway sends no body, the text stands as it was */
		out->len = start;
		if (!sb_add(out, utf8, n)) {
			put_err(err, errlen, "out of memory");
			return 0;
		}
		if (same)
			*same = 1;
	} else if (!utf8_ok((const unsigned char *)out->s + start, out->len - start) ||
		   count_paras(out->s + start, out->len - start) > count_paras(utf8, n) + 1) {
		out->len = start;
		put_err(err, errlen, "the Gateway's answer breaks the protocol");
		return 0;
	}
	if (from_used)
		strcpy(from_used, fr);
	return 1;
}

/* ---- the text in pieces ---- */

void translate_begin(TRANSLATE *t, const char *host, unsigned short port, const char *to,
		     const char *from, const char *utf8, long n)
{
	memset(t, 0, sizeof(*t));
	str_copy(t->host, host ? host : "", sizeof(t->host));
	t->port = port;
	str_copy(t->to, to ? to : "", sizeof(t->to));
	str_copy(t->from, from ? from : "", sizeof(t->from));
	t->text = utf8;
	t->len = n;
	t->piece = TR_PIECE_DEFAULT;
}

int translate_step(TRANSLATE *t, SBUF *out)
{
	long k;
	int same = 0;
	char fr[TR_LANG];
	if (t->pos >= t->len)
		return 0;
	k = translate_split(t->text + t->pos, t->len - t->pos,
			    t->piece > 0 && t->piece <= TR_PIECE ? t->piece : TR_PIECE_DEFAULT);
	t->err[0] = 0;
	if (!translate_piece(t->host, t->port, t->to, t->from, t->text + t->pos, k, out, fr, &same,
			     t->err, sizeof(t->err)))
		return -1;
	if (!t->from_used[0])
		str_copy(t->from_used, fr, sizeof(t->from_used));
	t->same = same;
	t->pos += k;
	return 1;
}

char *translate_utf8(const char *host, unsigned short port, const char *to, const char *from,
		     const char *utf8, long n, translate_turn_fn turn, char *from_used,
		     long *outlen, char *err, int errlen)
{
	TRANSLATE t;
	SBUF ans;
	int r;

	if (outlen)
		*outlen = 0;
	if (from_used)
		from_used[0] = 0;
	translate_begin(&t, host, port, to, from, utf8, n);
	sb_init(&ans);
	for (;;) {
		long a0 = ans.len, e, nl = 0, k0 = t.pos;
		if (turn && t.pos < t.len && turn(t.pos, t.len)) {
			put_err(err, errlen, "stopped");
			sb_free(&ans);
			return NULL;
		}
		r = translate_step(&t, &ans);
		if (r <= 0)
			break;
		/* the line ends a piece ended with come back as they went */
		while (t.pos - k0 - nl > 0 && (utf8[t.pos - 1 - nl] == '\n' || utf8[t.pos - 1 - nl] == '\r'))
			nl++;
		for (e = ans.len; e > a0 && (ans.s[e - 1] == '\n' || ans.s[e - 1] == '\r'); e--)
			;
		ans.len = e;
		if (nl && t.pos < t.len && !sb_add(&ans, utf8 + t.pos - nl, nl)) {
			put_err(err, errlen, "out of memory");
			sb_free(&ans);
			return NULL;
		}
	}
	if (r < 0) {
		put_err(err, errlen, t.err);
		sb_free(&ans);
		return NULL;
	}
	if (from_used)
		str_copy(from_used, t.from_used, TR_LANG);
	if (outlen)
		*outlen = ans.len;
	return sb_steal(&ans);	/* NUL-terminated; for an empty text, ans.s may be NULL */
}

char *translate_atari(const char *host, unsigned short port, const char *to, const char *from,
		      const char *atari, long n, translate_turn_fn turn, char *from_used,
		      long *outlen, char *err, int errlen)
{
	char *u, *r, *res;
	long ulen, rlen = 0;

	if (outlen)
		*outlen = 0;
	u = atari_to_utf8(atari, n, &ulen);
	if (!u) {
		put_err(err, errlen, "out of memory");
		return NULL;
	}
	r = translate_utf8(host, port, to, from, u, ulen, turn, from_used, &rlen, err, errlen);
	free(u);
	if (!r)
		return NULL;
	res = cs_to_atari(r, rlen, CS_UTF8, outlen);
	free(r);
	if (!res)
		put_err(err, errlen, "out of memory");
	return res;
}
