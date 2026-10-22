/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dltest - downloads' names: Content-Disposition (src/net/http.c's
 * http_disposition) against RFC 6266's examples (section 5) and the
 * cases of tc2231 (test.greenbytes.de/tech/tc2231, the test suite RFC
 * 6266 was written with: each case by its name there), then the name a
 * TOS disk can hold that EWeb offers for a file (src/browse's
 * br_file_name) from a name given and the address. Prints each failure
 * and how many passed.
 */
#include <stdio.h>
#include <string.h>

#include "browse.h"
#include "http.h"

static int failed, passed;

/* a header value: what it says (1 attachment, 0 inline, -1 ignored) and
   the name, UTF-8 */
typedef struct {
	const char *name, *value;
	int type;
	const char *file;
} CD;

static const CD cases[] = {
	/* RFC 6266, 5: examples */
	{ "rfc6266-1", "Attachment; filename=example.html", 1, "example.html" },
	{ "rfc6266-2", "INLINE; FILENAME= \"an example.html\"", 0, "an example.html" },
	{ "rfc6266-3", "attachment; filename*= UTF-8''%e2%82%ac%20rates", 1, "\xe2\x82\xac rates" },
	{ "rfc6266-4", "attachment; filename=\"EURO rates\"; filename*=utf-8''%e2%82%ac%20rates", 1,
	  "\xe2\x82\xac rates" },

	/* tc2231: Content-Disposition: Disposition-Type Inline */
	{ "inlonly", "inline", 0, "" },
	{ "inlonlyquoted", "\"inline\"", -1, "" },
	{ "inlwithasciifilename", "inline; filename=\"foo.html\"", 0, "foo.html" },
	{ "inlwithfnattach", "inline; filename=\"Not an attachment!\"", 0, "Not an attachment!" },
	{ "inlwithasciifilenamepdf", "inline; filename=\"foo.pdf\"", 0, "foo.pdf" },

	/* Disposition-Type Attachment */
	{ "attonly", "attachment", 1, "" },
	{ "attonlyquoted", "\"attachment\"", -1, "" },
	{ "attonlyucase", "ATTACHMENT", 1, "" },
	{ "attwithasciifilename", "attachment; filename=\"foo.html\"", 1, "foo.html" },
	{ "attwithasciifilename25", "attachment; filename=\"0000000000111111111122222\"", 1,
	  "0000000000111111111122222" },
	{ "attwithasciifilename35", "attachment; filename=\"00000000001111111111222222222233333\"", 1,
	  "00000000001111111111222222222233333" },
	{ "attwithasciifnescapedchar", "attachment; filename=\"f\\oo.html\"", 1, "foo.html" },
	{ "attwithasciifnescapedquote", "attachment; filename=\"\\\"quoting\\\" tested.html\"", 1,
	  "\"quoting\" tested.html" },
	{ "attwithquotedsemicolon", "attachment; filename=\"Here's a semicolon;.html\"", 1,
	  "Here's a semicolon;.html" },
	{ "attwithfilenameandextparam", "attachment; foo=\"bar\"; filename=\"foo.html\"", 1, "foo.html" },
	{ "attwithfilenameandextparamescaped", "attachment; foo=\"\\\"\\\\\";filename=\"foo.html\"", 1,
	  "foo.html" },
	{ "attwithasciifilenameucase", "attachment; FILENAME=\"foo.html\"", 1, "foo.html" },
	{ "attwithasciifilenamenq", "attachment; filename=foo.html", 1, "foo.html" },
	{ "attwithtokfncommanq", "attachment; filename=foo,bar.html", -1, "" },
	/* a ';' too many: browsers take these, nothing in them can be misread */
	{ "attwithasciifilenamenqs", "attachment; filename=foo.html ;", 1, "foo.html" },
	{ "attemptyparam", "attachment; ;filename=foo", 1, "foo" },
	{ "attwithasciifilenamenqws", "attachment; filename=foo bar.html", -1, "" },
	{ "attwithfntokensq", "attachment; filename='foo.bar'", 1, "'foo.bar'" },
	{ "attwithisofnplain", "attachment; filename=\"foo-\xe4.html\"", 1, "foo-\xc3\xa4.html" },
	{ "attwithutf8fnplain", "attachment; filename=\"foo-\xc3\xa4.html\"", 1, "foo-\xc3\x83\xc2\xa4.html" },
	{ "attwithfnrawpctenca", "attachment; filename=\"foo-%41.html\"", 1, "foo-%41.html" },
	{ "attwithfnusingpct", "attachment; filename=\"50%.html\"", 1, "50%.html" },
	{ "attwithfnrawpctencaq", "attachment; filename=\"foo-%\\41.html\"", 1, "foo-%41.html" },
	{ "attwithnamepct", "attachment; name=\"foo-%41.html\"", 1, "" },
	{ "attwithfilenamepctandiso", "attachment; filename=\"\xe4-%41.html\"", 1, "\xc3\xa4-%41.html" },
	{ "attwithfnrawpctenclong", "attachment; filename=\"foo-%c3%a4-%e2%82%ac.html\"", 1,
	  "foo-%c3%a4-%e2%82%ac.html" },
	{ "attwithasciifilenamews1", "attachment; filename =\"foo.html\"", 1, "foo.html" },
	{ "attwith2filenames", "attachment; filename=\"foo.html\"; filename=\"bar.html\"", -1, "" },
	{ "attfnbrokentoken", "attachment; filename=foo[1](2).html", -1, "" },
	{ "attfnbrokentokeniso", "attachment; filename=foo-\xe4.html", -1, "" },
	{ "attfnbrokentokenutf", "attachment; filename=foo-\xc3\xa4.html", -1, "" },
	{ "attmissingdisposition", "filename=foo.html", -1, "" },
	{ "attmissingdisposition2", "x=y; filename=foo.html", -1, "" },
	{ "attmissingdisposition3", "\"foo; filename=bar;baz\"; filename=qux", -1, "" },
	{ "attmissingdisposition4", "filename=foo.html, filename=bar.html", -1, "" },
	{ "emptydisposition", "; filename=foo.html", -1, "" },
	{ "doublecolon", ": inline; attachment; filename=foo.html", -1, "" },
	{ "attandinline", "inline; attachment; filename=foo.html", -1, "" },
	{ "attandinline2", "attachment; inline; filename=foo.html", -1, "" },
	{ "attbrokenquotedfn", "attachment; filename=\"foo.html\".txt", -1, "" },
	{ "attbrokenquotedfn2", "attachment; filename=\"bar", -1, "" },
	{ "attbrokenquotedfn3", "attachment; filename=foo\"bar;baz\"qux", -1, "" },
	{ "attmultinstances", "attachment; filename=foo.html, attachment; filename=bar.html", -1, "" },
	{ "attmissingdelim", "attachment; foo=foo filename=bar", -1, "" },
	{ "attmissingdelim2", "attachment; filename=bar foo=foo", -1, "" },
	{ "attmissingdelim3", "attachment filename=bar", -1, "" },
	{ "attreversed", "filename=foo.html; attachment", -1, "" },
	{ "attconfusedparam", "attachment; xfilename=foo.html", 1, "" },
	{ "attabspath", "attachment; filename=\"/foo.html\"", 1, "/foo.html" },
	{ "attabspathwin", "attachment; filename=\"\\\\foo.html\"", 1, "\\foo.html" },

	/* Additional Parameters */
	{ "attcdate", "attachment; creation-date=\"Wed, 12 Feb 1997 16:29:51 -0500\"", 1, "" },
	{ "attmdate", "attachment; modification-date=\"Wed, 12 Feb 1997 16:29:51 -0500\"", 1, "" },

	/* Disposition-Type Extension: as attachment (RFC 6266, 4.2) */
	{ "dispext", "foobar", 1, "" },
	{ "dispextbadfn", "attachment; example=\"filename=example.txt\"", 1, "" },

	/* RFC 2231/5987 Encoding: Character Sets. A filename* that isn't
	   RFC 8187's is passed over (the header stands) */
	{ "attwithisofn2231iso", "attachment; filename*=iso-8859-1''foo-%E4.html", 1, "foo-\xc3\xa4.html" },
	{ "attwithfn2231utf8", "attachment; filename*=UTF-8''foo-%c3%a4-%e2%82%ac.html", 1,
	  "foo-\xc3\xa4-\xe2\x82\xac.html" },
	{ "attwithfn2231noc", "attachment; filename*=''foo-%c3%a4-%e2%82%ac.html", 1, "" },
	{ "attwithfn2231utf8comp", "attachment; filename*=UTF-8''foo-a%cc%88.html", 1, "foo-a\xcc\x88.html" },
	{ "attwithfn2231utf8-bad", "attachment; filename*=iso-8859-1''foo-%c3%a4-%e2%82%ac.html", 1,
	  "foo-\xc3\x83\xc2\xa4-\xc3\xa2\xc2\x82\xc2\xac.html" },
	{ "attwithfn2231iso-bad", "attachment; filename*=utf-8''foo-%E4.html", 1, "" },
	{ "attwithfn2231ws1", "attachment; filename *=UTF-8''foo-%c3%a4.html", -1, "" },
	{ "attwithfn2231ws2", "attachment; filename*= UTF-8''foo-%c3%a4.html", 1, "foo-\xc3\xa4.html" },
	{ "attwithfn2231ws3", "attachment; filename* =UTF-8''foo-%c3%a4.html", 1, "foo-\xc3\xa4.html" },
	{ "attwithfn2231quot", "attachment; filename*=\"UTF-8''foo-%c3%a4.html\"", 1, "" },
	{ "attwithfn2231quot2", "attachment; filename*=\"foo%20bar.html\"", 1, "" },
	{ "attwithfn2231singleqmissing", "attachment; filename*=UTF-8'foo-%c3%a4.html", 1, "" },
	{ "attwithfn2231nbadpct1", "attachment; filename*=UTF-8''foo%", 1, "" },
	{ "attwithfn2231nbadpct2", "attachment; filename*=UTF-8''f%oo.html", 1, "" },
	{ "attwithfn2231dpct", "attachment; filename*=UTF-8''A-%2541.html", 1, "A-%41.html" },
	{ "attwithfn2231abspathdisguised", "attachment; filename*=UTF-8''%5cfoo.html", 1, "\\foo.html" },

	/* RFC2231 Encoding: Continuations (not read: RFC 6266 has none) */
	{ "attfncont", "attachment; filename*0=\"foo.\"; filename*1=\"html\"", 1, "" },
	{ "attfncontenc", "attachment; filename*0*=UTF-8''foo-%c3%a4; filename*1=\".html\"", 1, "" },

	/* RFC2231 Encoding: Fallback Behaviour: filename* first (4.3) */
	{ "attfnboth", "attachment; filename=\"foo-ae.html\"; filename*=UTF-8''foo-%c3%a4.html", 1,
	  "foo-\xc3\xa4.html" },
	{ "attfnboth2", "attachment; filename*=UTF-8''foo-%c3%a4.html; filename=\"foo-ae.html\"", 1,
	  "foo-\xc3\xa4.html" },
	{ "attfnboth3", "attachment; filename*0*=ISO-8859-15''euro-sign%3d%a4; "
	  "filename*=ISO-8859-1''currency-sign%3d%a4", 1, "currency-sign=\xc2\xa4" },
	{ "attnewandfn", "attachment; foobar=x; filename=\"foo.html\"", 1, "foo.html" },

	/* RFC2047 Encoding: not decoded */
	{ "attrfc2047token", "attachment; filename==?ISO-8859-1?Q?foo-=E4.html?=", -1, "" },
	{ "attrfc2047quoted", "attachment; filename=\"=?ISO-8859-1?Q?foo-=E4.html?=\"", 1,
	  "=?ISO-8859-1?Q?foo-=E4.html?=" },

	/* more: spaces and tabs anywhere the grammar has a gap, a charset
	   other than the two RFC 8187 names, a language, a value cut off,
	   a NUL encoded, the end of a header line */
	{ "lws", "attachment \t ", 1, "" },
	{ "lws2", "attachment;filename=\"plans.pdf\" \t;    \t\t foo=bar", 1, "plans.pdf" },
	{ "badname", "attachment; filename@=\"rates.pdf\"", -1, "" },
	{ "novalue", "attachment; filename=", -1, "" },
	{ "slash", "attachment; filename=total/; foo=bar", -1, "" },
	{ "twice", "attachment; filename=foo; filename=bar", -1, "" },
	{ "twicestar", "attachment; filename*=UTF-8''a; filename*=UTF-8''b", -1, "" },
	{ "latin2", "attachment; filename*=ISO-8859-2''%A4%20rates.pdf; filename=rates.pdf", 1, "rates.pdf" },
	{ "language", "attachment; filename*=UTF-8'en'%E2%82%AC%20rates.pdf", 1, "\xe2\x82\xac rates.pdf" },
	{ "badlanguage", "attachment; filename*=UTF-8'e_n'x.pdf; filename=y.pdf", 1, "y.pdf" },
	{ "cututf8", "attachment; filename*=UTF-8''%e2%82", 1, "" },
	{ "nul", "attachment; filename*=UTF-8''a%00b; filename=c", 1, "c" },
	{ "hebrew", "attachment; filename*=UTF-8''%D7%A9%D7%9C%D7%95%D7%9D.zip", 1,
	  "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.zip" },
	{ "line", "attachment; filename=a.txt\nX-Other: \"", 1, "a.txt" },
	{ "control", "attachment; filename=\"a\x01.txt\"", -1, "" },
	{ "empty", "", -1, "" },
	{ "spaces", "   ", -1, "" },
};

