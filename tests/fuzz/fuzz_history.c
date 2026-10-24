/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * A HISTORY.TXT from the fuzz input (src/browse/history.c), as one
 * written by hand might be: read, written as EWeb writes it, read back
 * and written again: the same file must come out, and each address in
 * it be visited.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char *w1, *w2;
	long l1, l2;
	int i;
	if (!hg_parse((const char *) d, (long) n))
		return 0;
	w1 = hg_write(&l1);
	if (w1 == NULL || !hg_parse(w1, l1)) {
		free(w1);
		return 0;
	}
	for (i = 0; i < hg_count(); i++)
		if (!hg_visited(hg_get(i)->url))
			abort();
	w2 = hg_write(&l2);
	if (w2 == NULL || l1 != l2 || memcmp(w1, w2, (size_t) l1) != 0)
		abort();
	free(w1);
	free(w2);
	hg_free();
	return 0;
}
