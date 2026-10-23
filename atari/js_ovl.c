/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_ovl.c - EWEB.PRG's side of EWEBJS.OVL (js_ovl.h): js.h's calls, made
 * into the overlay once it is loaded, and as js_none.c's until then. It is
 * loaded by js_available, the first time a site whose scripts are on is
 * opened (only with an FPU: main.c's br.scripts), into the heap (TT RAM
 * first), and kept: the documents' runtimes and their js_free are in it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "js_ovl.h"
#include "plat.h"

#ifndef JS_ABI
#error "JS_ABI: the Makefile gives it"
#endif

const int js_built = 1;

static char ovl_path[256];
static const js_exports *ovl;
static int tried;
static char problem[120];

void js_ovl_at(const char *path)
{
	snprintf(ovl_path, sizeof ovl_path, "%s", path);
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long) p[0] << 24 | (unsigned long) p[1] << 16 | (unsigned long) p[2] << 8 | p[3];
}

/* the caches cleared, as the overlay's code went into memory as data:
   the 68030's with its CACR, a 68040's or 68060's with CPUSHA (and the
   68060's branch cache, CACR's CABC) */
static long flush_caches(void)
{
	long cpu = get_cookie(0x5F435055L);	/* _CPU */
	if (cpu >= 40) {
		__asm__ volatile(".short 0xf4f8" ::: "memory");	/* cpusha bc */
		if (cpu >= 60)
			__asm__ volatile("movec %%cacr,%%d0\n\tbset #22,%%d0\n\tmovec %%d0,%%cacr" ::: "d0", "memory");
	} else if (cpu >= 20)
		__asm__ volatile("movec %%cacr,%%d0\n\tori.w #0x0808,%%d0\n\tmovec %%d0,%%cacr" ::: "d0", "memory");
	return 0;
}

static int fail(const char *why)
{
	char line[160];
	snprintf(problem, sizeof problem, "%s", why);
	snprintf(line, sizeof line, "EWEB js: EWEBJS.OVL %s\n", why);
	pf_debug(line);
	return 0;
}

static int load(void)
{
	unsigned char hdr[28], *img = NULL, *rest = NULL, *p, *end;
	unsigned long tsize, dsize, bsize, n, i, base;
	long size, restlen;
	int h = pf_open(ovl_path, PF_READ);
	if (h < 0)
		return fail("is missing (it goes next to EWEB.PRG).");
	size = pf_open_size(h);
	if (size < 28 || pf_read(h, hdr, 28) != 28 || hdr[0] != 0x60 || hdr[1] != 0x1A) {
		pf_close(h);
		return fail("isn't EWeb's.");
	}
	tsize = be32(hdr + 2);
	dsize = be32(hdr + 6);
	bsize = be32(hdr + 10);
	restlen = size - 28 - (long) (tsize + dsize);
	if (tsize < sizeof(js_exports) || restlen < 16) {
		pf_close(h);
		return fail("isn't EWeb's.");
	}
	img = malloc(tsize + dsize + bsize + 4);
	rest = malloc(restlen);
	if (img == NULL || rest == NULL) {
		free(img);
		free(rest);
		pf_close(h);
		return fail("doesn't fit in the memory.");
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
			if (at + 4 > tsize + dsize || (at & 1))
				goto bad;
			*(unsigned long *) (p + at) += base;
			do {			/* 1: 254 bytes on, 0: the end */
				if (rest + n >= end)
					goto bad;
				i = rest[n++];
				if (i == 1)
					at += 254;
			} while (i == 1);
			if (i == 0)
				break;
			at += i;
		}
	}
	/* the imports: each place gets the address of EWEB.PRG's (or the
	   distance to it from the place) */
	if (rest + n + 12 > end || be32(rest + n) != JS_OVL_IMPORTS || be32(rest + n + 4) != js_imports_crc)
		goto other;
	i = be32(rest + n + 8);
	n += 12;
	if ((unsigned long) (end - (rest + n)) < i * 8)
		goto bad;
	for (; i > 0; i--, n += 8) {
		unsigned long at = be32(rest + n), k = (unsigned long) rest[n + 4] << 8 | rest[n + 5];
		unsigned long to;
		if (at + 4 > tsize + dsize || k >= js_imports_n)
			goto bad;
		to = (unsigned long) js_imports[k];
		*(unsigned long *) (p + at) += rest[n + 7] ? to - base : to;
	}
	free(rest);
	ovl = (const js_exports *) p;
	if (ovl->magic != JS_OVL_MAGIC || ovl->abi != JS_ABI) {
		ovl = NULL;
		free(img);
		return fail("is from another version of EWeb.");
	}
	Supexec(flush_caches);
	pf_debug("EWEB js: EWEBJS.OVL loaded\n");
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

int js_available(void)
{
	if (!tried) {
		tried = 1;
		load();
	}
	return ovl != NULL;
}

const char *js_problem(void)
{
	return problem;
}

int js_page(doc *d, const char *url, const char *base, const js_host *h)
{
	return ovl ? ovl->page(d, url, base, h) : JS_NONE;
}

int js_changed(doc *d)
{
	return ovl ? ovl->changed(d) : 0;
}

const char *js_navigation(doc *d, const char **post, long *len, const char **ctype)
{
	return ovl ? ovl->navigation(d, post, len, ctype) : NULL;
}

const char *js_navigation_download(doc *d)
{
	return ovl ? ovl->navigation_download(d) : NULL;
}

void js_navigation_done(doc *d)
{
	if (ovl)
		ovl->navigation_done(d);
}

long js_due(doc *d)
{
	return ovl ? ovl->due(d) : -1;
}

void js_run_timers(doc *d)
{
	if (ovl)
		ovl->run_timers(d);
}

int js_event(doc *d, dnode *n, const char *type)
{
	return ovl ? ovl->event(d, n, type) : 0;
}

void js_run(doc *d, const char *code)
{
	if (ovl)
		ovl->run(d, code);
}

void js_toggled(doc *d, dnode *n)
{
	if (ovl)
		ovl->toggled(d, n);
}

void js_set_page(doc *d, const struct page *p)
{
	if (ovl)
		ovl->set_page(d, p);
}

void js_stats(doc *d, unsigned long *ms, long *bytes)
{
	if (ovl)
		ovl->stats(d, ms, bytes);
	else {
		*ms = 0;
		*bytes = 0;
	}
}
