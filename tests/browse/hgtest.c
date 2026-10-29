/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hgtest - EWeb's history (src/browse/history.c): the pages shown and
 * the links to them visited, their origins, a place on a page another
 * address, shown again the newest (counted as a change only when new),
 * titles on one line and not cut inside a character, no address with a
 * control character or a password, the newest 1000 kept (fewer when
 * their addresses are long), forgotten, and HISTORY.TXT written at quit
 * and read at the next start (by hand: another line ending, notes, odd
 * lines, an address twice).
 *
 *   hgtest DIR      (a folder it may write HISTORY.TXT in)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"
#include "plat.h"
#include "slots.h"

static int fails, checks;

static void want(const char *what, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("hgtest: %s failed\n", what);
	}
}

static int is(int i, const char *url, const char *title, long long when)
{
	const HG *e = hg_get(i);
	return e != NULL && strcmp(e->url, url) == 0 && strcmp(e->title, title) == 0 && e->when == when;
}

static const char by_hand[] =
	"# EWeb's history\r\n"
	"100\thttp://a.example/\tA\r\n"
	"no tabs at all\r\n"
	"101\t\tno address\r\n"
	"102\thttp://b.example/?q=1\tB \xd7\xa9\xd7\x9c\r\r\n"
	"103x\thttp://a.example/\tA again\n"
	"99999999999999999999999\thttp://c.example/#top\t\n"
	"104\thttp://d.example/\tno newline";

int main(int argc, char **argv)
{
	char path[512], title[400];
	unsigned long c0;
	char *d;
	long len;
	int i;

	if (argc < 2) {
		fprintf(stderr, "hgtest DIR\n");
		return 2;
	}
	snprintf(path, sizeof path, "%s/HISTORY.TXT", argv[1]);
	slot_remove(path);

	/* none at first, and none read when there is no file */
	hg_file(path);
	want("none at first", hg_count() == 0 && !hg_visited("http://a.example/"));

	/* shown, and the links to them visited */
	c0 = hg_changes;
	hg_add("http://a.example/", "A", 1000);
	hg_add("http://b.example/page#part", "B\twith\ta tab\r\n", 1001);
	hg_add("http://c.example/", NULL, 1002);
	want("three", hg_count() == 3 && hg_changes == c0 + 3);
	want("visited", hg_visited("http://a.example/") && hg_visited("http://b.example/page#part") &&
	     hg_visited("http://c.example/"));
	want("a place on a page is another address", !hg_visited("http://b.example/page") &&
	     !hg_visited("http://a.example/#x") && !hg_visited("http://a.example"));
	want("their origins", hg_origin("http://a.example", 16) && hg_origin("http://b.example", 16) &&
	     !hg_origin("https://a.example", 17) && !hg_origin("http://d.example", 16) &&
	     !hg_origin("http://a.exampl", 15));
	want("titles on one line", is(1, "http://b.example/page#part", "B with a tab  ", 1001) &&
	     is(2, "http://c.example/", "", 1002));

	/* shown again: the newest, with its new title, and no change to the links */
	c0 = hg_changes;
	hg_add("http://a.example/", "A, later", 1003);
	want("shown again: the newest", hg_count() == 3 && is(2, "http://a.example/", "A, later", 1003) &&
	     is(0, "http://b.example/page#part", "B with a tab  ", 1001));
	want("shown again: no change to the links", hg_changes == c0);

	/* a long title cut before a character, not inside it */
	memset(title, 0, sizeof title);
	for (i = 0; i < 127; i++)
		memcpy(title + 2 * i, "\xd7\xa9", 2);	/* 254 bytes of shin */
	memcpy(title + 254, "\xd7\x9c\xd7\x95", 4);
	hg_add("http://long.example/", title, 1004);
	want("a long title not cut inside a character", strlen(hg_get(3)->title) == 254 &&
	     memcmp(hg_get(3)->title, title, 254) == 0);
	memset(title, 'a', 253);
	memcpy(title + 253, "\xd7\x9c\xd7\x95", 5);	/* a whole one at the end: kept */
	hg_add("http://long.example/2", title, 1005);
	want("a whole character at the end kept", strlen(hg_get(4)->title) == 255);
	want("not an empty address", (hg_add("", "x", 1), hg_count() == 5));
	hg_add("http://a.example/x\ty\n1\thttp://forged.example/", "x", 1);
	hg_add("http://user:pw@a.example/", "x", 1);
	want("not an address with a control character or a password", hg_count() == 5 &&
	     !hg_visited("http://user:pw@a.example/"));
	hg_add("http://a.example/@home?a@b", "at", 1006);
	want("an @ after the host", hg_count() == 6 && hg_visited("http://a.example/@home?a@b"));

	/* written at quit, read at the next start */
	want("saved", hg_save());
	hg_free();
	hg_file(path);
	want("the next start", hg_count() == 6 && is(0, "http://b.example/page#part", "B with a tab  ", 1001) &&
	     is(2, "http://a.example/", "A, later", 1003) && hg_visited("http://c.example/"));
	d = slot_load(path, &len);	/* its newer slot (src/net/slots.c) */
	want("the file", d != NULL && strncmp(d, "# ", 2) == 0 && strstr(d, "\n1003\thttp://a.example/\tA, later\n"));
	free(d);

	/* the newest 1000 */
	c0 = hg_changes;
	for (i = 0; i < 1000; i++) {
		char u[64];
		snprintf(u, sizeof u, "http://many.example/%d", i);
		hg_add(u, "", 2000 + i);
	}
	want("an origin gone with its last page", !hg_origin("http://b.example", 16) &&
	     hg_origin("http://many.example", 19));
	want("the newest 1000", hg_count() == 1000 && !hg_visited("http://b.example/page#part") &&
	     !hg_visited("http://a.example/") && hg_visited("http://many.example/0") &&
	     hg_visited("http://many.example/999") && is(999, "http://many.example/999", "", 2999));
	want("each a change", hg_changes == c0 + 1000);

	/* long addresses: the oldest go before the history passes its bytes */
	for (i = 0; i < 400; i++) {
		char u[2001];
		memset(u, 'a', sizeof u - 1);
		u[sizeof u - 1] = 0;
		snprintf(u, 40, "http://long.example/%d/", i);
		u[strlen(u)] = 'a';
		hg_add(u, "", 3000 + i);
	}
	want("long addresses: the newest within the bytes", hg_count() > 100 && hg_count() < 140 &&
	     hg_get(hg_count() - 1)->when == 3399 && !hg_visited("http://many.example/999"));

	/* forgotten: at once, and on the disk at quit */
	c0 = hg_changes;
	hg_clear();
	want("forgotten", hg_count() == 0 && !hg_visited("http://many.example/5") && hg_changes == c0 + 1 &&
	     !hg_origin("http://many.example", 19));
	want("saved empty", hg_save());
	hg_free();
	hg_file(path);
	want("forgotten at the next start", hg_count() == 0);

	/* a file by hand */
	want("by hand", hg_parse(by_hand, (long) strlen(by_hand)));
	want("by hand: its lines", hg_count() == 4 && is(0, "http://b.example/?q=1", "B \xd7\xa9\xd7\x9c", 102) &&
	     is(1, "http://a.example/", "A again", 103) && is(2, "http://c.example/#top", "", 999999999999999999LL) &&
	     is(3, "http://d.example/", "no newline", 104));
	d = hg_write(&len);
	want("by hand: written as read", d != NULL && hg_parse(d, len) && hg_count() == 4 &&
	     is(1, "http://a.example/", "A again", 103));
	free(d);
	hg_free();
	slot_remove(path);
	printf("history: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
