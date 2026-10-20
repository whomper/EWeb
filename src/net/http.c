/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * http.c - EWeb's HTTP/1.1 client (http.h). One request at a time: the
 * request goes out whole, then the answer's status line and headers are
 * read a line at a time (each line, and all of them together, have a
 * limit), then the body by its length, in chunks, or up to the close.
 * Everything a server says is checked before it is used (the AERA
 * guidelines, 7.1).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "http.h"
#include "inflate.h"
#include "conn.h"
#include "tls.h"
#include "plat.h"

char http_user_agent[128] = "EWeb (Atari Falcon030)";
char http_accept_language[64] = "en-GB,en;q=0.9,he;q=0.8";
void (*http_cookie_in)(const HTTPREQ *rq, const URL *u, const char *method, const char *value);
void (*http_cookie_out)(const HTTPREQ *rq, const URL *u, const char *method, SBUF *req);

#define LINE_MAX_  16384		/* one header line */
#define HEAD_MAX   65536		/* all the headers of an answer */
#define HEAD_LINES 200
#define HOPS       20			/* redirects, as browsers allow */

/* ---------------- kept connections ---------------- */

typedef struct {
	CONN *c;
	char key[300];			/* "host port tls" */
	unsigned long used;
} KEPT;

static KEPT kept[HTTP_KEEP];
static unsigned long kept_clock;

static void drop(KEPT *k)
{
	if (k->c)
		conn_close(k->c);
	k->c = 0;
	k->key[0] = 0;
}

void http_close_all(void)
{
	int i;
	for (i = 0; i < HTTP_KEEP; i++)
		drop(&kept[i]);
}

char http_gateway[128];

/* the gateway's host and port (http_gateway) */
static unsigned short gateway_of(char *host, int size)
{
	const char *c = strrchr(http_gateway, ':');
	long port = c && c[1] ? strtol(c + 1, NULL, 10) : 0;
	if (c && port > 0 && port < 65536 && !strchr(c + 1, ']')) {
		snprintf(host, size, "%.*s", (int)(c - http_gateway), http_gateway);
		return (unsigned short)port;
	}
	str_copy(host, http_gateway, size);
	return HTTP_GATEWAY_PORT;
}

/* a connection to u's host, or to the gateway when there is one: a kept
   one, or a new one in the slot used longest ago */
static KEPT *get_conn(const URL *u, HTTPTIME *t, int *reused, char *err, int errlen)
{
	char key[300], host[256];
	int i, sec = u->tls ? SEC_TLS : SEC_PLAIN, gw = http_gateway[0] != 0;
	unsigned short port = u->port;
	KEPT *k = 0;
	unsigned long t0;
	if (gw) {
		port = gateway_of(host, sizeof(host));
		sec = SEC_PLAIN;
	} else {
		url_host(u, host, sizeof(host));
	}
	snprintf(key, sizeof(key), "%s %u %d", host, port, sec);
	for (i = 0; i < HTTP_KEEP; i++)
		if (kept[i].c && !kept[i].c->dead && !strcmp(kept[i].key, key)) {
			kept[i].used = ++kept_clock;
			*reused = 1;
			return &kept[i];
		}
	*reused = 0;
	if (u->tls && !tls_available && !gw) {
		snprintf(err, errlen, "%s needs a secure connection, and this edition of EWeb has no TLS", host);
		return 0;
	}
	for (i = 0; i < HTTP_KEEP; i++)
		if (!k || !kept[i].c || (k->c && kept[i].used < k->used))
			k = &kept[i];
	drop(k);
	t0 = pf_ms();
	k->c = conn_open("HTTP", host, port, 0, sec, err, errlen);
	if (!k->c) {
		/* name the host (the gateway), where the message doesn't */
		if (strcmp(err, "stopped") && (gw || !strstr(err, host))) {
			char e[200];
			str_copy(e, err, sizeof(e));
			if (gw)
				snprintf(err, errlen, "the gateway (%s): %s", host, e);
			else
				snprintf(err, errlen, "%s: %s", host, e);
		}
		return 0;
	}
	t->tls_ms += k->c->tls_ms;
	t->connect_ms += pf_ms() - t0 - k->c->tls_ms;
	t->tls_tries = k->c->tls_tries;
	k->c->timeout_ms = 60000;
	str_copy(k->key, key, sizeof(k->key));
	k->used = ++kept_clock;
	return k;
}

