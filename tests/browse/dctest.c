/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dctest - EWeb's disk cache (src/browse/dcache.c) on a clock of its
 * own: how long HTTP lets an answer stay fresh (max-age, Expires and
 * Date, a tenth of the time since Last-Modified, Age, no-cache), what it
 * doesn't let be kept (no-store, Vary: *, a cookie set, too big), asking
 * again (ETag, Last-Modified) and a 304, the pack and the index read at
 * the next start, a reset between a job and quit, the pack written again
 * when it grew too big, and a pack that isn't the index's.
 *
 *   dctest DIR      (a folder it may make CACHE in)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcache.h"
#include "plat.h"

static int fails, checks;
static long long clock_s = 1792400000LL;	/* Oct 2026 */

static long long fake_now(void)
{
	return clock_s;
}

static void want(const char *what, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("dctest: %s failed\n", what);
	}
}

/* the answer for key: 1 fresh, 0 stale, -1 none; its bytes must be data */
static int got(const char *key, const char *data, char *validate)
{
	DC_GOT g;
	int r;
	if (!dc_get(key, &g))
		return -1;
	r = data == NULL || ((long) strlen(data) == g.len && memcmp(g.data, data, (size_t) g.len) == 0) ? g.fresh : -2;
	if (validate != NULL)
		strcpy(validate, g.validate);
	free(g.data);
	return r;
}

static void put(const char *key, const char *headers, const char *data)
{
	dc_put(key, key, headers, "text/html", "utf-8", strncmp(key, "https", 5) ? "" : "TLS 1.2, test",
	       data, (long) strlen(data));
}

