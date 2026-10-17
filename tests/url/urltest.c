/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* urltest: src/net/url.c for tests/url/run.py. Each line of stdin is an
 * input and a base in hex (the base "-" for none); each line out is the
 * parsed URL, or "failure". */
#include <stdio.h>
#include <string.h>
#include "url.h"

static int unhex(const char *h, char *out, int size)
{
	int n = 0;
	while (h[0] && h[1] && n < size - 1) {
		unsigned v;
		if (sscanf(h, "%2x", &v) != 1)
			return 0;
		out[n++] = (char)v;
		h += 2;
	}
	out[n] = 0;
	return 1;
}

int main(void)
{
	static char line[4 * URL_MAX + 16], in[URL_MAX * 2], bs[URL_MAX * 2];
	static URL base, u;
	while (fgets(line, sizeof(line), stdin)) {
		char *sp = strchr(line, ' ');
		int have_base;
		if (!sp)
			return 1;
		*sp++ = 0;
		sp[strcspn(sp, "\n")] = 0;
		unhex(line, in, sizeof(in));
		have_base = strcmp(sp, "-") != 0;
		if (have_base) {
			unhex(sp, bs, sizeof(bs));
			if (!url_parse(&base, bs, 0)) {
				puts("failure");	/* the base itself */
				continue;
			}
		}
		if (url_parse(&u, in, have_base ? &base : 0))
			puts(u.href);
		else
			puts("failure");
		fflush(stdout);
	}
	return 0;
}