/* ---------------- reading the answer ---------------- */

/* one line (to LF, a CR before it dropped) of at most max bytes into b:
   its length, -1 when the connection failed (c->err) or -2 when the line
   is too long */
static long get_line(CONN *c, SBUF *b, long max)
{
	sb_reset(b);
	for (;;) {
		char *nl;
		long n;
		if (c->pos >= c->len) {
			char ch;
			/* conn_read fills the buffer and hands over its first byte */
			if (conn_read(c, &ch, 1) <= 0)
				return -1;
			c->pos--;
		}
		nl = memchr(c->buf + c->pos, '\n', c->len - c->pos);
		n = nl ? nl - (c->buf + c->pos) : c->len - c->pos;
		if (b->len + n > max)
			return -2;
		if (!sb_add(b, c->buf + c->pos, n)) {
			str_copy(c->err, "out of memory", sizeof(c->err));
			return -1;
		}
		c->pos += n;
		if (nl) {
			c->pos++;
			if (b->len && b->s[b->len - 1] == '\r')
				b->s[--b->len] = 0;
			if (!b->s && !sb_add(b, "", 0))
				return -1;
			return b->len;
		}
	}
}

typedef struct {
	int chunked, close_after, keep_alive, http10;
	int encoding;			/* 1 gzip, 2 deflate, 3 something else */
	int te_other;			/* a transfer coding that isn't chunked: read to the close */
	long clen;			/* -1: not given */
	int bad;			/* a header that can't be believed */
	char location[URL_MAX];
} RHDR;

static int is_name(const char *l, int n, const char *name)
{
	return (int)strlen(name) == n && !strncasecmp(l, name, n);
}

/* a whole number of digits only, at most 2^31 - 1; -1 if it isn't one */
static long digits(const char *v)
{
	long n = 0;
	const char *p = v;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p < '0' || *p > '9')
		return -1;
	for (; *p >= '0' && *p <= '9'; p++) {
		if (n > (0x7fffffffL - 9) / 10)
			return -1;
		n = n * 10 + (*p - '0');
	}
	while (*p == ' ' || *p == '\t')
		p++;
	return *p ? -1 : n;
}

static void content_type(const char *v, HTTPRESP *rs)
{
	int n = 0;
	const char *cs;
	while (*v == ' ')
		v++;
	while (v[n] && v[n] != ';' && v[n] != ' ' && n < (int)sizeof(rs->mime) - 1) {
		rs->mime[n] = (char)(v[n] >= 'A' && v[n] <= 'Z' ? v[n] + 32 : v[n]);
		n++;
	}
	rs->mime[n] = 0;
	rs->charset[0] = 0;
	for (cs = v; (cs = str_istr(cs, "charset")); cs += 7) {
		const char *p = cs + 7;
		if (cs > v && cs[-1] != ';' && cs[-1] != ' ')
			continue;
		while (*p == ' ')
			p++;
		if (*p != '=')
			continue;
		p++;
		while (*p == ' ')
			p++;
		if (*p == '"')
			p++;
		for (n = 0; p[n] && p[n] != '"' && p[n] != ';' && p[n] != ' ' &&
			    n < (int)sizeof(rs->charset) - 1; n++)
			rs->charset[n] = p[n];
		rs->charset[n] = 0;
		break;
	}
}

static void header(const char *l, HTTPRESP *rs, RHDR *h)
{
	const char *v = strchr(l, ':');
	int n;
	if (!v)
		return;
	n = (int)(v - l);
	v++;
	while (*v == ' ' || *v == '\t')
		v++;
	if (is_name(l, n, "Content-Length")) {
		long len = digits(v);
		if (len < 0 || (h->clen >= 0 && h->clen != len))
			h->bad = 1;	/* unreadable, or two that differ */
		h->clen = len;
	} else if (is_name(l, n, "Transfer-Encoding")) {
		/* chunked when it is the last coding */
		const char *e = v + strlen(v);
		while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
			e--;
		h->chunked = e - v >= 7 && !strncasecmp(e - 7, "chunked", 7);
		h->te_other = !h->chunked;
	} else if (is_name(l, n, "Content-Encoding")) {
		if (str_istr(v, "gzip"))
			h->encoding = 1;
		else if (str_istr(v, "deflate"))
			h->encoding = 2;
		else if (strncasecmp(v, "identity", 8) && *v)
			h->encoding = 3;
	} else if (is_name(l, n, "Connection")) {
		if (str_istr(v, "close"))
			h->close_after = 1;
		if (str_istr(v, "keep-alive"))
			h->keep_alive = 1;
	} else if (is_name(l, n, "Location")) {
		str_copy(h->location, v, sizeof(h->location));
	} else if (is_name(l, n, "Content-Type")) {
		content_type(v, rs);
	}
}

