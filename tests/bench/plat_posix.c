/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench on Linux: this computer, or the 68030 under QEMU. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "plat.h"

int main(int argc, char **argv)
{
	return nsbench(argc, argv);
}

void plat_init(void)
{
}

char *load(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long n;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n + 1);
	if (buf == NULL || fread(buf, 1, n, f) != (size_t) n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[n] = 0;
	fclose(f);
	*len = n;
	return buf;
}

void out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

unsigned long ticks(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long) ts.tv_sec * 1000000UL + ts.tv_nsec / 1000;
}
