/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * jsbench - a page's scripts run as EWEB.PRG runs them (src/js: the
 * runtime, its prelude, the scripts, DOMContentLoaded and load), for
 * counting the 68030 instructions that takes (tools/count68k.sh,
 * tools/prof68k.py):
 *
 *   jsbench [-n] [-r N] PAGE.HTM
 *
 * -n makes the runtime and nothing more (an empty page's cost); -r N
 * does it N times. Scripts from files aren't fetched.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "html.h"
#include "js.h"

char http_user_agent[128] = "EWeb (Atari Falcon030)";

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
	if (d == NULL || fread(d, 1, (size_t) len, f) != (size_t) len) {
		fclose(f);
		free(d);
		return NULL;
	}
	fclose(f);
	d[len] = 0;
	*n = (size_t) len;
	return d;
}

static void resolve(void *ud, const char *base, const char *href, char *out, int size)
{
	(void) ud, (void) base;
	snprintf(out, (size_t) size, "%s", href);
}

static void say(void *ud, const char *line)
{
	(void) ud;
	printf("%s\n", line);
}

int main(int argc, char **argv)
{
	static const char empty[] = "<!DOCTYPE html><title>x</title><body onload=''>";
	const ec_media m = { 608, 400, 16 };
	js_host h;
	int i = 1, k, reps = 1, none = 0;
	size_t n;
	char *data;
	for (; i < argc && argv[i][0] == '-'; i++)
		if (!strcmp(argv[i], "-r") && i + 1 < argc)
			reps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-n"))
			none = 1;
	if (!none && i >= argc) {
		fprintf(stderr, "jsbench [-n] [-r N] PAGE.HTM\n");
		return 2;
	}
	data = none ? (char *) empty : slurp(argv[i], &n);
	if (data == NULL)
		return 1;
	if (none)
		n = sizeof empty - 1;
	memset(&h, 0, sizeof h);
	h.resolve = resolve;
	h.log = say;
	h.console = say;
	for (k = 0; k < reps; k++) {
		html_opts o;
		doc *d;
		int r;
		memset(&o, 0, sizeof o);
		o.scripting = 1;
		d = doc_parse_opts(data, n, &o, &m);
		if (d == NULL)
			return 1;
		doc_add_ua(d);
		r = js_page(d, "file:///page.htm", "file:///page.htm", &h);
		if (k == 0)
			printf("js_page %d, title \"%s\"\n", r, d->title);
		doc_free(d);
	}
	return 0;
}
