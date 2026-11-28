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
 *   import N S TEXT           a Netscape cookies.txt (\t, \n) imported:
 *                             N lines taken, for S sites
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
			ck_max_bytes = 0;
			t_now = 1760000000LL;
		} else if (!strcmp(a, "now")) {
			t_now = atoll(b);
		} else if (!strcmp(a, "import")) {
			/* import N SITES TEXT: a cookies.txt (\t a tab, \n a line's
			   end) through ck_import, N lines taken for SITES sites */
			long want_n = strtol(b, &c, 10), want_s = strtol(c, &c, 10), k = 0;
			int got_s = 0, got_n;
			static char text[4096];
			if (*c == ' ')
				c++;
			for (; *c && k < (long) sizeof text - 1; c++)
				if (c[0] == '\\' && c[1] == 't')
					text[k++] = '\t', c++;
				else if (c[0] == '\\' && c[1] == 'n')
					text[k++] = '\n', c++;
				else
					text[k++] = *c;
			text[k] = 0;
			{
				/* in pieces of a line each, as ui/ckimport.c reads a
				   file: the counts are the whole's */
				long at = 0;
				got_n = 0;
				ck_import_start();
				while (at < k) {
					char *e = memchr(text + at, '\n', (size_t) (k - at));
					long l = e != NULL ? (long) (e - (text + at)) + 1 : k - at;
					got_n += ck_import(text + at, l, &got_s);
					at += l;
				}
			}
			if (got_n != want_n || got_s != want_s) {
				fprintf(stderr, "FAIL %s: import took %d for %d sites, not %ld for %ld\n", name, got_n,
					got_s, want_n, want_s);
				failed++;
				ok = 0;
			}
		} else if (!strcmp(a, "importmany")) {
			/* importmany N: a cookie on each of N sites, in pieces of
			   100 lines: N taken for N sites (1.29's count stopped at 64) */
			long want = strtol(b, NULL, 10), i, at = 0;
			int got_s = 0, got_n = 0;
			static char text[200000];
			ck_import_start();
			for (i = 0; i < want; i++) {
				at += snprintf(text + at, sizeof text - (size_t) at,
					       ".site%ld.com\tTRUE\t/\tFALSE\t1800000000\ts\t%ld\n", i, i);
				if (i % 100 == 99 || i == want - 1) {
					got_n += ck_import(text, at, &got_s);
					at = 0;
				}
			}
			if (got_n != want || got_s != want) {
				fprintf(stderr, "FAIL %s: importmany took %d for %d sites, not %ld\n", name, got_n, got_s, want);
				failed++;
				ok = 0;
			}
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
		} else if (!strcmp(a, "reload=")) {
			/* reload, and the text written out after it the text
			   before: values not used since are written as they were
			   read, not decrypted and encrypted again (a fresh nonce
			   would change them) */
			long len, len2;
			char *t = ck_text(&len), *t2;
			ck_clear();
			ck_load(t, len);
			t2 = ck_text(&len2);
			if (len != len2 || memcmp(t, t2, (size_t) len) != 0) {
				fprintf(stderr, "FAIL %s: the text written out changed over a reload\n", name);
				failed++;
				ok = 0;
			}
			free(t);
			free(t2);
		} else if (!strcmp(a, "jar")) {
			ck_max_bytes = atol(b) * 1024L;	/* jar KB: the cookie jar's size, 0 none */
		} else if (!strcmp(a, "bytes<=")) {
			if (ck_bytes() > atol(b)) {
				fprintf(stderr, "FAIL %s: the store is %ld bytes, more than %s\n", name, ck_bytes(), b);
				failed++;
				ok = 0;
			}
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
