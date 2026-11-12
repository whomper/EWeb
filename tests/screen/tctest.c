/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tctest.c - ui/tcpix.c: the canvas's 5-6-5 pixels made each true-colour
 * screen's form, from vq_scrninfo's answer as NVDI and fVDI give it (or
 * none), each checked byte by byte against the form worked out by hand
 */
#include <stdio.h>
#include <string.h>

#include "tcpix.h"

static int fails, checks;

/* a vq_scrninfo answer: planes, each colour's bits from its lowest
   place (listed lowest first, or highest first where down) */
static void info_of(short *info, short planes, const short at[4], const short n[4], int down, int intel)
{
	static const short base[4] = { 16, 32, 48, 64 };
	short c, j;
	memset(info, 0, 272 * sizeof *info);
	for (j = 16; j < 128; j++)
		info[j] = -1;
	info[0] = 2;
	info[1] = 2;
	info[2] = planes;
	info[14] = (short) (intel ? 0x80 : 0x02);
	for (c = 0; c < 4; c++) {
		info[8 + c] = n[c];
		for (j = 0; j < n[c]; j++)
			info[base[c] + j] = (short) (at[c] + (down ? n[c] - 1 - j : j));
	}
}

/* pixel v of the canvas made the form f: bytes want */
static void check(const char *what, const TC_FORM *f, uint16_t v, const char *want)
{
	uint8_t out[8];
	char got[24];
	short k;
	tc_row(f, &v, out, 1);
	for (k = 0; k < f->bytes; k++)
		sprintf(got + 2 * k, "%02x", out[k]);
	checks++;
	if (strcmp(got, want) != 0) {
		printf("FAIL %s: 5-6-5 %04x made %s, want %s (%s)\n", what, v, got, want, tc_name(f));
		fails++;
	}
	if (f->deep) {
		/* at 32 bits: the same colour widened (each part's top bits
		   again below) made the same pixel */
		unsigned r5 = v >> 11, g6 = v >> 5 & 63, b5 = v & 31;
		uint8_t px[4], out32[8];
		px[0] = 0;
		px[1] = (uint8_t) (r5 << 3 | r5 >> 2);
		px[2] = (uint8_t) (g6 << 2 | g6 >> 4);
		px[3] = (uint8_t) (b5 << 3 | b5 >> 2);
		tc_row32(f, px, out32, 1);
		checks++;
		if (memcmp(out, out32, (size_t) f->bytes) != 0) {
			printf("FAIL %s: at 32 bits %04x made otherwise (%s)\n", what, v, tc_name(f));
			fails++;
		}
	}
}

/* the form f seen as the VDI shows it (pure red, green and blue read
   back), made a form again: each colour the same as f's */
static void seen_back(const char *what, const TC_FORM *f, short planes)
{
	static const uint16_t pure[3] = { 0xf800, 0x07e0, 0x001f }, some[4] = { 0x1234, 0xffff, 0x8410, 0x0000 };
	uint8_t seen[3][4], a[4], b[4];
	short info[272], c;
	TC_FORM g;
	memset(seen, 0, sizeof seen);
	for (c = 0; c < 3; c++)
		tc_row(f, &pure[c], seen[c], 1);
	checks++;
	if (!tc_seen(info, planes, (const uint8_t (*)[4]) seen) || !tc_form(&g, planes, info)) {
		printf("FAIL %s seen: no form made\n", what);
		fails++;
		return;
	}
	for (c = 0; c < 4; c++) {
		tc_row(f, &some[c], a, 1);
		tc_row(&g, &some[c], b, 1);
		checks++;
		if (memcmp(a, b, (size_t) f->bytes) != 0) {
			printf("FAIL %s seen: %04x drawn differently (%s, seen as %s)\n", what, some[c], tc_name(f),
			       tc_name(&g));
			fails++;
			return;
		}
	}
}

