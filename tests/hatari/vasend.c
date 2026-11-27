/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * VASEND.PRG - a program that hands EWEB.ACC an address, for acc.py's
 * check of the accessory's VA_START and AC_CLOSE (DESIGN.md, The
 * accessory: EWEB.ACC). A test program, not a program of the family
 * (EWeb's tests run EWeb alone: the guidelines' 1.2): started at boot, it
 * finds "EWEB    " and sends it VA_START with C:\VAURL.TXT's first line,
 * then waits until C:\VAEND.TXT appears and ends, which under plain TOS
 * closes the accessory's windows (AC_CLOSE). It says each on the
 * emulator's console through NatFeats ("VASEND sent", "VASEND end").
 *
 *   make build/hatari/VASEND.PRG
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;		/* a test program: NatFeats always */

#define VA_START 0x4711

static char url[256];

static void say(const char *s)
{
	char line[300];
	strcpy(line, s);
	strcat(line, "\n");
	nf_debug(line);
}

int main(void)
{
	short msg[8], id = appl_init(), ew = -1, n;
	long h;
	if (id < 0)
		return 1;
	for (n = 0; n < 600 && (ew = appl_find("EWEB    ")) < 0; n++)
		evnt_timer_(100);
	h = Fopen("C:\\VAURL.TXT", 0);
	if (h >= 0) {
		long k = Fread((short) h, sizeof url - 1, url);
		Fclose((short) h);
		url[k > 0 ? k : 0] = 0;
		url[strcspn(url, "\r\n")] = 0;
	}
	evnt_timer_(3000);
	if (ew >= 0) {
		memset(msg, 0, sizeof msg);
		msg[0] = VA_START;
		msg[1] = id;
		msg[3] = (short) ((unsigned long) url >> 16);
		msg[4] = (short) (unsigned long) url;
		appl_write(ew, 16, msg);
		say("VASEND sent");
	} else {
		say("VASEND no EWEB");
	}
	while (Fattrib("C:\\VAEND.TXT", 0, 0) < 0)
		evnt_timer_(250);
	say("VASEND end");
	appl_exit();
	return 0;
}
