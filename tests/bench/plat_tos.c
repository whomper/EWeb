/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench on the Atari, plain TOS: the command line from the basepage
 * (NSBENCH.TTP), or from NSBENCH.CMD beside it when there is none (an
 * emulator's autostart passes none), files through GEMDOS, the 200 Hz
 * timer, and output on the console, in NSBENCH.LOG and, under Hatari, on
 * its NatFeats console too. Started from the desktop, it waits for a key
 * at the end, so the results stay on the screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "plat.h"

#define MAXARG 40

static long logh = -1;		/* NSBENCH.LOG, appended to */

static void log_open(void)
{
	logh = Fopen("NSBENCH.LOG", 1);
	if (logh >= 0)
		Fseek(0, (short) logh, 2);
	else
		logh = Fcreate("NSBENCH.LOG", 0);
}

int main(void)
{
	static char line[128];
	char *argv[MAXARG + 1], *s;
	unsigned char *bp = _BasePage;
	int argc = 0, len = bp[0x80], rc, from_cmd = 0;

	memcpy(line, bp + 0x81, len);
	line[len] = 0;
	if (len == 0) {
		size_t n;
		char *cmd = load("NSBENCH.CMD", &n);
		if (cmd != NULL) {
			strncpy(line, cmd, sizeof line - 1);
			free(cmd);
			from_cmd = 1;
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
	log_open();
	out("EWeb nsbench %s\n", EWEB_VERSION);
	rc = nsbench(argc, argv);
	out("done (%d)\n", rc);
	if (logh >= 0)
		Fclose((short) logh);
	if (!from_cmd) {
		Cconws("The results are in NSBENCH.LOG too. Press a key.\r\n");
		Bconin(2);
	}
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
	if (logh >= 0)
		Fwrite((short) logh, d - crlf, crlf);
}

unsigned long ticks(void)
{
	return tos_hz200();
}