static void dispositions(void)
{
	size_t i;
	char name[256], small[8];
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		int t = http_disposition(cases[i].value, name, sizeof name);
		if (t == cases[i].type && strcmp(name, cases[i].file) == 0) {
			passed++;
			continue;
		}
		failed++;
		fprintf(stderr, "dltest: %s: %d \"%s\", wanted %d \"%s\"\n", cases[i].name, t, name, cases[i].type,
			cases[i].file);
	}
	/* a name too long for the room: cut at a character */
	if (http_disposition("attachment; filename*=UTF-8''%e2%82%ac%e2%82%ac%e2%82%ac", small, sizeof small) == 1 &&
	    strcmp(small, "\xe2\x82\xac\xe2\x82\xac") == 0 &&
	    http_disposition("attachment; filename=\"\xe4\xe4\xe4\xe4\"", small, sizeof small) == 1 &&
	    strcmp(small, "\xc3\xa4\xc3\xa4\xc3\xa4") == 0 &&
	    http_disposition("attachment; filename=abcdefghij", small, sizeof small) == 1 &&
	    strcmp(small, "abcdefg") == 0) {
		passed++;
	} else {
		failed++;
		fprintf(stderr, "dltest: cut: \"%s\"\n", small);
	}
}

/* the TOS name offered: from the name given (NULL none) and the address */
typedef struct {
	const char *url, *given, *mime, *want;
} NAME;

