/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspjpeg.c - the Falcon's DSP56001 for pic.c's JPEGs: loads dsp/jpeg.a56
 * through the DSP's bootstrap and trades words with it through the host
 * port, as dspec.c does for the elliptic curves. The DSP is locked with
 * the XBIOS for the whole picture (a TLS handshake meanwhile does its
 * arithmetic on the 68030); a busy or missing DSP means the 68030 does
 * the picture.
 *
 * The host port is reached in supervisor mode: each MCU's words go in one
 * Supexec and its pixels come back in another, so the program's polls
 * between them run in user mode as ever (Super for the whole picture
 * would have to leave it at the very stack depth it entered it).
 *
 * A picture that ended cleanly leaves jpeg.a56 at its commands: the next
 * one asks it (PING, "JPG" back) rather than resetting the DSP and
 * loading it again; any other program there (TLS's) takes the PING for
 * nothing and doesn't answer.
 */
#include "tos.h"
#include "dspjpeg.h"
#include "../dsp/jpeg_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200   (*(volatile unsigned long *)0x4baL)
#define HISR    (*(volatile unsigned char *)0xffffa202L)
#define HTX32   (*(volatile unsigned long *)0xffffa204L)	/* 0, then the word's 3 bytes */
#define HTXH    (*(volatile unsigned char *)0xffffa205L)	/* the word's bytes */
#define HTXM    (*(volatile unsigned char *)0xffffa206L)
#define HTXL    (*(volatile unsigned char *)0xffffa207L)
#define HTX16   (*(volatile unsigned short *)0xffffa206L)	/* its low 2 */

long get_cookie(long id);

static int lock, alive;			/* alive: jpeg.a56 left at its commands */
/* a Supexec's work */
static const unsigned long *io_w;
static unsigned short *io_s;
static unsigned char *io_b;
static long io_n, io_pitch;
static int io_cols, io_rows;

int dspjpeg_present(void)
{
	return (get_cookie(0x5F534E44L) & 8) != 0;	/* _SND: a DSP56001 */
}

/* the rest in supervisor mode. A word waits at most a second: an MCU
   takes the DSP well under a millisecond */
static int wait_bit(int bit)
{
	unsigned long t0 = HZ200;
	while (!(HISR & bit))
		if (HZ200 - t0 > 200) {
			pic_dsp_failed = 1;
			return 0;
		}
	return 1;
}

static void sup_put(void)
{
	const unsigned long *w = io_w;
	long n = io_n;
	while (n-- > 0) {
		if (!(HISR & 2) && !wait_bit(2))
			return;
		HTX32 = *w++;
	}
}

static void sup_get16(void)
{
	unsigned short *d = io_s;
	int r, c;
	for (r = 0; r < io_rows; r++, d += io_pitch)
		for (c = 0; c < io_cols; c++) {
			if (!(HISR & 1) && !wait_bit(1))
				return;
			d[c] = HTX16;
		}
}

static void sup_get24(void)
{
	unsigned char *d = io_b, *o;
	int r, c;
	for (r = 0; r < io_rows; r++, d += io_pitch)
		for (c = 0, o = d; c < io_cols; c++) {
			/* its 3 bytes (a long from $A204 is 4 bus cycles, and
			   Hatari doesn't answer it) */
			if (!(HISR & 1) && !wait_bit(1))
				return;
			*o++ = HTXH;
			*o++ = HTXM;
			*o++ = HTXL;
		}
}

/* a moment, for the reset: reads of the host port, a quarter of a
   microsecond or more each, so 1 ms at least */
static void spin(void)
{
	int i;
	for (i = 0; i < 4000; i++)
		(void) HISR;
}

static void sup_reset(void)
{
	trap14_ww(30, 0x10);			/* Ongibit: hold the DSP in reset */
	spin();
	trap14_ww(29, ~0x10);			/* Offgibit: it starts its bootstrap */
	spin();
}

/* jpeg.a56 there? A PING, and "JPG" back within a millisecond */
static int io_alive;
static void sup_ping(void)
{
	int i;
	io_alive = 0;
	for (i = 0; i < 4000 && !(HISR & 2); i++)
		;
	if (!(HISR & 2))
		return;
	HTX32 = 5;
	for (i = 0; i < 4000 && !(HISR & 1); i++)
		;
	if (HISR & 1)
		io_alive = HTXH == 0x4A && HTXM == 0x50 && HTXL == 0x47;
}

static void put(const unsigned long *w, long n)
{
	if (pic_dsp_failed)
		return;
	io_w = w;
	io_n = n;
	Supexec(sup_put);
}

static void get16(unsigned short *d, int cols, int rows, long pitch)
{
	if (pic_dsp_failed)
		return;
	io_s = d;
	io_cols = cols;
	io_rows = rows;
	io_pitch = pitch;
	Supexec(sup_get16);
}

static void get24(unsigned char *d, int cols, int rows, long pitch)
{
	if (pic_dsp_failed)
		return;
	io_b = d;
	io_cols = cols;
	io_rows = rows;
	io_pitch = pitch;
	Supexec(sup_get24);
}

static int begin(void)
{
	lock = (int) trap14_w(104);		/* Dsp_Lock */
	if (lock == -1)
		return 0;			/* another program has it */
	pic_dsp_failed = 0;
	if (alive) {
		alive = 0;
		Supexec(sup_ping);
		if (io_alive)
			return 1;
	}
	/* reset (bit 4 of the sound chip's port A, through the XBIOS: see
	   dsprsa.c); then the bootstrap loads 512 words and starts them */
	Supexec(sup_reset);
	put(dsp_jpeg_prog, 512);
	return 1;
}

static void end(int clean)
{
	alive = clean && !pic_dsp_failed;
	if (lock == 0)
		trap14_w(105);			/* Dsp_Unlock */
}

const PIC_DSP dspjpeg_port = { begin, end, put, get16, get24 };
