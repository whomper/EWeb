/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * url.h - web addresses as the WHATWG URL Standard parses them, for the
 * schemes EWeb fetches (http, https): a link's href resolved against the
 * page's address, the host made lower case and checked, IPv4 and IPv6
 * addresses in their usual form, the default port left out, "." and ".."
 * in the path worked out, and what may not stand in a URL percent-encoded
 * (as UTF-8, but a query in a page's own encoding). Other schemes (mailto:, javascript:, data:, about:) are kept
 * as they are, for the browser to recognise; names that aren't ASCII
 * (which need IDNA) aren't taken yet.
 */
#ifndef URL_H
#define URL_H

/* the longest address taken: longer ones are refused */
#define URL_MAX 4096

typedef struct {
	char href[URL_MAX];		/* the URL as a string */
	unsigned short scheme;		/* length of the scheme, before ':' */
	unsigned short host, hostlen;	/* where the host is in href (0, 0: none) */
	unsigned short path;		/* where the path starts */
	unsigned short query;		/* where '?' is, or the end of the path */
	unsigned short frag;		/* where '#' is, or the end */
	unsigned short port;		/* to connect to: the one given, or the default */
	unsigned char special;		/* http or https (the schemes parsed in full) */
	unsigned char tls;		/* https */
} URL;

/* parse s, relative to base (NULL: s must be absolute). 1 with *u set,
   0 if it isn't a URL (the standard's "failure") */
int url_parse(URL *u, const char *s, const URL *base);

/* a page's encoding for the query of a URL in it (the URL Standard's
   "encoding", the document's): one of a byte a character, ASCII below
   0x80, hi[b] the character the byte 0x80 + b stands for (0xFFFD none) */
typedef struct {
	unsigned short hi[128];
} URL_ENC;
/* the same, s (UTF-8) in a page in enc (NULL: UTF-8): the query is
   percent-encoded after encoding in it, a character it lacks as "&#N;"
   (http and https: the standard's other schemes keep UTF-8) */
int url_parse_enc(URL *u, const char *s, const URL *base, const URL_ENC *enc);

/* what goes in a request: the path and the query ("/a/b?c") */
void url_target(const URL *u, char *out, int size);
/* the host as the Host header has it: with ":port" when it isn't the
   default, IPv6 in brackets */
void url_hostport(const URL *u, char *out, int size);
/* the host to connect to: no brackets around IPv6 */
void url_host(const URL *u, char *out, int size);

#endif