/* where a body goes: rq's sink, or memory */
typedef struct {
	const HTTPREQ *rq;
	SBUF *body;
	long max;
	int to_sink;
	int discard;			/* nowhere to keep it: read and forget */
	int failed;			/* 1 the sink refused, 2 too big */
} BODY;

#ifdef EW_ST
static char rbuf[8192];		/* the ST edition: 8 KB less for a 1 MB ST */
#else
static char rbuf[16384];
#endif

/* n bytes of the body (or, n < 0, up to the close); 1 ok, 0 failed */
static int read_body(CONN *c, BODY *b, long n, HTTPRESP *rs)
{
	while (n != 0) {
		long want = n < 0 || n > (long)sizeof(rbuf) ? (long)sizeof(rbuf) : n, k;
		char *to = rbuf;
		if (!b->to_sink && !b->discard) {
			if (b->body->len + want > b->max) {
				want = b->max - b->body->len;
				if (want <= 0) {
					b->failed = 2;
					return 0;
				}
			}
			if (!sb_room(b->body, want)) {
				b->failed = 2;
				return 0;
			}
			to = b->body->s + b->body->len;
		}
		k = conn_read(c, to, want);
		if (k <= 0)
			return n < 0 && k == 0;	/* the close ends a body without a length */
		rs->length += k;
		if (b->to_sink) {
			if (!b->rq->sink(b->rq->ud, rbuf, k)) {
				b->failed = 1;
				return 0;
			}
		} else if (!b->discard) {
			b->body->len += k;
			b->body->s[b->body->len] = 0;
		}
		if (n > 0)
			n -= k;
	}
	return 1;
}

/* a chunk size: hex digits, then an extension after ';' or nothing; -1
   if it isn't one, or is beyond 2^31 */
static long chunk_size(const char *s)
{
	long n = 0;
	int any = 0;
	while (*s == ' ' || *s == '\t')
		s++;
	for (;; s++, any = 1) {
		int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 0x20) >= 'a' && (*s | 0x20) <= 'f' ? (*s | 0x20) - 'a' + 10 : -1;
		if (d < 0)
			break;
		if (n > (0x7fffffffL >> 4))
			return -1;
		n = n * 16 + d;
	}
	while (*s == ' ' || *s == '\t')
		s++;
	return any && (!*s || *s == ';') ? n : -1;
}

/* ---------------- one exchange ---------------- */

/* send the request and read the answer: 1 ok, 0 failed, -1 failed on a
   kept connection before anything came back (worth one more try on a
   new one: the server may have closed it meanwhile) */
