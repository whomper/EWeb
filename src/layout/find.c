/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* find.c - the page's text as it is read (layout.h): words found in it
   (page_find: its runs folded into one string, case aside for ASCII and
   Latin-1, white space a space, a space between two runs, with where
   each run starts in it; a match is then a run and a byte at each end;
   the string is made for each search and not kept), and a stretch of it
   selected and copied. The display list has a line's runs in the order
   they are read (layout.c: a right-to-left line's last to first on the
   screen), so the text is read in the list's order */
#include <stdlib.h>
#include <string.h>

#include "layout.h"

typedef struct {
	char *s;			/* the folded text */
	long n;
	long *start;			/* each item's first byte in s, -1 for one not text */
} FT;

/* each byte folded: ASCII's capitals small, white space a space, the
   rest as it is (a table made at the first search) */
static unsigned char folded[256];

/* s (n bytes) folded into d, the same length: by folded[], and Latin-1's
   capitals small (U+00C0..U+00DE but U+00D7: their UTF-8's second byte) */
static void fold(const char *s, long n, char *d)
{
	long i;
	if (folded['A'] == 0) {
		for (i = 0; i < 256; i++)
			folded[i] = (unsigned char) (i >= 'A' && i <= 'Z' ? i + 32 :
						     i == '\t' || i == '\n' || i == '\r' || i == '\f' ? ' ' : i);
	}
	for (i = 0; i < n; i++) {
		unsigned char c = folded[(unsigned char) s[i]];
		if (c == 0xC3 && i + 1 < n) {
			unsigned char k = (unsigned char) s[i + 1];
			d[i++] = (char) c;
			c = (unsigned char) (k >= 0x80 && k <= 0x9E && k != 0x97 ? k + 0x20 : k);
		}
		d[i] = (char) c;
	}
}

/* a text run the reader sees */
static int is_run(const dl_item *it)
{
	return it->type == DL_TEXT && (it->rgb >> 24) != 0 && it->len > 0;
}

/* two runs on one line: their heights overlapping by more than half the
   smaller's (a superscript on its line; not the next line, however
   tight the lines) */
static int one_line(const dl_item *q, const dl_item *it)
{
	int32_t top = it->y > q->y ? it->y : q->y, bot = it->y + it->h < q->y + q->h ? it->y + it->h : q->y + q->h;
	int32_t h = it->h < q->h ? it->h : q->h;
	return 2 * (bot - top) > h;
}

/* run it straight after q on a line, either side (two parts of a word, a
   bold "Ata" and "ri", an "H", a subscript "2" and an "O") */
static int touches(const dl_item *q, const dl_item *it)
{
	return one_line(q, it) && ((it->x >= q->x + q->w - 1 && it->x <= q->x + q->w + 1) ||
				   (it->x + it->w >= q->x - 1 && it->x + it->w <= q->x + 1));
}

/* a run into the string, after a space when there is a gap between it
   and the one before (a word space, a line's end): a space in the words
   matches it, and two words don't run together */
static void add(FT *t, const dl_item *it, long i, const dl_item **prev)
{
	if (*prev != NULL && !touches(*prev, it))
		t->s[t->n++] = ' ';
	*prev = it;
	t->start[i] = t->n;
	fold(it->text, (long) it->len, t->s + t->n);
	t->n += (long) it->len;
}

static int make(const page *p, FT *t)
{
	const dl_item *prev = NULL;
	long i, n = 1;
	t->start = malloc((size_t) (p->nitems > 0 ? p->nitems : 1) * sizeof *t->start);
	if (t->start == NULL)
		return 0;
	for (i = 0; i < p->nitems; i++) {
		t->start[i] = -1;
		if (is_run(&p->items[i]))
			n += (long) p->items[i].len + 1;
	}
	t->s = malloc((size_t) n);
	if (t->s == NULL) {
		free(t->start);
		return 0;
	}
	t->n = 0;
	for (i = 0; i < p->nitems; i++)
		if (is_run(&p->items[i]))
			add(t, &p->items[i], i, &prev);
	t->s[t->n] = 0;
	return 1;
}

/* the words folded, white space one space, none at either end */
static char *words(const char *w, long *len)
{
	long n = (long) strlen(w), i, k = 0;
	char *d = malloc((size_t) n + 1);
	if (d == NULL)
		return NULL;
	fold(w, n, d);
	for (i = 0; i < n; i++)
		if (d[i] != ' ' || (k > 0 && d[k - 1] != ' '))
			d[k++] = d[i];
	while (k > 0 && d[k - 1] == ' ')
		k--;
	d[k] = 0;
	*len = k;
	return d;
}

/* the words at s + i: where the match ends in s, or -1 (a space in them
   matches one or more) */
