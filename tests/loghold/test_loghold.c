/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_loghold.c - conn_log_hold (the guidelines, 6.5): a job's
 * milestones held and written in a few opens of the log; with
 * conn_log_hold_bytes 0 the 4 KB buffer. The 4 KB buffer is taken from the
 * heap at the first line and given back when the log is switched off
 * (no BSS for an app that logs nothing); with no heap the lines go out one
 * at a time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "plat.h"
#include "conn.h"

static int opens, big_mallocs, fails, live, no_heap;
static long logged;
void *__real_malloc(size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n)
{
	if (n >= 4096) {
		if (no_heap)
			return 0;
		big_mallocs++;
		live++;
	}
	return __real_malloc(n);
}
void __wrap_free(void *p)
{
	/* only the log's blocks are 4 KB or more here: count by size */
	if (p && malloc_usable_size(p) >= 4096)
		live--;
	__real_free(p);
}

int pf_open(const char *path, int mode)
{
	opens++;
	return 3;
}
long pf_write(int h, const void *buf, long n) { logged += n; return n; }
void pf_close(int h) { }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return 0; }
void (*pf_idle)(void);
int net_open(const char *host, unsigned short port, char *err, int errlen) { return -1; }
long net_write(int h, const void *buf, long n) { return -1; }
long net_read(int h, void *buf, long max) { return -1; }
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* a job of 120 milestones of about 110 bytes (13 KB): opens of the log */
static int job(void)
{
	char line[91];
	int i;
	memset(line, 'x', 90);
	line[90] = 0;
	opens = big_mallocs = 0;
	logged = 0;
	conn_log_hold(1);
	for (i = 0; i < 120; i++)
		conn_log("HTTP", " -- ", line, 90);
	conn_log_hold(0);
	return opens;
}

int main(void)
{
	int n;
	char msg[100];
	strcpy(conn_logfile, "LOG.TXT");

	n = job();
	snprintf(msg, sizeof(msg), "held in 16 KB: 1 open for the job (%d), 16 KB from the heap (%d)", n, big_mallocs);
	check(n == 1 && big_mallocs == 1 && logged > 12000, msg);

	conn_log_hold_bytes = 0;
	n = job();
	snprintf(msg, sizeof(msg), "conn_log_hold_bytes 0: the 4 KB buffer, 4 opens (%d), one 4 KB block (%d)", n, big_mallocs);
	check(n == 4 && big_mallocs == 1 && logged > 12000, msg);

	conn_log_hold_bytes = 32768;
	n = job();
	snprintf(msg, sizeof(msg), "conn_log_hold_bytes 32 KB: 1 open (%d)", n);
	check(n == 1 && big_mallocs == 1, msg);

	conn_log_hold_bytes = 16384;
	opens = 0;
	conn_log("HTTP", " -- ", "not held", 8);
	check(opens == 1, "not held: a milestone written at once");

	/* switched off: the buffer goes back, and nothing is taken again */
	conn_logfile[0] = 0;
	conn_log("HTTP", " -- ", "off", 3);
	check(live == 0, "log switched off: the 4 KB buffer is given back");
	big_mallocs = 0;
	conn_log("HTTP", " -- ", "off", 3);
	check(big_mallocs == 0, "log off: nothing taken from the heap");

	/* no heap: lines still reach the file, one open each */
	strcpy(conn_logfile, "LOG.TXT");
	no_heap = 1;
	opens = 0;
	logged = 0;
	conn_log("HTTP", " -- ", "no heap", 7);
	check(opens > 0 && logged > 7, "no 4 KB in the heap: the line is still written");
	no_heap = 0;
	conn_logfile[0] = 0;
	conn_log("HTTP", " -- ", "off", 3);

	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails != 0;
}
