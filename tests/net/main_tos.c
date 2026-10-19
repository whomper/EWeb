/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* EWGET.TTP on the Atari, plain TOS: the command line from the basepage
 * (or the environment's ARGV), or from EWGET.CMD beside it when there is
 * none (an emulator's autostart passes none, and the desktop's box is
 * short), or from a .CMD file dropped on it; output on the console, in
 * EWGET.LOG and, under Hatari, on its NatFeats console too. Started from
 * the desktop, it waits for a key at the end, so the results stay on the
 * screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tos.h>

#include "ewget.h"
#include "http.h"
#include "plat.h"
#include "util.h"
#include "crash.h"

long pf_bounce_size(void);	/* plat_tos.c */
int pf_direct(void);

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
		pf_write((int)logh, crlf, d - crlf);
}

/* the ARGV convention (MiNT and some desktops): a command line of length
   127, and "ARGV=" in the environment, then argv[0], argv[1]... as
   strings, an empty one last */
static const char *argv_env(const unsigned char *bp)
{
	const char *e = *(const char *const *)(bp + 0x2c);
	if (!e || bp[0x80] != 127)
		return 0;
	while (*e) {
		if (!strncmp(e, "ARGV=", 5))
			return e + strlen(e) + 1;
		e += strlen(e) + 1;
	}
	return 0;
}

static int ends_cmd(const char *s)
{
	size_t n = strlen(s);
	return n > 4 && s[n - 4] == '.' && (s[n - 3] | 32) == 'c' && (s[n - 2] | 32) == 'm' &&
	       (s[n - 1] | 32) == 'd';
}

/* the words of line, which is changed, into argv[1...]; returns argc */
static int split(char *line, char **argv)
{
	int argc = 1;
	char *s;
	for (s = line; *s; s++)
		if (*s == '\r' || *s == '\n' || *s == '\t')
			*s = ' ';
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
	return argc;
}

int main(void)
{
	static char line[256], cmdline[512], name[160], dir[160];
	char *argv[MAXARG + 1];
	unsigned char *bp = _BasePage;
	const char *av = argv_env(bp);
	int argc, len = bp[0x80], rc, k, from_cmd = 0, unread = 0, missing = 0;
	long cpu = get_cookie(0x5F435055L);	/* _CPU */
	pf_size_open = pf_open_size;	/* a file read: opened, not looked up first (6.5) */
	dir[0] = 0;
	if (av) {
		/* the program's folder from argv[0]; the arguments after it */
		const char *slash = strrchr(av, '\\');
		if (slash && slash - av < (long)sizeof(dir) - 1) {
			memcpy(dir, av, slash - av + 1);
			dir[slash - av + 1] = 0;
		}
		line[0] = 0;
		for (av += strlen(av) + 1; *av; av += strlen(av) + 1)
			if (strlen(line) + strlen(av) + 2 < sizeof(line)) {
				strcat(line, " ");
				strcat(line, av);
			}
	} else {
		if (len > 125)
			len = 125;
		memcpy(line, bp + 0x81, len);
		line[len] = 0;
	}
	argv[0] = "ewget";
	argc = split(line, argv);
	/* nothing typed (the desktop may pass a space), or a .CMD file
	   dropped on the program: the command line is in the file; and
	   EWGET.CMD may name another .CMD file */
	for (k = 0; k < 2 && (argc == 1 || (argc == 2 && ends_cmd(argv[1]))); k++) {
		long n;
		char *cmd = 0;
		if (argc == 2) {
			str_copy(name, argv[1], sizeof(name));
			cmd = pf_load(name, &n);
		} else if (k == 0) {
			if (dir[0]) {
				snprintf(name, sizeof(name), "%sEWGET.CMD", dir);
				cmd = pf_load(name, &n);
			}
			if (!cmd) {
				str_copy(name, "EWGET.CMD", sizeof(name));
				cmd = pf_load(name, &n);
			}
		} else {
			break;			/* an empty .CMD file */
		}
		if (cmd) {
			strncpy(cmdline, cmd, sizeof cmdline - 1);
			free(cmd);
			from_cmd = 1;
			argc = split(cmdline, argv);
		} else if (argc == 2) {
			unread = 1;
			argc = 1;		/* not an address: the usage */
		} else {
			/* say where it was looked for */
			int d = (int)Dgetdrv();
			name[0] = (char)('A' + d);
			name[1] = ':';
			if (Dgetpath(name + 2, d + 1) < 0)
				name[2] = 0;
			strcat(name, "\\");
			missing = 1;
		}
	}
	argv[argc] = NULL;
	/* a crash in EWGET writes CRASH.LOG and ends it cleanly; one in the
	   system, or after EWGET ended, is reported from TOS's record the
	   next time */
	crash_install("", "EWGET", EWEB_VERSION);
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
	if (pf_direct())
		out("files: to and from TT-RAM as they are, 32 KB at a time (_FRB cookie set: the disk driver bounces them)\n");
	else if (pf_bounce_size())
		out("files: to and from TT-RAM through EWGET's %ld KB in ST-RAM (no _FRB cookie)\n", pf_bounce_size() / 1024);
	else
		out("files: straight (no ST-RAM for a buffer, no _FRB cookie)\n");
	if (crash_previous())
		out("the last run ended with %s in EWGET: see CRASH.LOG\n", crash_previous());
	else if (crash_log()[0] && pf_size(crash_log()) > 0)
		out("there is a CRASH.LOG from an earlier run\n");
	if (unread)
		out("can't read %s\n", name);
	else if (missing)
		out("nothing typed, and no EWGET.CMD in %s\n", name);
	else if (from_cmd)
		out("command line from %s\n", name);
	rc = ewget(argc, argv);
	out("done (%d)\n", rc);
	if (logh >= 0)
		Fclose((short)logh);
	crash_remove();
	if (!from_cmd) {
		Cconws("The results are in EWGET.LOG too. Press a key.\r\n");
		Bconin(2);
	}
	return rc;
}
