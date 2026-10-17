/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* streamtest: libparserutils' input stream, which turns a page into the
 * UTF-8 Hubbub reads. Documents go in in random chunks, read between them
 * the way Hubbub reads (as far as it can, then wait for more), and what
 * comes out must be:
 *   - valid UTF-8: the same bytes;
 *   - any bytes as UTF-8: what the WHATWG Encoding Standard's UTF-8 decoder
 *     gives (each byte that can't start a sequence, and each sequence that
 *     ends too soon, is one U+FFFD), whatever the chunks;
 *   - ISO-8859-1: every byte as its character, the last one too.
 * Prints the failures; returns non-zero if there were any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/input/inputstream.h>

static uint32_t seed = 2026;

static uint32_t rnd(uint32_t n)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed % n;
}

/* the stream's output for doc in chunks of at most maxchunk bytes (0: in
 * one go) */
static size_t stream(const char *enc, const uint8_t *doc, size_t n,
		size_t maxchunk, uint8_t *out)
{
	parserutils_inputstream *s;
	size_t at = 0, o = 0;
	int eof = 0;

	if (parserutils_inputstream_create(enc, 1, NULL, &s) != PARSERUTILS_OK) {
		fprintf(stderr, "streamtest: no stream for %s\n", enc);
		exit(1);
	}
	for (;;) {
		const uint8_t *p;
		size_t l;
		parserutils_error e = parserutils_inputstream_peek(s, 0, &p, &l);

		if (e == PARSERUTILS_OK) {
			memcpy(out + o, p, l);
			o += l;
			parserutils_inputstream_advance(s, l);
			continue;
		}
		if (e == PARSERUTILS_EOF)
			break;
		if (e != PARSERUTILS_NEEDDATA || eof) {
			fprintf(stderr, "streamtest: peek gave %d\n", e);
			exit(1);
		}
		if (at < n) {
			size_t c = maxchunk ? 1 + rnd(maxchunk) : n;
			if (c > n - at)
				c = n - at;
			parserutils_inputstream_append(s, doc + at, c);
			at += c;
		} else {
			parserutils_inputstream_append(s, NULL, 0);
			eof = 1;
		}
	}
	parserutils_inputstream_destroy(s);
	return o;
}

static uint8_t *fffd(uint8_t *o)
{
	*o++ = 0xef;
	*o++ = 0xbf;
	*o++ = 0xbd;
	return o;
}

/* the WHATWG Encoding Standard's UTF-8 decoder, to UTF-8 */
static size_t whatwg(const uint8_t *in, size_t n, uint8_t *out)
{
	uint8_t *o = out;
	size_t i = 0;

	while (i < n) {
		uint8_t b = in[i], lo = 0x80, hi = 0xbf;
		size_t need, j, k;

		if (b < 0x80) {
			*o++ = b;
			i++;
			continue;
		}
		if (b >= 0xc2 && b <= 0xdf) {
			need = 1;
		} else if (b >= 0xe0 && b <= 0xef) {
			need = 2;
			if (b == 0xe0) lo = 0xa0;
			if (b == 0xed) hi = 0x9f;
		} else if (b >= 0xf0 && b <= 0xf4) {
			need = 3;
			if (b == 0xf0) lo = 0x90;
			if (b == 0xf4) hi = 0x8f;
		} else {
			o = fffd(o);
			i++;
			continue;
		}
		for (k = 0, j = i + 1; k < need && j < n; k++, j++) {
			if (in[j] < lo || in[j] > hi)
				break;
			lo = 0x80;
			hi = 0xbf;
		}
		if (k == need) {
			memcpy(o, in + i, j - i);
			o += j - i;
		} else {
			o = fffd(o);
		}
		i = j;
	}
	return o - out;
}

/* a random character, n bytes of it in UTF-8 */
static size_t utf8_char(uint8_t *o)
{
	uint32_t c;

	switch (rnd(4)) {
	case 0: c = rnd(0x80); break;
	case 1: c = 0x80 + rnd(0x800 - 0x80); break;
	case 2: do c = 0x800 + rnd(0x10000 - 0x800);
		while (c >= 0xd800 && c <= 0xdfff); break;
	default: c = 0x10000 + rnd(0x110000 - 0x10000); break;
	}
	if (c < 0x80) {
		o[0] = c;
		return 1;
	}
	if (c < 0x800) {
		o[0] = 0xc0 | c >> 6;
		o[1] = 0x80 | (c & 0x3f);
		return 2;
	}
	if (c < 0x10000) {
		o[0] = 0xe0 | c >> 12;
		o[1] = 0x80 | (c >> 6 & 0x3f);
		o[2] = 0x80 | (c & 0x3f);
		return 3;
	}
	o[0] = 0xf0 | c >> 18;
	o[1] = 0x80 | (c >> 12 & 0x3f);
	o[2] = 0x80 | (c >> 6 & 0x3f);
	o[3] = 0x80 | (c & 0x3f);
	return 4;
}