static int exchange(const HTTPREQ *rq, const char *method, const URL *u, HTTPRESP *rs, RHDR *h,
		    SBUF *body, char *err, int errlen)
{
	SBUF req, line;
	KEPT *k;
	CONN *c;
	BODY b;
	char target[URL_MAX], hostport[300];
	int reused, got_status = 0, lines = 0, ok = 0;
	long head = 0, start = body ? body->len : 0, r;
	unsigned long t0;
	k = get_conn(u, &rs->t, &reused, err, errlen);
	if (!k)
		return 0;
	c = k->c;
	rs->reused = reused;
	str_copy(rs->security, conn_security(c), sizeof(rs->security));
	url_target(u, target, sizeof(target));
	url_hostport(u, hostport, sizeof(hostport));
	sb_init(&req);
	sb_init(&line);
	if (http_gateway[0])	/* to the gateway: the whole address (proxy form) */
		sb_printf(&req, "%s %s://%s%s HTTP/1.1\r\n", method, u->tls ? "https" : "http", hostport, target);
	else
		sb_printf(&req, "%s %s HTTP/1.1\r\n", method, target);
	{
		/* the log has the path, not the query: a form sent with GET
		   puts its fields there, and sites their tokens (7.3) */
		char shown[URL_MAX + 40];
		size_t q = strcspn(target, "?");
		snprintf(shown, sizeof(shown), "%s %.*s%s HTTP/1.1", method, (int)q, target, target[q] ? "?..." : "");
		conn_log("HTTP", " >> ", shown, (long)strlen(shown));
	}
	sb_printf(&req, "Host: %s\r\nUser-Agent: %s\r\nAccept: %s\r\nAccept-Language: %s\r\n"
		  "Accept-Encoding: %s\r\n", hostport, http_user_agent,
		  rq->accept ? rq->accept : "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
		  http_accept_language, rq->sink ? "identity" : "gzip, deflate");
	if (rq->referrer && *rq->referrer)
		sb_printf(&req, "Referer: %s\r\n", rq->referrer);
	if (http_cookie_out != NULL)
		http_cookie_out(rq, u, method, &req);
	if (rq->headers)
		sb_adds(&req, rq->headers);
	if (rq->body && strcmp(method, "GET") && strcmp(method, "HEAD"))
		sb_printf(&req, "Content-Type: %s\r\nContent-Length: %ld\r\n",
			  rq->ctype ? rq->ctype : "application/x-www-form-urlencoded", rq->bodylen);
	else if (!strcmp(method, "POST") || !strcmp(method, "PUT"))
		sb_adds(&req, "Content-Length: 0\r\n");
	sb_adds(&req, "\r\n");
	if (!req.s || !conn_write(c, req.s, req.len) ||
	    (rq->body && strcmp(method, "GET") && strcmp(method, "HEAD") && !conn_write(c, rq->body, rq->bodylen))) {
		snprintf(err, errlen, "%s: %s", hostport, c->err[0] ? c->err : "connection lost while sending");
		sb_free(&req);
		drop(k);
		return reused ? -1 : 0;
	}
	sb_free(&req);
	t0 = pf_ms();
	/* the status line, skipping 1xx answers (100 Continue) */
	for (;;) {
		r = get_line(c, &line, LINE_MAX_);
		if (r == -2)
			goto too_long;
		if (r < 0)
			goto lost;
		if (!line.len && !got_status)
			continue;		/* a stray blank line before it */
		if (strncmp(line.s, "HTTP/1.", 7) || line.len < 12 || line.s[8] != ' ' ||
		    line.s[9] < '1' || line.s[9] > '5' || line.s[10] < '0' || line.s[10] > '9' ||
		    line.s[11] < '0' || line.s[11] > '9' || (line.len > 12 && line.s[12] != ' ')) {
			snprintf(err, errlen, "%s didn't answer in HTTP", hostport);
			goto fail;
		}
		if (!got_status)
			rs->t.wait_ms += pf_ms() - t0;
		got_status = 1;
		conn_log("HTTP", " << ", line.s, line.len);
		rs->status = (line.s[9] - '0') * 100 + (line.s[10] - '0') * 10 + (line.s[11] - '0');
		str_copy(rs->reason, line.len > 13 ? line.s + 13 : "", sizeof(rs->reason));
		memset(h, 0, sizeof(*h));
		h->clen = -1;
		h->http10 = line.s[7] == '0';
		sb_reset(&rs->headers);
		rs->mime[0] = rs->charset[0] = 0;
		for (;;) {
			r = get_line(c, &line, LINE_MAX_);
			if (r == -2)
				goto too_long;
			if (r < 0)
				goto lost;
			if (!line.len)
				break;
			head += line.len;
			if (head > HEAD_MAX || ++lines > HEAD_LINES)
				goto too_long;
			if ((line.s[0] == ' ' || line.s[0] == '\t') && rs->headers.len) {
				/* a folded line: part of the one before */
				rs->headers.len--;
				sb_adds(&rs->headers, " ");
				sb_adds(&rs->headers, str_trim(line.s));
				sb_adds(&rs->headers, "\n");
				continue;
			}
			sb_add(&rs->headers, line.s, line.len);
			sb_adds(&rs->headers, "\n");
			header(line.s, rs, h);
			if (http_cookie_in != NULL && line.len > 11 && !strncasecmp(line.s, "set-cookie:", 11))
				http_cookie_in(rq, u, method, str_trim(line.s + 11));
		}
		if (rs->status >= 200 || rs->status == 101)
			break;
	}
	if (h->bad) {
		snprintf(err, errlen, "%s sent a length that can't be right", hostport);
		goto fail;
	}
	if (h->http10 && !h->keep_alive)
		h->close_after = 1;
	memset(&b, 0, sizeof(b));
	b.rq = rq;
	b.body = body;
	b.max = rq->maxbody > 0 ? rq->maxbody : HTTP_MAXBODY;
	b.to_sink = rq->sink && rs->status >= 200 && rs->status < 300;
	b.discard = !b.to_sink && !body;
	t0 = pf_ms();
	if (!strcmp(method, "HEAD") || rs->status == 204 || rs->status == 304 || rs->status < 200) {
		ok = 1;
	} else if (h->chunked) {
		for (;;) {
			long n;
			r = get_line(c, &line, 1024);
			if (r == -2)
				goto too_long;
			if (r < 0)
				goto lost;
			n = chunk_size(line.s);
			if (n < 0) {
				snprintf(err, errlen, "%s sent a chunk that can't be read", hostport);
				goto fail;
			}
			if (!n) {
				int tl = 0;
				do {	/* the trailer, up to a blank line */
					r = get_line(c, &line, LINE_MAX_);
					if (r == -2 || ++tl > HEAD_LINES)
						goto too_long;
					if (r < 0)
						goto lost;
				} while (line.len);
				break;
			}
			if (!read_body(c, &b, n, rs))
				goto body_err;
			r = get_line(c, &line, 2);
			if (r < 0 || line.len) {
				if (r == -1)
					goto lost;
				snprintf(err, errlen, "%s sent a chunk longer than it said", hostport);
				goto fail;
			}
		}
		ok = 1;
	} else if (h->clen >= 0 && !h->te_other) {
		if (!b.to_sink && !b.discard && h->clen > b.max) {
			b.failed = 2;
			goto body_err;
		}
		if (!b.to_sink && !b.discard && !sb_reserve(b.body, h->clen)) {
			str_copy(err, "not enough memory for the server's answer", errlen);
			goto fail;
		}
		if (!read_body(c, &b, h->clen, rs))
			goto body_err;
		ok = 1;
	} else {
		/* up to the close */
		if (!read_body(c, &b, -1, rs))
			goto body_err;
		h->close_after = 1;
		ok = 1;
	}
	rs->t.body_ms += pf_ms() - t0;
	rs->encoding = h->encoding;
	rs->size = rs->length;
	if ((h->encoding == 1 || h->encoding == 2) && !b.to_sink && body && body->len > start) {
		SBUF plain;
		int good;
		t0 = pf_ms();
		sb_init(&plain);
		good = h->encoding == 1 ?
		       inflate_gzip((const unsigned char *)body->s + start, body->len - start, &plain, b.max) :
		       inflate_zlib((const unsigned char *)body->s + start, body->len - start, &plain, b.max);
		if (good <= 0) {
			sb_free(&plain);
			if (good < 0)
				snprintf(err, errlen, "the page from %s unpacks to more than %ld KB", hostport, b.max >> 10);
			else
				snprintf(err, errlen, "the packed page from %s is damaged", hostport);
			ok = 0;
		} else {
			body->len = start;
			if (!start) {
				/* the unpacked page takes the packed one's place */
				sb_free(body);
				*body = plain;
				if (!body->s)
					ok = sb_add(body, "", 0);
			} else {
				ok = sb_add(body, plain.s ? plain.s : "", plain.len);
				sb_free(&plain);
			}
			rs->size = body->len - start;
		}
		rs->t.unpack_ms += pf_ms() - t0;
	}
	{
		char msg[160];
		snprintf(msg, sizeof(msg), "%d, %ld bytes%s%s", rs->status, rs->length,
			 rs->size != rs->length ? " packed" : "", reused ? "" : " (new connection)");
		conn_log("HTTP", " -- ", msg, strlen(msg));
	}
	if (h->close_after || !ok)
		drop(k);
	sb_free(&line);
	return ok;

body_err:
	if (b.failed == 2)
		snprintf(err, errlen, "the page from %s is too big (more than %ld KB)", hostport, b.max >> 10);
	else if (b.failed == 1)
		snprintf(err, errlen, "couldn't save what %s sent (disk full?)", hostport);
	else
		snprintf(err, errlen, "%s: %s", hostport, c->err[0] ? c->err : "connection lost");
	goto fail;
too_long:
	snprintf(err, errlen, "%s sent headers too long to be real", hostport);
	goto fail;
lost:
	snprintf(err, errlen, "%s: %s", hostport, c->err[0] ? c->err : "connection lost");
	sb_free(&line);
	drop(k);
	return (reused && !got_status) ? -1 : 0;
fail:
	/* the rest of the answer is unread: the connection goes */
	sb_free(&line);
	drop(k);
	return 0;
}