static long match(const FT *t, long i, const char *w, long wn)
{
	long j;
	for (j = 0; j < wn; j++) {
		if (i >= t->n)
			return -1;
		if (w[j] == ' ') {
			if (t->s[i] != ' ')
				return -1;
			while (i < t->n && t->s[i] == ' ')
				i++;
			continue;
		}
		if (t->s[i] != w[j])
			return -1;
		i++;
	}
	return i;
}

/* an offset in s as a place on the page: the run it is in (a match's
   end, end: the run its last byte is in, the byte after it) */
static void place(const page *p, const FT *t, long off, int end, pg_pos *at)
{
	long i, in = end ? off - 1 : off;
	at->item = -1;
	at->off = 0;
	for (i = 0; i < p->nitems; i++)
		if (t->start[i] >= 0 && t->start[i] <= in && in < t->start[i] + (long) p->items[i].len) {
			at->item = i;
			at->off = (uint32_t) (off - t->start[i]);
			return;
		}
}

long page_find(const page *p, const char *what, const pg_pos *from, int back, pg_pos *a, pg_pos *b, long *nth)
{
	FT t;
	char *w;
	long wn, i, count = 0, first = -1, last = -1, after = -1, before = -1, n_first = 0, n_last = 0,
	     n_after = 0, n_before = 0, found, at = -1;
	*nth = 0;
	if (p == NULL || !make(p, &t))
		return -1;
	w = words(what, &wn);
	if (w == NULL || wn == 0) {
		free(w);
		free(t.s);
		free(t.start);
		return w == NULL ? -1 : 0;
	}
	/* from: its offset in s (a run that isn't text: the next that is) */
	if (from != NULL && from->item >= 0 && from->item < p->nitems) {
		long k;
		for (k = from->item; k < p->nitems && t.start[k] < 0; k++)
			;
		at = k < p->nitems ? t.start[k] + (k == from->item ? (long) from->off : 0) : t.n;
	}
	for (i = 0; i < t.n; i++) {
		if (t.s[i] != w[0] || match(&t, i, w, wn) < 0)
			continue;	/* (its first byte here: a call only where it may start) */
		count++;
		if (first < 0) {
			first = i;
			n_first = count;
		}
		last = i;
		n_last = count;
		if (at >= 0 && i >= at && after < 0) {
			after = i;
			n_after = count;
		}
		if (at >= 0 && i < at) {
			before = i;
			n_before = count;
		}
	}
	/* forward: the first at or after from, else (round) the first; back:
	   the last before it, else (round) the last */
	if (!back) {
		found = after >= 0 ? after : first;
		*nth = after >= 0 ? n_after : n_first;
	} else {
		found = before >= 0 ? before : last;
		*nth = before >= 0 ? n_before : n_last;
	}
	if (found >= 0) {
		place(p, &t, found, 0, a);
		place(p, &t, match(&t, found, w, wn), 1, b);
	}
	free(w);
	free(t.s);
	free(t.start);
	return count;
}

/* ---------------------------------------------------------------- a stretch of text */

void page_range(const page *p, const pg_pos *x, const pg_pos *y, pg_range *r)
{
	int first = x->item < y->item || (x->item == y->item && x->off <= y->off);
	(void) p;
	r->a = first ? *x : *y;
	r->b = first ? *y : *x;
}

int page_range_part(const pg_range *r, long i, uint32_t len, uint32_t *s, uint32_t *e)
{
	if (i < r->a.item || i > r->b.item)
		return 0;
	*s = i == r->a.item ? r->a.off : 0;
	*e = i == r->b.item && r->b.off < len ? r->b.off : len;
	return *s < *e;
}

/* the run nearest (x, y) (clipped: one shown there only); -1 none. Its
   distance down and across in *by, *bx */
static long nearest(const page *p, int32_t x, int32_t y, int clipped, int32_t *by, int32_t *bx)
{
	const dl_item *d = p->items, *end = d + p->nitems, *best = NULL;
	int32_t ny = INT32_MAX, nx = INT32_MAX;
	for (; d < end; d++) {
		int32_t t, dx, dy;
		if (d->type != DL_TEXT)
			continue;
		/* (the distance down first: most runs are further than the best) */
		t = d->y;
		if (y < t)
			dy = t - y;
		else if (y < (t += d->h))
			dy = 0;
		else
			dy = y - t + 1;
		if (dy > ny)
			continue;
		t = d->x;
		if (x < t)
			dx = t - x;
		else if (x < (t += d->w))
			dx = 0;
		else
			dx = x - t + 1;
		if ((dy == ny && dx >= nx) || !is_run(d))
			continue;
		if (clipped && d->clip != 0) {
			/* (where it is nearest the point, shown there) */
			int32_t cx = x < d->x ? d->x : x >= d->x + d->w ? d->x + d->w - 1 : x;
			int32_t cy = y < d->y ? d->y : y >= d->y + d->h ? d->y + d->h - 1 : y, x1 = cx + 1, y1 = cy + 1;
			if (!page_clip(p, d->clip, &cx, &cy, &x1, &y1))
				continue;
		}
		best = d;
		nx = dx;
		ny = dy;
		if ((dx | dy) == 0)
			break;		/* (under the point: none nearer) */
	}
	*by = ny;
	*bx = nx;
	return best != NULL ? (long) (best - p->items) : -1;
}

