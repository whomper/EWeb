/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat.h - what EWeb's core needs from the machine: files, folders,
 * the clock, a millisecond timer and TCP. Implemented for TOS in
 * atari/plat_tos.c and for Linux in tests/host/plat_posix.c (the tests
 * and ewget).
 */
#ifndef PLAT_H
#define PLAT_H

#include <stddef.h>

#ifdef EW_TOS
#define PF_SEP '\\'
#else
#define PF_SEP '/'
#endif

/* ---- files ---- */
#define PF_READ   0
#define PF_WRITE  1	/* create or truncate */
#define PF_APPEND 2
#define PF_UPDATE 3	/* an existing file, read and written over in place, not truncated */

int  pf_open(const char *path, int mode);	/* handle >= 0, < 0 on error */
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_seek(int h, long off);			/* from the start; new position or < 0 */
long pf_size(const char *path);			/* -1 if missing */
unsigned long pf_stamp(const char *path);	/* when it was last written (date, time); 0 if missing */
unsigned long pf_info(const char *path, long *size);	/* both at once: pf_stamp, and pf_size in *size */
long pf_open_size(int h);			/* an open file's size, its position left at the start; < 0 on error */
extern long (*pf_size_open)(int h);		/* common's futil.c: set to pf_open_size, pf_load opens
						   first and doesn't look the file up (6.5) */
int  pf_exists(const char *path);
int  pf_mkdir(const char *path);		/* 0 ok or already there */
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
/* call cb for every plain file in dir; stop early if cb returns non-zero */
int  pf_list(const char *dir, int (*cb)(const char *name, long size, void *ud), void *ud);

/* whole-file helpers: pf_load returns a malloc'ed NUL-terminated copy */
char *pf_load(const char *path, long *len);
int   pf_save(const char *path, const void *data, long len);

/* ---- time ---- */
typedef struct {
	short year, mon, day, hour, min, sec;
} PFTIME;

void pf_now(PFTIME *t);		/* local wall clock */
unsigned long pf_ms(void);	/* monotonic milliseconds (5 ms steps on TOS) */
void pf_debug(const char *s);	/* emulator console / stderr */
/* raw unpredictable bytes (timer jitter on the Atari, /dev/urandom on
   Linux); tls.c hashes them with a seed kept on disk */
void pf_entropy(unsigned char *buf, int n);

/* ---- TCP ---- */
int  net_init(void);			/* 1 if a TCP/IP stack is present */
const char *net_stack(void);		/* "STinG", "MiNTnet", "POSIX" ... */
/* connect (resolving names); handle >= 0, or < 0 with err filled in */
int  net_open(const char *host, unsigned short port, char *err, int errlen);
long net_write(int h, const void *buf, long n);	/* bytes sent or < 0 */
/* non-blocking: > 0 bytes read, 0 nothing yet, < 0 closed or failed */
long net_read(int h, void *buf, long max);
void net_close(int h);
/* close once what was sent has gone out, waiting at most this long */
void net_close_wait(int h, int seconds);

/* the UI can show progress and keep the screen alive while we wait */
extern void (*pf_idle)(void);

#endif
