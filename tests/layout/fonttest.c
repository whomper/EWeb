/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fonttest - EWEB.FNT's advances as the layout measures them
 * (src/gfx/font.c): font_advance and font_width through the tables of
 * places (aoff for ASCII, noff for U+0080 to U+07FF and U+2000 to
 * U+206F) the same as font_glyph's glyph, for every character to U+2FFF
 * but the surrogates, in every face (each family and style at every size
 * to 63 px); font_widths' table of bytes below 0x80 the same; and a lead
 * byte with nothing after it, or a bad one after it, measured as U+FFFD.
 *
 *   fonttest EWEB.FNT
 */
#include <stdio.h>
#include <string.h>

#include "font.h"

static int bad;

static void fail(const char *what, uint32_t cp, int fam, int st, int px, long got, long want)
{
	if (bad++ < 10)
		printf("fonttest: %s U+%04X (family %d, style %d, %d px): %ld, not %ld\n", what, (unsigned) cp, fam,
		       st, px, got, want);
}

int main(int argc, char **argv)
{
	int fam, st, px;
	long n = 0;
	uint32_t cp;

	if (argc != 2 || !font_init(argv[1])) {
		fprintf(stderr, "fonttest: usage: fonttest EWEB.FNT (a font it can read)\n");
		return 2;
	}
	for (fam = 0; fam < 4; fam++)
		for (st = 0; st < 4; st++)
			for (px = 1; px < 64; px++) {
				ew_font *f = font_get(fam, st, px);
				const uint8_t *wt = font_widths(f);
				int a = font_advance(f, 'a');
				for (cp = 0; cp < 0x3000; cp++) {
					GLYPH g;
					char b[4];
					int l = 0;
					if (cp >= 0xD800 && cp < 0xE000)
						continue;
					font_glyph(f, cp, &g);
					if (font_advance(f, cp) != g.adv)
						fail("font_advance", cp, fam, st, px, font_advance(f, cp), g.adv);
					if (cp < 0x80) {
						b[l++] = (char) cp;
						if (wt == NULL || wt[cp] != g.adv)
							fail("font_widths", cp, fam, st, px, wt ? wt[cp] : -1, g.adv);
					} else if (cp < 0x800) {
						b[l++] = (char) (0xC0 | cp >> 6);
						b[l++] = (char) (0x80 | (cp & 63));
					} else {
						b[l++] = (char) (0xE0 | cp >> 12);
						b[l++] = (char) (0x80 | (cp >> 6 & 63));
						b[l++] = (char) (0x80 | (cp & 63));
					}
					b[l++] = 'a';
					if (font_width(f, b, (size_t) l) != g.adv + a)
						fail("font_width", cp, fam, st, px, font_width(f, b, (size_t) l), g.adv + a);
					n++;
				}
				if (font_width(f, "\xd7", 1) != font_advance(f, 0xFFFD))
					fail("a lead byte at the end", 0xD7, fam, st, px, font_width(f, "\xd7", 1),
					     font_advance(f, 0xFFFD));
				if (font_width(f, "\xd7" "a", 2) != font_advance(f, 0xFFFD) + a)
					fail("a lead byte before a letter", 0xD7, fam, st, px, font_width(f, "\xd7" "a", 2),
					     font_advance(f, 0xFFFD) + a);
			}
	font_free();
	if (bad) {
		printf("fonttest: %d of %ld wrong\n", bad, n);
		return 1;
	}
	printf("fonttest: %ld characters measured as their glyphs, in every face\n", n);
	return 0;
}
