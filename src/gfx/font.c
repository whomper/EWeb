/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * font.c - EWEB.FNT: the table of faces is read at start, a face's
 * glyphs when it is first used (a page uses a handful of faces). A face's
 * characters come in blocks (tools/mkfont.py): the first, which nearly
 * every page shows, read with the face; each of the others (more Latin
 * letters, Greek, Cyrillic, symbols) when a page first shows one of its
 * characters. The file stays open from font_init to font_free, so a block
 * is a seek and a read, not a search of the folder (the guidelines' 6.5).
 * Each block is DEFLATE'd in the file: read whole, then unpacked by the
 * HTTP client's inflate. EWFNT2, the file before, had one block.
 */
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "plat.h"
#include "inflate.h"

static int fh = -1;			/* EWEB.FNT, open while the fonts are */
static uint16_t ncodes, nfaces, nblocks;
static uint16_t *codes;			/* the first block's, sorted */
static uint16_t nx, *xcodes, *xidx;	/* the other blocks' codes, sorted together, and each
					   one's place in its block */
static uint8_t *xblk;			/* and its block */
static uint16_t bcount[FONT_BLOCKS];	/* each block's codes */
static ew_font *faces;
static uint32_t *foff, *fstored, *fsize;	/* each face's first block: where, its packed and
						   unpacked sizes (the others' are read from the
						   file when first needed: few pages want one) */
static long fdir, fper;			/* where the faces' table is in the file, and an entry's size */
static int16_t ascii[96];		/* code index of 0x20..0x7f, -1 if none */
static uint8_t picked[4][4][64];	/* font_get's face (+ 1) for a family, style and size
					   (to 63 px), kept as it is asked for: a page asks often */
static uint16_t aoff[96];		/* where the advance of ' ' to 0x7F is in a face: most of
					   the text a page measures */
static int qmark;			/* the index of '?' */
/* where the advance of U+0080 to U+07FF (Latin-1 and Latin, Greek,
   Cyrillic, Hebrew: two bytes of UTF-8) and of U+2000 to U+206F (the
   typographic quotes, dashes and spaces) is in a face, as aoff's: 0 not
   looked up yet, 1 for none (Hebrew points, format characters), 2 for a
   character of another block (font_glyph's), else its place. A Hebrew
   page's text through font_glyph (index_of's search, glyph_at) took over
   half its layout. Each entry is found the first time it is asked for
   (near_place): the whole table at the start was 45 ms of the 8 MHz ST's,
   the speed rig, 1 Nov 2026 */
#define NEAR_N (0x800 - 0x80)
#define PUNCT_N 0x70
#define NEAR_OTHER 2
static uint16_t noff[NEAR_N + PUNCT_N];	/* (not in the heap: the pages' own places stay
						   as they were) */

static unsigned be16(const uint8_t *p) { return (unsigned) p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

void font_free(void)
{
	font_drop();
	free(faces);
	free(codes);
	free(xcodes);
	free(xidx);
	free(xblk);
	xcodes = xidx = NULL;
	xblk = NULL;
	nx = 0;
	free(foff);
	free(fstored);
	free(fsize);
	faces = NULL;
	codes = NULL;
	foff = fstored = fsize = NULL;
	memset(picked, 0, sizeof picked);
	nfaces = ncodes = 0;
	if (fh >= 0)
		pf_close(fh);
	fh = -1;
}

/* the faces' glyphs given back (a small machine short of memory for a page);
   each is read again when next used */
void font_drop(void)
{
	int i, b;
	for (i = 0; faces != NULL && i < nfaces; i++) {
		free(faces[i].data);
		faces[i].data = NULL;
		free(faces[i].wt);
		faces[i].wt = NULL;
		for (b = 0; b < FONT_BLOCKS - 1; b++) {
			free(faces[i].more[b]);
			faces[i].more[b] = NULL;
		}
	}
}

static int read_at(int h, long off, void *buf, long n)
{
	return pf_seek(h, off) == off && pf_read(h, buf, n) == n;
}

/* the other blocks' codes in one sorted list (each block's is sorted) */
static int by_code(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
	return x < y ? -1 : x > y;
}

int font_init(const char *path)
{
	uint8_t head[12], *dir = NULL;
	uint32_t *all = NULL;
	long dsize, per;
	int h, i, b, v3, ok = 0;

	font_free();
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	if (!read_at(h, 0, head, 12) || memcmp(head, "EWFNT", 5) != 0 || (head[5] != '2' && head[5] != '3'))
		goto done;
	v3 = head[5] == '3';
	nfaces = be16(head + 8);
	nblocks = v3 ? be16(head + 10) : 1;
	if (nblocks < 1 || nblocks > FONT_BLOCKS)
		goto done;
	if (v3) {
		uint8_t c[2 * FONT_BLOCKS];
		if (!read_at(h, 12, c, 2L * nblocks))
			goto done;
		for (b = 0; b < nblocks; b++)
			bcount[b] = (uint16_t) be16(c + 2 * b);
	} else {
		bcount[0] = (uint16_t) be16(head + 10);
	}
	ncodes = bcount[0];
	for (nx = 0, b = 1; b < nblocks; b++)
		nx = (uint16_t) (nx + bcount[b]);
	per = v3 ? 4 + 12L * nblocks : 16;
	dsize = 2L * (ncodes + nx) + per * nfaces;
	fdir = (v3 ? 12 + 2L * nblocks : 12) + 2L * (ncodes + nx);
	fper = per;
	codes = malloc(ncodes * sizeof *codes);
	dir = malloc(dsize);
	faces = calloc(nfaces, sizeof *faces);
	foff = malloc(nfaces * sizeof *foff);
	fstored = malloc(nfaces * sizeof *fstored);
	fsize = malloc(nfaces * sizeof *fsize);
	if (nx) {
		xcodes = malloc(nx * sizeof *xcodes);
		xidx = malloc(nx * sizeof *xidx);
		xblk = malloc(nx);
		all = malloc(nx * sizeof *all);
	}
	if (!codes || !dir || !faces || !foff || !fstored || !fsize || nfaces == 0 || ncodes == 0 ||
	    (nx && (!xcodes || !xidx || !xblk || !all)))
		goto done;
	if (!read_at(h, v3 ? 12 + 2L * nblocks : 12, dir, dsize))
		goto done;
	for (i = 0; i < ncodes; i++)
		codes[i] = be16(dir + 2 * i);
	{
		/* each: code << 16 | block << 12 | place (a block holds fewer than 4096) */
		long k = 0, at = ncodes;
		int sorted = 1;
		for (b = 1; b < nblocks; b++)
			for (i = 0; i < bcount[b]; i++, at++) {
				all[k] = (uint32_t) be16(dir + 2 * at) << 16 | (uint32_t) b << 12 | (uint32_t) i;
				if (i > 0 && all[k] >> 16 <= all[k - 1] >> 16)
					sorted = 0;
				k++;
			}
		if (nx && sorted) {
			/* each block's codes in order (mkfont.py's): merged, not
			   sorted (qsort's 9000 calls were most of an 8 MHz ST's
			   0.73 s in font_init) */
			uint32_t *m = malloc(nx * sizeof *m);
			long from[FONT_BLOCKS], to[FONT_BLOCKS], n;
			if (m == NULL) {
				qsort(all, nx, sizeof *all, by_code);
			} else {
				for (b = 1, at = 0; b < nblocks; b++) {
					from[b] = at;
					to[b] = at += bcount[b];
				}
				for (n = 0; n < nx; n++) {
					int best = 0;
					for (b = 1; b < nblocks; b++)
						if (from[b] < to[b] && (best == 0 || all[from[b]] < all[from[best]]))
							best = b;
					m[n] = all[from[best]++];
				}
				memcpy(all, m, nx * sizeof *m);
				free(m);
			}
		} else if (nx) {
			qsort(all, nx, sizeof *all, by_code);
		}
		for (k = 0; k < nx; k++) {
			xcodes[k] = (uint16_t) (all[k] >> 16);
			xblk[k] = (uint8_t) (all[k] >> 12 & 15);
			xidx[k] = (uint16_t) (all[k] & 4095);
		}
	}
	for (i = 0; i < nfaces; i++) {
		const uint8_t *d = dir + 2L * (ncodes + nx) + per * i;
		faces[i].family = d[0];
		faces[i].style = d[1];
		faces[i].px = d[2];
		foff[i] = be32(d + 4);
		fstored[i] = be32(d + 8);
		fsize[i] = be32(d + 12);
	}
	for (i = 0; i < 96; i++)
		ascii[i] = -1;
	qmark = 0;
	for (i = 0; i < ncodes; i++) {
		if (codes[i] >= 0x20 && codes[i] < 0x80)
			ascii[codes[i] - 0x20] = (int16_t) i;
		if (codes[i] == '?')
			qmark = i;
	}
	for (i = 0; i < 96; i++)
		aoff[i] = (uint16_t) (4 + 10 * (ascii[i] >= 0 ? ascii[i] : qmark));
	memset(noff, 0, sizeof noff);	/* (each looked up again in these fonts) */
	ok = 1;
done:
	free(dir);
	free(all);
	fh = h;
	if (!ok)
		font_free();
	return ok;
}

int (*font_poll)(void);

/* unpacking a block in pieces: the packed bytes read whole first */
struct packed_src {
	const uint8_t *p;
	long left;
};

static int packed_get(void *u, unsigned char *buf, long n)
{
	struct packed_src *s = u;
	if (n > s->left)
		n = s->left;
	memcpy(buf, s->p, (size_t) n);
	s->p += n;
	s->left -= n;
	return (int) n;
}

/* stored bytes of DEFLATE unpacked into exactly size bytes at out: 2 KB at
   a time with font_poll's turn between (a face, 10 KB, took a stock
   Falcon 130 ms in one go: most of a small page's layout; the guidelines'
   Speed, 31 Oct 2026); 1 when whole */
static int unpack_block(const uint8_t *packed, long stored, uint8_t *out, long size)
{
	struct packed_src src;
	INFLATE *z;
	long got = 0, n;
	if (font_poll == NULL) {
		SBUF o;
		int ok;
		sb_init(&o);
		ok = sb_reserve(&o, size) && inflate_raw(packed, stored, &o, size) == 1 && o.len == size;
		if (ok)
			memcpy(out, o.s, (size_t) size);
		sb_free(&o);
		return ok;
	}
	src.p = packed;
	src.left = stored;
	if ((z = inflate_open(packed_get, &src)) == NULL)
		return 0;
	while (got < size && (n = inflate_read(z, out + got, size - got > 2048 ? 2048 : size - got)) > 0) {
		got += n;
		if (got < size)
			font_poll();	/* (a stop is seen by the layout after: the face is made whole) */
	}
	inflate_close(z);
	return got == size;
}

/* a face's block b read and unpacked (NULL: it can't be) */
static uint8_t *read_block(int face, int b)
{
	uint32_t off, stored, size;
	uint8_t *packed, *data;
	SBUF out;
	if (b == 0) {
		off = foff[face];
		stored = fstored[face];
		size = fsize[face];
	} else {
		uint8_t e[12];
		if (fh < 0 || !read_at(fh, fdir + fper * face + 4 + 12 * b, e, 12))
			return NULL;
		off = be32(e);
		stored = be32(e + 4);
		size = be32(e + 8);
	}
	(void) out;
	packed = malloc(stored ? stored : 1);
	/* the block's room at its exact size: no doubling while it unpacks */
	data = malloc(size ? size : 1);
	if (packed && data && fh >= 0 && read_at(fh, off, packed, stored) && (long) size >= 4 + 10L * bcount[b] &&
	    unpack_block(packed, (long) stored, data, (long) size)) {
		free(packed);
		return data;
	}
	free(data);
	free(packed);
	return NULL;
}

static int load(ew_font *f)
{
	static const ew_font *loading;	/* (a turn while it unpacks may draw: not read twice) */
	if (f->data || f->failed || f == loading)
		return f->data != NULL;
	loading = f;
	f->data = read_block((int) (f - faces), 0);
	loading = NULL;
	if (f->data == NULL)
		f->failed = 1;
	if (f->data) {
		f->ascent = f->data[0];
		f->descent = f->data[1];
	}
	return f->data != NULL;
}

/* the face's glyphs there to use (read again if font_drop gave them back) */
static int ready(ew_font *f)
{
	return f != NULL && (f->data != NULL || (!f->failed && load(f)));
}

ew_font *font_get(int family, int style, int px)
{
	ew_font *best = NULL;
	uint8_t *pk = NULL;
	int i, bd = 1 << 30;

	if (family >= 0 && family < 4 && style >= 0 && style < 4 && px >= 0 && px < 64)
		pk = &picked[family][style][px];
	if (pk != NULL && *pk != 0) {
		best = &faces[*pk - 1];
	} else {
		for (i = 0; i < nfaces; i++) {
			int d;
			if (faces[i].family != family || faces[i].style != style)
				continue;
			/* the nearest size; between two, the smaller (text fits) */
			d = faces[i].px > px ? 2 * (faces[i].px - px) + 1 : 2 * (px - faces[i].px);
			if (d < bd) {
				bd = d;
				best = &faces[i];
			}
		}
		if (best == NULL && nfaces > 0)
			best = &faces[0];
		if (pk != NULL && best != NULL && best - faces < 255)
			*pk = (uint8_t) (best - faces + 1);
	}
	if (best != NULL && !best->data && !best->failed && load(best) == 0) {
		/* a face it can't read: its metrics from its size, no glyphs */
		best->ascent = (uint8_t) (best->px * 4 / 5 + 1);
		best->descent = (uint8_t) (best->px / 4 + 1);
	}
	return best;
}

static int index_of(uint32_t cp)
{
	int lo = 0, hi = ncodes - 1;
	if (cp >= 0x20 && cp < 0x80 && ascii[cp - 0x20] >= 0)
		return ascii[cp - 0x20];
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		if (codes[mid] == cp)
			return mid;
		if (codes[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* where cp is among the other blocks' codes, -1 if it isn't */
static int xindex_of(uint32_t cp)
{
	int lo = 0, hi = nx - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		if (xcodes[mid] == cp)
			return mid;
		if (xcodes[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* a character the fonts haven't got that is drawn as nothing: format
   characters, Hebrew points, and emoji (EWEB.FNT has no pictures of
   them: webdriveruniversity.com's front page was a row of '?'s, 3 Nov
   2026), with what joins and colours them (variation selectors, the
   keycap, tags); the symbols among them the fonts have (U+2605, U+2713...)
   are drawn, since only a character not found comes here */
static inline int zero_width(uint32_t cp)
{
	return cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xAD || cp == 0xFEFF ||
	       (cp >= 0x200E && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
	       (cp >= 0x2066 && cp <= 0x2069) || (cp >= 0x591 && cp <= 0x5C7) ||
	       (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x20E3 ||
	       (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x231A && cp <= 0x23FF) ||
	       (cp >= 0xE0020 && cp <= 0xE007F);
}

static inline int blank(uint32_t cp)
{
	return cp == 0xA0 || cp == 0x2007 || cp == 0x202F || (cp >= 0x2000 && cp <= 0x200A);
}

/* noff's slot for cp, -1 if it has none */
static long near_slot(uint32_t cp)
{
	return cp - 0x80 < NEAR_N ? (long) (cp - 0x80) : cp - 0x2000 < PUNCT_N ? (long) (NEAR_N + cp - 0x2000) : -1;
}

/* noff's entry for cp, as font_glyph finds its glyph: the first
   block's place, else another block's, else a space's, nothing (1) or
   '?'s */
static uint16_t near_find(uint32_t cp)
{
	int i = index_of(cp);
	if (i >= 0)
		return (uint16_t) (4 + 10 * i);
	if (xindex_of(cp) >= 0)
		return NEAR_OTHER;
	if (blank(cp))
		return (uint16_t) (4 + 10 * (ascii[0] >= 0 ? ascii[0] : qmark));
	if (zero_width(cp))
		return 1;
	return (uint16_t) (4 + 10 * qmark);
}

/* a face's block b (1 on), read when first needed; NULL if it can't be */
static const uint8_t *block_of(ew_font *f, int b)
{
	if (f->more[b - 1] == NULL && !(f->bad & 1 << b)) {
		uint8_t *d = read_block((int) (f - faces), b);
		if (f->more[b - 1] != NULL)
			free(d);	/* (read meanwhile, in a turn as it unpacked) */
		else if (d == NULL)
			f->bad |= (uint8_t) (1 << b);
		else
			f->more[b - 1] = d;
	}
	return f->more[b - 1];
}

/* the glyph at place i in a block (data) */
static void glyph_at(const uint8_t *data, int i, GLYPH *g)
{
	const uint8_t *m = data + 4 + 10 * i;
	uint32_t off = be32(m + 6);
	g->adv = m[0];
	g->x = (int8_t) m[1];
	g->y = (int8_t) m[2];
	g->w = m[3];
	g->h = m[4];
	g->bits = off ? data + off : NULL;
}

void font_glyph(ew_font *f, uint32_t cp, GLYPH *g)
{
	int i;

	if (!ready(f)) {
		memset(g, 0, sizeof *g);
		return;
	}
	i = index_of(cp);
	if (i < 0 && nx > 0 && cp >= 0x80 && (i = xindex_of(cp)) >= 0) {
		const uint8_t *data = block_of(f, xblk[i]);
		if (data != NULL) {
			glyph_at(data, xidx[i], g);
			return;
		}
		i = -1;		/* (its block can't be read: as a character the fonts haven't got) */
	}
	if (i < 0) {
		if (blank(cp)) {
			i = index_of(' ');
		} else if (zero_width(cp)) {
			memset(g, 0, sizeof *g);
			return;		/* nothing to draw: format characters, Hebrew points */
		} else {
			i = qmark;
		}
	}
	glyph_at(f->data, i, g);
}

/* the place of cp's advance in a face (noff), 0 if it has none there */
static unsigned near_place(uint32_t cp)
{
	long k = near_slot(cp);
	unsigned o;
	if (k < 0)
		return 0;
	if ((o = noff[k]) == 0)
		o = noff[k] = near_find(cp);
	return o == NEAR_OTHER ? 0 : o;
}

int font_advance(ew_font *f, uint32_t cp)
{
	GLYPH g;
	unsigned o;
	if (cp - 0x20 < 96)
		return ready(f) ? f->data[aoff[cp - 0x20]] : 0;
	if ((o = near_place(cp)) != 0)
		return o == 1 || !ready(f) ? 0 : f->data[o];
	font_glyph(f, cp, &g);
	return g.adv;
}

uint32_t utf8_next(const char **p, const char *end)
{
	const unsigned char *s = (const unsigned char *) *p;
	uint32_t c = *s++, n, min;

	if (c < 0x80) {
		*p = (const char *) s;
		return c;
	}
	if (c >= 0xC2 && c < 0xE0) { n = 1; c &= 0x1F; min = 0x80; }
	else if (c >= 0xE0 && c < 0xF0) { n = 2; c &= 0x0F; min = 0x800; }
	else if (c >= 0xF0 && c < 0xF5) { n = 3; c &= 0x07; min = 0x10000; }
	else { *p = (const char *) s; return 0xFFFD; }
	if ((const char *) s + n > end) {
		*p = (const char *) s;
		return 0xFFFD;
	}
	while (n--) {
		if ((*s & 0xC0) != 0x80) {
			*p = (const char *) s;
			return 0xFFFD;
		}
		c = c << 6 | (*s++ & 0x3F);
	}
	*p = (const char *) s;
	return c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000) ? 0xFFFD : c;
}

const uint8_t *font_widths(ew_font *f)
{
	int c, a;
	if (f == NULL || f->wt != NULL)
		return f != NULL ? f->wt : NULL;
	if (!ready(f) || (f->wt = calloc(256, 1)) == NULL)
		return NULL;
	for (c = 0; c < 0x80; c++) {
		/* (below a space: '?', as font_glyph draws it) */
		a = f->data[c < 0x20 ? 4 + 10 * qmark : aoff[c - 0x20]];
		f->wt[c] = (uint8_t) (a < 0 ? 0 : a > 255 ? 255 : a);	/* (a face under 64 px: none wider) */
	}
	return f->wt;
}

long font_width(ew_font *f, const char *s, size_t n)
{
	const char *e = s + n;
	long w = 0;
	if (!ready(f))
		return 0;
	while (s < e) {
		unsigned c = (unsigned char) *s - 0x20, o;
		if (c < 96) {
			w += f->data[aoff[c]];	/* ASCII: its advance at hand */
			s++;
		} else if (c - (0xC2 - 0x20) < 0xE0 - 0xC2 && s + 1 < e && ((unsigned char) s[1] & 0xC0) == 0x80 &&
			   ((o = noff[(((c + 0x20) & 0x1F) << 6 | ((unsigned char) s[1] & 0x3F)) - 0x80]) == 1 ||
			    o > NEAR_OTHER)) {
			/* two bytes of UTF-8 (Hebrew, Latin-1...) with its advance at
			   hand (else font_advance, which looks it up the first time) */
			if (o != 1)
				w += f->data[o];
			s += 2;
		} else {
			w += font_advance(f, utf8_next(&s, e));
		}
	}
	return w;
}