int main(int argc, char **argv)
{
	char dir[400], path[500], v[256];
	long bytes;
	int i;

	if (argc < 2) {
		fprintf(stderr, "dctest DIR\n");
		return 2;
	}
	snprintf(dir, sizeof dir, "%s/CACHE", argv[1]);
	snprintf(path, sizeof path, "%s/EWEB.PAK", dir);
	pf_remove(path);
	snprintf(path, sizeof path, "%s/EWEB.IDX", dir);
	pf_remove(path);
	dc_now = fake_now;
	dc_dir(dir, 1L << 20);

	/* freshness */
	put("http://a/1", "Cache-Control: max-age=60\n", "one");
	want("max-age: fresh", got("http://a/1", "one", NULL) == 1);
	clock_s += 61;
	want("max-age: stale after", got("http://a/1", "one", v) == 0 && v[0] == 0);
	put("http://a/2", "Date: Mon, 19 Oct 2026 10:00:00 GMT\nExpires: Mon, 19 Oct 2026 11:00:00 GMT\n", "two");
	clock_s += 3599;
	want("Expires less Date", got("http://a/2", "two", NULL) == 1);
	clock_s += 2;
	want("Expires: stale after", got("http://a/2", "two", NULL) == 0);
	put("http://a/3", "Date: Mon, 19 Oct 2026 10:00:00 GMT\nLast-Modified: Thu, 09 Oct 2025 10:00:00 GMT\n"
	    "ETag: \"x3\"\n", "three");
	clock_s += 86399;
	want("a tenth since Last-Modified, a day at most", got("http://a/3", "three", v) == 1);
	clock_s += 2;
	want("then asked with its ETag", got("http://a/3", "three", v) == 0 &&
	     strcmp(v, "If-None-Match: \"x3\"\r\n") == 0);
	put("http://a/4", "Cache-Control: max-age=100\nAge: 90\n", "four");
	clock_s += 11;
	want("less its Age", got("http://a/4", "four", NULL) == 0);
	put("http://a/5", "Cache-Control: no-cache, max-age=500\nLast-Modified: Thu, 09 Oct 2025 10:00:00 GMT\n", "five");
	want("no-cache: asked each time, by date", got("http://a/5", "five", v) == 0 &&
	     strcmp(v, "If-Modified-Since: Thu, 09 Oct 2025 10:00:00 GMT\r\n") == 0);
	dc_fresh("http://a/5", "Cache-Control: max-age=30\n");
	want("a 304 makes it fresh", got("http://a/5", "five", NULL) == 1);

	/* not kept */
	put("http://b/1", "Cache-Control: no-store, max-age=60\n", "x");
	put("http://b/2", "Cache-Control: max-age=60\nVary: *\n", "x");
	put("http://b/3", "Cache-Control: max-age=60\nSet-Cookie: id=1\n", "x");
	put("http://b/4", "Content-Type: text/html\n", "x");
	want("no-store, Vary *, a cookie, nothing to go by: not kept",
	     got("http://b/1", NULL, NULL) == -1 && got("http://b/2", NULL, NULL) == -1 &&
	     got("http://b/3", NULL, NULL) == -1 && got("http://b/4", NULL, NULL) == -1);
	{
		char *big = malloc((1L << 17) + 2);
		memset(big, 'b', (1L << 17) + 1);
		big[(1L << 17) + 1] = 0;
		put("http://b/5", "Cache-Control: max-age=60\n", big);
		want("an eighth of the cache: not kept", got("http://b/5", NULL, NULL) == -1);
		free(big);
	}
	put("http://a/1", "Cache-Control: no-store\n", "x");
	want("no-store forgets what was kept", got("http://a/1", NULL, NULL) == -1);

	/* the next start: the pack and the index */
	dc_job_end();
	put("http://c/1", "Cache-Control: max-age=1000\n", "kept in memory, then the pack at quit");
	put("https://c/2", "Cache-Control: max-age=1000\n", "secure");
	dc_close();
	dc_dir(dir, 1L << 20);
	want("the next start", got("http://a/3", "three", NULL) == 0 && got("http://a/5", "five", NULL) == 1 &&
	     got("http://c/1", "kept in memory, then the pack at quit", NULL) == 1 && dc_count(&bytes) == 6);
	{
		DC_GOT g;
		want("how it came, kept", dc_get("https://c/2", &g) && strcmp(g.security, "TLS 1.2, test") == 0 &&
		     strcmp(g.mime, "text/html") == 0 && strcmp(g.charset, "utf-8") == 0);
		free(g.data);
	}

	/* a reset after a job: what it brought is lost, the rest is read */
	put("http://d/1", "Cache-Control: max-age=1000\n", "lost");
	dc_job_end();
	dc_dir(dir, 1L << 20);		/* no dc_close: the index from before */
	want("a reset: the job's answers lost", got("http://d/1", NULL, NULL) == -1);
	want("a reset: the others read", got("http://c/1", "kept in memory, then the pack at quit", NULL) == 1);
	put("http://d/2", "Cache-Control: max-age=1000\n", "after the reset");
	dc_job_end();
	want("added after the pack's lost end", got("http://d/2", "after the reset", NULL) == 1 &&
	     got("http://a/3", "three", NULL) == 0);
	dc_close();

	/* too big: written again with those used last */
	dc_dir(dir, 64L << 10);
	{
		char data[4097];
		memset(data, 'z', 4096);
		data[4096] = 0;
		for (i = 0; i < 30; i++) {
			char key[64];
			snprintf(key, sizeof key, "http://e/%d", i);
			data[0] = (char) ('A' + i);
			clock_s++;
			put(key, "Cache-Control: max-age=100000\n", data);
			dc_job_end();
		}
		dc_close();
		dc_dir(dir, 64L << 10);
		dc_count(&bytes);
		data[0] = (char) ('A' + 29);
		want("compacted: within three quarters", bytes <= 48L << 10 && bytes >= 40L << 10);
		want("compacted: the last used kept", got("http://e/29", data, NULL) == 1);
		want("compacted: the first gone", got("http://e/0", NULL, NULL) == -1);
		snprintf(path, sizeof path, "%s/EWEB.PAK", dir);
		want("compacted: the pack's size", pf_size(path) <= (48L << 10) + 16);
		dc_close();
	}

	/* a pack that isn't the index's: forgotten */
	{
		int h = pf_open(path, PF_UPDATE);
		want("the pack opened", h >= 0);
		if (h >= 0) {
			pf_write(h, "EWDCffffffff", 12);
			pf_close(h);
		}
		dc_dir(dir, 1L << 20);
		want("another pack: nothing read", got("http://e/29", NULL, NULL) == -1 && dc_count(NULL) == 0);
		dc_close();
	}
	dc_dir(dir, 1L << 20);
	dc_clear();
	printf("disk cache: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
