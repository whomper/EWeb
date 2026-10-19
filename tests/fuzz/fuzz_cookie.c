/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Cookies from the fuzz input (src/net/cookie.c): each line a Set-Cookie
 * value from an answer to one of a few addresses (by the line's first
 * byte), then the Cookie header for each, and the lasting ones written
 * out as the file's text, read back and written again: the same text
 * must come out.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cookie.h"

static long long fixed(void)
{
	return 1760000000LL;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char *const where[4] = { "http://example.com/a/b", "https://www.example.co.il/",
					       "http://127.0.0.1:8080/x", "https://sub.example.com/deep/er/page" };
	static char s[16384];
	URL u[4];
	CKCTX x = { 1, 1, NULL };
	char *line, *next, *t1, *t2;
	long l1, l2;
	int i;
	if (n >= sizeof(s))
		return 0;
	memcpy(s, d, n);
	s[n] = 0;
	if (strlen(s) != n)
		return 0;
	ck_now = fixed;
	ck_file(NULL);
	ck_clear();
	for (i = 0; i < 4; i++)
		url_parse(&u[i], where[i], 0);
	for (line = s; line != NULL && *line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		ck_set(&u[(unsigned char) line[0] & 3], line + (line[0] ? 1 : 0), &x);
	}
	for (i = 0; i < 4; i++) {
		SBUF h;
		sb_init(&h);
		ck_get(&u[i], &x, &h);
		sb_free(&h);
	}
	t1 = ck_text(&l1);
	ck_clear();
	ck_load(t1, l1);
	t2 = ck_text(&l2);
	if (l1 != l2 || memcmp(t1, t2, (size_t) l1) != 0)
		abort();
	free(t1);
	free(t2);
	ck_clear();
	return 0;
}
