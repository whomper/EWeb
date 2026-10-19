/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * docparse - a page made into EWeb's document (src/doc: Hubbub's parse
 * into EWeb's own nodes, as EWEB.PRG does it), for counting the 68030
 * instructions that takes (tools/count68k.sh, tools/prof68k.py):
 *
 *   docparse [-s] [-r N] PAGE.HTM...
 *
 * -s styles it too (the UA sheet and the page's <style>s, as EWEB.PRG
 * without fetching linked sheets); -r N does each page N times. Prints
 * each page's elements.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	char *d;
	long len;
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc((size_t) len + 1);
	if (d != NULL && fread(d, 1, (size_t) len, f) != (size_t) len) {
		free(d);
		d = NULL;
	}
	fclose(f);
	*n = (size_t) len;
	return d;
}

static void sheet(void *ud, int is_link, const char *s, size_t len)
{
	if (!is_link)
		ec_sheet(((doc *) ud)->css, s, len, EC_ORIGIN_AUTHOR);
}

int main(int argc, char **argv)
{
	ec_media m = { 640, 400, 16 };
	int i = 1, style = 0, reps = 1, r;
	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-s"))
			style = 1;
		else if (!strcmp(argv[i], "-r") && i + 1 < argc)
			reps = atoi(argv[++i]);
	}
	for (; i < argc; i++) {
		size_t n;
		char *data = slurp(argv[i], &n);
		if (data == NULL) {
			fprintf(stderr, "docparse: can't read %s\n", argv[i]);
			return 1;
		}
		for (r = 0; r < reps; r++) {
			doc *d = doc_parse(data, n, NULL, &m);
			if (d == NULL)
				return 1;
			if (style) {
				doc_add_ua(d);
				doc_sheets(d, sheet, d);
				doc_select(d);
			}
			if (r == reps - 1)
				printf("%s: %lu elements\n", argv[i], (unsigned long) d->elements);
			doc_free(d);
		}
		free(data);
	}
	return 0;
}
