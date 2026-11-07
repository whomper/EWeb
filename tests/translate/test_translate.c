/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_translate.c - common/src/translate.c against a scripted network: the codes, the split,
 * the request, every answer the service may give (and the ones it must not), the whole text in
 * pieces with Hebrew, and that no word of the text reaches the protocol log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "conn.h"
#include "charset.h"
#include "translate.h"

static int fails;
static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* ---- the stand-in platform ---- */
static unsigned long now;
static char logbuf[200000];
static long loglen;
static char sent[40000];	/* what the client wrote to the current connection */
static long sentlen;
static const char *answers[40];	/* the canned answer of each connection, in order */
static long answer_len[40];
static int nconn, cur;
static long fed;
static int refuse_open;

int pf_open(const char *path, int mode) { return 7; }
long pf_write(int h, const void *buf, long n)
{
	if (loglen + n < (long)sizeof(logbuf)) {
		memcpy(logbuf + loglen, buf, n);
		loglen += n;
	}
	return n;
}
void pf_close(int h) { }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return now; }
static void idle(void) { }
void (*pf_idle)(void) = idle;

int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	if (refuse_open) {
		snprintf(err, errlen, "connection refused");
		return -1;
	}
	cur = nconn++;
	fed = 0;
	sentlen = 0;
	return 5;
}
long net_write(int h, const void *buf, long n)
{
	if (sentlen + n < (long)sizeof(sent)) {
		memcpy(sent + sentlen, buf, n);
		sentlen += n;
	}
	return n;
}
long net_read(int h, void *buf, long max)
{
	long n = answer_len[cur] - fed;
	now += 1;
	if (n <= 0)
		return -1;		/* the other side has closed */
	if (n > 61)
		n = 61;
	if (n > max)
		n = max;
	memcpy(buf, answers[cur] + fed, n);
	fed += n;
	return n;
}
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

/* one canned answer for the next connection(s) */
static void script(int i, const char *a, long n)
{
	answers[i] = a;
	answer_len[i] = n < 0 ? (long)strlen(a) : n;
}
static void reset(void)
{
	nconn = 0;
	loglen = 0;
	memset(answers, 0, sizeof(answers));
	refuse_open = 0;
}

/* an answer with a Content-Length */
static char abuf[40][2048];
static const char *ok200(int i, const char *body, const char *from)
{
	snprintf(abuf[i], sizeof(abuf[i]), "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
		 "X-Translate-From: %s\r\nContent-Length: %d\r\n\r\n%s", from, (int)strlen(body), body);
	return abuf[i];
}

