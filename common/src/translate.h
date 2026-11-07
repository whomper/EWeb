/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * translate.h - the client of the Gateway's translation service (translategate;
 * standards/TRANSLATION-SERVICE.md). Plain HTTP/1.0 on the home network, UTF-8
 * on the wire, one request and one answer a connection. An app touches this
 * only when its Translate setting is On: with it Off nothing here is called.
 *
 * The text is never written to the protocol log (conn_logfile): only the
 * language pair, the sizes and the answer's status.
 */
#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "util.h"

#define TR_PIECE 8192		/* the most the protocol lets a request carry (UTF-8 bytes) */
#define TR_PIECE_DEFAULT 2048	/* what the client sends at a time: a few paragraphs, seconds on the Gateway */
#define TR_LANG  8		/* a language code with its NUL: "en", "he", "pt-BR" */

/* a turn between pieces: done and total in bytes of the Atari text; non-zero
   stops (Esc) */
typedef int (*translate_turn_fn)(long done, long total);

/* ---- the pure parts (host tests, no network) ---- */

/* 1 if code is a language code the protocol accepts ([a-z]{2,3}, then
   optionally - and 2-4 letters or digits), else 0. Nothing else is ever put
   in a request line. */
int  translate_code_ok(const char *code);
/* the length of the first piece of s[0..n): at most max bytes, ending after
   the last blank line (a paragraph) that fits, else after the last line
   end, else after the last space, else at max, never inside a UTF-8
   character. n > 0 gives at least 1. */
long translate_split(const char *s, long n, long max);
/* the request for a piece: the lines up to the empty one, then the body.
   from may be NULL or "" (the engine detects it). 0 if a code is not ok or
   there is no room. */
int  translate_request(SBUF *out, const char *host, const char *to, const char *from,
		       const char *body, long n);
/* "HTTP/1.0 200 OK" -> 200; 0 if the line isn't a status line */
int  translate_status_code(const char *line);
/* what to tell the user for a refusal: "the language pair isn't installed
   on the Gateway" ... (code 400, 413, 422, 503, 504, else the number) */
const char *translate_refusal(int code);

/* ---- the network (conn.c: SEC_PLAIN, a turn for the machine while it waits) ---- */

/* GET /status: info gets "translategate 1, engine NAME VERSION, ready" as
   one line. 1 when the service answers; 0 with err otherwise. */
int  translate_status(const char *host, unsigned short port, char *info, long infomax,
		      char *err, int errlen);
/* GET /languages: appends "CODE<TAB>NAME" lines to out. */
int  translate_languages(const char *host, unsigned short port, SBUF *out, char *err, int errlen);
/* one piece (n <= TR_PIECE UTF-8 bytes) appended to out; from_used (TR_LANG
   bytes) gets the language the engine used; *same (may be NULL) is 1 when the
   text is already in the target language (the Gateway sent no body: out then gets the
   piece as it was). 0 with err on a refusal, a timeout, a stop (conn_stop) or an
   answer that breaks the protocol (not UTF-8, more than 4 times the request, more
   paragraphs than it). */
int  translate_piece(const char *host, unsigned short port, const char *to, const char *from,
		     const char *utf8, long n, SBUF *out, char *from_used, int *same,
		     char *err, int errlen);

/* ---- the text in pieces, for an app that shows each as it comes ----
   The app owns the UTF-8 text, which must stay while the cursor is used. One
   step is one piece of at most t->piece bytes (TR_PIECE_DEFAULT; set it before the
   first step), one connection, appended to the app's SBUF. Between steps the app
   draws, and may stop: it just doesn't call again. Inside a step the machine gets
   its turns through pf_idle; to drop a translation at once (the user went to the
   next letter, Esc) the app sets conn_stop from there: the step returns -1 with
   err "stopped", the connection is closed, and the Gateway stops the job when it
   sees the close. */
typedef struct {
	char host[104];
	unsigned short port;
	char to[TR_LANG], from[TR_LANG];
	const char *text;
	long len, pos;		/* the text, and how much of it is done */
	long piece;
	char from_used[TR_LANG];	/* the language of the first piece */
	int same;		/* the last piece was already in the target language */
	char err[160];
} TRANSLATE;
void translate_begin(TRANSLATE *t, const char *host, unsigned short port, const char *to,
		     const char *from, const char *utf8, long n);
/* 1: a piece was appended to out; 0: the text is done; -1: failed (t->err), nothing appended */
int  translate_step(TRANSLATE *t, SBUF *out);

/* ---- the whole text ---- */

/* UTF-8 in, UTF-8 out (malloc'd, NUL-terminated, *outlen its length): the pieces
   in order, a turn (turn, may be NULL) before each, the line ends a piece ended
   with kept between the answers. NULL with err on the first failure (nothing
   partial is returned). The Gateway decides how the lines inside a paragraph are
   handled (standards/TRANSLATION-SERVICE.md): the answer's paragraphs are
   the request's; the app wraps the lines. */
char *translate_utf8(const char *host, unsigned short port, const char *to, const char *from,
		     const char *utf8, long n, translate_turn_fn turn, char *from_used,
		     long *outlen, char *err, int errlen);
/* the same for Atari text (logical-order Hebrew, 0xC2-0xDC) through common's charsets,
   in and out. EMail's letters are UTF-8 (the Atari set has lost Cyrillic, Greek and
   CJK already): it uses translate_utf8. */
char *translate_atari(const char *host, unsigned short port, const char *to, const char *from,
		      const char *atari, long n, translate_turn_fn turn, char *from_used,
		      long *outlen, char *err, int errlen);

#endif
