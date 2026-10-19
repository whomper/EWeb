/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * A bookmark file from the fuzz input (src/browse/bookmark.c), as
 * another browser might have written it: read, written as EWeb writes
 * it, read back and written again: the same file must come out.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bookmark.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char *w1, *w2;
	long l1, l2;
	if (!bm_parse((const char *) d, (long) n))
		return 0;
	w1 = bm_write(&l1);
	if (w1 == NULL || !bm_parse(w1, l1)) {
		free(w1);
		return 0;
	}
	w2 = bm_write(&l2);
	if (w2 == NULL || l1 != l2 || memcmp(w1, w2, (size_t) l1) != 0)
		abort();
	free(w1);
	free(w2);
	bm_free();
	return 0;
}
