/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sitetest - which sites' scripts run (br_scripts_site, src/browse): on
 * no site, on the sites chosen (SCRIPTS=), on every site but those left
 * out (NOSCRIPTS=) and those whose scripts were too much since EWeb
 * started (the newest 16), a site tried again when chosen once more.
 * Prints each failure and how many passed.
 */
#include <stdio.h>
#include <string.h>

#include "browse.h"

static int failed, passed;

static void expect(const char *name, int got, int want)
{
	if (got == want) {
		passed++;
		return;
	}
	failed++;
	fprintf(stderr, "sitetest: %s: %d, not %d\n", name, got, want);
}

int main(void)
{
	browser b;
	char url[64];
	int i;
	br_init(&b, 600, 1024);
	expect("the sites chosen by default", b.script_mode, 1);
	b.script_sites = "example.com file";
	b.noscript_sites = "";
	expect("chosen: a site in the list", br_scripts_site(&b, "https://Example.COM:8443/x?y#z"), 1);
	expect("chosen: another", br_scripts_site(&b, "http://example.org/"), 0);
	expect("chosen: a part of a name isn't the name", br_scripts_site(&b, "http://www.example.com/"), 0);
	expect("chosen: the disk", br_scripts_site(&b, "file:///C:/PAGES/A.HTM"), 1);
	expect("chosen: EWeb's own pages", br_scripts_site(&b, "about:history"), 0);
	b.script_sites = "*";
	expect("chosen: \"*\" is every site", br_scripts_site(&b, "http://example.org/"), 1);
	b.script_mode = 0;
	expect("none: not even the chosen", br_scripts_site(&b, "http://example.org/"), 0);
	b.script_mode = 2;
	b.script_sites = "";
	b.noscript_sites = "ads.example, slow.example";
	expect("every site: one not chosen", br_scripts_site(&b, "http://news.example/"), 1);
	expect("every site: one left out", br_scripts_site(&b, "http://slow.example/a"), 0);
	expect("every site: EWeb's own pages", br_scripts_site(&b, "about:bookmarks"), 0);
	br_scripts_too_much(&b, "http://heavy.example/page");
	expect("every site: one too much is left out", br_scripts_site(&b, "https://heavy.example/other"), 0);
	expect("every site: the others still run", br_scripts_site(&b, "http://news.example/"), 1);
	br_scripts_retry(&b, "http://heavy.example/");
	expect("every site: tried again", br_scripts_site(&b, "http://heavy.example/"), 1);
	b.script_mode = 1;
	b.script_sites = "light.example";
	br_scripts_too_much(&b, "http://light.example/");
	expect("chosen: a site's too much doesn't leave it out", br_scripts_site(&b, "http://light.example/"), 1);
	b.script_mode = 2;
	for (i = 0; i < 17; i++) {
		snprintf(url, sizeof url, "http://s%d.example/", i);
		br_scripts_too_much(&b, url);
	}
	expect("every site: the oldest of 17 forgotten", br_scripts_site(&b, "http://s0.example/"), 1);
	expect("every site: the newest kept", br_scripts_site(&b, "http://s16.example/"), 0);
	br_free(&b);
	/* live view's address: the page, then the window's page area */
	{
		char live[200];
		br_live_url("https://news.example/a b#top", live, sizeof live);
		expect("live view: no size before a window", strcmp(live, "http://webgate/live?url=https%3A%2F%2Fnews.example%2Fa%20b"), 0);
		br_live_size(784, 520);
		br_live_url("https://news.example/a b#top", live, sizeof live);
		expect("live view: the window's size named",
		       strcmp(live, "http://webgate/live?url=https%3A%2F%2Fnews.example%2Fa%20b&w=784&h=520"), 0);
		br_live_url("about:start", live, sizeof live);
		expect("live view: a page not on the web", strcmp(live, "http://webgate/live?w=784&h=520"), 0);
		br_live_url(NULL, live, 50);
		expect("live view: a short buffer", strcmp(live, "http://webgate/live?w=784&h=520"), 0);
		br_live_url("https://news.example/a-long-path-that-does-not-fit", live, 60);
		expect("live view: a long address cut, its size kept", strstr(live, "&w=784&h=520") != NULL, 1);
	}
	printf("sitetest: %d passed, %d failed\n", passed, failed);
	return failed != 0;
}