int http_request(const HTTPREQ *rq, HTTPRESP *rs, SBUF *body, char *err, int errlen)
{
	static RHDR h;
	static URL next;
	const char *method = rq->method ? rq->method : "GET";
	HTTPREQ r = *rq;
	unsigned long t0 = pf_ms();
	long start = body ? body->len : 0;
	int hops;
	memset(rs, 0, sizeof(*rs));
	sb_init(&rs->headers);
	if (!url_parse(&rs->url, rq->url, 0) || !rs->url.special) {
		snprintf(err, errlen, "EWeb can't fetch this address: %.80s", rq->url);
		return 0;
	}
	for (hops = 0;; hops++) {
		int x;
		char hp[300];
		url_hostport(&rs->url, hp, sizeof(hp));
		x = exchange(&r, method, &rs->url, rs, &h, body, err, errlen);
		if (x < 0) {		/* the kept connection had gone */
			if (body)
				body->len = start;
			rs->length = 0;
			x = exchange(&r, method, &rs->url, rs, &h, body, err, errlen);
		}
		if (x <= 0) {
			rs->t.total_ms = pf_ms() - t0;
			return 0;
		}
		if (http_gateway[0]) {
			/* how the Pi reached the server (webgate's X-Gateway-TLS) */
			char v[80];
			if (http_header(rs, "X-Gateway-TLS", v, sizeof(v)))
				snprintf(rs->security, sizeof(rs->security), "%.70s, through the gateway", v);
			else
				rs->security[0] = 0;
		}
		if (rq->no_redirect || !h.location[0] ||
		    (rs->status != 301 && rs->status != 302 && rs->status != 303 &&
		     rs->status != 307 && rs->status != 308))
			break;
		if (!url_parse(&next, h.location, &rs->url) || !next.special)
			break;		/* somewhere EWeb can't go: the 3xx is the answer */
		if (hops == HOPS) {
			snprintf(err, errlen, "%s redirects too many times", hp);
			rs->t.total_ms = pf_ms() - t0;
			return 0;
		}
		conn_log("HTTP", " -- ", "redirected", 10);
		/* a POST answered with 301, 302 or 303 becomes a GET, as browsers do */
		if ((rs->status == 303 && strcmp(method, "HEAD")) ||
		    ((rs->status == 301 || rs->status == 302) && !strcmp(method, "POST"))) {
			method = "GET";
			r.body = 0;
			r.bodylen = 0;
		}
		rs->url = next;
		rs->redirects++;
		if (body)
			body->len = start;
		rs->length = rs->size = 0;
	}
	rs->t.total_ms = pf_ms() - t0;
	return 1;
}

void http_resp_free(HTTPRESP *rs)
{
	sb_free(&rs->headers);
}

const char *http_header(const HTTPRESP *rs, const char *name, char *out, int size)
{
	const char *p = rs->headers.s;
	int n = (int)strlen(name);
	while (p && *p) {
		const char *e = strchr(p, '\n');
		if (!e)
			e = p + strlen(p);
		if (e - p > n && p[n] == ':' && !strncasecmp(p, name, n)) {
			const char *v = p + n + 1;
			int m;
			while (*v == ' ' || *v == '\t')
				v++;
			m = (int)(e - v);
			if (m >= size)
				m = size - 1;
			memcpy(out, v, m);
			out[m] = 0;
			return out;
		}
		p = *e ? e + 1 : e;
	}
	return 0;
}
