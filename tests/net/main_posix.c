/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* ewget on this computer: output on stdout. */
#include <stdarg.h>
#include <stdio.h>

#include "ewget.h"
#include "http.h"

void out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	snprintf(http_user_agent, sizeof(http_user_agent), "Mozilla/5.0 (Atari Falcon030; 68030) EWeb/%s",
		 EWEB_VERSION);
	return ewget(argc, argv);
}