int main(void)
{
	SBUF out, req;
	char err[160], from[TR_LANG];
	static char big[30000];
	long i, k;

	conn_logfile[0] = 'L';	/* any path: pf_open above takes it */
	conn_logfile[1] = 0;

	/* codes */
	check(translate_code_ok("en") && translate_code_ok("he") && translate_code_ok("pt-BR") &&
	      translate_code_ok("zh-Hans") && translate_code_ok("yue"), "language codes in");
	check(!translate_code_ok("") && !translate_code_ok("e") && !translate_code_ok("english") &&
	      !translate_code_ok("EN") && !translate_code_ok("en ") && !translate_code_ok("en\r\nX: y") &&
	      !translate_code_ok("en&to=x") && !translate_code_ok("en-") && !translate_code_ok("en-x") &&
	      !translate_code_ok(NULL), "everything else out");

	/* split */
	check(translate_split("abc", 3, 10) == 3, "split: it all fits");
	check(translate_split("aaaa\n\nbbbb\n\ncccc", 16, 12) == 12, "split: at the last paragraph break that fits (12)");
	check(translate_split("aaaa\n\nbbbbbbb\ncc cc", 19, 17) == 6, "split: the paragraph break wins over a later line end (6)");
	check(translate_split("aaaa bbbb cccc dddd", 19, 12) == 10, "split: else at a space");
	check(translate_split("Aaa bbb. Ccc ddd eee fff", 24, 18) == 9, "split: a sentence end before a line end or a space");
	check(translate_split("Aaa bbb\nccc ddd. Eee fff ggg", 28, 20) == 17, "split: ...and before an earlier line end");
	check(translate_split("aaaabbbbccccdddd", 16, 7) == 7, "split: else at the limit");
	check(translate_split("aaaa\r\n\r\nbbbbbbbbbbbbbbbbbbbb", 28, 10) == 8, "split: CR LF CR LF is a paragraph break");
	{
		const char *h = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";	/* 2 bytes a letter */
		check(translate_split(h, 16, 5) == 4 && translate_split(h, 16, 3) == 2, "split: never inside a UTF-8 character");
	}
	for (i = 0; i < (long)sizeof(big); i++)
		big[i] = i % 100 == 99 ? '\n' : (i % 7 == 6 ? ' ' : 'a' + i % 26);
	for (k = 0, i = 0; i < (long)sizeof(big);) {
		long p = translate_split(big + i, sizeof(big) - i, TR_PIECE);
		if (p < 1 || p > TR_PIECE)
			break;
		i += p;
		k++;
	}
	check(i == (long)sizeof(big) && k >= 4, "split: 30,000 bytes in pieces that add up, none over 8 KB");

	/* the request */
	sb_init(&req);
	check(translate_request(&req, "PI_IP", "en", "he", "shalom", 6) &&
	      !strncmp(req.s, "POST /translate?to=en&from=he HTTP/1.0\r\nHost: PI_IP\r\n", 53) &&
	      strstr(req.s, "Content-Length: 6\r\n") && strstr(req.s, "charset=utf-8") &&
	      !strcmp(req.s + req.len - 10, "\r\n\r\nshalom"), "request with from, length and body");
	check(translate_request(&req, "PI_IP", "en", "", "x", 1) && !strstr(req.s, "from="), "request: no from means detect");
	check(!translate_request(&req, "PI_IP", "en\r\nX: 1", NULL, "x", 1) &&
	      !translate_request(&req, "PI IP", "en", NULL, "x", 1) &&
	      !translate_request(&req, "PI_IP", "en", "h e", "x", 1) &&
	      !translate_request(&req, "", "en", NULL, "x", 1), "request: nothing unchecked reaches a line");
	sb_free(&req);

	/* status line and refusals */
	check(translate_status_code("HTTP/1.0 200 OK") == 200 && translate_status_code("HTTP/1.1 422 x") == 422 &&
	      translate_status_code("HTTP/1.0 200") == 200 && !translate_status_code("HTTP/2 200 OK") &&
	      !translate_status_code("ICY 200 OK") && !translate_status_code("HTTP/1.0 2x0 OK") &&
	      !translate_status_code("HTTP/1.0 2000 OK") && !translate_status_code(""), "status lines");

	/* a piece: 200 with a Content-Length */
	sb_init(&out);
	reset();
	script(0, ok200(0, "Hello world", "he"), -1);
	check(translate_piece("PI_IP", 8480, "en", NULL, "SHALOM OLAM", 11, &out, from, NULL, err, sizeof(err)) &&
	      out.len == 11 && !memcmp(out.s, "Hello world", 11) && !strcmp(from, "he"), "a piece: answer and the language used");
	check(!strncmp(sent, "POST /translate?to=en HTTP/1.0\r\n", 32) && sentlen > 11 && !memcmp(sent + sentlen - 11, "SHALOM OLAM", 11),
	      "a piece: the request as built went out");

	/* refusals */
	{
		static const struct { int code; const char *text; } r[] = {
			{ 400, "HTTP/1.0 400 Bad Request\r\n\r\nxyzzy-a" }, { 413, "HTTP/1.0 413 Too Large\r\n\r\nxyzzy-b" },
			{ 422, "HTTP/1.0 422 No Pair\r\n\r\nxyzzy-c" }, { 503, "HTTP/1.0 503 Loading\r\n\r\nxyzzy-d" },
			{ 504, "HTTP/1.0 504 Slow\r\n\r\nxyzzy-e" },
		};
		for (i = 0; i < 5; i++) {
			char what[80];
			reset();
			script(0, r[i].text, -1);
			sb_reset(&out);
			snprintf(what, sizeof(what), "refusal %d: nothing returned, the app's own words in err", r[i].code);
			check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)) &&
			      out.len == 0 && err[0] && !strstr(err, "xyzzy") &&
			      !strcmp(err, translate_refusal(r[i].code)), what);
		}
	}

	/* answers it must not take */
	reset();
	script(0, "<html>hello</html>", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)), "not an HTTP answer: refused");
	reset();
	script(0, "HTTP/1.0 200 OK\r\nContent-Length: 99999\r\n\r\nx", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)), "more than 4 times the request: refused");
	reset();
	script(0, ok200(0, "caf\xE9 latin1", "de"), -1);
	sb_reset(&out);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)) && out.len == 0, "not UTF-8: refused");
	reset();
	script(0, ok200(0, "a\n\nb\n\nc\n\nd\n\ne", "de"), -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "one paragraph only", 18, &out, from, NULL, err, sizeof(err)), "more paragraphs than the request: refused");
	reset();
	script(0, "HTTP/1.0 200 OK\r\nContent-Length: 20\r\n\r\nshort", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)), "an answer that breaks off: refused");
	reset();
	refuse_open = 1;
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)) && !strcmp(err, "connection refused"), "no Gateway: the network's own error");
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 0, &out, from, NULL, err, sizeof(err)) &&
	      !translate_piece("PI_IP", 8480, "en", NULL, big, TR_PIECE + 1, &out, from, NULL, err, sizeof(err)) &&
	      !translate_piece("PI_IP", 8480, "EN", NULL, "x", 1, &out, from, NULL, err, sizeof(err)), "empty, too long, bad code: no connection tried");
	check(nconn == 0, "...and none was");

	/* an HTTP/1.0 answer ended by the close */
	reset();
	script(0, "HTTP/1.0 200 OK\r\nX-Translate-From: fr\r\n\r\nBonjour tout le monde", -1);
	sb_reset(&out);
	check(translate_piece("PI_IP", 8480, "en", NULL, "Hello", 5, &out, from, NULL, err, sizeof(err)) && out.len == 21 && !strcmp(from, "fr"),
	      "no Content-Length: read to the close");

	/* status and languages */
	{
		char info[80];
		reset();
		script(0, ok200(0, "translategate 1\nengine argos 1.9.6\nready\n", "en"), -1);
		check(translate_status("PI_IP", 8480, info, sizeof(info), err, sizeof(err)) &&
		      !strcmp(info, "translategate 1, engine argos 1.9.6, ready"), "GET /status as one line");
		check(!strncmp(sent, "GET /status HTTP/1.0\r\n", 22), "...a GET");
		reset();
		script(0, ok200(0, "hello world", "en"), -1);
		check(!translate_status("PI_IP", 8480, info, sizeof(info), err, sizeof(err)), "a server that isn't translategate: refused");
		reset();
		script(0, "HTTP/1.0 200 OK\r\n\r\nen\tEnglish\nhe\t\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\n", -1);
		sb_reset(&out);
		check(translate_languages("PI_IP", 8480, &out, err, sizeof(err)) && strstr(out.s, "en\tEnglish\n") &&
		      strstr(out.s, "he\t\xD7\xA2"), "GET /languages");
	}

	/* the whole text, Hebrew in Atari text, three pieces */
	{
		/* 0xC2.. are Atari's Hebrew letters (alef = 0xC2, bet = 0xC3) */
		static char atari[24000], want[24000];
		char word[] = { (char)0xC2, (char)0xC3, (char)0xC4, ' ', 0 };
		long n = 0, wn = 0, outlen = 0;
		char *res;
		static char eng[40][100];
		while (n < 20000) {
			memcpy(atari + n, word, 4);
			n += 4;
			if (n % 400 == 0) {
				atari[n++] = '\n';
				atari[n++] = '\n';
			}
		}
		reset();
		/* the "engine" answers each piece with the same letters in UTF-8: alef bet gimel */
		for (i = 0; i < 40; i++) {
			long m = 0;
			m += snprintf(eng[i] + m, sizeof(eng[i]) - m, "PIECE%ld \xD7\x90\xD7\x91\xD7\x92\n\n", i);
			script(i, ok200(i, eng[i], "he"), -1);
		}
		res = translate_atari("PI_IP", 8480, "en", NULL, atari, n, NULL, from, &outlen, err, sizeof(err));
		if (!res)
			printf("     err: %s (connections %d)\n", err, nconn);
		check(res != NULL, "the whole text in pieces (UTF-8 doubles the Hebrew), answers joined");
		if (res) {
			char *u = atari_to_utf8(res, outlen, &wn);
			check(nconn >= 15 && nconn <= 40 && strstr(res, "PIECE0") && strstr(res, "PIECE1") && strstr(res, "PIECE2") &&
			      (unsigned char)strstr(res, "PIECE0")[7] == 0xC2 && !strcmp(from, "he"),
			      "...in order, Hebrew back in Atari's letters");
			check(strstr(res, "PIECE0") < strstr(res, "PIECE1") && strstr(res, "\n\nPIECE1"), "...paragraph breaks kept between the pieces");
			free(u);
			free(res);
		}
		(void)want;
	}

	/* already in the target language: no body, the piece stands */
	reset();
	script(0, "HTTP/1.0 200 OK\r\nX-Translate-From: en\r\nX-Translate-Same: 1\r\nContent-Length: 0\r\n\r\n", -1);
	sb_reset(&out);
	{
		int same = 0;
		check(translate_piece("PI_IP", 8480, "en", NULL, "Already English.", 16, &out, from, &same, err, sizeof(err)) &&
		      same == 1 && out.len == 16 && !memcmp(out.s, "Already English.", 16) && !strcmp(from, "en"),
		      "already in the target language: 200, no body, same=1, the text as it was");
	}

	/* the cursor: a piece at a time, the app shows each, a stop between or inside */
	{
		static char text[9000];
		TRANSLATE t;
		SBUF got;
		int r, steps = 0;
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 60 == 59 ? '\n' : (i % 11 == 10 ? ' ' : 'a' + i % 26);
		reset();
		for (i = 0; i < 10; i++)
			script(i, ok200(i, "piece", "de"), -1);
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, sizeof(text) - 1);
		sb_init(&got);
		while ((r = translate_step(&t, &got)) > 0)
			steps++;
		check(r == 0 && steps == nconn && steps >= 4 && steps <= 6 && got.len == 5 * steps && t.pos == t.len &&
		      !strcmp(t.from_used, "de"), "translate_step: 2 KB pieces to the end, the first language kept");
		sb_free(&got);
		/* a failure keeps what was done and says why */
		reset();
		script(0, ok200(0, "one", "de"), -1);
		script(1, "HTTP/1.0 504 Slow\r\n\r\nx", -1);
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, sizeof(text) - 1);
		sb_init(&got);
		check(translate_step(&t, &got) == 1 && translate_step(&t, &got) == -1 &&
		      !strcmp(t.err, translate_refusal(504)) && got.len == 3 && t.pos > 0 && t.pos < t.len,
		      "translate_step: a 504 on the second piece: -1, the first stays");
		sb_free(&got);
		/* the app drops it at once: conn_stop set from pf_idle */
		reset();
		script(0, "HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\nxx", -1);	/* the rest never comes */
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, 300);
		sb_init(&got);
		conn_stop = 1;
		check(translate_step(&t, &got) == -1 && !strcmp(t.err, "stopped") && got.len == 0, "conn_stop: the piece is dropped, \"stopped\"");
		conn_stop = 0;
		sb_free(&got);
	}

	/* UTF-8 in and out, no Atari conversion */
	{
		char *r;
		long rl = 0;
		reset();
		for (i = 0; i < 4; i++)
			script(i, ok200(i, "\xD0\x9F\xD1\x80\xD0\xB8", "ru"), -1);
		r = translate_utf8("PI_IP", 8480, "en", NULL, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 12, NULL, from, &rl, err, sizeof(err));
		check(r && rl == 6 && !memcmp(r, "\xD0\x9F\xD1\x80\xD0\xB8", 6) && !r[6] && !strcmp(from, "ru"), "translate_utf8: Cyrillic stays Cyrillic, no '?'");
		free(r);
	}

	/* a turn between pieces, and a stop */
	{
		static int turns, stop_at;
		static long last_done;
		static char text[20000];
		char *r;
		int stopper(long done, long total) { turns++; last_done = done; return stop_at && turns >= stop_at; }
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 50 == 49 ? '\n' : 'a' + i % 26;
		reset();
		for (i = 0; i < 40; i++)
			script(i, ok200(i, "ok", "en"), -1);
		turns = 0;
		stop_at = 0;
		r = translate_atari("PI_IP", 8480, "he", "en", text, sizeof(text) - 1, stopper, from, NULL, err, sizeof(err));
		check(r != NULL && turns >= 8 && turns == nconn && last_done > 0, "a turn before each piece (2 KB a piece)");
		free(r);
		reset();
		for (i = 0; i < 40; i++)
			script(i, ok200(i, "ok", "en"), -1);
		turns = 0;
		stop_at = 2;
		r = translate_atari("PI_IP", 8480, "he", "en", text, sizeof(text) - 1, stopper, from, NULL, err, sizeof(err));
		check(r == NULL && !strcmp(err, "stopped") && nconn == 1, "Esc between pieces: nothing partial, no more connections");
	}

	/* a refusal in the middle returns nothing */
	{
		static char text[20000];
		char *r;
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 50 == 49 ? '\n' : 'a' + i % 26;
		reset();
		script(0, ok200(0, "first", "en"), -1);
		script(1, "HTTP/1.0 503 Busy\r\n\r\nbusy", -1);
		r = translate_atari("PI_IP", 8480, "he", NULL, text, sizeof(text) - 1, NULL, from, NULL, err, sizeof(err));
		check(r == NULL && !strcmp(err, translate_refusal(503)), "a refusal on the second piece: NULL and the reason");
	}

	/* no word of the text in the protocol log */
	{
		static const char secret[] = "THE-SECRET-LETTER-TEXT";
		reset();
		script(0, ok200(0, "THE-SECRET-ANSWER-TEXT", "en"), -1);
		sb_reset(&out);
		translate_piece("PI_IP", 8480, "en", NULL, secret, sizeof(secret) - 1, &out, from, NULL, err, sizeof(err));
		conn_log_flush();
		check(loglen > 0 && !memmem(logbuf, loglen, "SECRET", 6), "the protocol log has the pair and the sizes, no word of the text or of the answer");
		check(memmem(logbuf, loglen, "auto>en, 22 bytes", 17) != NULL, "...the pair and the size are there");
	}

	sb_free(&out);
	printf("%s\n", fails ? "FAILED" : "all translate checks passed");
	return fails != 0;
}
