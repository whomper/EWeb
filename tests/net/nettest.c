/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nettest: EWeb's URL parser and HTTP client (src/net) on this computer,
 * with no network: common/tests/fuzz/fuzz_net.c plays the server, saying
 * what each case gives it in pieces of 1 to 97 bytes, so every boundary in
 * a line or a header is met. The cases are what servers do (lengths,
 * chunks, gzip and deflate, redirects, kept connections that the server
 * has closed) and what a hostile one might send (lengths that can't be
 * right, endless headers, a gzip bomb). Built with AddressSanitizer and
 * UBSan for make test. Prints the failures; non-zero if there were any. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "inflate.h"
#include "url.h"
#include "conn.h"
#include "fuzz_net.h"
#include "vectors.h"

static int fails, cases;

static void fail(const char *name, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(const char *name, const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "nettest: %s: ", name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fails++;
}

/* ---------------- URLs ---------------- */

static void url_case(const char *in, const char *base, const char *want)
{
	static URL b, u;
	int ok;
	cases++;
	if (base && !url_parse(&b, base, 0)) {
		fail("url", "base %s doesn't parse", base);
		return;
	}
	ok = url_parse(&u, in, base ? &b : 0);
	if (!want ? ok : !ok || strcmp(u.href, want))
		fail("url", "%s against %s: %s, wanted %s", in, base ? base : "nothing",
		     ok ? u.href : "failure", want ? want : "failure");
}

static void urls(void)
{
	static const char *rfc[][2] = {	/* RFC 3986, 5.4.1 and 5.4.2 */
		{ "g", "http://a/b/c/g" }, { "./g", "http://a/b/c/g" }, { "g/", "http://a/b/c/g/" },
		{ "/g", "http://a/g" }, { "//g", "http://g/" }, { "?y", "http://a/b/c/d;p?y" },
		{ "g?y", "http://a/b/c/g?y" }, { "#s", "http://a/b/c/d;p?q#s" },
		{ "g#s", "http://a/b/c/g#s" }, { "g?y#s", "http://a/b/c/g?y#s" },
		{ ";x", "http://a/b/c/;x" }, { "g;x", "http://a/b/c/g;x" },
		{ "g;x?y#s", "http://a/b/c/g;x?y#s" }, { "", "http://a/b/c/d;p?q" },
		{ ".", "http://a/b/c/" }, { "./", "http://a/b/c/" }, { "..", "http://a/b/" },
		{ "../", "http://a/b/" }, { "../g", "http://a/b/g" }, { "../..", "http://a/" },
		{ "../../", "http://a/" }, { "../../g", "http://a/g" }, { "../../../g", "http://a/g" },
		{ "../../../../g", "http://a/g" }, { "/./g", "http://a/g" }, { "/../g", "http://a/g" },
		{ "g.", "http://a/b/c/g." }, { ".g", "http://a/b/c/.g" }, { "g..", "http://a/b/c/g.." },
		{ "..g", "http://a/b/c/..g" }, { "./../g", "http://a/b/g" }, { "./g/.", "http://a/b/c/g/" },
		{ "g/./h", "http://a/b/c/g/h" }, { "g/../h", "http://a/b/c/h" },
		{ "g;x=1/./y", "http://a/b/c/g;x=1/y" }, { "g;x=1/../y", "http://a/b/c/y" },
		{ "g?y/./x", "http://a/b/c/g?y/./x" }, { "g#s/../x", "http://a/b/c/g#s/../x" },
		{ "http:g", "http://a/b/c/g" }, { "g:h", "g:h" },
	};
	URL u;
	char s[300];
	size_t i;
	for (i = 0; i < sizeof(rfc) / sizeof(rfc[0]); i++)
		url_case(rfc[i][0], "http://a/b/c/d;p?q", rfc[i][1]);
	/* what pages have in them */
	url_case("HTTPS://Example.COM:443/a b?c d#e f", 0, "https://example.com/a%20b?c%20d#e%20f");
	url_case("\\\\cdn.example.org\\x.css", "https://www.example.org/", "https://cdn.example.org/x.css");
	url_case("https://\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.co.il/", 0, "https://xn--9dbne9b.co.il/");
	url_case("/\xd7\xa9", "https://example.org/", "https://example.org/%D7%A9");
	url_case("http://0x7f.1/", 0, "http://127.0.0.1/");
	url_case("http://[0:0::1]:8080/x", 0, "http://[::1]:8080/x");
	url_case("http://user:pa:ss@host/", 0, "http://user:pa%3Ass@host/");
	url_case("javascript:void(0)", "https://example.org/", "javascript:void(0)");
	url_case("http://exa mple.org/", 0, 0);
	url_case("http://host:99999/", 0, 0);
	url_case("https://", 0, 0);
	url_case("page.html", 0, 0);
	cases++;
	if (!url_parse(&u, "https://[::1]:8443/a/b?c#d", 0))
		fail("url", "IPv6 with a port doesn't parse");
	url_target(&u, s, sizeof(s));
	if (strcmp(s, "/a/b?c"))
		fail("url", "target %s", s);
	url_hostport(&u, s, sizeof(s));
	if (strcmp(s, "[::1]:8443"))
		fail("url", "Host %s", s);
	url_host(&u, s, sizeof(s));
	if (strcmp(s, "::1") || u.port != 8443 || !u.tls)
		fail("url", "host %s port %u", s, u.port);
}

/* ---------------- inflate ---------------- */

static void inflates(void)
{
	SBUF b;
	int r;
	cases += 5;
	sb_init(&b);
	if (inflate_gzip(v_gz, sizeof(v_gz), &b, 1 << 20) != 1 || b.len != sizeof(v_text) ||
	    memcmp(b.s, v_text, b.len))
		fail("inflate", "gzip");
	sb_reset(&b);
	if (inflate_zlib(v_zl, sizeof(v_zl), &b, 1 << 20) != 1 || b.len != sizeof(v_text) ||
	    memcmp(b.s, v_text, b.len))
		fail("inflate", "zlib");
	sb_reset(&b);
	if ((r = inflate_gzip(v_bomb, sizeof(v_bomb), &b, 65536)) != -1 || b.len > 65536)
		fail("inflate", "the bomb gave %d with %ld bytes", r, b.len);
	sb_reset(&b);
	if (inflate_gzip(v_bad, sizeof(v_bad), &b, 1 << 20) == 1 &&
	    b.len == sizeof(v_text) && !memcmp(b.s, v_text, b.len))
		fail("inflate", "the damaged one came out whole");
	sb_reset(&b);
	if (inflate_gzip(v_gz, 20, &b, 1 << 20) == 1)
		fail("inflate", "a cut-off one came out whole");
	sb_free(&b);
}

/* ---------------- HTTP ---------------- */

static unsigned char said[4 << 20];
static long said_n;

/* what the server says: text, and binary bodies with %B */
static void say(const char *text)
{
	long n = (long)strlen(text);
	memcpy(said + said_n, text, n);
	said_n += n;
}

static void say_bin(const unsigned char *d, long n)
{
	memcpy(said + said_n, d, n);
	said_n += n;
}

typedef struct {
	int ok;
	HTTPRESP rs;
	SBUF body;
	char err[200];
} RES;

static int sink_bytes;
static int sink(void *ud, const char *d, long n)
{
	(void)ud;
	(void)d;
	sink_bytes += (int)n;
	return 1;
}

static void get(RES *r, const char *url, const HTTPREQ *with)
{
	HTTPREQ rq;
	memset(&rq, 0, sizeof(rq));
	if (with)
		rq = *with;
	rq.url = url;
	sb_init(&r->body);
	r->err[0] = 0;
	r->ok = http_request(&rq, &r->rs, &r->body, r->err, sizeof(r->err));
}

static void done(RES *r)
{
	http_resp_free(&r->rs);
	sb_free(&r->body);
}

/* one request to a server that says what was put together with say() */
static void one(RES *r, const char *url, const HTTPREQ *with)
{
	http_close_all();
	fuzz_net_set(said, said_n);
	get(r, url, with);
	said_n = 0;
}

static void want_body(const char *name, RES *r, int status, const char *body)
{
	cases++;
	if (!r->ok)
		fail(name, "failed: %s", r->err);
	else if (r->rs.status != status)
		fail(name, "status %d, wanted %d", r->rs.status, status);
	else if (body && (r->body.len != (long)strlen(body) || (r->body.len && memcmp(r->body.s, body, r->body.len))))
		fail(name, "body \"%.60s\" (%ld bytes), wanted \"%s\"", r->body.s ? r->body.s : "", r->body.len, body);
}

static void want_err(const char *name, RES *r, const char *part)
{
	cases++;
	if (r->ok)
		fail(name, "worked (status %d), wanted an error with \"%s\"", r->rs.status, part);
	else if (!strstr(r->err, part))
		fail(name, "error \"%s\", wanted one with \"%s\"", r->err, part);
}

static void https(void)
{
	static const char u[] = "http://x.test/a/b";
	RES r;
	HTTPREQ rq;
	char h[100];
	int i;

	say("HTTP/1.1 200 OK\r\nContent-Type: Text/HTML; charset=UTF-8\r\nContent-Length: 5\r\n\r\nhello");
	one(&r, u, 0);
	want_body("length", &r, 200, "hello");
	cases++;
	if (strcmp(r.rs.mime, "text/html") || strcmp(r.rs.charset, "UTF-8") || r.rs.length != 5)
		fail("length", "mime %s charset %s length %ld", r.rs.mime, r.rs.charset, r.rs.length);
	cases++;
	if (!http_header(&r.rs, "content-length", h, sizeof(h)) || strcmp(h, "5"))
		fail("header", "Content-Length read as %s", http_header(&r.rs, "content-length", h, sizeof(h)) ? h : "none");
	done(&r);

	say("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: text/html;charset=\"windows-1255\"\r\n\r\n"
	    "5;name=value\r\nhello\r\n6\r\n world\r\n0\r\nX-Trailer: 1\r\n\r\n");
	one(&r, u, 0);
	want_body("chunked", &r, 200, "hello world");
	cases++;
	if (strcmp(r.rs.charset, "windows-1255"))
		fail("chunked", "charset %s", r.rs.charset);
	done(&r);

	say("HTTP/1.0 200 OK\r\nX-Folded: a\r\n b\r\n\r\nall of it, up to the close");
	one(&r, u, 0);
	want_body("close", &r, 200, "all of it, up to the close");
	cases++;
	if (!http_header(&r.rs, "X-Folded", h, sizeof(h)) || strcmp(h, "a b"))
		fail("folded", "X-Folded: %s", h);
	done(&r);

	say("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200\r\nContent-Length: 2\r\n\r\nok");
	one(&r, u, 0);
	want_body("100 Continue", &r, 200, "ok");
	done(&r);

	/* gzip and deflate */
	{
		char hd[100];
		snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n", (int)sizeof(v_gz));
		say(hd);
		say_bin(v_gz, sizeof(v_gz));
		one(&r, u, 0);
		cases++;
		if (!r.ok || r.body.len != sizeof(v_text) || memcmp(r.body.s, v_text, r.body.len) ||
		    r.rs.length != sizeof(v_gz) || r.rs.size != sizeof(v_text) || r.rs.encoding != 1)
			fail("gzip", "%s, %ld bytes", r.ok ? "wrong" : r.err, r.body.len);
		done(&r);
		say("HTTP/1.1 200 OK\r\nContent-Encoding: deflate\r\nTransfer-Encoding: chunked\r\n\r\n");
		snprintf(hd, sizeof(hd), "%x\r\n", (unsigned)sizeof(v_zl));
		say(hd);
		say_bin(v_zl, sizeof(v_zl));
		say("\r\n0\r\n\r\n");
		one(&r, u, 0);
		cases++;
		if (!r.ok || r.body.len != sizeof(v_text) || memcmp(r.body.s, v_text, r.body.len))
			fail("deflate", "%s, %ld bytes", r.ok ? "wrong" : r.err, r.body.len);
		done(&r);
		snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n", (int)sizeof(v_bomb));
		say(hd);
		say_bin(v_bomb, sizeof(v_bomb));
		memset(&rq, 0, sizeof(rq));
		rq.maxbody = 1 << 20;
		one(&r, u, &rq);
		want_err("gzip bomb", &r, "unpacks to more than 1024 KB");
		done(&r);
		snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: %d\r\n\r\n", (int)sizeof(v_bad));
		say(hd);
		say_bin(v_bad, sizeof(v_bad));
		one(&r, u, 0);
		cases++;
		if (r.ok && r.body.len == sizeof(v_text) && !memcmp(r.body.s, v_text, r.body.len))
			fail("damaged gzip", "came out whole");
		done(&r);
	}

	/* redirects */
	say("HTTP/1.1 301 Moved\r\nLocation: ../c?q=1#f\r\nContent-Length: 4\r\n\r\ngone"
	    "HTTP/1.1 302 Found\r\nLocation: https://x.test/d\r\nContent-Length: 0\r\n\r\n");
	say("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nhere");
	one(&r, u, 0);
	cases++;
	/* the third hop is https: fuzz_net has no TLS, so it stops there */
	if (r.ok || r.rs.redirects != 2 || strcmp(r.rs.url.href, "https://x.test/d"))
		fail("redirect", "ok %d, %d redirects, at %s (%s)", r.ok, r.rs.redirects, r.rs.url.href, r.err);
	done(&r);
	say("HTTP/1.1 302 Found\r\nLocation: /c\r\nContent-Length: 0\r\n\r\n"
	    "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nhere");
	one(&r, u, 0);
	want_body("redirect", &r, 200, "here");
	cases++;
	if (r.rs.redirects != 1 || strcmp(r.rs.url.href, "http://x.test/c") || !r.rs.reused)
		fail("redirect", "%d redirects, at %s, reused %d", r.rs.redirects, r.rs.url.href, r.rs.reused);
	done(&r);
	say("HTTP/1.1 302 Found\r\nLocation: mailto:a@b.test\r\nContent-Length: 0\r\n\r\n");
	one(&r, u, 0);
	want_body("redirect to mailto", &r, 302, "");
	done(&r);
	for (i = 0; i < 22; i++)
		say("HTTP/1.1 302 Found\r\nLocation: /a/b\r\nContent-Length: 0\r\n\r\n");
	one(&r, u, 0);
	want_err("redirect loop", &r, "redirects too many times");
	done(&r);
	say("HTTP/1.1 303 See Other\r\nLocation: /done\r\nContent-Length: 0\r\n\r\n"
	    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
	memset(&rq, 0, sizeof(rq));
	rq.method = "POST";
	rq.body = "a=1";
	rq.bodylen = 3;
	one(&r, u, &rq);
	want_body("POST, 303", &r, 200, "ok");
	done(&r);
	memset(&rq, 0, sizeof(rq));
	rq.no_redirect = 1;
	say("HTTP/1.1 307 Temporary\r\nLocation: /x\r\nContent-Length: 0\r\n\r\n");
	one(&r, u, &rq);
	want_body("no redirect", &r, 307, "");
	done(&r);

	/* kept connections */
	http_close_all();
	say("HTTP/1.1 204 No Content\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
	fuzz_net_set(said, said_n);
	said_n = 0;
	get(&r, u, 0);
	want_body("204", &r, 204, "");
	done(&r);
	get(&r, u, 0);
	want_body("kept", &r, 200, "abc");
	cases++;
	if (!r.rs.reused)
		fail("kept", "a new connection");
	done(&r);
	/* the server closed the kept one: the request goes again, on a new one */
	http_close_all();
	say("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none" FUZZ_NET_NEXT
	    "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
	fuzz_net_set(said, said_n);
	said_n = 0;
	get(&r, u, 0);
	want_body("closed meanwhile 1", &r, 200, "one");
	done(&r);
	get(&r, u, 0);
	want_body("closed meanwhile 2", &r, 200, "two");
	cases++;
	if (r.rs.reused)
		fail("closed meanwhile", "it says the connection was kept");
	done(&r);
	http_close_all();

	/* HEAD, a download to a sink, and an error page beside it */
	memset(&rq, 0, sizeof(rq));
	rq.method = "HEAD";
	say("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n");
	one(&r, u, &rq);
	want_body("HEAD", &r, 200, "");
	done(&r);
	memset(&rq, 0, sizeof(rq));
	rq.sink = sink;
	sink_bytes = 0;
	say("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789");
	one(&r, u, &rq);
	want_body("sink", &r, 200, "");
	cases++;
	if (sink_bytes != 10)
		fail("sink", "%d bytes", sink_bytes);
	done(&r);
	say("HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnone");
	one(&r, u, &rq);
	want_body("sink, 404", &r, 404, "none");
	done(&r);

	/* what can't be believed */
	say("HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\nhello");
	one(&r, u, 0);
	want_err("length -5", &r, "length that can't be right");
	done(&r);
	say("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!");
	one(&r, u, 0);
	want_err("two lengths", &r, "length that can't be right");
	done(&r);
	say("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999\r\n\r\nhello");
	one(&r, u, 0);
	want_err("a huge length", &r, "length that can't be right");
	done(&r);
	say("HTTP/1.1 200 OK\r\nContent-Length: 2000000\r\n\r\nhello");
	memset(&rq, 0, sizeof(rq));
	rq.maxbody = 1 << 20;
	one(&r, u, &rq);
	want_err("too big", &r, "too big");
	done(&r);
	say("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
	for (i = 0; i < 300; i++)
		say("1000\r\n"), memset(said + said_n, 'x', 4096), said_n += 4096, say("\r\n");
	say("0\r\n\r\n");
	one(&r, u, &rq);
	want_err("chunks too big", &r, "too big");
	done(&r);
	say("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n0\r\n\r\n");
	one(&r, u, 0);
	want_err("bad chunk", &r, "chunk that can't be read");
	done(&r);
	say("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nfffffffffff\r\nhello\r\n0\r\n\r\n");
	one(&r, u, 0);
	want_err("huge chunk", &r, "chunk that can't be read");
	done(&r);
	say("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nhello\r\n0\r\n\r\n");
	one(&r, u, 0);
	want_err("chunk longer than said", &r, "longer than it said");
	done(&r);
	say("HTTP/1.1 200 OK\r\nX-Long: ");
	memset(said + said_n, 'a', 20000);
	said_n += 20000;
	say("\r\n\r\n");
	one(&r, u, 0);
	want_err("a long header", &r, "headers too long");
	done(&r);
	say("HTTP/1.1 200 OK\r\n");
	for (i = 0; i < 250; i++)
		say("X-Many: yes\r\n");
	say("\r\n");
	one(&r, u, 0);
	want_err("many headers", &r, "headers too long");
	done(&r);
	say("SSH-2.0-OpenSSH_9.6\r\n");
	one(&r, u, 0);
	want_err("not HTTP", &r, "didn't answer in HTTP");
	done(&r);
	say("HTTP/1.1 20 OK\r\n\r\n");
	one(&r, u, 0);
	want_err("a short status", &r, "didn't answer in HTTP");
	done(&r);
	say("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhalf");
	one(&r, u, 0);
	want_err("cut off", &r, "x.test");
	done(&r);
	say("");
	one(&r, u, 0);
	want_err("nobody there", &r, "x.test");
	done(&r);
	one(&r, "ftp://x.test/", 0);
	want_err("ftp", &r, "can't fetch");
	done(&r);
	http_close_all();
}

int main(void)
{
	urls();
	inflates();
	https();
	if (fails)
		fprintf(stderr, "nettest: %d of %d failed\n", fails, cases);
	else
		fprintf(stderr, "nettest: all %d passed\n", cases);
	return fails != 0;
}
