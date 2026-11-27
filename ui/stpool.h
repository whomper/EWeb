/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* stpool.h - the accessory's ST RAM, taken once at its start (stpool.c) */
#ifndef EW_STPOOL_H
#define EW_STPOOL_H

int st_pool_init(long bytes);		/* before the first event call; 1: taken */
int st_pool_on(void);			/* 1: the accessory's pool is in use */
void *st_pool_alloc(long n);		/* NULL: no room */
void st_pool_free(void *p);
long st_pool_largest(void);		/* the largest piece it could give */
long st_pool_size(void);

#endif
