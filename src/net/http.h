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
} HTTPREQ;

/* 1: an answer came (rs->status says which), with its body appended to
   body (unpacked); 0: none, err says why. rs is set up here and keeps
   the answer's headers: http_resp_free when done with it. */
int  http_request(const HTTPREQ *rq, HTTPRESP *rs, SBUF *body, char *err, int errlen);
void http_resp_free(HTTPRESP *rs);
/* the first header of that name in the answer, or NULL */
const char *http_header(const HTTPRESP *rs, const char *name, char *out, int size);
void http_close_all(void);	/* close the kept connections */

/* how EWeb names itself to servers, and the languages it asks for */
extern char http_user_agent[128];
extern char http_accept_language[64];

#endif