/* a byte likely to make trouble in UTF-8 */
static uint8_t odd_byte(void)
{
	static const uint8_t b[] = { 'a', '<', 0x80, 0x8f, 0x90, 0x9f, 0xa0,
		0xbf, 0xc0, 0xc1, 0xc2, 0xdf, 0xe0, 0xe1, 0xed, 0xef, 0xf0,
		0xf4, 0xf5, 0xf8, 0xfe, 0xff };
	return b[rnd(sizeof b)];
}

static int fails;

static void check(const char *what, size_t len, size_t chunk,
		const uint8_t *want, size_t wn, const uint8_t *got, size_t gn)
{
	size_t i;

	if (wn == gn && memcmp(want, got, wn) == 0)
		return;
	for (i = 0; i < wn && i < gn && want[i] == got[i]; i++)
		;
	if (fails++ < 10)
		fprintf(stderr, "streamtest: %s, %zu bytes in chunks of up to "
			"%zu: %zu bytes out, %zu wanted, first difference at "
			"%zu\n", what, len, chunk, gn, wn, i);
}

#define MAXDOC 20000

int main(void)
{
	static uint8_t doc[MAXDOC], want[MAXDOC * 3], got[MAXDOC * 3];
	/* Unicode's example of maximal subparts (The Unicode Standard,
	 * 3.9, Table 3-8) */
	static const uint8_t t38[] = { 0x61, 0xf1, 0x80, 0x80, 0xe1, 0x80,
		0xc2, 0x62, 0x80, 0x63, 0x80, 0xbf, 0x64 };
	static const uint8_t t38_out[] = { 0x61, 0xef, 0xbf, 0xbd, 0xef,
		0xbf, 0xbd, 0xef, 0xbf, 0xbd, 0x62, 0xef, 0xbf, 0xbd, 0x63,
		0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd, 0x64 };
	static const size_t chunks[] = { 0, 1, 2, 3, 7, 64, 1000 };
	size_t n, gn, wn, i, c, round;

	/* fixed cases */
	gn = stream("UTF-8", t38, sizeof t38, 0, got);
	check("Table 3-8", sizeof t38, 0, t38_out, sizeof t38_out, got, gn);
	for (c = 1; c < 4; c++) {
		gn = stream("UTF-8", t38, sizeof t38, c, got);
		check("Table 3-8", sizeof t38, c, t38_out, sizeof t38_out,
			got, gn);
	}

	/* every length near a multiple of 64 (the pivot buffer's size in
	 * characters) and of 4096 (the buffers' first size) */
	for (n = 1; n < 9000; n++) {
		if (n % 64 > 2 && n % 64 < 62 && (n < 4090 || n > 4100))
			continue;
		memset(doc, 'a', n);
		doc[n - 1] = 'z';
		gn = stream("UTF-8", doc, n, 0, got);
		check("ASCII", n, 0, doc, n, got, gn);
		for (i = 0; i < n; i++)
			want[2 * i] = 0xc0 | (0xe9 >> 6),
			want[2 * i + 1] = 0x80 | (0xe9 & 0x3f);
		memset(doc, 0xe9, n);
		gn = stream("ISO-8859-1", doc, n, 0, got);
		check("ISO-8859-1", n, 0, want, 2 * n, got, gn);
	}

	for (round = 0; round < 3000; round++) {
		size_t len = rnd(round < 2900 ? 600 : MAXDOC / 4);

		/* valid UTF-8 */
		for (n = 0; n + 4 <= len; )
			n += utf8_char(doc + n);
		c = chunks[rnd(sizeof chunks / sizeof chunks[0])];
		gn = stream("UTF-8", doc, n, c, got);
		check("UTF-8", n, c, doc, n, got, gn);

		/* UTF-8 with mistakes in it */
		for (n = 0; n + 4 <= len; )
			if (rnd(4) == 0)
				doc[n++] = odd_byte();
			else
				n += utf8_char(doc + n);
		if (rnd(2))
			doc[n++] = 0xf0 + rnd(5);	/* cut short at the end */
		wn = whatwg(doc, n, want);
		c = chunks[rnd(sizeof chunks / sizeof chunks[0])];
		gn = stream("UTF-8", doc, n, c, got);
		check("not UTF-8", n, c, want, wn, got, gn);

		/* any bytes */
		for (n = 0; n < len; n++)
			doc[n] = rnd(256);
		wn = whatwg(doc, n, want);
		c = chunks[rnd(sizeof chunks / sizeof chunks[0])];
		gn = stream("UTF-8", doc, n, c, got);
		check("random bytes", n, c, want, wn, got, gn);
	}

	if (fails)
		fprintf(stderr, "streamtest: %d failed\n", fails);
	else
		fprintf(stderr, "streamtest: all passed\n");
	return fails != 0;
}
