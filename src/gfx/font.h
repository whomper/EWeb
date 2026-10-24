/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * font.h - EWeb's own bitmap fonts (EWEB.FNT, made by tools/mkfont.py
 * from DejaVu): Sans, Serif and Mono, regular, bold, italic and bold
 * italic, at a set of pixel sizes, for Latin, Hebrew, Greek, Cyrillic
 * and the common signs. A face is read from the file the first time it is
 * wanted, its blocks of characters past the first when a page shows one.
 */
#ifndef EW_FONT_H
#define EW_FONT_H

#include <stddef.h>
#include <stdint.h>

enum { FF_SANS, FF_SERIF, FF_MONO };
#define FS_BOLD   1
#define FS_ITALIC 2

typedef struct {
	uint8_t adv;			/* how far the pen moves */
	int8_t x, y;			/* the bitmap's top left, from the pen on the baseline */
	uint8_t w, h;
	const uint8_t *bits;		/* h rows of (w + 7) / 8 bytes, left pixel in bit 7; NULL: blank */
} GLYPH;

#define FONT_BLOCKS	6	/* blocks of characters a face may have */

typedef struct ew_font {
	uint8_t family, style, px;
	uint8_t ascent, descent;	/* above and below the baseline */
	uint8_t bad;			/* its blocks that can't be read, a bit each */
	uint8_t *data;			/* the face's first block of characters, read on first use */
	uint8_t *more[FONT_BLOCKS - 1];	/* its other blocks, each read when a page first needs one
					   of their characters (Greek, Cyrillic...) */
	int failed;
} ew_font;

/* reads the file's table of faces: 0 if it can't */
int font_init(const char *path);
void font_free(void);
void font_drop(void);		/* the glyphs given back, read again when used */

/* the face nearest to px of that family and style; never NULL once
   font_init succeeded (a face that can't be read draws nothing) */
ew_font *font_get(int family, int style, int px);

/* a character's glyph ('?' for one the fonts don't have) */
void font_glyph(ew_font *f, uint32_t cp, GLYPH *g);
int  font_advance(ew_font *f, uint32_t cp);
/* the width of UTF-8 text */
long font_width(ew_font *f, const char *s, size_t n);

/* the next character of UTF-8 text at *p (before end); bad bytes come
   out as U+FFFD, one at a time */
uint32_t utf8_next(const char **p, const char *end);

#endif
