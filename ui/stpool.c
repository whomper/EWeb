/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stpool.c - the accessory's ST RAM (DESIGN.md, The accessory: EWEB.ACC):
 * one block of ST RAM taken before the first event call (libc's acc_core,
 * so that it is the accessory's own on plain TOS: the guidelines' 2.2),
 * handed out to what must be in ST RAM (the screen's bands, the toolbar's
 * and the throbber's pictures, the disk's bounce buffer, the kept page).
 * The program takes ST RAM from GEMDOS as before; st_pool_on() is 0 there.
 *
 * First fit over the pieces in address order, each with its size in front
 * (16 bytes, so that every piece starts on a 16-byte line), the free ones
 * joined with their neighbours when given back.
 */
#include <stddef.h>
#include <stdlib.h>
#include "stpool.h"

typedef struct piece {
	long size;		/* the whole piece, this head included */
	long used;		/* 1: given out */
	struct piece *next;	/* the next in address order, NULL: the last */
	long pad;
} piece;

static piece *first;
static long pool_size;

int st_pool_init(long bytes)
{
	char *p;
	bytes &= ~15L;
	if (bytes < 4096)
		return 0;
	p = acc_core(bytes + 16, 0);
	if ((long) p <= 0)
		return 0;
	first = (piece *) (((long) p + 15) & ~15L);
	first->size = bytes;
	first->used = 0;
	first->next = NULL;
	pool_size = bytes;
	return 1;
}

int st_pool_on(void)
{
	return first != NULL;
}

void *st_pool_alloc(long n)
{
	piece *q;
	if (first == NULL || n <= 0)
		return NULL;
	n = (n + sizeof(piece) + 15) & ~15L;
	for (q = first; q != NULL; q = q->next)
		if (!q->used && q->size >= n) {
			if (q->size - n >= (long) sizeof(piece) + 64) {
				piece *r = (piece *) ((char *) q + n);
				r->size = q->size - n;
				r->used = 0;
				r->next = q->next;
				q->next = r;
				q->size = n;
			}
			q->used = 1;
			return q + 1;
		}
	return NULL;
}

void st_pool_free(void *p)
{
	piece *q, *prev = NULL;
	if (p == NULL)
		return;
	for (q = first; q != NULL && q + 1 != (piece *) p; q = q->next)
		prev = q;
	if (q == NULL || !q->used)
		return;
	q->used = 0;
	if (q->next != NULL && !q->next->used) {
		q->size += q->next->size;
		q->next = q->next->next;
	}
	if (prev != NULL && !prev->used) {
		prev->size += q->size;
		prev->next = q->next;
	}
}

long st_pool_largest(void)
{
	piece *q;
	long most = 0;
	for (q = first; q != NULL; q = q->next)
		if (!q->used && q->size - (long) sizeof(piece) > most)
			most = q->size - (long) sizeof(piece);
	return most;
}

long st_pool_size(void)
{
	return pool_size;
}
