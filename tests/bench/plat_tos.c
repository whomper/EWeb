/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench on the Atari, plain TOS: the command line from the basepage
 * (NSBENCH.TTP), or from NSBENCH.CMD beside it when there is none (an
 * emulator's autostart passes none), files through GEMDOS, the 200 Hz
 * timer, and output on the console, in NSBENCH.LOG and, under Hatari, on
 * its NatFeats console too. Started from the desktop, it waits for a key
 * at the end, so the results stay on the screen. Built with BENCH_NAME,
 * BENCH_TITLE and BENCH_MAIN, the same for another bench (PICBENCH.TTP,
 * picbench.c), on any Atari from a 68000 ST. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "plat.h"

#define MAXARG 40

#ifndef BENCH_NAME
#define BENCH_NAME "NSBENCH"		/* its .TTP, .CMD and .LOG */
#define BENCH_TITLE "nsbench"
#define BENCH_MAIN nsbench
#endif

static long logh = -1;		/* NSBENCH.LOG (BENCH_NAME's), appended to */

extern long heap_tt, heap_st;	/* common/atari/libc.c: what the heap took */

/* ---------------------------------------------------------------- the machine
 * What a timing means depends on the machine: where the program and its
 * memory are (TT-RAM is the accelerator's, ST-RAM sits on the Falcon's
 * 16 MHz bus), how fast the CPU really runs, and how fast each RAM reads. */

/* the loop below: one subq.l and one taken bne.s, from the instruction
 * cache. Motorola's 68030 timing tables give it 6 to 8 cycles a round, so a
 * 68030 at 50 MHz with its cache on runs about 6.2 to 8.3 million rounds a
 * second (Hatari's cycle-exact 68030 counts fewer cycles: its rates are
 * no measure of the clock) */

static void spin(long n)
{
	__asm__ volatile("1:\tsubq.l #1,%0\n\tbne.s 1b" : "+d"(n) : : "cc");
}

/* reads n48 * 48 bytes from p, twelve longs at a time */
static void read_block(const void *p, long n48)
{
	__asm__ volatile("1:\tmovem.l (%0)+,%%d1-%%d7/%%a1-%%a5\n\tsubq.l #1,%1\n\tbne.s 1b"
		: "+a"(p), "+d"(n48) : : "d1", "d2", "d3", "d4", "d5", "d6", "d7",
		"a1", "a2", "a3", "a4", "a5", "memory", "cc");
}

/* rounds of the loop a second */
static unsigned long loop_rate(void)
{
	unsigned long t0, t, rounds = 0;

	t0 = tos_hz200();
	do {
		spin(250000);
		rounds += 250000;
		t = tos_hz200() - t0;
	} while (t < 200);	/* at least a second */
	return rounds / t * 200;
}

/* KB a second read from a 240 KB block of the given RAM (Mxalloc mode:
 * 0 ST, 1 TT), or 0 when there is none */
static unsigned long read_rate(short mode)
{
	const long size = 240L * 1024;
	char *b = Mxalloc(size, mode);
	unsigned long t0, t, bytes = 0;

	if ((long) b == -32)		/* TOS 1.x: no Mxalloc, only ST-RAM */
		b = mode ? NULL : Malloc(size);
	if ((long) b <= 0)
		return 0;
	t0 = tos_hz200();
	do {
		read_block(b, size / 48);
		bytes += size;
		t = tos_hz200() - t0;
	} while (t < 100);	/* at least half a second */
	Mfree(b);
	return bytes / 1024 / t * 200;
}

static long cacr;

static void sup_cacr(void)
{
	/* movec cacr,d0 as its words: a 68000 build's assembler has no movec
	   (and machine_report doesn't call this on one) */
	__asm__ volatile(".dc.w 0x4e7a,0x0002\n\tmove.l %%d0,%0" : "=g"(cacr) : : "d0");
}

static void machine_report(void)
{
	long cpu = get_cookie(0x5F435055L), fpu = get_cookie(0x5F465055L);	/* _CPU, _FPU */
	long mch = get_cookie(0x5F4D4348L);					/* _MCH */
	long st_free = (long) Mxalloc(-1, 0), tt_free = (long) Mxalloc(-1, 1);
	if (st_free == -32) {		/* TOS 1.x */
		st_free = (long) Malloc(-1);
		tt_free = 0;
	}
	unsigned long loops = loop_rate(), st, tt;

	out("machine: _MCH %08lx, CPU 680%02ld, FPU %s\n", mch, cpu,
		(fpu >> 16) ? "yes" : "none");
	if (cpu >= 20) {		/* a 68000 has no CACR */
		Supexec(sup_cacr);
		out("caches: CACR %04lx: instructions %s%s, data %s%s\n", cacr & 0xffff,
			cacr & 0x0001 ? "on" : "off", cacr & 0x0010 ? " with burst" : "",
			cacr & 0x0100 ? "on" : "off", cacr & 0x1000 ? " with burst" : "");
	}
	out("loop: %lu rounds a second (6.2 to 8.3 million at 50 MHz)\n", loops);
	/* where its text starts (the basepage's p_tbase), for a profiler's
	   addresses */
	out("program in %s at $%08lx; free ST-RAM %ld KB, TT-RAM %ld KB\n",
		(unsigned long) &machine_report >= 0x01000000UL ? "TT-RAM" : "ST-RAM",
		*(unsigned long *) ((unsigned char *) _BasePage + 8),
		st_free > 0 ? st_free / 1024 : 0, tt_free > 0 ? tt_free / 1024 : 0);
	st = read_rate(0);
	tt = read_rate(1);
	out("reading: ST-RAM %lu KB/s, TT-RAM %lu KB/s\n", st, tt);
}

static void log_open(void)
{
	logh = Fopen(BENCH_NAME ".LOG", 1);
	if (logh >= 0)
		Fseek(0, (short) logh, 2);
	else
		logh = Fcreate(BENCH_NAME ".LOG", 0);
}

int main(void)
{
	static char line[128];
	char *argv[MAXARG + 1], *s;
	unsigned char *bp = _BasePage;
	int argc = 0, len = bp[0x80], rc, from_cmd = 0;

	memcpy(line, bp + 0x81, len);
	line[len] = 0;
	if (len == 0) {
		size_t n;
		char *cmd = load(BENCH_NAME ".CMD", &n);
		if (cmd != NULL) {
			strncpy(line, cmd, sizeof line - 1);
			free(cmd);
			from_cmd = 1;
		}
		for (s = line; *s; s++)
			if (*s == '\r' || *s == '\n')
				*s = ' ';
	}
	argv[argc++] = BENCH_TITLE;
	for (s = line; *s && argc < MAXARG; ) {
		while (*s == ' ')
			s++;
		if (*s == 0)
			break;
		argv[argc++] = s;
		while (*s && *s != ' ')
			s++;
		if (*s)
			*s++ = 0;
	}
	argv[argc] = NULL;
	log_open();
	out("EWeb " BENCH_TITLE " %s\n", EWEB_VERSION);
	machine_report();
	rc = BENCH_MAIN(argc, argv);
	out("heap: %ld KB from TT-RAM, %ld KB from ST-RAM\n", heap_tt / 1024, heap_st / 1024);
	out("done (%d)\n", rc);
	if (logh >= 0)
		Fclose((short) logh);
	if (!from_cmd) {
		Cconws("The results are in " BENCH_NAME ".LOG too. Press a key.\r\n");
		Bconin(2);
	}
	return rc;
}

void plat_init(void)
{
}

char *load(const char *path, size_t *len)
{
	long h = Fopen(path, 0), n;
	char *buf;

	if (h < 0)
		return NULL;
	n = Fseek(0, (short) h, 2);
	Fseek(0, (short) h, 0);
	buf = n >= 0 ? malloc(n + 1) : NULL;
	if (buf == NULL || Fread((short) h, n, buf) != n) {
		free(buf);
		Fclose((short) h);
		return NULL;
	}
	buf[n] = 0;
	Fclose((short) h);
	*len = n;
	return buf;
}

void out(const char *fmt, ...)
{
	char buf[256], crlf[300];
	const char *s;
	char *d = crlf;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	nf_debug(buf);
	for (s = buf; *s && d < crlf + sizeof crlf - 2; s++) {
		if (*s == '\n')
			*d++ = '\r';
		*d++ = *s;
	}
	*d = 0;
	Cconws(crlf);
	if (logh >= 0)
		Fwrite((short) logh, d - crlf, crlf);
}

unsigned long ticks(void)
{
	return tos_hz200();
}
