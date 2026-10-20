/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* sys/time.h - gettimeofday, for QuickJS's Date.now (atari/js_libc.c) */
#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <time.h>
struct timeval { long tv_sec; long tv_usec; };
int gettimeofday(struct timeval *tv, void *tz);
#endif
