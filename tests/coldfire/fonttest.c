/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fonttest.c - src/gfx/font.c as EWEBFB.PRG builds it (ColdFire code,
 * -mcpu=5475) under qemu-m68k -cpu cfv4e, against the host's build of
 * the same: EWEB.FNT read, every face of every family, style and size
 * EWeb asks for, every character from U+0020 to U+30FF looked up (each
 * block of a face read and unpacked when one of its characters is first
 * asked for, as a page's text does), each glyph's advance, box and bits
 * summed. A FireBee tester's EWeb crashed on scrolling from 1.17 on, when
 * Latin-1 and Hebrew moved into blocks read as a page first shows one of
 * their letters (tests/coldfire/run.sh).
 *
 *     fonttest EWEB.FNT
 *
 * prints a sum for each face and one for all; 0 when every face was read.
 */
#include <stdint.h>
#include <string.h>
#include "font.h"
#include "plat.h"

#ifdef __mcoldfire__
#include "rt.h"
#define SYS_LSEEK 19
int pf_open(const char *path, int mode)
{
	(void) mode;
	return (int) sys_open(path, 0, 0);
}
long pf_read(int h, void *buf, long n)
{
	long got = 0, r;
	while (got < n && (r = sys_read(h, (char *) buf + got, n - got)) > 0)
		got += r;
	return got;
}
long pf_seek(int h, long off)
{
	return sys_call4(SYS_LSEEK, h, off, 0, 0);
}
void pf_close(int h)
{
	sys_close(h);
}
/* util.c's (common's ColdFire libc.c has sb_room and sb_add, not these) */
#include <stdlib.h>
#include "util.h"
void sb_init(SBUF *b)
{
	b->s = 0;
	b->len = b->cap = 0;
}
void sb_free(SBUF *b)
{
	free(b->s);
	sb_init(b);
}
int sb_reserve(SBUF *b, long n)
{
	char *p;
	if (n < 0)
		return 0;
	if (b->len + n + 1 <= b->cap)
		return 1;
	p = realloc(b->s, b->len + n + 1);
	if (!p)
		return 0;
	b->s = p;
	b->cap = b->len + n + 1;
	return 1;
}
static void say(const char *s)
{
	out(s);
}
static void say_hex(unsigned long v)
{
	out_hex(v);
}
#else
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
int pf_open(const char *path, int mode)
{
	(void) mode;
	return open(path, O_RDONLY);
}
long pf_read(int h, void *buf, long n)
{
	return (long) read(h, buf, (size_t) n);
}
long pf_seek(int h, long off)
{
	return (long) lseek(h, off, SEEK_SET);
}
void pf_close(int h)
{
	close(h);
}
static void say(const char *s)
{
	fputs(s, stdout);
}
static void say_hex(unsigned long v)
{
	printf("%08lx", v);
}
#endif

static uint32_t sum;

static void add(uint32_t v)
{
	sum = (sum ^ v) * 16777619u;	/* FNV-1a, a word at a time */
}

int main(int argc, char **argv)
{
	static const uint8_t sizes[] = { 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32 };
	ew_font *seen[512];
	int nseen = 0, fam, st, k, i, bad = 0;
	uint32_t all = 2166136261u;
	if (argc < 2 || !font_init(argv[1])) {
		say("fonttest: no EWEB.FNT\n");
		return 2;
	}
	for (fam = FF_SANS; fam <= FF_COND; fam++)
		for (st = 0; st < 4; st++)
			for (k = 0; k < (int) sizeof sizes; k++) {
				ew_font *f = font_get(fam, st, sizes[k]);
				uint32_t cp;
				for (i = 0; i < nseen && seen[i] != f; i++)
					;
				if (i < nseen || nseen == (int) (sizeof seen / sizeof *seen))
					continue;
				seen[nseen++] = f;
				sum = 2166136261u;
				for (cp = 0x20; cp < 0x3100; cp++) {
					GLYPH g;
					int r, n;
					font_glyph(f, cp, &g);
					add(cp);
					add(g.adv);
					add((uint32_t) (g.x & 0xff) << 16 | (uint32_t) (g.y & 0xff) << 8);
					add((uint32_t) g.w << 8 | g.h);
					add((uint32_t) font_advance(f, cp));
					if (g.bits != NULL) {
						n = (g.w + 7) / 8 * g.h;
						for (r = 0; r < n; r++)
							add(g.bits[r]);
					}
				}
				if (f->data == NULL || f->bad || f->shortb)
					bad++;
				say("face ");
				say_hex((uint32_t) f->family << 16 | (uint32_t) f->style << 8 | f->px);
				say(" ");
				say_hex(sum);
				say("\n");
				all = (all ^ sum) * 16777619u;
			}
	say("all ");
	say_hex(all);
	say(bad ? " (faces or blocks not read)\n" : "\n");
	font_free();
	return bad != 0;
}
