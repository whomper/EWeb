/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat_tos.c - plat.h on TOS: files, folders and time on GEMDOS, and
 * the machine's timers for unpredictable bytes (TCP is common/'s
 * net_tos.c).
 */
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "../src/plat.h"
#include "bigbuf.h"

void (*pf_idle)(void);

int pf_open(const char *path, int mode)
{
	long h;
	if (mode == PF_WRITE)
		return (int)Fcreate(path, 0);
	if (mode == PF_UPDATE)
		return (int)Fopen(path, 2);	/* read and write: the TLS seed is read through it too */
	h = Fopen(path, mode == PF_APPEND ? 2 : 0);
	if (h < 0 && mode == PF_APPEND)
		h = Fcreate(path, 0);
	else if (h >= 0 && mode == PF_APPEND)
		Fseek(0, (short)h, 2);
	return (int)h;
}

/* Files and DMA. A Falcon's SCSI and ACSI disks work by DMA, and the DMA
   chip reaches ST-RAM only, while GEMDOS moves whole sectors straight
   between the disk and the caller's buffer. The family's rule (the
   guidelines' 6.5): where the hard disk driver says it bounces TT-RAM
   through ST-RAM itself (the _FRB cookie: HDDRIVER on Erez's Falcon,
   common's big_direct), TT-RAM goes to and from the disk as it is, in
   32 KB pieces; where it doesn't, each piece is copied through 32 KB of
   our own in ST-RAM (8 KB when there isn't that much), so nothing is
   left to a driver that might DMA where the DMA can't reach. */
#define PIECE (32L * 1024)
static char *bounce;			/* in ST-RAM; (char *)-1: none to be had */
static long bounce_len;
static int direct = -1;			/* big_direct(), asked once */

/* the ST-RAM buffer's size, 0 when there is none or none is needed (it
   is made the first time) */
long pf_bounce_size(void)
{
	if (direct < 0)
		direct = big_direct();
	if (direct)
		return 0;
	if (!bounce) {
		long b = (long)Mxalloc(PIECE, 0);	/* ST-RAM only */
		bounce_len = PIECE;
		if (b <= 0) {
			b = (long)Mxalloc(8192, 0);
			bounce_len = 8192;
		}
		bounce = b > 0 ? (char *)b : (char *)-1;
	}
	return bounce != (char *)-1 ? bounce_len : 0;
}

int pf_direct(void)
{
	pf_bounce_size();
	return direct;
}

static char *bounce_for(const void *buf, long n)
{
	if (n < 512 || (unsigned long)buf < 0x01000000UL || !pf_bounce_size())
		return 0;
	return bounce;
}

long pf_read(int h, void *buf, long n)
{
	char *b = bounce_for(buf, n);
	long done = 0, piece = b ? bounce_len : (unsigned long)buf >= 0x01000000UL ? PIECE : n;
	while (done < n) {
		long k = n - done < piece ? n - done : piece;
		long r = Fread((short)h, k, b ? b : (char *)buf + done);
		if (r < 0)
			return done ? done : r;
		if (b)
			memcpy((char *)buf + done, b, r);
		done += r;
		if (r < k)
			break;
	}
	return done;
}

long pf_write(int h, const void *buf, long n)
{
	char *b = bounce_for(buf, n);
	long done = 0, piece = b ? bounce_len : (unsigned long)buf >= 0x01000000UL ? PIECE : n;
	while (done < n) {
		long k = n - done < piece ? n - done : piece, w;
		if (b)
			memcpy(b, (const char *)buf + done, k);
		w = Fwrite((short)h, k, b ? b : (const char *)buf + done);
		if (w < 0)
			return done ? done : w;
		done += w;
		if (w < k)
			break;
	}
	return done;
}
void pf_close(int h) { Fclose((short)h); }
long pf_seek(int h, long off) { return Fseek(off, (short)h, 0); }

long pf_open_size(int h)
{
	long n = Fseek(0, (short)h, 2);
	return n >= 0 && Fseek(0, (short)h, 0) == 0 ? n : -1;
}

static DTA dta;

long pf_size(const char *path)
{
	void *old = Fgetdta();
	long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07) == 0 ? (long)dta.d_length : -1;
	Fsetdta(old);
	return r;
}

