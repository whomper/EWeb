/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * twtest - Tailwind's utilities made by EWeb (src/browse/tw.c) held to
 * Tailwind's own: for each class in the cases (tests/browse/twcases.txt,
 * tools/mktw.py --cases: what Tailwind 3.4.17 makes of it, as EWeb keeps
 * it), the rules tw_sheet makes of it alone must be those, media query,
 * selector and declarations. Then a page's classes together: each once,
 * every @media closed, the base sheet first.
 *
 *   twtest CASES
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tw.h"

static int fails, checks;

static void want(const char *what, const char *cls, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("twtest: %s: %s\n", cls, what);
	}
}

#define MAXR 16

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *) a, *(char *const *) b);
}

/* tw_sheet's rules past the base sheet, as "media|selector{body}" */
static int made(const char *out, size_t base, char **r)
{
	const char *p = out + base;
	char media[128] = "";
	int n = 0;
	while (*p) {
		const char *e = strchr(p, '\n');
		size_t l = e ? (size_t) (e - p) : strlen(p);
		if (l > 7 && !strncmp(p, "@media ", 7)) {
			size_t q = l - 7 - 2;	/* (" {") */
			if (q >= sizeof media)
				q = sizeof media - 1;
			memcpy(media, p + 7, q);
			media[q] = 0;
		} else if (l == 1 && *p == '}') {
			media[0] = 0;
		} else if (l > 0 && n < MAXR) {
			r[n] = malloc(strlen(media) + l + 2);
			sprintf(r[n], "%s|%.*s", media, (int) l, p);
			n++;
		}
		p += l + (e != NULL);
	}
	return n;
}

static void check(const char *cls, char **want_r, int nw, size_t base)
{
	tw_name nm;
	size_t len;
	char *out, *got[MAXR];
	int ng, i, same;
	nm.s = cls;
	nm.len = strlen(cls);
	out = tw_sheet(&nm, 1, &len);
	if (out == NULL) {
		want("no sheet", cls, 0);
		return;
	}
	ng = made(out, base, got);
	qsort(got, (size_t) ng, sizeof *got, cmp_str);
	qsort(want_r, (size_t) nw, sizeof *want_r, cmp_str);
	same = ng == nw;
	for (i = 0; same && i < ng; i++)
		same = !strcmp(got[i], want_r[i]);
	if (!same) {
		want("not as Tailwind makes it", cls, 0);
		for (i = 0; i < nw; i++)
			printf("  want %s\n", want_r[i]);
		for (i = 0; i < ng; i++)
			printf("  made %s\n", got[i]);
	} else {
		want("", cls, 1);
	}
	for (i = 0; i < ng; i++)
		free(got[i]);
	free(out);
}

int main(int argc, char **argv)
{
	FILE *f;
	char line[2048], cls[256] = "", *rs[MAXR];
	int nr = 0, i, have = 0;
	size_t base, len;
	char *b;
	tw_name page[4];
	if (argc != 2 || (f = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "usage: twtest CASES\n");
		return 2;
	}
	b = tw_sheet(NULL, 0, &base);
	want("the base sheet alone", "(none)", b != NULL && base > 1000 && !strncmp(b, "*, ::before", 11));
	free(b);
	while (fgets(line, sizeof line, f) != NULL) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == 'C' && line[1] == '\t') {
			if (have)
				check(cls, rs, nr, base);
			for (i = 0; i < nr; i++)
				free(rs[i]);
			nr = 0;
			strncpy(cls, line + 2, sizeof cls - 1);
			cls[sizeof cls - 1] = 0;
			have = 1;
		} else if (line[0] == 'R' && line[1] == '\t' && !strstr(line, "-moz-") && nr < MAXR) {
			rs[nr++] = strdup(line + 2);
		}
	}
	if (have)
		check(cls, rs, nr, base);
	for (i = 0; i < nr; i++)
		free(rs[i]);
	fclose(f);
	/* a page's: repeated, and one EWeb doesn't know, passed over; the
	   unconditional rules before every @media, each @media closed */
	page[0].s = "md:hidden";
	page[0].len = 9;
	page[1].s = "flex";
	page[1].len = 4;
	page[2].s = "md:hidden";
	page[2].len = 9;
	page[3].s = "no-such-thing";
	page[3].len = 13;
	b = tw_sheet(page, 4, &len);
	{
		const char *one = b != NULL ? strstr(b + base, "md\\:hidden") : NULL;
		want("a page's classes", "(page)", one != NULL && strstr(one + 1, "md\\:hidden") == NULL &&
		     strstr(b + base, ".flex{display: flex}\n@media (min-width: 768px) {\n"
				      ".md\\:hidden{display: none}\n}\n") != NULL);
	}
	free(b);
	printf("twtest: %d of %d failed\n", fails, checks);
	return fails != 0;
}
