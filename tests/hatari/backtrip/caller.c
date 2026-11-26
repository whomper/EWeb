/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The caller for tests/hatari/backtrip.py (the guidelines' 4.6, EWeb's
 * half), standing in for EMail: started without --resume, it starts
 * C:\EWEB.PRG with "about:bookmarks --back=C:\CALLER.PRG" and ends
 * (the rig changes CALLER to NOTHER in its bytes for a program that isn't
 * there); started again by EWeb with " --resume", it says so and ends.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;		/* a test program: NatFeats always */

int main(void)
{
	static char cmd[128], tail[128];
	static char out[128];
	const char *back = "about:bookmarks --back=C:\\CALLER.PRG";
	appl_init();
	shel_read(cmd, tail);
	if (strstr(tail + 1, "--resume") != NULL) {
		nf_debug("CALLER resumed\n");
	} else {
		out[0] = (char) strlen(back);
		strcpy(out + 1, back);
		nf_debug(shel_write(1, 1, 0, "C:\\EWEB.PRG", out) ? "CALLER started EWeb\n" :
			 "CALLER shel_write failed\n");
	}
	appl_exit();
	return 0;
}