int main(void)
{
	static const short n888[4] = { 8, 8, 8, 0 }, n8888[4] = { 8, 8, 8, 8 };
	static const short n565[4] = { 5, 6, 5, 0 }, n555[4] = { 5, 5, 5, 0 };
	short info[272];
	TC_FORM f;
	int down;

	/* the Falcon's own, with no vq_scrninfo (TOS's VDI): as drawn */
	tc_form(&f, 16, NULL);
	checks++;
	if (!f.same || f.intel)
		printf("FAIL Falcon: not as drawn\n"), fails++;
	{
		/* (copied as it is: the canvas's words in this machine's order) */
		uint16_t row[2] = { 0xf800, 0x1234 }, out[2];
		tc_row(&f, row, (uint8_t *) out, 2);
		checks++;
		if (memcmp(row, out, sizeof row) != 0)
			printf("FAIL Falcon: not copied as it is\n"), fails++;
	}

	/* NVDI's on the Falcon: green 5 bits from 6 (bit 5 the overlay's) */
	{
		static const short at[4] = { 11, 6, 0, 0 };
		info_of(info, 16, at, n555, 0, 0);
		tc_form(&f, 16, info);
		checks++;
		if (!f.same)
			printf("FAIL NVDI Falcon: not as drawn (%s)\n", tc_name(&f)), fails++;
	}
	/* NVDI's 5-6-5 on a card the other way round */
	{
		static const short at[4] = { 11, 5, 0, 0 };
		info_of(info, 16, at, n565, 0, 1);
		tc_form(&f, 16, info);
		check("5-6-5 Intel", &f, 0xf800, "00f8");
		check("5-6-5 Intel", &f, 0x1234, "3412");
		seen_back("5-6-5 Intel", &f, 16);
	}
	/* 15 bits, 0RRRRRGG GGGBBBBB: none and a card's 5-5-5 in 16 planes */
	tc_form(&f, 15, NULL);
	check("15", &f, 0xffff, "7fff");
	check("15", &f, 0xf800, "7c00");
	check("15", &f, 0x07e0, "03e0");
	check("15", &f, 0x001f, "001f");
	seen_back("15", &f, 15);
	{
		static const short at[4] = { 10, 5, 0, 0 };
		info_of(info, 16, at, n555, 0, 0);
		tc_form(&f, 16, info);
		check("5-5-5 in 16", &f, 0xf800, "7c00");
		check("5-5-5 in 16", &f, 0x0400, "0200");	/* green's top bit */
		seen_back("5-5-5 in 16", &f, 16);
	}
	/* 5-5-5 the other way round (a card on an Intel bus): green's bits
	   in both bytes as they lie */
	{
		static const short at[4] = { 10, 5, 0, 0 };
		info_of(info, 16, at, n555, 0, 1);
		tc_form(&f, 15, info);
		check("5-5-5 Intel", &f, 0x07e0, "e003");
		seen_back("5-5-5 Intel", &f, 15);
	}
	/* 24 bits, R G B */
	tc_form(&f, 24, NULL);
	check("24", &f, 0xf800, "ff0000");
	check("24", &f, 0x8410, "848284");	/* 10000 100000 10000: each part widened */
	check("24", &f, 0x0000, "000000");
	seen_back("24", &f, 24);
	/* 24 bits, B G R (Intel) */
	{
		static const short at[4] = { 16, 8, 0, 0 };
		info_of(info, 24, at, n888, 0, 1);
		tc_form(&f, 24, info);
		check("24 Intel", &f, 0xf800, "0000ff");
		seen_back("24 Intel (B G R)", &f, 24);
	}
	/* 32 bits with none: 00RRGGBB */
	tc_form(&f, 32, NULL);
	check("32", &f, 0xffff, "00ffffff");
	check("32", &f, 0x07e0, "0000ff00");
	/* fVDI's (ARAnyM): ARGB, alpha set; listed either way */
	for (down = 0; down < 2; down++) {
		static const short at[4] = { 16, 8, 0, 24 };
		info_of(info, 32, at, n8888, down, 0);
		tc_form(&f, 32, info);
		check(down ? "ARGB listed down" : "ARGB", &f, 0xf800, "ffff0000");
		check(down ? "ARGB listed down" : "ARGB", &f, 0x001f, "ff0000ff");
		check(down ? "ARGB listed down" : "ARGB", &f, 0x0000, "ff000000");
		seen_back("ARGB", &f, 32);
	}
	/* a Radeon's in Intel order: B G R A in memory */
	{
		static const short at[4] = { 16, 8, 0, 24 };
		info_of(info, 32, at, n8888, 0, 1);
		tc_form(&f, 32, info);
		check("BGRA", &f, 0xf800, "0000ffff");
		check("BGRA", &f, 0x07e0, "00ff00ff");
		seen_back("BGRA", &f, 32);
	}
	/* RGBA (red in the top byte) */
	{
		static const short at[4] = { 24, 16, 8, 0 };
		info_of(info, 32, at, n8888, 0, 0);
		tc_form(&f, 32, info);
		check("RGBA", &f, 0x001f, "0000ffff");
		seen_back("RGBA", &f, 32);
	}
	/* an answer that makes no sense (a bit twice): the usual */
	{
		static const short at[4] = { 16, 16, 0, 0 };
		info_of(info, 32, at, n888, 0, 0);
		tc_form(&f, 32, info);
		check("bad answer", &f, 0xf800, "00ff0000");
	}
	/* a row, several pixels */
	{
		uint16_t row[3] = { 0xf800, 0x07e0, 0x001f };
		uint8_t out[12];
		tc_form(&f, 24, NULL);
		tc_row(&f, row, out, 3);
		checks++;
		if (memcmp(out, "\xff\x00\x00\x00\xff\x00\x00\x00\xff", 9) != 0)
			printf("FAIL a row of 24\n"), fails++;
	}
	/* a row at 32 bits, 8 bits a colour as they are; 15 and 16 bits not drawn so */
	{
		static const uint8_t row[12] = { 0, 0x12, 0x34, 0x56, 0, 0xff, 0x01, 0x80, 0, 0, 0, 0 };
		static const short at[4] = { 8, 16, 24, 0 }, n[4] = { 8, 8, 8, 8 };
		uint8_t out[12];
		tc_form(&f, 24, NULL);
		tc_row32(&f, row, out, 3);
		checks++;
		if (!f.deep || memcmp(out, "\x12\x34\x56\xff\x01\x80\x00\x00\x00", 9) != 0)
			printf("FAIL a row of 24 from 32 bits\n"), fails++;
		info_of(info, 32, at, n, 0, 1);	/* (red at 8, alpha the lowest byte, Intel order: A R G B in memory) */
		tc_form(&f, 32, info);
		tc_row32(&f, row, out, 2);
		checks++;
		if (!f.deep || memcmp(out, "\xff\x12\x34\x56\xff\xff\x01\x80", 8) != 0)
			printf("FAIL a row of Intel ARGB from 32 bits (%s)\n", tc_name(&f)), fails++;
		tc_form(&f, 16, NULL);
		checks++;
		if (f.deep)
			printf("FAIL 16 bits drawn at 32\n"), fails++;
	}
	checks++;
	if (tc_form(&f, 8, NULL) || tc_form(&f, 12, NULL))
		printf("FAIL 8 or 12 planes taken as true colour\n"), fails++;
	printf("tctest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
