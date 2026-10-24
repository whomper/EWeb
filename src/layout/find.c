/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* find.c - words found on a page (layout.h: page_find): the page's text
   runs, in the order they are drawn, folded into one string (case aside
   for ASCII and Latin-1, white space a space, a space between two runs),
   with where each run starts in it; a match is then a run and a byte at
   each end. The string is made for each search and not kept */
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

/* a run into the string, after a space when there is a gap between it
   and the one before (a word space, a line's end): a space in the words
   matches it, and two words don't run together; none between two parts
   of a word (a bold "Ata" and "ri"), side by side on a line, either way */
static void add(FT *t, const dl_item *it, long i, const dl_item **prev)
{
	const dl_item *q = *prev;
	if (q != NULL && !(it->y + it->base == q->y + q->base &&
			   ((it->x >= q->x + q->w - 1 && it->x <= q->x + q->w + 1) ||
			    (it->x + it->w >= q->x - 1 && it->x + it->w <= q->x + 1))))
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
	for (i = 0; i < p->nitems; i++) {
		const dl_item *it = &p->items[i];
		long j, k, last;
		if (!is_run(it))
			continue;
		if (!(it->flags & DLF_RTL)) {
			add(t, it, i, &prev);
			continue;
		}
		/* right-to-left runs side by side on a line come left to right
		   in the list: they go in last to first, as they are read */
		for (j = i, last = i; j < p->nitems; j++) {
			const dl_item *r = &p->items[j];
			if (!is_run(r))
				continue;
			if (!(r->flags & DLF_RTL) || r->y + r->base != it->y + it->base)
				break;
			last = j;
		}
		for (k = last; k >= i; k--)
			if (is_run(&p->items[k]))
				add(t, &p->items[k], k, &prev);
		i = last;
	}
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

long page_find(const page *p, const char *what, const pg_pos *from, int back, long want, pg_pos *a, pg_pos *b,
	       long *nth)
{
	FT t;
	char *w;
	long wn, i, count = 0, first = -1, last = -1, after = -1, before = -1, n_first = 0, n_last = 0,
	     n_after = 0, n_before = 0, found, at = -1, wanted = -1;
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
		if (count == want)
			wanted = i;
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
	if (want > 0) {
		found = wanted >= 0 ? wanted : last;
		*nth = wanted >= 0 ? want : n_last;
	} else if (!back) {
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
