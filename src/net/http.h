/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * http.h - HTTP/1.1 for EWeb, on common/'s connections (conn.c) and TLS
 * (tls.c, BearSSL, RSA on the DSP). It grew from ECal's and EContact's
 * client: redirects, chunked answers, gzip and deflate, and connections
 * kept open between requests, with what a browser needs besides: several
 * hosts kept open at once, addresses resolved as the URL Standard says
 * (url.c), the charset of the answer, a limit on everything a server
 * sends, and how long each part of a request took.
 */
#ifndef HTTP_H
#define HTTP_H

#include "util.h"
#include "url.h"

/* connections kept open between requests (a page, its stylesheets and
   images come from two or three hosts; a TLS handshake costs a Falcon a
   second or more) */
#define HTTP_KEEP 4

/* an answer kept in memory may be this big, unpacked, unless the request
   says otherwise */
#define HTTP_MAXBODY (8L << 20)

typedef struct {
	unsigned long connect_ms;	/* finding the host and connecting (DNS, TCP) */
	unsigned long tls_ms;		/* the TLS handshakes, tries the server refused included */
	unsigned long wait_ms;		/* from sending a request to the answer's first line */
	unsigned long body_ms;		/* from there to the answer's last byte */
	unsigned long unpack_ms;	/* gzip or deflate */
	unsigned long total_ms;		/* all of it, redirects included */
	int tls_tries;			/* connections the last handshake took */
} HTTPTIME;

typedef struct {
	int status;			/* 200, 404 ... */
	char reason[64];
	char mime[64];			/* of the body, lower case: "text/html" */
	char charset[40];		/* from Content-Type, "" if none */
	URL url;			/* where the answer came from, after redirects */
	int redirects;			/* how many were followed */
	int encoding;			/* 0 as sent, 1 gzip, 2 deflate */
	int packed;			/* the body is as it came, not unpacked: HTTPREQ.head
					   said so (a gzip file to be saved) */
	long length;			/* body bytes as they came (packed) */
	long size;			/* body bytes, unpacked */
	int reused;			/* the last request went over a kept connection */
	char security[96];		/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305", "" if plain */
	SBUF headers;			/* the last answer's header lines, "Name: value\n" */
	HTTPTIME t;
} HTTPRESP;

typedef struct {
	const char *method;		/* NULL: GET */
	const char *url;		/* absolute: http or https */
	const char *referrer;		/* the page it was found on, NULL: none */
	const char *accept;		/* NULL: what a browser asks for a page */
	const char *ctype;		/* of the body */
	const char *body;
	long bodylen;
	const char *headers;		/* more header lines, each with CRLF */
	/* the body of a 2xx answer goes here as it comes instead of into
	   memory (a download: nothing packed is asked for) */
	int (*sink)(void *ud, const char *data, long n);
	void *ud;
	long maxbody;			/* in memory, unpacked; 0: HTTP_MAXBODY */
	int no_redirect;		/* return a 3xx as it is */
	/* for cookies: a page (a navigation), not a picture or a stylesheet;
	   and the page it came from (NULL: typed) */
	int top;
	const char *site;
	/* types whose 2xx body isn't wanted, space-separated, lower case ("text/"
	   for all of them): such an answer is left unread (its connection
	   goes) and the request fails, err naming the type. NULL: any */
	const char *refuse;
	/* a 2xx answer's headers have come, its body not yet (rs has them, and
	   its encoding): 0 stops the request there (its connection goes, and
	   it fails with err "stopped"); it may set rs->packed to keep a packed
	   body as it came. ud is the sink's. NULL: none */
	int (*head)(void *ud, HTTPRESP *rs);
} HTTPREQ;

/* 1: an answer came (rs->status says which), with its body appended to
   body (unpacked); 0: none, err says why. rs is set up here and keeps
   the answer's headers: http_resp_free when done with it. */
int  http_request(const HTTPREQ *rq, HTTPRESP *rs, SBUF *body, char *err, int errlen);
void http_resp_free(HTTPRESP *rs);
/* the first header of that name in the answer, or NULL */
const char *http_header(const HTTPRESP *rs, const char *name, char *out, int size);
/* the same, where its value is in rs->headers (it ends at a newline) */
const char *http_header_at(const HTTPRESP *rs, const char *name);

/* a Content-Disposition value (RFC 6266; it ends at NUL or a newline):
   1 attachment (or a type EWeb doesn't know, which counts as one, 4.2),
   0 inline, -1 none (a header that isn't RFC 6266's is ignored whole, as
   tc2231's cases have it). name gets the file name it gives, UTF-8, ""
   for none: filename* (RFC 8187) when it is good, else filename, read as
   ISO-8859-1; cut at a character to fit size */
int http_disposition(const char *v, char *name, int size);
void http_close_all(void);	/* close the kept connections */

/* cookies (the browser sets these to cookie.c's; NULL: none): each
   Set-Cookie of an answer to u (redirects too), and the Cookie header
   line ("Cookie: ...\r\n") for a request to u, added to req */
extern void (*http_cookie_in)(const HTTPREQ *rq, const URL *u, const char *method, const char *value);
extern void (*http_cookie_out)(const HTTPREQ *rq, const URL *u, const char *method, SBUF *req);

/* AERA-Gateway's webgate on the Pi: "PI_IP" or "PI_IP:8280", "" for
   none. When set, every request goes there in proxy form ("GET
   https://host/path") and the Pi makes the connection onwards, TLS 1.2
   or 1.3; rs->security then says how the Pi reached the server. */
extern char http_gateway[128];
#define HTTP_GATEWAY_PORT 8280

/* how EWeb names itself to servers, and the languages it asks for */
extern char http_user_agent[128];
extern char http_accept_language[64];

#endif
