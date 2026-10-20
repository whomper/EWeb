/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_libc.c - the C library calls QuickJS makes that the family's
 * freestanding one hasn't (atari/include/js): the time (as the cookies
 * take it, cookie.c's ck_now), and the streams its debugging dumps write
 * to (EWeb's log). Built with the FPU, as QuickJS is.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "cookie.h"
#include "plat.h"
#include "tls.h"

int gettimeofday(struct timeval *tv, void *tz)
{
	(void) tz;
	tv->tv_sec = (long) ck_now();
	tv->tv_usec = (long) (pf_ms() % 1000) * 1000;
	return 0;
}

int clock_gettime(int clk, struct timespec *ts)
{
	if (clk == CLOCK_MONOTONIC) {
		unsigned long ms = pf_ms();
		ts->tv_sec = (time_t) (ms / 1000);
		ts->tv_nsec = (long) (ms % 1000) * 1000000L;
		return 0;
	}
	ts->tv_sec = (time_t) ck_now();
	ts->tv_nsec = 0;
	return 0;
}

time_t time(time_t *t)
{
	time_t v = (time_t) ck_now();
	if (t)
		*t = v;
	return v;
}

/* the local time: the Atari's clock is local, its offset from UTC the one
   TLS keeps (tls_tz_minutes) */
struct tm *localtime_r(const time_t *t, struct tm *tm)
{
	long long s = (long long) *t + tls_tz_minutes * 60LL;
	long days = (long) (s >= 0 ? s / 86400 : (s - 86399) / 86400), sec = (long) (s - days * 86400LL);
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned long doe = (unsigned long) (z - era * 146097);
	unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long y = (long) yoe + era * 400;
	unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	int d = (int) (doy - (153 * mp + 2) / 5 + 1), m = (int) (mp < 10 ? mp + 3 : mp - 9);
	memset(tm, 0, sizeof *tm);
	tm->tm_year = (int) (y + (m <= 2)) - 1900;
	tm->tm_mon = m - 1;
	tm->tm_mday = d;
	tm->tm_hour = (int) (sec / 3600);
	tm->tm_min = (int) (sec / 60 % 60);
	tm->tm_sec = (int) (sec % 60);
	tm->tm_wday = (int) ((days % 7 + 11) % 7);
	tm->tm_gmtoff = tls_tz_minutes * 60L;
	return tm;
}

/* the dumps' streams: one line at a time to the log */
static char line[256];
static int at;
static struct { int dummy; } out_file;
FILE *const js_stdout = (FILE *) &out_file;

int fputc(int c, FILE *f)
{
	(void) f;
	if (c == '\n' || at == (int) sizeof line - 2) {
		line[at++] = '\n';
		line[at] = 0;
		pf_debug(line);
		at = 0;
		if (c == '\n')
			return c;
	}
	line[at++] = (char) c;
	return c;
}

int putchar(int c)
{
	return fputc(c, js_stdout);
}

int fputs(const char *s, FILE *f)
{
	while (*s)
		fputc(*s++, f);
	return 0;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	const char *s = p;
	size_t k;
	for (k = 0; k < size * n; k++)
		fputc(s[k], f);
	return n;
}

int fflush(FILE *f)
{
	(void) f;
	return 0;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char b[512];
	int n = vsnprintf(b, sizeof b, fmt, ap);
	fputs(b, f);
	return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vfprintf(f, fmt, ap);
	va_end(ap);
	return n;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vfprintf(js_stdout, fmt, ap);
	va_end(ap);
	return n;
}
