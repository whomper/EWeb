/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Content-Disposition values from the fuzz input (src/net/http.c's
 * http_disposition, RFC 6266 with RFC 8187's filename*), as a server
 * may send them: the name must come out whole UTF-8 that fits its room,
 * at every room from none to plenty (the first byte picks it).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "http.h"

/* is s whole, well-formed UTF-8 */
static int utf8(const unsigned char *s)
{
	while (*s) {
		int n = *s < 0x80 ? 0 : *s >= 0xc2 && *s <= 0xdf ? 1 : *s >= 0xe0 && *s <= 0xef ? 2 :
			*s >= 0xf0 && *s <= 0xf4 ? 3 : -1, k;
		if (n < 0)
			return 0;
		for (k = 1; k <= n; k++)
			if ((s[k] & 0xc0) != 0x80)
				return 0;
		s += n + 1;
	}
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static char v[8192];
	char *name;
	int size, t;
	if (n < 1 || n >= sizeof(v))
		return 0;
	size = 1 + d[0] % 64 * 5;
	memcpy(v, d + 1, n - 1);
	v[n - 1] = 0;
	name = malloc((size_t)size);	/* just its room: AddressSanitizer sees past it */
	if (name == NULL)
		return 0;
	t = http_disposition(v, name, size);
	if (t < -1 || t > 1 || strlen(name) >= (size_t)size || !utf8((const unsigned char *)name))
		abort();
	free(name);
	return 0;
}