static const NAME names[] = {
	/* the address's, as before */
	{ "http://h.test/", NULL, "", "INDEX.HTM" },
	{ "http://h.test/dir/", NULL, "text/html", "INDEX.HTM" },
	{ "http://h.test", NULL, "text/html", "INDEX.HTM" },
	{ "http://h.test/about", NULL, "", "ABOUT" },
	{ "http://h.test/pic.jpeg?x=1#f", NULL, "image/jpeg", "PIC.JPG" },
	{ "http://h.test/a%20b.txt", NULL, "text/plain", "AB.TXT" },
	{ "http://h.test/Small.Bin", NULL, "application/octet-stream", "SMALL.BIN" },
	{ "http://h.test/chunked", NULL, "application/octet-stream", "CHUNKED" },
	{ "http://h.test/a/news", NULL, "application/zip", "NEWS.ZIP" },
	/* a script's ending: a page's, else the type's */
	{ "http://h.test/download.php?id=123", NULL, "", "DOWNLOAD.HTM" },
	{ "http://h.test/download.php?id=123", NULL, "text/html", "DOWNLOAD.HTM" },
	{ "http://h.test/download.php?id=123", NULL, "application/zip", "DOWNLOAD.ZIP" },
	{ "http://h.test/get.asp", NULL, "application/octet-stream", "GET" },
	/* gzip files */
	{ "http://h.test/files/EWeb-0.26.tar.gz", NULL, "application/x-gzip", "EWEB-026.TGZ" },
	{ "http://h.test/files/data.gz", NULL, "application/gzip", "DATA.GZ" },
	{ "http://h.test/files/data", NULL, "application/x-tar+gzip", "DATA.TGZ" },
	/* a name given (Content-Disposition, <a download>) first */
	{ "http://h.test/download.php?id=1", "Report 2026.txt", "text/plain", "REPORT20.TXT" },
	{ "http://h.test/d.php", "\xe2\x82\xac rates.pdf", "application/pdf", "RATES.PDF" },
	{ "http://h.test/x", "foo.html", "text/html", "FOO.HTM" },
	{ "http://h.test/x", "report.php", "text/html", "REPORT.HTM" },
	{ "http://h.test/x", "data.php", "application/octet-stream", "DATA.PHP" },
	{ "http://h.test/x", "README", "text/plain", "README.TXT" },
	{ "http://h.test/x", "README", "", "README" },
	/* a base and no ending of its own: the type's, nothing else's */
	{ "http://h.test/x", "LICENSE", "application/octet-stream", "LICENSE" },
	{ "http://h.test/x", "Makefile", "", "MAKEFILE" },
	{ "http://h.test/dl.php?id=1", "notes", "application/octet-stream", "NOTES" },
	{ "http://h.test/x", "report.", "text/plain", "REPORT.TXT" },
	{ "http://h.test/x", "file.\xd7\xa9", "application/zip", "FILE.ZIP" },
	{ "http://h.test/x", "notes", "text/html", "NOTES.HTM" },
	{ "http://h.test/x", "photo.JPEG", "image/jpeg", "PHOTO.JPG" },
	{ "http://h.test/x", "verylongfilename.html", "text/html", "VERYLONG.HTM" },
	{ "http://h.test/x", "backup.tgz", "application/octet-stream", "BACKUP.TGZ" },
	{ "http://h.test/x", "Archive.tar.gz", "application/x-gzip", "ARCHIVE.TGZ" },
	{ "http://h.test/x", "foo-%41.html", "text/html", "FOO-41.HTM" },
	{ "http://h.test/x", "'foo.bar'", "", "FOO.BAR" },
	{ "http://h.test/x", "export.csv", "text/html", "EXPORT.CSV" },
	/* its last part only (RFC 6266, 4.3; tc2231 attabspath) */
	{ "http://h.test/x", "/foo.html", "text/html", "FOO.HTM" },
	{ "http://h.test/x", "\\foo.html", "application/octet-stream", "FOO.HTM" },
	{ "http://h.test/x", "../../AUTO/EVIL.PRG", "", "EVIL.PRG" },
	{ "http://h.test/x", "C:\\AUTO\\EVIL.PRG", "", "EVIL.PRG" },
	/* nothing of it a TOS name can hold: the address's, its ending kept */
	{ "http://h.test/get.php?f=3", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.zip", "application/zip", "GET.ZIP" },
	{ "http://h.test/get.php?f=3", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "application/zip", "GET.ZIP" },
	{ "http://h.test/", "...", "", "INDEX.HTM" },
	{ "http://h.test/x.bin", "", "", "X.BIN" },
};

/* stack frames on the stack itself (not ASan's own, which come from a
   pool), so that a call of br_file_name finds what the call before it
   left there */
const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
	return "detect_stack_use_after_return=0";
}

static void file_names(void)
{
	size_t i;
	char out[16];
	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		/* a call first that leaves an ending on the stack: what the
		   next doesn't set itself it would take ("README.QQQ") */
		br_file_name("http://h.test/x", "prime.QQQQQ", "", out, sizeof out);
		br_file_name(names[i].url, names[i].given, names[i].mime, out, sizeof out);
		if (strcmp(out, names[i].want) == 0) {
			passed++;
			continue;
		}
		failed++;
		fprintf(stderr, "dltest: name of %s, %s, %s: %s, wanted %s\n", names[i].url,
			names[i].given ? names[i].given : "(none)", names[i].mime, out, names[i].want);
	}
}

int main(void)
{
	dispositions();
	file_names();
	fprintf(stderr, "dltest: %d passed, %d failed\n", passed, failed);
	return failed != 0;
}
