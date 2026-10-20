/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cookietest - EWeb's cookies (src/net/cookie.c) against cases worked
 * out from RFC 6265bis: cookietest CASES.TXT. Each case starts with the
 * store empty and the time at 1760000000 (9 Oct 2025); its lines:
 *
 *   now T                     the time (UTC seconds)
 *   set URL LINE              a Set-Cookie line from the answer to URL,
 *                             a page opened by itself
 *   set3 URL SITE LINE        from a picture of the page SITE
 *   get URL WANT              the Cookie header for URL, a page typed
 *                             ("-": none)
 *   getx URL SITE TOP SAFE WANT   for URL found on SITE: a page (TOP 1)
 *                             or a picture (0), GET (SAFE 1) or POST
 *   count N                   cookies kept
 *   reload                    the lasting ones written out as text, the
 *                             store emptied and read back from it
 *   same A B 0|1              ck_same_site
 *   key FILE                  the values in the text encrypted with the
 *                             key in FILE (made when it isn't there)
 *   hidden TEXT               TEXT isn't in the text written out
 *
 * Prints each failure, then how many cases passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cookie.h"

static long long t_now;

static long long fake_now(void)
{
	return t_now;
}

int main(int argc, char **argv)
{
	FILE *f;
	static char line[8192], name[256];
	int failed = 0, passed = 0, ok = 1, have = 0;

	if (argc != 2 || (f = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "cookietest CASES.TXT\n");
		return 2;
	}
	ck_now = fake_now;
	ck_file(NULL, NULL);
	while (fgets(line, sizeof line, f) != NULL) {
		char *a = line, *b, *c;
		URL u;
		line[strcspn(line, "\n")] = 0;
		if (line[0] == '#' || line[0] == 0)
			continue;
		b = strchr(a, ' ');
		if (b != NULL)
			*b++ = 0;
		else
			b = a + strlen(a);
		if (!strcmp(a, "case")) {
			if (have && ok)
				passed++;
			have = 1;
			ok = 1;
			snprintf(name, sizeof name, "%s", b);
			ck_clear();
			ck_file(NULL, NULL);
			t_now = 1760000000LL;
		} else if (!strcmp(a, "now")) {
			t_now = atoll(b);
		} else if (!strcmp(a, "set") || !strcmp(a, "set3")) {
			CKCTX x = { 1, 1, NULL, 0 };
			char *site = NULL;
			c = strchr(b, ' ');
			*c++ = 0;
			if (a[3] == '3') {
				site = c;
				c = strchr(c, ' ');
				*c++ = 0;
				x.top = 0;
				x.site = site;
			}
			if (!url_parse(&u, b, NULL)) {
				fprintf(stderr, "FAIL %s: not a URL %s\n", name, b);
				failed++;
				ok = 0;
				continue;
			}
			t_now++;		/* each cookie made a second after the one before */
			ck_set(&u, c, &x);
		} else if (!strcmp(a, "get") || !strcmp(a, "getx")) {
			CKCTX x = { 1, 1, NULL, 0 };
			char *want;
			SBUF got;
			c = strchr(b, ' ');
			*c++ = 0;
			want = c;
			if (a[3] == 'x') {
				char *s2 = strtok(c, " "), *top = strtok(NULL, " "), *safe = strtok(NULL, " ");
				want = safe + strlen(safe) + 1;
				x.site = s2;
				x.top = atoi(top);
				x.safe = atoi(safe);
			}
			if (!strcmp(want, "-"))
				want = "";
			url_parse(&u, b, NULL);
			sb_init(&got);
			ck_get(&u, &x, &got);
			if (strcmp(got.s ? got.s : "", want) != 0) {
				fprintf(stderr, "FAIL %s: %s %s\n  want: %s\n  got:  %s\n", name, a, b, want,
					got.s ? got.s : "");
				failed++;
				ok = 0;
			}
			sb_free(&got);
		} else if (!strcmp(a, "count")) {
			if (ck_count() != atol(b)) {
				fprintf(stderr, "FAIL %s: count %ld, want %s\n", name, ck_count(), b);
				failed++;
				ok = 0;
			}
		} else if (!strcmp(a, "reload")) {
			long len;
			char *t = ck_text(&len);
			ck_clear();
			ck_load(t, len);
			free(t);
		} else if (!strcmp(a, "key")) {
			ck_file(NULL, b);
		} else if (!strcmp(a, "hidden")) {
			long len;
			char *t = ck_text(&len);
			if (strstr(t, b) != NULL) {
				fprintf(stderr, "FAIL %s: \"%s\" in the text written out\n", name, b);
				failed++;
				ok = 0;
			}
			free(t);
		} else if (!strcmp(a, "same")) {
			char *h2;
			c = strchr(b, ' ');
			*c++ = 0;
			h2 = c;
			c = strchr(c, ' ');
			*c++ = 0;
			if (ck_same_site(b, h2) != atoi(c)) {
				fprintf(stderr, "FAIL %s: same %s %s, want %s\n", name, b, h2, c);
				failed++;
				ok = 0;
			}
		} else {
			fprintf(stderr, "cookietest: what is \"%s\"?\n", a);
			return 2;
		}
	}
	if (have && ok)
		passed++;
	fclose(f);
	ck_clear();
	fprintf(stderr, "cookies: %d cases passed, %d checks failed\n", passed, failed);
	return failed != 0;
}