unsigned long pf_stamp(const char *path)
{
	void *old = Fgetdta();
	unsigned long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07) == 0 ? (unsigned long)dta.d_date << 16 | dta.d_time : 0;
	Fsetdta(old);
	return r;
}

unsigned long pf_info(const char *path, long *size)
{
	void *old = Fgetdta();
	unsigned long r = 0;
	Fsetdta(&dta);
	*size = -1;
	if (Fsfirst(path, 0x07) == 0) {
		r = (unsigned long)dta.d_date << 16 | dta.d_time;
		*size = (long)dta.d_length;
	}
	Fsetdta(old);
	return r;
}

int pf_exists(const char *path)
{
	void *old = Fgetdta();
	long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x17);
	Fsetdta(old);
	return r == 0;
}

int pf_mkdir(const char *path)
{
	long r = Dcreate(path);
	return (r == 0 || r == -36 || pf_exists(path)) ? 0 : (int)r;
}

int pf_remove(const char *path) { return (int)Fdelete(path); }
int pf_rename(const char *from, const char *to) { return (int)Frename(from, to); }

int pf_list(const char *dir, int (*cb)(const char *, long, void *), void *ud)
{
	char pat[200];
	DTA d;
	void *old = Fgetdta();
	long r;
	int n = 0;
	strncpy(pat, dir, sizeof(pat) - 6);
	pat[sizeof(pat) - 6] = 0;
	if (*pat && pat[strlen(pat) - 1] != '\\')
		strcat(pat, "\\");
	strcat(pat, "*.*");
	Fsetdta(&d);
	for (r = Fsfirst(pat, 0x01); r == 0; r = Fsnext()) {
		if (d.d_attrib & 0x18)
			continue;
		n++;
		if (cb(d.d_fname, (long)d.d_length, ud))
			break;
		Fsetdta(&d);	/* the callback may have used the DTA */
	}
	Fsetdta(old);
	return n;
}

void pf_now(PFTIME *t)
{
	unsigned short d = (unsigned short)Tgetdate();
	unsigned short tm = (unsigned short)Tgettime();
	t->year = 1980 + (d >> 9);
	t->mon = (d >> 5) & 15;
	t->day = d & 31;
	t->hour = tm >> 11;
	t->min = (tm >> 5) & 63;
	t->sec = (tm & 31) * 2;
}

unsigned long pf_ms(void)
{
	return tos_ms();	/* right even where timer C runs fast */
}

/* Timer jitter: the MFP's timer C counts every 26 us and the video
 * chip's line counter every 32 us; read in a loop whose length depends
 * on the previous readings, they drift against the CPU clock, the bus
 * and interrupts. Each byte folds in several readings. */
#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */
static unsigned char *ent_buf;
static int ent_n;

static void sup_entropy(void)
{
	volatile unsigned char *tcdr = (volatile unsigned char *)0xfffffa23L;
	volatile unsigned long *hz200 = (volatile unsigned long *)0x4baL;
	unsigned long acc = *hz200;
	int i, k;
	for (i = 0; i < ent_n; i++) {
		for (k = 0; k < 8; k++) {
			volatile int spin, j = (int)(acc & 31) + 8;
			for (spin = 0; spin < j; spin++)
				;
			acc = (acc << 5) ^ (acc >> 27) ^ *tcdr ^ (*hz200 << 3);
		}
		ent_buf[i] ^= (unsigned char)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24));
	}
}

void pf_entropy(unsigned char *buf, int n)
{
	int i;
	unsigned short d = (unsigned short)Tgetdate(), tm = (unsigned short)Tgettime();
	for (i = 0; i < n; i++)
		buf[i] = (unsigned char)(i * 151);
	buf[0] ^= (unsigned char)tm;
	buf[1] ^= (unsigned char)(tm >> 8);
	buf[2] ^= (unsigned char)d;
	buf[3] ^= (unsigned char)(d >> 8);
	ent_buf = buf;
	ent_n = n;
	Supexec(sup_entropy);
	/* XBIOS Random(), and where the stack happens to be */
	for (i = 0; i + 2 < n; i += 3) {
		unsigned long r = (unsigned long)trap14_w(17);
		buf[i] ^= (unsigned char)r;
		buf[i + 1] ^= (unsigned char)(r >> 8);
		buf[i + 2] ^= (unsigned char)(r >> 16);
	}
	buf[n - 1] ^= (unsigned char)((unsigned long)&i >> 3);
}

void pf_debug(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}
