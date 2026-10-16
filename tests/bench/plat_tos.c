/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench on the Atari, plain TOS: the command line from the basepage
 * (NSBENCH.TTP), or from NSBENCH.CMD beside it when there is none (an
 * emulator's autostart passes none), files through GEMDOS, the 200 Hz timer, and output on the
 * console and, under Hatari, its NatFeats console too. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "plat.h"

#define MAXARG 40

int main(void)
{
	static char line[128];
	char *argv[MAXARG + 1], *s;
	unsigned char *bp = _BasePage;
	int argc = 0, len = bp[0x80], rc;

	memcpy(line, bp + 0x81, len);
	line[len] = 0;
	if (len == 0) {
		size_t n;
		char *cmd = load("NSBENCH.CMD", &n);
		if (cmd != NULL) {
			strncpy(line, cmd, sizeof line - 1);
			free(cmd);
		}
		for (s = line; *s; s++)
			if (*s == '\r' || *s == '\n')
				*s = ' ';
	}
	argv[argc++] = "nsbench";
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
	rc = nsbench(argc, argv);
	out("done (%d)\n", rc);
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
}

unsigned long ticks(void)
{
	return tos_hz200();
}
