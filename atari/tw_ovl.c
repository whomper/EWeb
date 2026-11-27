/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tw_ovl.c - EWEB.PRG's side of EWEBTW.OVL (tw_ovl.h): tw_sheet loads the
 * overlay the first time a page needs it and calls it; tw_script, which
 * every page's head is looked at with, stays here. The loading is
 * atari/js_ovl.c's: the image read, TOS's fixups and the imports applied,
 * the caches flushed. Missing, damaged or from another version, it is
 * said once in the log and the page shows without Tailwind's styles.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tos.h>

#include "tw_ovl.h"
#include "plat.h"
#include "codepad.h"

#ifndef TW_ABI
#error "TW_ABI: the Makefile gives it"
#endif

#define OVL_MAX (1L << 20)	/* the most a part of the file may be */

static char ovl_path[256];
static const tw_exports *ovl;
static int failed;		/* for good: missing, damaged, another version */
static long cpu;		/* the _CPU cookie, for flush_caches */

void tw_ovl_at(const char *path)
{
	snprintf(ovl_path, sizeof ovl_path, "%s", path);
}

int tw_script(const char *url)
{
	if (!strncmp(url, "https:", 6))
		url += 6;
	return !strncmp(url, "//cdn.tailwindcss.com", 21) && (url[21] == 0 || url[21] == '/' || url[21] == '?');
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long) p[0] << 24 | (unsigned long) p[1] << 16 | (unsigned long) p[2] << 8 | p[3];
}

/* the code just read made what the CPU runs (js_ovl.c's) */
static long flush_caches(void)
{
#ifdef __mcoldfire__
	unsigned long set, way;
	__asm__ volatile("nop" ::: "memory");
	for (set = 0; set < 512; set++)
		for (way = 0; way < 4; way++)
			__asm__ volatile("cpushl %%bc,(%0)" : : "a"(set << 4 | way) : "memory");
	__asm__ volatile("nop" ::: "memory");
	return 0;
#else
	if (cpu >= 40) {
		__asm__ volatile(".short 0xf4f8" ::: "memory");	/* cpusha bc */
		if (cpu >= 60)
			__asm__ volatile("movec %%cacr,%%d0\n\tbset #22,%%d0\n\tmovec %%d0,%%cacr" ::: "d0", "memory");
	} else if (cpu >= 20)
		__asm__ volatile("movec %%cacr,%%d0\n\tori.w #0x0808,%%d0\n\tmovec %%d0,%%cacr" ::: "d0", "memory");
	return 0;
#endif
}

static int fail(const char *why)
{
	char line[160];
	snprintf(line, sizeof line, "EWEB tw: EWEBTW.OVL %s\n", why);
	pf_debug(line);
	return 0;
}

static int load(void)
{
	unsigned char hdr[28], *img = NULL, *rest = NULL, *p, *end;
	unsigned long tsize, dsize, bsize, n, i, base, top;
	long size, restlen;
	char line[80];
	int h = pf_open(ovl_path, PF_READ);
	if (h < 0)
		return fail("is missing (it goes next to EWEB.PRG): pages made with Tailwind show without its styles.");
	size = pf_open_size(h);
	if (size < 28 || pf_read(h, hdr, 28) != 28 || hdr[0] != 0x60 || hdr[1] != 0x1A) {
		pf_close(h);
		return fail("isn't EWeb's.");
	}
	tsize = be32(hdr + 2);
	dsize = be32(hdr + 6);
	bsize = be32(hdr + 10);
	if (tsize < sizeof(tw_exports) || tsize > OVL_MAX || dsize > OVL_MAX || bsize > OVL_MAX ||
	    size - 28 < (long) (tsize + dsize) + 16) {
		pf_close(h);
		return fail("isn't EWeb's.");
	}
	top = tsize + dsize - 4;	/* the last place a long may be */
	restlen = size - 28 - (long) (tsize + dsize);
	img = malloc(tsize + dsize + bsize + 4);
	rest = malloc(restlen);
	if (img == NULL || rest == NULL) {
		free(img);
		free(rest);
		pf_close(h);
		fail("doesn't fit in the memory.");
		return -1;	/* (tried again when a page next needs it) */
	}
	p = (unsigned char *) (((unsigned long) img + 3) & ~3UL);
	base = (unsigned long) p;
	if (pf_read(h, p, (long) (tsize + dsize)) != (long) (tsize + dsize) || pf_read(h, rest, restlen) != restlen) {
		pf_close(h);
		free(img);
		free(rest);
		return fail("couldn't be read.");
	}
	pf_close(h);
	memset(p + tsize + dsize, 0, bsize);
	/* TOS's fixups: the places that hold an address in the overlay */
	end = rest + restlen;
	n = 4;
	if ((i = be32(rest)) != 0) {
		unsigned long at = i;
		for (;;) {
			if (at > top || (at & 1))
				goto bad;
			*(unsigned long *) (p + at) += base;
			do {			/* 1: 254 bytes on, 0: the end */
				if (rest + n >= end)
					goto bad;
				i = rest[n++];
				if (i == 1)
					at += 254;
			} while (i == 1 && at <= top);
			if (i == 0)
				break;
			at += i;
		}
	}
	/* the imports: each place gets the address of EWEB.PRG's (or the
	   distance to it from the place) */
	if ((unsigned long) (end - rest) < n + 12 || be32(rest + n) != TW_OVL_IMPORTS ||
	    be32(rest + n + 4) != tw_imports_crc)
		goto other;
	i = be32(rest + n + 8);
	n += 12;
	if (i > (unsigned long) (end - (rest + n)) / 8)
		goto bad;
	for (; i > 0; i--, n += 8) {
		unsigned long at = be32(rest + n), k = (unsigned long) rest[n + 4] << 8 | rest[n + 5];
		unsigned long to;
		if (at > top || (at & 1) || k >= tw_imports_n)
			goto bad;
		to = (unsigned long) tw_imports[k];
		*(unsigned long *) (p + at) += rest[n + 7] ? to - base : to;
	}
	free(rest);
	ovl = (const tw_exports *) p;
	if (ovl->magic != TW_OVL_MAGIC || ovl->abi != TW_ABI_OF) {
		ovl = NULL;
		free(img);
		return fail("is from another version of EWeb.");
	}
	cpu = get_cookie(0x5F435055L);	/* _CPU */
	Supexec(flush_caches);
	snprintf(line, sizeof line, "EWEB tw: EWEBTW.OVL loaded at %08lx\n", base);
	pf_debug(line);
	return 1;
other:
	free(rest);
	free(img);
	return fail("is from another version of EWeb.");
bad:
	free(rest);
	free(img);
	return fail("is damaged.");
}

char *tw_sheet(tw_name *names, int n, size_t *len)
{
	/* (the CSS engine's code, next, at 1.26's places modulo 256: never run) */
	CODE_PAD((uintptr_t) names == 1, CP_TW_OVL_TW_SHEET);
	if (ovl == NULL && !failed) {
		int r = load();
		if (r == 0)
			failed = 1;
	}
	return ovl != NULL ? ovl->sheet(names, n, len) : NULL;
}
