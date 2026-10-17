/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* EWGET.TTP on the Atari, plain TOS: the command line from the basepage,
 * or from EWGET.CMD beside it when there is none (an emulator's autostart
 * passes none), output on the console, in EWGET.LOG and, under Hatari, on
 * its NatFeats console too. Started from the desktop, it waits for a key
 * at the end, so the results stay on the screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "ewget.h"
#include "http.h"
#include "plat.h"

#define MAXARG 20

static long logh = -1;		/* EWGET.LOG, appended to */
static long cacr;

static void sup_cacr(void)
{
	__asm__ volatile("movec %%cacr,%0" : "=d"(cacr));
}

void out(const char *fmt, ...)
{
	char buf[1024], crlf[1100];
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
		Fwrite((short)logh, d - crlf, crlf);
}

int main(void)
{
	static char line[256];
	char *argv[MAXARG + 1], *s;
	unsigned char *bp = _BasePage;
	int argc = 0, len = bp[0x80], rc, from_cmd = 0;
	long cpu = get_cookie(0x5F435055L);	/* _CPU */
	memcpy(line, bp + 0x81, len);
	line[len] = 0;
	if (len == 0) {
		long n;
		char *cmd = pf_load("EWGET.CMD", &n);
		if (cmd) {
			strncpy(line, cmd, sizeof line - 1);
			free(cmd);
			from_cmd = 1;
		}
	}
	for (s = line; *s; s++)
		if (*s == '\r' || *s == '\n')
			*s = ' ';
	argv[argc++] = "ewget";
	for (s = line; *s && argc < MAXARG;) {
		while (*s == ' ')
			s++;
		if (!*s)
			break;
		argv[argc++] = s;
		while (*s && *s != ' ')
			s++;
		if (*s)
			*s++ = 0;
	}
	argv[argc] = NULL;
	logh = Fopen("EWGET.LOG", 1);
	if (logh >= 0)
		Fseek(0, (short)logh, 2);
	else
		logh = Fcreate("EWGET.LOG", 0);
	snprintf(http_user_agent, sizeof(http_user_agent), "Mozilla/5.0 (Atari Falcon030; 68030) EWeb/%s",
		 EWEB_VERSION);
	out("EWeb ewget %s\n", EWEB_VERSION);
	if (cpu >= 20) {
		Supexec(sup_cacr);
		out("CPU 680%02ld, caches: instructions %s, data %s; program in %s\n", cpu,
		    cacr & 0x0001 ? "on" : "off", cacr & 0x0100 ? "on" : "off",
		    (unsigned long)&main >= 0x01000000UL ? "TT-RAM" : "ST-RAM");
	}
	rc = ewget(argc, argv);
	out("done (%d)\n", rc);
	if (logh >= 0)
		Fclose((short)logh);
	if (!from_cmd) {
		Cconws("The results are in EWGET.LOG too. Press a key.\r\n");
		Bconin(2);
	}
	return rc;
}
