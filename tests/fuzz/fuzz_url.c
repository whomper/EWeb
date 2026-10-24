/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Addresses in pages, from the fuzz input: hrefs that may be anything,
 * resolved against a base (src/net/url.c). The input is the href, then
 * after the first newline the base (none, if there is no newline); each
 * result is parsed again, and must come out the same (the standard's
 * serialisation is idempotent); so must the href parsed in a page of a
 * byte a character (url_parse_enc: its query in that encoding). Where
 * url_join_quick joins the href without a parse, the parse must give
 * the same address, in either encoding; and where url_origin_quick
 * tells its origin, the parse's must be that.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "url.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static URL base, u, again;
	static char s[2 * URL_MAX];
	char *nl, t[300];
	int have_base = 0;
	if (n >= sizeof(s))
		return 0;
	memcpy(s, d, n);
	s[n] = 0;
	if (strlen(s) != n)
		return 0;		/* a NUL inside: not something a C string carries */
	nl = strchr(s, '\n');
	if (nl) {
		*nl = 0;
		have_base = url_parse(&base, nl + 1, 0);
	}
	{
		static char q[URL_MAX];
		if (url_join_quick(have_base ? &base : 0, s, q, sizeof(q)) &&
		    (!url_parse(&u, s, have_base ? &base : 0) || strcmp(u.href, q)))
			abort();
	}
	{
		static char o[URL_MAX];
		int k = url_origin_quick(have_base ? &base : 0, s, o, sizeof(o));
		if (k != 0 && url_parse(&u, s, have_base ? &base : 0) &&
		    (!u.special || (k < 0 ? u.path != base.path || memcmp(u.href, base.href, base.path) :
				    u.path != k || memcmp(u.href, o, k))))
			abort();
	}
	if (!url_parse(&u, s, have_base ? &base : 0))
		return 0;
	url_target(&u, t, sizeof(t));
	url_hostport(&u, t, sizeof(t));
	url_host(&u, t, sizeof(t));
	if (!url_parse(&again, u.href, 0) || strcmp(again.href, u.href))
		abort();
	{
		/* an encoding with letters at some bytes and none at others */
		static URL_ENC e;
		int i;
		for (i = 0; i < 128; i++)
			e.hi[i] = (unsigned short)(i % 3 ? 0x5d0 + i : i % 2 ? 0xe0 + i : 0xfffd);
		if (url_parse_enc(&u, s, have_base ? &base : 0, &e) &&
		    (!url_parse(&again, u.href, 0) || strcmp(again.href, u.href)))
			abort();
		{
			static char q[URL_MAX];
			if (url_join_quick(have_base ? &base : 0, s, q, sizeof(q)) && strcmp(u.href, q))
				abort();
		}
	}
	return 0;
}
