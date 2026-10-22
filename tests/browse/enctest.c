/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * enctest - the encoding of a page (src/doc/html.c, src/browse): the
 * HTML Standard's encoding sniffing (13.2.3.2) for pages that name none
 * (a BOM, the HTTP answer's or the user's choice, the prescan, then UTF-8
 * found by its bytes, then the locale's default) and for text files;
 * the default by the address (br_fallback: Israel's .il); the labels
 * EWeb knows and View > Encoding's list; and a URL's query in its page's
 * encoding (the URL Standard's percent-encode after encoding: the Web
 * Platform Tests' url/resources/percent-encoding.json for the encodings
 * EWeb has, and Hebrew's). Then the UTF-8 detection against a plain
 * validator on random bytes, at every alignment, under the sanitizers.
 * Prints each failure and how many passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browse.h"
#include "doc.h"
#include "html.h"
#include "url.h"

static int failed, passed;

static void result(const char *name, int ok, const char *got)
{
	if (ok) {
		passed++;
		return;
	}
	failed++;
	fprintf(stderr, "enctest: %s failed: %s\n", name, got);
}

static ec_media media = { 640, 400, 16 };

/* the page parsed: what it was read as */
static void sniff(const char *name, const char *page, size_t len, const char *charset, const char *fallback,
		  const char *want)
{
	html_opts o;
	doc *d;
	memset(&o, 0, sizeof o);
	o.charset = charset;
	o.fallback = fallback;
	d = doc_parse_opts(page, len, &o, &media);
	result(name, d != NULL && strcmp(d->charset, want) == 0, d != NULL ? d->charset : "out of memory");
	doc_free(d);
}
#define SNIFF(name, page, charset, fallback, want) sniff(name, page, sizeof page - 1, charset, fallback, want)

/* the text of a document's first <pre> ("" none) */
static const char *pre_text(const doc *d)
{
	const dnode *n = d != NULL ? d->root : NULL;
	while (n != NULL && !(n->type == DN_ELEMENT && n->tag == T_PRE)) {
		if (n->first != NULL) {
			n = n->first;
			continue;
		}
		while (n != NULL && n->next == NULL)
			n = n->parent;
		if (n != NULL)
			n = n->next;
	}
	{
		static char t[300];
		const dnode *c = n != NULL ? n->first : NULL;
		size_t l = c != NULL && c->type == DN_TEXT ? c->len : 0;
		if (l >= sizeof t)
			l = sizeof t - 1;
		if (l > 0)
			memcpy(t, c->text, l);
		t[l] = 0;
		return t;
	}
}

static void plain(const char *name, const char *file, size_t len, const char *want_text, const char *want_cs)
{
	html_opts o;
	doc *d;
	char got[300];
	memset(&o, 0, sizeof o);
	o.plaintext = 1;
	d = doc_parse_opts(file, len, &o, &media);
	snprintf(got, sizeof got, "%s, \"%s\"", d != NULL ? d->charset : "out of memory", pre_text(d));
	result(name, d != NULL && strcmp(d->charset, want_cs) == 0 && strcmp(pre_text(d), want_text) == 0, got);
	doc_free(d);
}
#define PLAIN(name, file, want_text, want_cs) plain(name, file, sizeof file - 1, want_text, want_cs)

static void sniffing(void)
{
	static char late[1400], aligned[64];
	size_t i, k;
	SNIFF("ASCII, nothing named", "<p>plain</p>", NULL, NULL, "windows-1252");
	SNIFF("UTF-8, nothing named", "<p>\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d caf\xc3\xa9</p>", NULL, NULL, "UTF-8");
	SNIFF("windows-1255, nothing named", "<p>\xf9\xec\xe5\xed</p>", NULL, NULL, "windows-1252");
	SNIFF("windows-1255 under .il", "<p>\xf9\xec\xe5\xed</p>", NULL, "windows-1255", "windows-1255");
	SNIFF("UTF-8 under .il is UTF-8", "<p>\xd7\xa9\xd7\x9c</p>", NULL, "windows-1255", "UTF-8");
	SNIFF("ASCII under .il", "<p>plain</p>", NULL, "windows-1255", "windows-1255");
	SNIFF("UTF-8, then a byte it can't have", "<p>caf\xc3\xa9 caf\xe9</p>", NULL, NULL, "windows-1252");
	SNIFF("an overlong form isn't UTF-8", "<p>\xc0\xaf</p>", NULL, NULL, "windows-1252");
	SNIFF("a surrogate isn't UTF-8", "<p>\xed\xa0\x80</p>", NULL, NULL, "windows-1252");
	SNIFF("beyond U+10FFFF isn't UTF-8", "<p>\xf4\x90\x80\x80</p>", NULL, NULL, "windows-1252");
	SNIFF("a 4-byte character", "<p>\xf0\x9f\x98\x80</p>", NULL, NULL, "UTF-8");
	SNIFF("cut short at the end", "<p>\xd7\xa9\xd7", NULL, NULL, "UTF-8");
	SNIFF("only a sequence cut short", "<p>\xe2\x82", NULL, NULL, "windows-1252");
	SNIFF("a <meta> in the prescan wins", "<meta charset=windows-1255><p>\xd7\xa9</p>", NULL, NULL,
	      "windows-1255");
	SNIFF("the HTTP charset wins", "<p>\xd7\xa9\xd7\x9c</p>", "windows-1255", NULL, "windows-1255");
	SNIFF("a label EWeb hasn't: sniffed", "<p>\xd7\xa9\xd7\x9c</p>", "x-mac-hebrew", NULL, "UTF-8");
	SNIFF("a <meta> with a label libparserutils hasn't (cp1255)", "<meta charset=cp1255><p>\xf9</p>", NULL, NULL,
	      "windows-1255");
	SNIFF("an HTTP charset libparserutils hasn't (x-cp1255)", "<p>\xf9\xec</p>", "x-cp1255", NULL, "windows-1255");
	SNIFF("a <meta> naming UTF-16 is UTF-8", "<meta charset=unicodefffe><p>\xd7\xa9</p>", NULL, "windows-1255",
	      "UTF-8");
	SNIFF("an HTTP charset utf-16 is UTF-16LE", "<\0p\0>\0", "utf-16", NULL, "UTF-16LE");
	SNIFF("the choice by hand wins over the page's meta", "<meta charset=utf-8><p>\xf9</p>", "ISO-8859-8", NULL,
	      "ISO-8859-8");
	SNIFF("a BOM wins over everything", "\xef\xbb\xbf<p>\xd7\xa9</p>", "windows-1255", "windows-1255", "UTF-8");
	SNIFF("UTF-16 by its BOM", "\xff\xfe<\0p\0>\0", NULL, NULL, "UTF-16LE");
	/* found UTF-8 is tentative: a <meta> past the prescan's 1024 bytes
	   still says (the parse begins again, as for windows-1252) */
	memset(late, 'x', sizeof late);
	{
		static const char a[] = "<title>\xd7\xa9</title>", m[] = "<meta charset=windows-1255>";
		memcpy(late, a, sizeof a - 1);
		memcpy(late + 1200, m, sizeof m - 1);
	}
	late[sizeof late - 1] = 0;
	sniff("a <meta> past the prescan after UTF-8 found", late, strlen(late), NULL, NULL, "windows-1255");
	/* a character beyond ASCII at each place in a run of ASCII, from an
	   odd address and an even one (the detection reads four at a time) */
	for (k = 0; k < 2; k++)
		for (i = 0; i + 2 < 40; i++) {
			char name[60], *s = aligned + k;
			memset(aligned, 'a', sizeof aligned);
			s[i] = (char) 0xc3;
			s[i + 1] = (char) 0xa9;
			snprintf(name, sizeof name, "UTF-8 at %d, from %s", (int) i, k ? "odd" : "even");
			sniff(name, s, 40, NULL, NULL, "UTF-8");
			s[i + 1] = 'a';
			snprintf(name, sizeof name, "a lone lead byte at %d, from %s", (int) i, k ? "odd" : "even");
			sniff(name, s, 40, NULL, NULL, "windows-1252");
		}
	/* text files: no prescan, the rest as for a page; their text as it is */
	PLAIN("a text file", "<p>a & b</p>\n", "<p>a & b</p>\n", "windows-1252");
	PLAIN("a text file's first newline stays", "\nx", "\nx", "windows-1252");
	PLAIN("a text file in UTF-8", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "UTF-8");
	PLAIN("a text file's <meta> is text", "<meta charset=windows-1255>\xd7\xa9",
	      "<meta charset=windows-1255>\xd7\xa9", "UTF-8");
	PLAIN("a text file's CR LF", "a\r\nb\rc", "a\nb\nc", "windows-1252");
}

static void fallbacks(void)
{
	static const struct {
		const char *url, *want;
	} t[] = {
		{ "http://www.example.co.il/page.html", "windows-1255" },
		{ "https://EXAMPLE.IL:8080/x?y#z", "windows-1255" },
		{ "http://example.il./", "windows-1255" },
		{ "http://user:pw@example.il/", "windows-1255" },
		{ "http://example.il@example.com/", "windows-1252" },
		{ "http://il.example.com/", "windows-1252" },
		{ "http://example.com/x.il", "windows-1252" },
		{ "http://example.com/?q=.il", "windows-1252" },
		{ "http://xil/", "windows-1252" },
		{ "http://.il/", "windows-1252" },
		{ "http://[::1]/", "windows-1252" },
		{ "file:///C:/PAGES/X.IL", "windows-1252" },
		{ "about:blank", "windows-1252" }
	};
	size_t i;
	for (i = 0; i < sizeof t / sizeof t[0]; i++) {
		const char *got = br_fallback(t[i].url);
		result(t[i].url, strcmp(got, t[i].want) == 0, got);
	}
}

static void labels(void)
{
	static const struct {
		const char *label, *want;
	} t[] = {
		/* the Encoding Standard's labels (encodings.json) of every
		   encoding EWeb decodes (not windows-874: libparserutils has
		   ISO-8859-11, without its 0x80-0x9F) */
		{ "unicode-1-1-utf-8", "UTF-8" }, { "unicode11utf8", "UTF-8" }, { "unicode20utf8", "UTF-8" },
		{ "utf-8", "UTF-8" }, { "utf8", "UTF-8" }, { "x-unicode20utf8", "UTF-8" }, { "csisolatin2", "ISO-8859-2" },
		{ "iso-8859-2", "ISO-8859-2" }, { "iso-ir-101", "ISO-8859-2" }, { "iso8859-2", "ISO-8859-2" },
		{ "iso88592", "ISO-8859-2" }, { "iso_8859-2", "ISO-8859-2" }, { "iso_8859-2:1987", "ISO-8859-2" },
		{ "l2", "ISO-8859-2" }, { "latin2", "ISO-8859-2" }, { "csisolatin3", "ISO-8859-3" },
		{ "iso-8859-3", "ISO-8859-3" }, { "iso-ir-109", "ISO-8859-3" }, { "iso8859-3", "ISO-8859-3" },
		{ "iso88593", "ISO-8859-3" }, { "iso_8859-3", "ISO-8859-3" }, { "iso_8859-3:1988", "ISO-8859-3" },
		{ "l3", "ISO-8859-3" }, { "latin3", "ISO-8859-3" }, { "csisolatin4", "ISO-8859-4" },
		{ "iso-8859-4", "ISO-8859-4" }, { "iso-ir-110", "ISO-8859-4" }, { "iso8859-4", "ISO-8859-4" },
		{ "iso88594", "ISO-8859-4" }, { "iso_8859-4", "ISO-8859-4" }, { "iso_8859-4:1988", "ISO-8859-4" },
		{ "l4", "ISO-8859-4" }, { "latin4", "ISO-8859-4" }, { "csisolatincyrillic", "ISO-8859-5" },
		{ "cyrillic", "ISO-8859-5" }, { "iso-8859-5", "ISO-8859-5" }, { "iso-ir-144", "ISO-8859-5" },
		{ "iso8859-5", "ISO-8859-5" }, { "iso88595", "ISO-8859-5" }, { "iso_8859-5", "ISO-8859-5" },
		{ "iso_8859-5:1988", "ISO-8859-5" }, { "arabic", "ISO-8859-6" }, { "asmo-708", "ISO-8859-6" },
		{ "csiso88596e", "ISO-8859-6" }, { "csiso88596i", "ISO-8859-6" }, { "csisolatinarabic", "ISO-8859-6" },
		{ "ecma-114", "ISO-8859-6" }, { "iso-8859-6", "ISO-8859-6" }, { "iso-8859-6-e", "ISO-8859-6" },
		{ "iso-8859-6-i", "ISO-8859-6" }, { "iso-ir-127", "ISO-8859-6" }, { "iso8859-6", "ISO-8859-6" },
		{ "iso88596", "ISO-8859-6" }, { "iso_8859-6", "ISO-8859-6" }, { "iso_8859-6:1987", "ISO-8859-6" },
		{ "csisolatingreek", "ISO-8859-7" }, { "ecma-118", "ISO-8859-7" }, { "elot_928", "ISO-8859-7" },
		{ "greek", "ISO-8859-7" }, { "greek8", "ISO-8859-7" }, { "iso-8859-7", "ISO-8859-7" },
		{ "iso-ir-126", "ISO-8859-7" }, { "iso8859-7", "ISO-8859-7" }, { "iso88597", "ISO-8859-7" },
		{ "iso_8859-7", "ISO-8859-7" }, { "iso_8859-7:1987", "ISO-8859-7" }, { "sun_eu_greek", "ISO-8859-7" },
		{ "csiso88598e", "ISO-8859-8" }, { "csisolatinhebrew", "ISO-8859-8" }, { "hebrew", "ISO-8859-8" },
		{ "iso-8859-8", "ISO-8859-8" }, { "iso-8859-8-e", "ISO-8859-8" }, { "iso-ir-138", "ISO-8859-8" },
		{ "iso8859-8", "ISO-8859-8" }, { "iso88598", "ISO-8859-8" }, { "iso_8859-8", "ISO-8859-8" },
		{ "iso_8859-8:1988", "ISO-8859-8" }, { "visual", "ISO-8859-8" }, { "csiso88598i", "ISO-8859-8-I" },
		{ "iso-8859-8-i", "ISO-8859-8-I" }, { "logical", "ISO-8859-8-I" }, { "csisolatin6", "ISO-8859-10" },
		{ "iso-8859-10", "ISO-8859-10" }, { "iso-ir-157", "ISO-8859-10" }, { "iso8859-10", "ISO-8859-10" },
		{ "iso885910", "ISO-8859-10" }, { "l6", "ISO-8859-10" }, { "latin6", "ISO-8859-10" },
		{ "iso-8859-13", "ISO-8859-13" }, { "iso8859-13", "ISO-8859-13" }, { "iso885913", "ISO-8859-13" },
		{ "iso-8859-14", "ISO-8859-14" }, { "iso8859-14", "ISO-8859-14" }, { "iso885914", "ISO-8859-14" },
		{ "csisolatin9", "ISO-8859-15" }, { "iso-8859-15", "ISO-8859-15" }, { "iso8859-15", "ISO-8859-15" },
		{ "iso885915", "ISO-8859-15" }, { "iso_8859-15", "ISO-8859-15" }, { "l9", "ISO-8859-15" },
		{ "iso-8859-16", "ISO-8859-16" }, { "cp1250", "windows-1250" }, { "windows-1250", "windows-1250" },
		{ "x-cp1250", "windows-1250" }, { "cp1251", "windows-1251" }, { "windows-1251", "windows-1251" },
		{ "x-cp1251", "windows-1251" }, { "ansi_x3.4-1968", "windows-1252" }, { "ascii", "windows-1252" },
		{ "cp1252", "windows-1252" }, { "cp819", "windows-1252" }, { "csisolatin1", "windows-1252" },
		{ "ibm819", "windows-1252" }, { "iso-8859-1", "windows-1252" }, { "iso-ir-100", "windows-1252" },
		{ "iso8859-1", "windows-1252" }, { "iso88591", "windows-1252" }, { "iso_8859-1", "windows-1252" },
		{ "iso_8859-1:1987", "windows-1252" }, { "l1", "windows-1252" }, { "latin1", "windows-1252" },
		{ "us-ascii", "windows-1252" }, { "windows-1252", "windows-1252" }, { "x-cp1252", "windows-1252" },
		{ "cp1253", "windows-1253" }, { "windows-1253", "windows-1253" }, { "x-cp1253", "windows-1253" },
		{ "cp1254", "windows-1254" }, { "csisolatin5", "windows-1254" }, { "iso-8859-9", "windows-1254" },
		{ "iso-ir-148", "windows-1254" }, { "iso8859-9", "windows-1254" }, { "iso88599", "windows-1254" },
		{ "iso_8859-9", "windows-1254" }, { "iso_8859-9:1989", "windows-1254" }, { "l5", "windows-1254" },
		{ "latin5", "windows-1254" }, { "windows-1254", "windows-1254" }, { "x-cp1254", "windows-1254" },
		{ "cp1255", "windows-1255" }, { "windows-1255", "windows-1255" }, { "x-cp1255", "windows-1255" },
		{ "cp1256", "windows-1256" }, { "windows-1256", "windows-1256" }, { "x-cp1256", "windows-1256" },
		{ "cp1257", "windows-1257" }, { "windows-1257", "windows-1257" }, { "x-cp1257", "windows-1257" },
		{ "cp1258", "windows-1258" }, { "windows-1258", "windows-1258" }, { "x-cp1258", "windows-1258" },
		{ "unicodefffe", "UTF-16BE" }, { "utf-16be", "UTF-16BE" }, { "csunicode", "UTF-16LE" },
		{ "iso-10646-ucs-2", "UTF-16LE" }, { "ucs-2", "UTF-16LE" }, { "unicode", "UTF-16LE" },
		{ "unicodefeff", "UTF-16LE" }, { "utf-16", "UTF-16LE" }, { "utf-16le", "UTF-16LE" },
		/* and as pages write them */
		{ "utf8", "UTF-8" }, { "UNICODE-1-1-UTF-8", "UTF-8" }, { "latin1", "windows-1252" },
		{ "iso-8859-1", "windows-1252" }, { "US-ASCII", "windows-1252" }, { "iso-8859-9", "windows-1254" },
		{ "ISO-8859-8-i", "ISO-8859-8-I" }, { "logical", "ISO-8859-8-I" }, { "visual", "ISO-8859-8" },
		{ " windows-1255\t", "windows-1255" }, { "csISOLatinHebrew", "ISO-8859-8" }, { "x-nothing", NULL },
		{ "", NULL }
	};
	size_t i;
	int k;
	for (i = 0; i < sizeof t / sizeof t[0]; i++) {
		const char *got = html_encoding(t[i].label);
		char name[64];
		snprintf(name, sizeof name, "the label \"%s\"", t[i].label);
		result(name, t[i].want == NULL ? got == NULL : got != NULL && strcmp(got, t[i].want) == 0,
		       got != NULL ? got : "(none)");
	}
	/* View > Encoding's list: each by the name html_encoding gives it,
	   a byte a character (but UTF-8), the menu's label found by it */
	for (k = 0; k < br_nencodings; k++) {
		const char *n = br_encodings[k].name, *got = html_encoding(n);
		uint16_t hi[128];
		int one = html_enc_table(n, hi);
		char name[80];
		snprintf(name, sizeof name, "View > Encoding's %s", n);
		result(name, got != NULL && strcmp(got, n) == 0 && one == (strcmp(n, "UTF-8") != 0) &&
		       strcmp(br_encoding_label(n), br_encodings[k].label) == 0, got != NULL ? got : "(none)");
	}
	{
		uint16_t hi[128];
		result("windows-1255's table", html_enc_table("windows-1255", hi) && hi[0xe0 - 0x80] == 0x5d0 &&
		       hi[0x80 - 0x80] == 0x20ac && hi[0xfa - 0x80] == 0x5ea, "");
		result("ISO-8859-8's table: what it lacks", html_enc_table("ISO-8859-8", hi) && hi[0xe0 - 0x80] == 0x5d0 &&
		       hi[0xa1 - 0x80] == 0xfffd, "");
		result("a label's table is its encoding's: latin1 is windows-1252",
		       html_enc_table("latin1", hi) && hi[0] == 0x20ac && hi[0xe9 - 0x80] == 0xe9, "");
		result("no table for UTF-16", !html_enc_table("UTF-16LE", hi), "");
		result("no table for no name", !html_enc_table("", hi) && !html_enc_table("x-nothing", hi), "");
	}
}

/* the Encoding Standard's indexes (encoding.spec.whatwg.org, its
   index-NAME.txt files of 18 Sep 2024; the standard is the WHATWG's, CC BY
   4.0) of the encodings EWeb reads a byte a character: the code points of
   the bytes 0x80-0xFF, FFFD for the bytes an index leaves out (ISO-8859-8-I
   has ISO-8859-8's). Not windows-874: libparserutils has ISO-8859-11. */
static const struct {
	const char *name, *cp;
} sb_index[] = {
	{ "windows-1250",
	  "20AC0081201A0083201E2026202020210088203001602039015A0164017D0179"
	  "009020182019201C201D202220132014009821220161203A015B0165017E017A"
	  "00A002C702D8014100A4010400A600A700A800A9015E00AB00AC00AD00AE017B"
	  "00B000B102DB014200B400B500B600B700B80105015F00BB013D02DD013E017C"
	  "015400C100C2010200C40139010600C7010C00C9011800CB011A00CD00CE010E"
	  "01100143014700D300D4015000D600D70158016E00DA017000DC00DD016200DF"
	  "015500E100E2010300E4013A010700E7010D00E9011900EB011B00ED00EE010F"
	  "01110144014800F300F4015100F600F70159016F00FA017100FC00FD016302D9" },
	{ "windows-1251",
	  "04020403201A0453201E20262020202120AC203004092039040A040C040B040F"
	  "045220182019201C201D202220132014009821220459203A045A045C045B045F"
	  "00A0040E045E040800A4049000A600A7040100A9040400AB00AC00AD00AE0407"
	  "00B000B104060456049100B500B600B704512116045400BB0458040504550457"
	  "0410041104120413041404150416041704180419041A041B041C041D041E041F"
	  "0420042104220423042404250426042704280429042A042B042C042D042E042F"
	  "0430043104320433043404350436043704380439043A043B043C043D043E043F"
	  "0440044104420443044404450446044704480449044A044B044C044D044E044F" },
	{ "windows-1252",
	  "20AC0081201A0192201E20262020202102C62030016020390152008D017D008F"
	  "009020182019201C201D20222013201402DC21220161203A0153009D017E0178"
	  "00A000A100A200A300A400A500A600A700A800A900AA00AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900BA00BB00BC00BD00BE00BF"
	  "00C000C100C200C300C400C500C600C700C800C900CA00CB00CC00CD00CE00CF"
	  "00D000D100D200D300D400D500D600D700D800D900DA00DB00DC00DD00DE00DF"
	  "00E000E100E200E300E400E500E600E700E800E900EA00EB00EC00ED00EE00EF"
	  "00F000F100F200F300F400F500F600F700F800F900FA00FB00FC00FD00FE00FF" },
	{ "windows-1253",
	  "20AC0081201A0192201E20262020202100882030008A2039008C008D008E008F"
	  "009020182019201C201D20222013201400982122009A203A009C009D009E009F"
	  "00A00385038600A300A400A500A600A700A800A9FFFD00AB00AC00AD00AE2015"
	  "00B000B100B200B3038400B500B600B703880389038A00BB038C00BD038E038F"
	  "0390039103920393039403950396039703980399039A039B039C039D039E039F"
	  "03A003A1FFFD03A303A403A503A603A703A803A903AA03AB03AC03AD03AE03AF"
	  "03B003B103B203B303B403B503B603B703B803B903BA03BB03BC03BD03BE03BF"
	  "03C003C103C203C303C403C503C603C703C803C903CA03CB03CC03CD03CEFFFD" },
	{ "windows-1254",
	  "20AC0081201A0192201E20262020202102C62030016020390152008D008E008F"
	  "009020182019201C201D20222013201402DC21220161203A0153009D009E0178"
	  "00A000A100A200A300A400A500A600A700A800A900AA00AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900BA00BB00BC00BD00BE00BF"
	  "00C000C100C200C300C400C500C600C700C800C900CA00CB00CC00CD00CE00CF"
	  "011E00D100D200D300D400D500D600D700D800D900DA00DB00DC0130015E00DF"
	  "00E000E100E200E300E400E500E600E700E800E900EA00EB00EC00ED00EE00EF"
	  "011F00F100F200F300F400F500F600F700F800F900FA00FB00FC0131015F00FF" },
	{ "windows-1255",
	  "20AC0081201A0192201E20262020202102C62030008A2039008C008D008E008F"
	  "009020182019201C201D20222013201402DC2122009A203A009C009D009E009F"
	  "00A000A100A200A320AA00A500A600A700A800A900D700AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900F700BB00BC00BD00BE00BF"
	  "05B005B105B205B305B405B505B605B705B805B905BA05BB05BC05BD05BE05BF"
	  "05C005C105C205C305F005F105F205F305F4FFFDFFFDFFFDFFFDFFFDFFFDFFFD"
	  "05D005D105D205D305D405D505D605D705D805D905DA05DB05DC05DD05DE05DF"
	  "05E005E105E205E305E405E505E605E705E805E905EAFFFDFFFD200E200FFFFD" },
	{ "windows-1256",
	  "20AC067E201A0192201E20262020202102C62030067920390152068606980688"
	  "06AF20182019201C201D20222013201406A921220691203A0153200C200D06BA"
	  "00A0060C00A200A300A400A500A600A700A800A906BE00AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B9061B00BB00BC00BD00BE061F"
	  "06C1062106220623062406250626062706280629062A062B062C062D062E062F"
	  "063006310632063306340635063600D7063706380639063A0640064106420643"
	  "00E0064400E2064506460647064800E700E800E900EA00EB0649064A00EE00EF"
	  "064B064C064D064E00F4064F065000F7065100F9065200FB00FC200E200F06D2" },
	{ "windows-1257",
	  "20AC0081201A0083201E20262020202100882030008A2039008C00A802C700B8"
	  "009020182019201C201D20222013201400982122009A203A009C00AF02DB009F"
	  "00A0FFFD00A200A300A4FFFD00A600A700D800A9015600AB00AC00AD00AE00C6"
	  "00B000B100B200B300B400B500B600B700F800B9015700BB00BC00BD00BE00E6"
	  "0104012E0100010600C400C501180112010C00C90179011601220136012A013B"
	  "01600143014500D3014C00D500D600D701720141015A016A00DC017B017D00DF"
	  "0105012F0101010700E400E501190113010D00E9017A011701230137012B013C"
	  "01610144014600F3014D00F500F600F701730142015B016B00FC017C017E02D9" },
	{ "windows-1258",
	  "20AC0081201A0192201E20262020202102C62030008A20390152008D008E008F"
	  "009020182019201C201D20222013201402DC2122009A203A0153009D009E0178"
	  "00A000A100A200A300A400A500A600A700A800A900AA00AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900BA00BB00BC00BD00BE00BF"
	  "00C000C100C2010200C400C500C600C700C800C900CA00CB030000CD00CE00CF"
	  "011000D1030900D300D401A000D600D700D800D900DA00DB00DC01AF030300DF"
	  "00E000E100E2010300E400E500E600E700E800E900EA00EB030100ED00EE00EF"
	  "011100F1032300F300F401A100F600F700F800F900FA00FB00FC01B020AB00FF" },
	{ "iso-8859-2",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0010402D8014100A4013D015A00A700A80160015E0164017900AD017D017B"
	  "00B0010502DB014200B4013E015B02C700B80161015F0165017A02DD017E017C"
	  "015400C100C2010200C40139010600C7010C00C9011800CB011A00CD00CE010E"
	  "01100143014700D300D4015000D600D70158016E00DA017000DC00DD016200DF"
	  "015500E100E2010300E4013A010700E7010D00E9011900EB011B00ED00EE010F"
	  "01110144014800F300F4015100F600F70159016F00FA017100FC00FD016302D9" },
	{ "iso-8859-3",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0012602D800A300A4FFFD012400A700A80130015E011E013400ADFFFD017B"
	  "00B0012700B200B300B400B5012500B700B80131015F011F013500BDFFFD017C"
	  "00C000C100C2FFFD00C4010A010800C700C800C900CA00CB00CC00CD00CE00CF"
	  "FFFD00D100D200D300D4012000D600D7011C00D900DA00DB00DC016C015C00DF"
	  "00E000E100E2FFFD00E4010B010900E700E800E900EA00EB00EC00ED00EE00EF"
	  "FFFD00F100F200F300F4012100F600F7011D00F900FA00FB00FC016D015D02D9" },
	{ "iso-8859-4",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A001040138015600A40128013B00A700A8016001120122016600AD017D00AF"
	  "00B0010502DB015700B40129013C02C700B80161011301230167014A017E014B"
	  "010000C100C200C300C400C500C6012E010C00C9011800CB011600CD00CE012A"
	  "01100145014C013600D400D500D600D700D8017200DA00DB00DC0168016A00DF"
	  "010100E100E200E300E400E500E6012F010D00E9011900EB011700ED00EE012B"
	  "01110146014D013700F400F500F600F700F8017300FA00FB00FC0169016B02D9" },
	{ "iso-8859-5",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0040104020403040404050406040704080409040A040B040C00AD040E040F"
	  "0410041104120413041404150416041704180419041A041B041C041D041E041F"
	  "0420042104220423042404250426042704280429042A042B042C042D042E042F"
	  "0430043104320433043404350436043704380439043A043B043C043D043E043F"
	  "0440044104420443044404450446044704480449044A044B044C044D044E044F"
	  "2116045104520453045404550456045704580459045A045B045C00A7045E045F" },
	{ "iso-8859-6",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0FFFDFFFDFFFD00A4FFFDFFFDFFFDFFFDFFFDFFFDFFFD060C00ADFFFDFFFD"
	  "FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD061BFFFDFFFDFFFD061F"
	  "FFFD062106220623062406250626062706280629062A062B062C062D062E062F"
	  "0630063106320633063406350636063706380639063AFFFDFFFDFFFDFFFDFFFD"
	  "0640064106420643064406450646064706480649064A064B064C064D064E064F"
	  "065006510652FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD" },
	{ "iso-8859-7",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A02018201900A320AC20AF00A600A700A800A9037A00AB00AC00ADFFFD2015"
	  "00B000B100B200B303840385038600B703880389038A00BB038C00BD038E038F"
	  "0390039103920393039403950396039703980399039A039B039C039D039E039F"
	  "03A003A1FFFD03A303A403A503A603A703A803A903AA03AB03AC03AD03AE03AF"
	  "03B003B103B203B303B403B503B603B703B803B903BA03BB03BC03BD03BE03BF"
	  "03C003C103C203C303C403C503C603C703C803C903CA03CB03CC03CD03CEFFFD" },
	{ "iso-8859-8",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0FFFD00A200A300A400A500A600A700A800A900D700AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900F700BB00BC00BD00BEFFFD"
	  "FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD"
	  "FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD2017"
	  "05D005D105D205D305D405D505D605D705D805D905DA05DB05DC05DD05DE05DF"
	  "05E005E105E205E305E405E505E605E705E805E905EAFFFDFFFD200E200FFFFD" },
	{ "iso-8859-8-i",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0FFFD00A200A300A400A500A600A700A800A900D700AB00AC00AD00AE00AF"
	  "00B000B100B200B300B400B500B600B700B800B900F700BB00BC00BD00BEFFFD"
	  "FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD"
	  "FFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFDFFFD2017"
	  "05D005D105D205D305D405D505D605D705D805D905DA05DB05DC05DD05DE05DF"
	  "05E005E105E205E305E405E505E605E705E805E905EAFFFDFFFD200E200FFFFD" },
	{ "iso-8859-10",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0010401120122012A0128013600A7013B011001600166017D00AD016A014A"
	  "00B0010501130123012B0129013700B7013C011101610167017E2015016B014B"
	  "010000C100C200C300C400C500C6012E010C00C9011800CB011600CD00CE00CF"
	  "00D00145014C00D300D400D500D6016800D8017200DA00DB00DC00DD00DE00DF"
	  "010100E100E200E300E400E500E6012F010D00E9011900EB011700ED00EE00EF"
	  "00F00146014D00F300F400F500F6016900F8017300FA00FB00FC00FD00FE0138" },
	{ "iso-8859-13",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A0201D00A200A300A4201E00A600A700D800A9015600AB00AC00AD00AE00C6"
	  "00B000B100B200B3201C00B500B600B700F800B9015700BB00BC00BD00BE00E6"
	  "0104012E0100010600C400C501180112010C00C90179011601220136012A013B"
	  "01600143014500D3014C00D500D600D701720141015A016A00DC017B017D00DF"
	  "0105012F0101010700E400E501190113010D00E9017A011701230137012B013C"
	  "01610144014600F3014D00F500F600F701730142015B016B00FC017C017E2019" },
	{ "iso-8859-14",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A01E021E0300A3010A010B1E0A00A71E8000A91E821E0B1EF200AD00AE0178"
	  "1E1E1E1F012001211E401E4100B61E561E811E571E831E601EF31E841E851E61"
	  "00C000C100C200C300C400C500C600C700C800C900CA00CB00CC00CD00CE00CF"
	  "017400D100D200D300D400D500D61E6A00D800D900DA00DB00DC00DD017600DF"
	  "00E000E100E200E300E400E500E600E700E800E900EA00EB00EC00ED00EE00EF"
	  "017500F100F200F300F400F500F61E6B00F800F900FA00FB00FC00FD017700FF" },
	{ "iso-8859-15",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A000A100A200A320AC00A5016000A7016100A900AA00AB00AC00AD00AE00AF"
	  "00B000B100B200B3017D00B500B600B7017E00B900BA00BB01520153017800BF"
	  "00C000C100C200C300C400C500C600C700C800C900CA00CB00CC00CD00CE00CF"
	  "00D000D100D200D300D400D500D600D700D800D900DA00DB00DC00DD00DE00DF"
	  "00E000E100E200E300E400E500E600E700E800E900EA00EB00EC00ED00EE00EF"
	  "00F000F100F200F300F400F500F600F700F800F900FA00FB00FC00FD00FE00FF" },
	{ "iso-8859-16",
	  "0080008100820083008400850086008700880089008A008B008C008D008E008F"
	  "0090009100920093009400950096009700980099009A009B009C009D009E009F"
	  "00A001040105014120AC201E016000A7016100A9021800AB017900AD017A017B"
	  "00B000B1010C0142017D201D00B600B7017E010D021900BB015201530178017C"
	  "00C000C100C2010200C4010600C600C700C800C900CA00CB00CC00CD00CE00CF"
	  "0110014300D200D300D4015000D6015A017000D900DA00DB00DC0118021A00DF"
	  "00E000E100E2010300E4010700E600E700E800E900EA00EB00EC00ED00EE00EF"
	  "0111014400F200F300F4015100F6015B017100F900FA00FB00FC0119021B00FF" },
};

/* each encoding's table (the decoder's, html_enc_table) against its index:
   0x80-0x9F's C1 controls in the ISO-8859s and the Windows code pages'
   unassigned bytes, and windows-1255's 0xCA (U+05BA), were once U+FFFD */
static void indexes(void)
{
	size_t k;
	int i;
	for (k = 0; k < sizeof sb_index / sizeof sb_index[0]; k++) {
		uint16_t hi[128];
		char name[64], got[80];
		int ok = html_enc_table(sb_index[k].name, hi);
		snprintf(name, sizeof name, "%s against the Encoding Standard's index", sb_index[k].name);
		snprintf(got, sizeof got, "no table");
		for (i = 0; ok && i < 128; i++) {
			char four[5];
			unsigned want;
			memcpy(four, sb_index[k].cp + 4 * i, 4);
			four[4] = 0;
			want = (unsigned) strtoul(four, NULL, 16);
			if (hi[i] != want) {
				snprintf(got, sizeof got, "byte 0x%02X is U+%04X, the index's U+%04X", 0x80 + i, hi[i], want);
				ok = 0;
			}
		}
		result(name, ok, got);
	}
}

/* url (UTF-8) parsed in a page in enc ("UTF-8" or one of a byte a
   character), relative to base or not */
static void query(const char *name, const char *enc, const char *base, const char *url, const char *want)
{
	static URL u, b;
	URL_ENC t, *e = NULL;
	int ok;
	if (strcmp(enc, "UTF-8") != 0) {
		if (!html_enc_table(enc, t.hi)) {
			result(name, 0, "no table");
			return;
		}
		e = &t;
	}
	if (base != NULL) {
		url_parse(&b, base, NULL);
		ok = url_parse_enc(&u, url, &b, e);
	} else {
		ok = url_parse_enc(&u, url, NULL, e);
	}
	result(name, ok && strcmp(u.href, want) == 0, ok ? u.href : "failure");
}

static void queries(void)
{
	/* the Web Platform Tests' url/resources/percent-encoding.json, as
	   url/percent-encoding.window.js uses it (?INPUT#INPUT on a page in
	   the encoding): those for UTF-8, windows-1252 and ISO-8859-2 */
#define WPT "https://doesnotmatter.invalid/?"
	query("WPT U+2020 utf-8", "UTF-8", NULL, WPT "\xe2\x80\xa0#\xe2\x80\xa0", WPT "%E2%80%A0#%E2%80%A0");
	query("WPT U+2020 windows-1252", "windows-1252", NULL, WPT "\xe2\x80\xa0#\xe2\x80\xa0", WPT "%86#%E2%80%A0");
	query("WPT U+000E A utf-8", "UTF-8", NULL, WPT "\x0e" "A#\x0e" "A", WPT "%0EA#%0EA");
	query("WPT U+00A2 utf-8", "UTF-8", NULL, WPT "\xc2\xa2#\xc2\xa2", WPT "%C2%A2#%C2%A2");
	query("WPT U+00A2 iso-8859-2", "ISO-8859-2", NULL, WPT "\xc2\xa2#\xc2\xa2", WPT "%26%23162%3B#%C2%A2");
	query("WPT a-acute | utf-8", "UTF-8", NULL, WPT "\xc3\xa1|#\xc3\xa1|", WPT "%C3%A1|#%C3%A1|");
	/* Hebrew: the query in the page's encoding, the path and the
	   fragment in UTF-8 whatever it is */
	query("Hebrew in windows-1255", "windows-1255", "http://example.co.il/a/", "s?q=\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",
	      "http://example.co.il/a/s?q=%F9%EC%E5%ED");
	query("Hebrew in ISO-8859-8", "ISO-8859-8", "http://example.co.il/a/", "?q=\xd7\x90",
	      "http://example.co.il/a/?q=%E0");
	query("Hebrew in UTF-8", "UTF-8", "http://example.co.il/a/", "?q=\xd7\x90", "http://example.co.il/a/?q=%D7%90");
	query("the path stays UTF-8", "windows-1255", NULL, "http://example.co.il/\xd7\x90?\xd7\x90#\xd7\x90",
	      "http://example.co.il/%D7%90?%E0#%D7%90");
	query("the euro in windows-1255", "windows-1255", NULL, "http://x.il/?p=\xe2\x82\xac", "http://x.il/?p=%80");
	query("what windows-1255 lacks", "windows-1255", NULL, "http://x.il/?c=\xe2\x9c\x93&e=\xc3\xa9",
	      "http://x.il/?c=%26%2310003%3B&e=%26%23233%3B");
	query("a 4-byte character it lacks", "windows-1255", NULL, "http://x.il/?\xf0\x9f\x98\x80",
	      "http://x.il/?%26%23128512%3B");
	query("percent-escapes stay", "windows-1255", NULL, "http://x.il/?q=%D7%90+%41", "http://x.il/?q=%D7%90+%41");
	query("the special-query set", "windows-1255", NULL, "http://x.il/?a b\"<>'`{}", "http://x.il/?a%20b%22%3C%3E%27`{}");
	query("not UTF-8: the byte as it is", "windows-1255", NULL, "http://x.il/?\xe0", "http://x.il/?%E0");
	query("a byte that can't start UTF-8: the byte", "windows-1255", NULL, "http://x.il/?\x80\xc1\xbf\xf8",
	      "http://x.il/?%80%C1%BF%F8");
	/* U+FFFD isn't any of the bytes an encoding leaves out (the table
	   has it for each of them): what the encoding lacks, as any other */
	query("U+FFFD in windows-1255", "windows-1255", NULL, "http://x.il/?q=\xef\xbf\xbd",
	      "http://x.il/?q=%26%2365533%3B");
	query("U+FFFD in ISO-8859-8", "ISO-8859-8", NULL, "http://x.il/?q=\xef\xbf\xbd", "http://x.il/?q=%26%2365533%3B");
	query("U+FFFD in windows-1252", "windows-1252", NULL, "http://x.com/?q=\xef\xbf\xbd",
	      "http://x.com/?q=%26%2365533%3B");
	/* what the decoders once left out, sent as the byte it was (as
	   browsers do), not as &#65533; */
	query("windows-1255's 0xCA (U+05BA)", "windows-1255", NULL, "http://x.il/?q=\xd6\xba", "http://x.il/?q=%CA");
	query("windows-1252's 0x81 and 0x8D (C1 controls)", "windows-1252", NULL, "http://x.com/?\xc2\x81\xc2\x8d",
	      "http://x.com/?%81%8D");
	query("ISO-8859-8's 0x80 (a C1 control)", "ISO-8859-8", NULL, "http://x.il/?\xc2\x80", "http://x.il/?%80");
	query("mailto keeps it", "windows-1255", NULL, "mailto:x@y.il?subject=\xd7\x90", "mailto:x@y.il?subject=\xd7\x90");
#undef WPT
}

/* the detection against a plain validator (the Encoding Standard's
   UTF-8 decoder's ranges, a byte at a time), on random bytes made mostly
   of ASCII and UTF-8 */
static int ref_utf8(const unsigned char *s, size_t n)
{
	size_t i = 0;
	int multi = 0;
	while (i < n) {
		unsigned c = s[i], k, lo = 0x80, hi = 0xbf, j;
		if (c < 0x80) {
			i++;
			continue;
		}
		if (c >= 0xc2 && c <= 0xdf)
			k = 2;
		else if (c >= 0xe0 && c <= 0xef)
			k = 3, lo = c == 0xe0 ? 0xa0 : 0x80, hi = c == 0xed ? 0x9f : 0xbf;
		else if (c >= 0xf0 && c <= 0xf4)
			k = 4, lo = c == 0xf0 ? 0x90 : 0x80, hi = c == 0xf4 ? 0x8f : 0xbf;
		else
			return 0;
		for (j = 1; j < k; j++) {
			if (i + j >= n)
				return multi;
			if (s[i + j] < lo || s[i + j] > hi)
				return 0;
			lo = 0x80, hi = 0xbf;
		}
		multi = 1;
		i += k;
	}
	return multi;
}

static void random_bytes(void)
{
	static const unsigned char pick[] = { 'a', '<', 'p', '>', ' ', 0xc3, 0xa9, 0xd7, 0x90, 0xe2, 0x82, 0xac,
					      0xf0, 0x9f, 0x98, 0x80, 0xed, 0xa0, 0xc0, 0xff, 0xe9, 0xf4, 0x8f };
	static unsigned char buf[80];
	unsigned long seed = 11;
	int i, k, bad = 0;
	for (i = 0; i < 20000 && !bad; i++) {
		int n, off;
		html_opts o;
		doc *d;
		seed = seed * 1103515245UL + 12345UL;
		n = (int) ((seed >> 16) % 64);
		seed = seed * 1103515245UL + 12345UL;
		off = (int) ((seed >> 16) % 4);
		for (k = 0; k < n; k++) {
			seed = seed * 1103515245UL + 12345UL;
			buf[off + k] = (seed >> 16) % 3 ? 'a' : pick[(seed >> 18) % sizeof pick];
		}
		memset(&o, 0, sizeof o);
		o.plaintext = 1;	/* (no prescan: the detection alone) */
		d = doc_parse_opts((const char *) buf + off, (size_t) n, &o, &media);
		if (d == NULL || strcmp(d->charset, ref_utf8(buf + off, (size_t) n) ? "UTF-8" : "windows-1252") != 0)
			bad = 1;
		doc_free(d);
	}
	result("random bytes against a plain validator", !bad, "differs");
}

int main(void)
{
	sniffing();
	fallbacks();
	labels();
	indexes();
	queries();
	random_bytes();
	fprintf(stderr, "enctest: %d passed, %d failed\n", passed, failed);
	return failed != 0;
}
