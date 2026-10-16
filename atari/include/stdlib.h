/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* common/'s stdlib.h, and what NetSurf's libraries need besides
 * (atari/libc_extra.c). */
#ifndef EWEB_STDLIB_H
#define EWEB_STDLIB_H

#include_next <stdlib.h>

void *bsearch(const void *key, const void *base, size_t n, size_t size,
	int (*cmp)(const void *, const void *));
void abort(void) __attribute__((noreturn));

#endif
