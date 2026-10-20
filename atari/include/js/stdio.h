/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* stdio.h for QuickJS: the family's (strings only), and the few stream
   calls QuickJS's debugging dumps make, which go to EWeb's log
   (atari/js_libc.c) */
#ifndef QJS_STDIO_H
#define QJS_STDIO_H
#include_next <stdio.h>
#include <stdarg.h>
extern FILE *const js_stdout;
#define stdout js_stdout
#define stderr js_stdout
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int putchar(int c);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fflush(FILE *f);
#endif
