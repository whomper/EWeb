/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The C library calls NetSurf's libraries need beyond common/atari/libc.c:
 * bsearch (character set aliases), abort (Hubbub's charset detection, on a
 * case that can't happen), errno, and assert's failure when NDEBUG is off. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <tos.h>

int errno;

void *bsearch(const void *key, const void *base, size_t n, size_t size,
	int (*cmp)(const void *, const void *))
{
	const char *b = base;

	while (n > 0) {
		const char *mid = b + (n / 2) * size;
		int c = cmp(key, mid);
		if (c == 0)
			return (void *) mid;
		if (c > 0) {
			b = mid + size;
			n = n - n / 2 - 1;
		} else {
			n = n / 2;
		}
	}
	return NULL;
}

void abort(void)
{
	exit(3);
}

#ifndef NDEBUG
void eweb_assert_fail(const char *expr, const char *file, int line)
{
	char buf[200];
	snprintf(buf, sizeof buf, "assertion failed: %s (%s:%d)\r\n", expr, file, line);
	nf_debug(buf);
	Cconws(buf);
	exit(4);
}
#endif
