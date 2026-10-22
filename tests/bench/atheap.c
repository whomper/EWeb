/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * atheap - the Atari's heap (common/atari/libc.c, built with HEAP_TEST
 * for its heap_* names) as malloc and free in the Linux 68030 programs
 * that count instructions (build/m68k/docparse), so their counts are
 * what EWEB.PRG's allocator costs, not glibc's. GEMDOS's Mxalloc and
 * Malloc hand out pieces of one big area, mapped low as an Atari's
 * memory is (the heap takes an address with the top bit set for GEMDOS's
 * error numbers).
 */
#include <stddef.h>
#include <sys/mman.h>

void *heap_malloc(size_t n);
void heap_free(void *p);
void *heap_calloc(size_t n, size_t m);
void *heap_realloc(void *p, size_t n);

#define CORE (96L * 1024 * 1024)

short _is_acc;		/* crt0.S's, which the heap asks: a program here */

static char *core;
static size_t used;

void *Mxalloc(long n, int mode)
{
	void *p;
	if (core == NULL) {
		core = mmap((void *) 0x10000000L, CORE, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (core == MAP_FAILED || (long) core <= 0) {
			core = NULL;
			return (void *) -39L;
		}
	}
	if (n == -1)
		return (void *) (long) (CORE - used);
	if (n <= 0 || (size_t) n > CORE - used)
		return (void *) -39L;		/* ENSMEM */
	p = core + used;
	used += ((size_t) n + 15) & ~(size_t) 15;
	return p;
}

void *Malloc(long n)
{
	return Mxalloc(n, 0);
}

void *malloc(size_t n)
{
	return heap_malloc(n);
}

void free(void *p)
{
	heap_free(p);
}

void *calloc(size_t n, size_t m)
{
	return heap_calloc(n, m);
}

void *realloc(void *p, size_t n)
{
	return heap_realloc(p, n);
}
