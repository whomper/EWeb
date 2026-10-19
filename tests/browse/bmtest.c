/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bmtest - EWeb's bookmarks (src/browse/bookmark.c): files other
 * browsers export read (folders left out, character references, quotes
 * of either kind, links that aren't pages left out), what EWeb writes
 * read back the same, adding (twice: once), removing, and the file
 * written at each change and read at the next start.
 *
 *   bmtest DIR      (a folder it may write BOOKMARK.HTM in)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bookmark.h"
#include "plat.h"

static int fails, checks;

static void want(const char *what, int ok)
{
	checks++;
	if (!ok) {
		fails++;
		printf("bmtest: %s failed\n", what);
	}
}

static int is(int i, const char *url, const char *title)
{
	return i < bm_n && strcmp(bm_list[i].url, url) == 0 && strcmp(bm_list[i].title, title) == 0;
}

static const char firefox[] =
	"<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
	"<!-- This is an automatically generated file. -->\n"
	"<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
	"<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks Menu</H1>\n\n<DL><p>\n"
	"    <DT><A HREF=\"place:parent=toolbar\">Most Visited</A>\n"
	"    <DT><H3 ADD_DATE=\"1600000000\">Atari</H3>\n"
	"    <DL><p>\n"
	"        <DT><A HREF=\"https://en.wikipedia.org/wiki/Atari_Falcon\" ADD_DATE=\"1600000001\" "
	"ICON=\"data:image/png;base64,iVBOR\">Atari Falcon - Wikipedia</A>\n"
	"        <DT><a href='http://68k.news/?a=1&amp;b=2' add_date=1>68k.news &amp; more\n  news</a>\n"
	"    </DL><p>\n"
	"    <DT><A HREF=\"javascript:alert(1)\">A bookmarklet</A>\n"
	"    <DT><A HREF=\"https://example.com/\">&#x5e9;&#1500;&#x05D5;&#1501; &lt;b&gt; &quot;q&quot; <b>x</b></A>\n"
	"    <DT><A HREF=\"\">Nothing</A>\n"
	"    <DT><A NAME=x>No address</A>\n"
	"</DL><p>\n";

int main(int argc, char **argv)
{
	char path[512];
	char *d;
	long len;

	if (argc < 2) {
		fprintf(stderr, "bmtest DIR\n");
		return 2;
	}
	snprintf(path, sizeof path, "%s/BOOKMARK.HTM", argv[1]);
	pf_remove(path);

	/* another browser's file */
	want("parse", bm_parse(firefox, (long) strlen(firefox)));
	want("three links", bm_n == 3);
	want("in a folder", is(0, "https://en.wikipedia.org/wiki/Atari_Falcon", "Atari Falcon - Wikipedia"));
	want("single quotes, &amp;, white space", is(1, "http://68k.news/?a=1&b=2", "68k.news & more news"));
	want("character references, a tag", is(2, "https://example.com/",
						"\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d <b> \"q\" x"));

	/* what EWeb writes, read back */
	d = bm_write(&len);
	want("written", d != NULL && strstr(d, "<!DOCTYPE NETSCAPE-Bookmark-file-1>") == d &&
	     strstr(d, "HREF=\"http://68k.news/?a=1&amp;b=2\">68k.news &amp; more news</A>\r\n") != NULL);
	want("read back", d != NULL && bm_parse(d, len) && bm_n == 3 &&
	     is(1, "http://68k.news/?a=1&b=2", "68k.news & more news") &&
	     is(2, "https://example.com/", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d <b> \"q\" x"));
	free(d);

	/* adding and removing, each on the disk at once */
	bm_file(path);
	want("none at first", bm_n == 0);
	want("added", bm_add("http://a.example/?x=1&y=<2>", "A & \"B\" <c>") == 1);
	want("added again: no", bm_add("http://a.example/?x=1&y=<2>", "other") == -1);
	want("a second", bm_add("http://b.example/", "") == 1);
	want("a third", bm_add("http://c.example/", "C") == 1);
	want("kept as given", is(0, "http://a.example/?x=1&y=<2>", "A & \"B\" <c>") && is(1, "http://b.example/", ""));
	want("found", bm_find("http://c.example/") == 2 && bm_find("http://d.example/") == -1);
	want("removed", bm_remove(1) == 1 && bm_n == 2 && is(1, "http://c.example/", "C"));
	want("removed: none there", bm_remove(5) == 0);
	bm_free();
	bm_file(path);
	want("the next start", bm_n == 2 && is(0, "http://a.example/?x=1&y=<2>", "A & \"B\" <c>") &&
	     is(1, "http://c.example/", "C"));
	bm_free();
	pf_remove(path);
	printf("bookmarks: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