/* a combining mark (Unicode's commonest: Latin's accents, Hebrew's points
   and cantillation, Arabic's vowels, and those for symbols): it goes
   with the character before it, whether the fonts have it or not */
static int mark(uint32_t cp)
{
	return (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x591 && cp <= 0x5C7 && cp != 0x5BE && cp != 0x5C0 &&
						 cp != 0x5C3 && cp != 0x5C6) ||
	       (cp >= 0x610 && cp <= 0x61A) || (cp >= 0x64B && cp <= 0x65F) || cp == 0x670 ||
	       (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
	       (cp >= 0xFE20 && cp <= 0xFE2F);
}

int page_pos_at(const page *p, int32_t x, int32_t y, pg_pos *at)
{
	long best;
	int32_t bx, by, px, w;
	const dl_item *d;
	const char *s, *e, *q;
	/* the nearest, then, if a box hides it there, the nearest shown */
	best = nearest(p, x, y, 0, &by, &bx);
	if (best >= 0 && p->items[best].clip != 0) {
		int32_t cx, cy, x1, y1;
		d = &p->items[best];
		cx = x < d->x ? d->x : x >= d->x + d->w ? d->x + d->w - 1 : x;
		cy = y < d->y ? d->y : y >= d->y + d->h ? d->y + d->h - 1 : y;
		x1 = cx + 1;
		y1 = cy + 1;
		if (!page_clip(p, d->clip, &cx, &cy, &x1, &y1))
			best = nearest(p, x, y, 1, &by, &bx);
	}
	if (best < 0)
		return 0;
	/* the byte: the nearest gap between two characters, from the run's
	   right in a right-to-left one; never before a character with no
	   width (a combining accent, a Hebrew vowel point: it goes with the
	   letter before it) */
	d = &p->items[best];
	px = d->flags & DLF_RTL ? d->x + d->w - x : x - d->x;
	s = d->text;
	e = s + d->len;
	for (q = s, w = 0; q < e;) {
		const char *n = q;
		uint32_t cp = utf8_next(&n, e);
		int32_t a = font_advance(d->font, cp);
		if (a > 0 && !mark(cp) && px < w + a / 2)
			break;
		w += a;
		q = n;
	}
	at->item = best;
	at->off = (uint32_t) (q - s);
	return bx == 0 && by == 0 ? 2 : 1;
}

static int word_byte(unsigned char c)
{
	return c >= 0x80 || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_';
}

/* the run read just before (dir < 0) or after run i, if it touches it
   (two parts of a word), else -1 */
static long beside(const page *p, long i, int dir)
{
	const dl_item *it = &p->items[i];
	long j;
	for (j = i + dir; j >= 0 && j < p->nitems; j += dir) {
		const dl_item *q = &p->items[j];
		if (!is_run(q))
			continue;
		return (q->flags & DLF_RTL) == (it->flags & DLF_RTL) && touches(q, it) ? j : -1;
	}
	return -1;
}

void page_word(const page *p, const pg_pos *at, pg_pos *a, pg_pos *b)
{
	const dl_item *d = &p->items[at->item];
	const unsigned char *t = (const unsigned char *) d->text;
	uint32_t s = at->off, e;
	long i = at->item, j;
	if (s > d->len)
		s = d->len;
	if ((s == d->len || !word_byte(t[s])) && s > 0 && word_byte(t[s - 1]))
		s--;
	e = s;
	a->item = b->item = i;
	if (s < d->len && word_byte(t[s])) {
		while (s > 0 && word_byte(t[s - 1]))
			s--;
		while (e < d->len && word_byte(t[e]))
			e++;
		a->off = s;
		b->off = e;
		/* on into the runs beside it that it goes on in (a bold "Ata"
		   and "ri") */
		while (a->off == 0 && (j = beside(p, a->item, -1)) >= 0) {
			const dl_item *q = &p->items[j];
			uint32_t k = q->len;
			if (!word_byte((unsigned char) q->text[k - 1]))
				break;
			while (k > 0 && word_byte((unsigned char) q->text[k - 1]))
				k--;
			a->item = j;
			a->off = k;
		}
		while (b->off == p->items[b->item].len && (j = beside(p, b->item, 1)) >= 0) {
			const dl_item *q = &p->items[j];
			uint32_t k = 0;
			if (!word_byte((unsigned char) q->text[0]))
				break;
			while (k < q->len && word_byte((unsigned char) q->text[k]))
				k++;
			b->item = j;
			b->off = k;
		}
		return;
	}
	if (e < d->len) {
		/* (not a word: the character there) */
		e++;
		while (e < d->len && (t[e] & 0xC0) == 0x80)
			e++;
	}
	a->off = s;
	b->off = e;
}

/* a character counted in the page's text: not white space, its first
   byte */
static int counted(unsigned char c)
{
	return c > ' ' && (c & 0xC0) != 0x80;
}

long page_text_index(const page *p, const pg_pos *at)
{
	long i, n = 0;
	for (i = 0; i <= at->item && i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		uint32_t k, m;
		if (!is_run(d))
			continue;
		m = i == at->item && at->off < d->len ? at->off : d->len;
		for (k = 0; k < m; k++)
			n += counted((unsigned char) d->text[k]);
	}
	return n;
}

int page_text_place(const page *p, long index, int end, pg_pos *at)
{
	long i, n = 0, last = -1;
	if (index <= 0)
		end = 0;
	for (i = 0; i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		uint32_t k;
		if (!is_run(d))
			continue;
		for (k = 0; k < d->len; k++) {
			if (!counted((unsigned char) d->text[k]))
				continue;
			if (!end && n == index) {
				at->item = i;
				at->off = k;
				return 1;
			}
			if (++n == index && end) {
				/* (after its last byte) */
				for (k++; k < d->len && (d->text[k] & 0xC0) == 0x80; k++)
					;
				at->item = i;
				at->off = k;
				return 1;
			}
		}
		last = i;
	}
	if (last < 0)
		return 0;
	at->item = last;	/* (past the end: the end) */
	at->off = p->items[last].len;
	return 1;
}

/* what goes between two runs read one after the other: none between two
   parts of a word, a space between two on a line, a line break, and an
   empty line before a run lower than two of its lines down */
static long gap(const dl_item *q, const dl_item *it, char *s)
{
	if (one_line(q, it)) {
		if (touches(q, it) || q->text[q->len - 1] == ' ' || it->text[0] == ' ')
			return 0;	/* (a part of a word, or a space there already) */
		*s = ' ';
		return 1;
	}
	if (it->y + it->base > q->y + q->base + it->font->px * 2) {
		memcpy(s, "\r\n\r\n", 4);
		return 4;
	}
	memcpy(s, "\r\n", 2);
	return 2;
}

char *page_copy_text(const page *p, const pg_range *r, long *n)
{
	const dl_item *q = NULL;
	long i, cap = 3, k = 0, i0 = r ? r->a.item : 0, i1 = r ? r->b.item : p->nitems - 1;
	char *s;
	for (i = i0; i <= i1 && i < p->nitems; i++)
		if (is_run(&p->items[i]))
			cap += (long) p->items[i].len + 4;
	if ((s = malloc((size_t) cap)) == NULL)
		return NULL;
	for (i = i0; i <= i1 && i < p->nitems; i++) {
		const dl_item *d = &p->items[i];
		uint32_t a = 0, e = d->len;
		if (!is_run(d) || (r != NULL && !page_range_part(r, i, d->len, &a, &e)))
			continue;
		if (q != NULL)
			k += gap(q, d, s + k);
		memcpy(s + k, d->text + a, e - a);
		k += (long) (e - a);
		q = d;
	}
	if (r == NULL) {
		s[k++] = '\r';
		s[k++] = '\n';
	}
	s[k] = 0;
	*n = k;
	return s;
}

int page_reveal_item(page *p, long i, int top)
{
	int k[16], m = 0, s, moved = 0;
	unsigned c;
	int32_t y, h;
	if (p == NULL || i < 0 || i >= p->nitems || p->nscr == 0)
		return 0;
	/* the boxes that scroll that it is in, innermost first, by its
	   boxes hiding what overflows them (a box that scrolls is one) */
	for (c = p->items[i].clip; c != 0 && c <= (unsigned) p->nclips && m < 16;
	     c = p->items[p->clips[c - 1]].clip)
		for (s = 0; s < p->nscr; s++)
			if (p->scr[s].clip == p->clips[c - 1]) {
				k[m++] = s;
				break;
			}
	y = p->items[i].y;
	h = p->items[i].h;
	while (m-- > 0) {
		struct pg_scroll *sc = &p->scr[k[m]];
		const dl_item *b = &p->items[sc->clip];
		int32_t to = sc->sy, x0, y0, w0, h0;
		if (top || y < b->y)
			to = sc->sy + (y - b->y);
		else if (y + h > b->y + b->h)
			to = sc->sy + (y + h - (b->y + b->h));
		if (page_scroll(p, k[m], sc->sx, to, &x0, &y0, &w0, &h0)) {
			y = p->items[i].y;	/* (moved with its box) */
			moved = 1;
		}
	}
	return moved;
}
