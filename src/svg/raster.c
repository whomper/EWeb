/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * raster.c - paths filled and stroked into a picture, anti-aliased
 * (raster.h). A fill's edges go through a band of rows at a time: each
 * piece of an edge in a pixel's cell adds its height there (cover) and
 * its height times its two ends' places across the cell (area); a row
 * read from the left then has, at each pixel, the cover of the cells
 * before it whole and its own cell's part, which is the area the path
 * covers in it exactly. 32-bit arithmetic but where an edge is cut at
 * a row, and curves halved into lines with shifts alone.
 */
#include <stdlib.h>
#include <string.h>

#include "raster.h"
#include "fixmath.h"

#define F RS_ONE
#define BAND 16				/* rows worked out at once */
#define CLOSED 0x40000000L		/* in rs_canvas.sub: the subpath was closed */
/* x / 255 rounded, for x up to 255 * 255 (no division) */
#define DIV255(x) ((((x) + 128) * 257) >> 16)

int rs_init(rs_canvas *c, int w, int h)
{
	memset(c, 0, sizeof *c);
	if (w <= 0 || h <= 0)
		return 0;
	c->w = w;
	c->h = h;
	c->px = calloc((size_t) w * h, 4);
	c->cell = malloc((size_t) (w + 2) * BAND * 2 * sizeof *c->cell);
	if (c->px == NULL || c->cell == NULL) {
		rs_free(c);
		return 0;
	}
	return 1;
}

void rs_free(rs_canvas *c)
{
	free(c->px);
	free(c->pt);
	free(c->sub);
	free(c->e);
	free(c->cell);
	c->px = NULL;
	c->pt = NULL;
	c->sub = NULL;
	c->e = NULL;
	c->cell = NULL;
}

/* ------------------------------------------------------------ the path */

static void put(rs_canvas *c, int32_t x, int32_t y)
{
	if (c->npt + 2 > c->ptcap) {
		long cap = c->ptcap ? c->ptcap * 2 : 256;
		int32_t *t = realloc(c->pt, (size_t) cap * sizeof *t);
		if (t == NULL) {
			c->nomem = 1;
			return;
		}
		c->pt = t;
		c->ptcap = cap;
	}
	c->pt[c->npt++] = x;
	c->pt[c->npt++] = y;
}

void rs_move(rs_canvas *c, int32_t x, int32_t y)
{
	if (c->nsub > 0 && (c->sub[c->nsub - 1] & ~CLOSED) == c->npt - 2 && !(c->sub[c->nsub - 1] & CLOSED)) {
		/* a subpath of one point: moved on from */
		c->pt[c->npt - 2] = x;
		c->pt[c->npt - 1] = y;
		return;
	}
	if (c->nsub == c->subcap) {
		long cap = c->subcap ? c->subcap * 2 : 16;
		long *t = realloc(c->sub, (size_t) cap * sizeof *t);
		if (t == NULL) {
			c->nomem = 1;
			return;
		}
		c->sub = t;
		c->subcap = cap;
	}
	c->sub[c->nsub++] = c->npt;
	put(c, x, y);
}

/* the pen: the path's last point (a closed subpath's: its first) */
static int pen(const rs_canvas *c, int32_t *x, int32_t *y)
{
	long s;
	if (c->nsub == 0)
		return 0;
	s = c->sub[c->nsub - 1];
	if (s & CLOSED) {
		*x = c->pt[s & ~CLOSED];
		*y = c->pt[(s & ~CLOSED) + 1];
	} else {
		*x = c->pt[c->npt - 2];
		*y = c->pt[c->npt - 1];
	}
	return 1;
}

void rs_line(rs_canvas *c, int32_t x, int32_t y)
{
	int32_t px, py;
	if (!pen(c, &px, &py)) {
		rs_move(c, x, y);
		return;
	}
	if (c->sub[c->nsub - 1] & CLOSED)
		rs_move(c, px, py);	/* a line after a close: a subpath from its start */
	put(c, x, y);
}

/* a cubic curve halved depth times (de Casteljau, at the middle: sums and
   shifts), its pieces' ends added as lines */
static void halve(rs_canvas *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3,
		  int32_t y3, int depth)
{
	int32_t ax, ay, bx, by, cx, cy, dx, dy, ex, ey, mx, my;
	if (depth == 0) {
		put(c, x3, y3);
		return;
	}
	ax = (x0 + x1) >> 1, ay = (y0 + y1) >> 1;
	bx = (x1 + x2) >> 1, by = (y1 + y2) >> 1;
	cx = (x2 + x3) >> 1, cy = (y2 + y3) >> 1;
	dx = (ax + bx) >> 1, dy = (ay + by) >> 1;
	ex = (bx + cx) >> 1, ey = (by + cy) >> 1;
	mx = (dx + ex) >> 1, my = (dy + ey) >> 1;
	halve(c, x0, y0, ax, ay, dx, dy, mx, my, depth - 1);
	halve(c, mx, my, ex, ey, cx, cy, x3, y3, depth - 1);
}

static int32_t iabs(int32_t v)
{
	return v < 0 ? -v : v;
}

void rs_cubic(rs_canvas *c, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
	int32_t x0, y0, dd, d2;
	int depth = 0;
	if (!pen(c, &x0, &y0)) {
		rs_move(c, x3, y3);
		return;
	}
	if (c->sub[c->nsub - 1] & CLOSED)
		rs_move(c, x0, y0);
	/* how far the curve strays from its chord, at most: its control
	   polygon's second differences; each halving quarters them. Halved
	   until a piece strays under a fifth of a pixel */
	dd = iabs(x0 - 2 * x1 + x2) + iabs(y0 - 2 * y1 + y2);
	d2 = iabs(x1 - 2 * x2 + x3) + iabs(y1 - 2 * y2 + y3);
	if (d2 > dd)
		dd = d2;
	while (dd > F / 5 && depth < 7) {
		dd >>= 2;
		depth++;
	}
	halve(c, x0, y0, x1, y1, x2, y2, x3, y3, depth);
}

void rs_close(rs_canvas *c)
{
	if (c->nsub > 0)
		c->sub[c->nsub - 1] |= CLOSED;
}

void rs_clear(rs_canvas *c)
{
	c->npt = 0;
	c->nsub = 0;
	c->ne = 0;
}

/* ------------------------------------------------------------ filling */

static void edge(rs_canvas *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	if (y0 == y1)
		return;		/* (a level edge covers nothing) */
	if (c->ne == c->ecap) {
		long cap = c->ecap ? c->ecap * 2 : 256;
		rs_edge *t = realloc(c->e, (size_t) cap * sizeof *t);
		if (t == NULL) {
			c->nomem = 1;
			return;
		}
		c->e = t;
		c->ecap = cap;
	}
	c->e[c->ne].x0 = x0;
	c->e[c->ne].y0 = y0;
	c->e[c->ne].x1 = x1;
	c->e[c->ne].y1 = y1;
	c->ne++;
}

/* the band being worked out: its cells from column cx0, cw of them a row */
typedef struct {
	int32_t *cell;
	int cx0, cw;
	int32_t top;			/* its first row's top, a point's units */
	int32_t xl, xr;			/* the cells' left and right, a point's units */
} BANDS;

/* a piece of an edge within a row (r, its top in rt): each cell it goes
   through its part */
static void row_piece(BANDS *b, int r, int32_t rt, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	int32_t *row = b->cell + (long) r * b->cw * 2;
	int32_t fy0 = y0 - rt, fy1 = y1 - rt;
	int c0, c1;
	if (x0 < b->xl)
		x0 = b->xl;
	else if (x0 > b->xr)
		x0 = b->xr;
	if (x1 < b->xl)
		x1 = b->xl;
	else if (x1 > b->xr)
		x1 = b->xr;
	x0 -= b->xl;
	x1 -= b->xl;
	c0 = x0 >> 8;
	c1 = x1 >> 8;
	if (c0 == c1 || x0 == x1) {
		int32_t *k = row + c0 * 2;
		k[0] += fy1 - fy0;
		k[1] += (fy1 - fy0) * ((x0 - (c0 << 8)) + (x1 - (c0 << 8)));
		return;
	}
	{
		/* across cells: cut where it crosses each cell's side */
		int32_t dx = x1 - x0, dy = fy1 - fy0;
		int step = dx > 0 ? 1 : -1;
		int cell = c0;
		int32_t px = x0, py = fy0;
		for (;;) {
			int32_t bx, by, *k = row + cell * 2;
			if (cell == c1) {
				k[0] += fy1 - py;
				k[1] += (fy1 - py) * ((px - (cell << 8)) + (x1 - (cell << 8)));
				return;
			}
			bx = step > 0 ? (cell + 1) << 8 : cell << 8;
			by = fy0 + (int32_t) ((int32_t) (bx - x0) * dy / dx);	/* |dy| <= 256: 32 bits */
			k[0] += by - py;
			k[1] += (by - py) * ((px - (cell << 8)) + (bx - (cell << 8)));
			px = bx;
			py = by;
			cell += step;
		}
	}
}

/* the edge's x where it is at y: in 32 bits when the product fits */
static int32_t x_at(const rs_edge *e, int32_t y)
{
	int32_t dx = e->x1 - e->x0, dy = e->y1 - e->y0, t = y - e->y0;
	if (dx > -32768 && dx < 32768 && t > -65536 && t < 65536)
		return e->x0 + dx * t / dy;
	return e->x0 + (int32_t) ((int64_t) dx * t / dy);
}

/* an edge's part in the band */
static void band_edge(BANDS *b, const rs_edge *e, int32_t bot)
{
	int32_t lo = e->y0 < e->y1 ? e->y0 : e->y1, hi = e->y0 < e->y1 ? e->y1 : e->y0;
	int32_t ya, yb, xa, xb, y, x;
	if (lo < b->top)
		lo = b->top;
	if (hi > bot)
		hi = bot;
	if (lo >= hi)
		return;
	/* from where it enters the band to where it leaves, its way */
	if (e->y0 < e->y1)
		ya = lo, yb = hi;
	else
		ya = hi, yb = lo;
	xa = x_at(e, ya);
	xb = x_at(e, yb);
	/* row by row */
	x = xa;
	y = ya;
	if (ya < yb) {
		int r = (ya - b->top) >> 8;
		for (;;) {
			int32_t rt = b->top + (r << 8), rb = rt + F, nx;
			if (yb <= rb) {
				row_piece(b, r, rt, x, y, xb, yb);
				break;
			}
			nx = x_at(e, rb);
			row_piece(b, r, rt, x, y, nx, rb);
			x = nx;
			y = rb;
			r++;
		}
	} else {
		int r = (ya - 1 - b->top) >> 8;
		for (;;) {
			int32_t rt = b->top + (r << 8), nx;
			if (yb >= rt) {
				row_piece(b, r, rt, x, y, xb, yb);
				break;
			}
			nx = x_at(e, rt);
			row_piece(b, r, rt, x, y, nx, rt);
			x = nx;
			y = rt;
			r--;
		}
	}
}

static uint32_t isqrt(uint32_t v)
{
	uint32_t r = 0, bit = 1UL << 30;
	while (bit > v)
		bit >>= 2;
	while (bit != 0) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else
			r >>= 1;
		bit >>= 2;
	}
	return r;
}

/* a gradient's colour at t (16.16, 1.0 its end), spread past its ends */
static uint32_t ramp_at(const rs_paint *p, int32_t t)
{
	int32_t k = t >> 8;			/* 0 .. 255 along it */
	if (k < 0 || k > 255) {
		if (p->spread == RS_REPEAT)
			k &= 255;
		else if (p->spread == RS_REFLECT)
			k = (k & 256) ? 255 - (k & 255) : (k & 255);
		else
			k = k < 0 ? 0 : 255;
	}
	return p->ramp[k];
}

/* the edges filled */
static void fill_edges(rs_canvas *c, int evenodd, const rs_paint *pt)
{
	int32_t x0 = 0x7fffffff, y0 = 0x7fffffff, x1 = -0x7fffffff, y1 = -0x7fffffff;
	unsigned sr = (pt->rgb >> 16) & 255, sg = (pt->rgb >> 8) & 255, sb = pt->rgb & 255, alpha = pt->alpha;
	int ry0, ry1, cx1, by;
	long i;
	BANDS b;
	if (c->ne == 0 || alpha == 0)
		return;
	for (i = 0; i < c->ne; i++) {
		const rs_edge *e = &c->e[i];
		if (e->x0 < x0) x0 = e->x0;
		if (e->x1 < x0) x0 = e->x1;
		if (e->x0 > x1) x1 = e->x0;
		if (e->x1 > x1) x1 = e->x1;
		if (e->y0 < y0) y0 = e->y0;
		if (e->y1 < y0) y0 = e->y1;
		if (e->y0 > y1) y1 = e->y0;
		if (e->y1 > y1) y1 = e->y1;
	}
	if (x1 <= 0 || y1 <= 0 || x0 >= c->w * F || y0 >= c->h * F)
		return;
	b.cell = c->cell;
	b.cx0 = x0 < 0 ? 0 : x0 >> 8;
	cx1 = x1 >= c->w * F ? c->w - 1 : (x1 - 1) >> 8;
	b.cw = cx1 - b.cx0 + 2;
	b.xl = b.cx0 << 8;
	b.xr = (cx1 + 1) << 8;
	ry0 = y0 < 0 ? 0 : y0 >> 8;
	ry1 = y1 >= c->h * F ? c->h : (y1 + F - 1) >> 8;
	for (by = ry0; by < ry1; by += BAND) {
		int rows = ry1 - by < BAND ? ry1 - by : BAND, r;
		b.top = by << 8;
		memset(b.cell, 0, (size_t) rows * b.cw * 2 * sizeof *b.cell);
		for (i = 0; i < c->ne; i++)
			band_edge(&b, &c->e[i], (by + rows) << 8);
		for (r = 0; r < rows; r++) {
			const int32_t *k = b.cell + (long) r * b.cw * 2;
			uint8_t *p = c->px + ((long) (by + r) * c->w + b.cx0) * 4;
			/* a gradient: where the row's first pixel's centre is along it,
			   and each step right */
			int32_t gx = ((int32_t) b.cx0 << 16) + 32768, gy = ((int32_t) (by + r) << 16) + 32768;
			int32_t u = 0, v = 0;
			int32_t run = 0;
			int x;
			if (pt->kind != RS_SOLID) {
				u = (int32_t) (((int64_t) pt->m[0] * gx + (int64_t) pt->m[1] * gy) >> 16) + pt->m[2];
				if (pt->kind == RS_RADIAL)
					v = (int32_t) (((int64_t) pt->m[3] * gx + (int64_t) pt->m[4] * gy) >> 16) + pt->m[5];
			}
			for (x = 0; x < b.cw - 1; x++, k += 2, p += 4) {
				int32_t cv;
				unsigned a, na, cr = sr, cg = sg, cb = sb;
				run += k[0];
				cv = run * (2 * F) - k[1];
				if (cv < 0)
					cv = -cv;
				a = (unsigned) (cv >> 9);	/* 2 * F * F: whole */
				if (evenodd) {
					a &= 511;
					if (a > 256)
						a = 512 - a;
				}
				if (a == 0)
					goto next;
				if (a > 255)
					a = 255;
				if (pt->kind != RS_SOLID) {
					int32_t t;
					uint32_t col;
					if (pt->kind == RS_LINEAR) {
						t = u;
					} else {
						/* |(u, v)|, each kept within 8 (4.12 squared fits) */
						int32_t uu = u >> 4, vv = v >> 4;
						if (uu > 32767) uu = 32767;
						if (uu < -32767) uu = -32767;
						if (vv > 32767) vv = 32767;
						if (vv < -32767) vv = -32767;
						t = (int32_t) isqrt((uint32_t) (uu * uu) + (uint32_t) (vv * vv)) << 4;
					}
					col = ramp_at(pt, t);
					cr = (col >> 16) & 255;
					cg = (col >> 8) & 255;
					cb = col & 255;
					a = DIV255(a * (col >> 24));
				}
				if (alpha < 255)
					a = DIV255(a * alpha);
				if (a == 0)
					goto next;
				if (a == 255) {
					p[0] = (uint8_t) cr;
					p[1] = (uint8_t) cg;
					p[2] = (uint8_t) cb;
					p[3] = 255;
					goto next;
				}
				na = 255 - a;
				p[0] = (uint8_t) DIV255(cr * a + p[0] * na);
				p[1] = (uint8_t) DIV255(cg * a + p[1] * na);
				p[2] = (uint8_t) DIV255(cb * a + p[2] * na);
				p[3] = (uint8_t) DIV255(255 * a + p[3] * na);
			next:
				if (pt->kind != RS_SOLID) {
					u += pt->m[0];
					v += pt->m[3];
				}
			}
		}
	}
}

void rs_fill(rs_canvas *c, int evenodd, const rs_paint *p)
{
	long s;
	c->ne = 0;
	for (s = 0; s < c->nsub; s++) {
		long a = c->sub[s] & ~CLOSED, z = s + 1 < c->nsub ? (c->sub[s + 1] & ~CLOSED) : c->npt, i;
		for (i = a; i + 2 < z; i += 2)
			edge(c, c->pt[i], c->pt[i + 1], c->pt[i + 2], c->pt[i + 3]);
		if (z - a >= 4)
			edge(c, c->pt[z - 2], c->pt[z - 1], c->pt[a], c->pt[a + 1]);	/* closed for the fill */
	}
	if (!c->nomem)
		fill_edges(c, evenodd, p);
}

/* ------------------------------------------------------------ stroking */

/* a polygon's edges, wound as every stroke's piece is (clockwise on the
   screen, its area negative), so that the pieces join, not cancel */
static void piece(rs_canvas *c, const int32_t *q, int n)
{
	int64_t area = 0;
	int i;
	for (i = 0; i < n; i++) {
		int j = (i + 1) % n;
		area += (int64_t) q[2 * i] * q[2 * j + 1] - (int64_t) q[2 * j] * q[2 * i + 1];
	}
	for (i = 0; i < n; i++) {
		int j = (i + 1) % n;
		if (area > 0)
			edge(c, q[2 * j], q[2 * j + 1], q[2 * i], q[2 * i + 1]);
		else
			edge(c, q[2 * i], q[2 * i + 1], q[2 * j], q[2 * j + 1]);
	}
}

/* a disc of radius hw about (x, y), twelve-sided past a few pixels */
static void disc(rs_canvas *c, int32_t x, int32_t y, int32_t hw)
{
	/* cos and sin of 30 degree steps, times 1024 */
	static const int16_t cs[12][2] = {
		{ 1024, 0 }, { 887, 512 }, { 512, 887 }, { 0, 1024 }, { -512, 887 }, { -887, 512 },
		{ -1024, 0 }, { -887, -512 }, { -512, -887 }, { 0, -1024 }, { 512, -887 }, { 887, -512 }
	};
	int32_t q[24];
	int i;
	for (i = 0; i < 12; i++) {
		q[2 * i] = x + (int32_t) ((int64_t) hw * cs[i][0] >> 10);
		q[2 * i + 1] = y + (int32_t) ((int64_t) hw * cs[i][1] >> 10);
	}
	piece(c, q, 12);
}

/* the normal to a segment (dx, dy), hw long, in *nx, *ny; 0 if it has no
   length */
static int normal(int32_t dx, int32_t dy, int32_t hw, int32_t *nx, int32_t *ny)
{
	int32_t sx = dx, sy = dy;
	uint32_t len;
	if (dx == 0 && dy == 0)
		return 0;
	/* its length at a scale that fits 32 bits squared */
	while (iabs(sx) > 32767 || iabs(sy) > 32767) {
		sx >>= 1;
		sy >>= 1;
	}
	len = isqrt((uint32_t) (sx * sx + sy * sy));
	if (len == 0)
		len = 1;
	*nx = (int32_t) (-(int64_t) sy * hw / (int32_t) len);
	*ny = (int32_t) ((int64_t) sx * hw / (int32_t) len);
	return 1;
}

/* the join at (x, y) between a segment whose normal is (ax, ay) and the
   next, (bx, by), going on (dx, dy) */
static void join(rs_canvas *c, const rs_stroke_style *s, int32_t x, int32_t y, int32_t ax, int32_t ay, int32_t bx,
		 int32_t by, int32_t dx, int32_t dy, int32_t hw)
{
	int32_t q[8];
	int64_t turn = (int64_t) ax * dx + (int64_t) ay * dy;
	int sg = turn > 0 ? -1 : 1;	/* the outer side: away from the turn */
	if (s->join == RS_ROUND_JOIN) {
		disc(c, x, y, hw);
		return;
	}
	if (turn == 0 && (int64_t) ax * bx + (int64_t) ay * by > 0)
		return;			/* straight on */
	q[0] = x, q[1] = y;
	q[2] = x + sg * ax, q[3] = y + sg * ay;
	if (s->join == RS_MITER) {
		/* the miter's tip: along the bisector, hw / cos(half the angle
		   between the normals) out; past the limit, a bevel */
		int64_t mx = (int64_t) ax + bx, my = (int64_t) ay + by, m2 = mx * mx + my * my;
		(void) m2;
		uint32_t ml = isqrt64((uint64_t) m2);
		if (ml > 0 && (int64_t) 2 * hw * 65536 <= (int64_t) s->miter * ml) {
			/* the bisector made hw long, times 2 hw / its length (the
			   miter's length over the width, within the limit) */
			int64_t r16 = (int64_t) 2 * hw * 65536 / ml;
			q[4] = x + sg * (int32_t) (mx * hw / ml * r16 / 65536);
			q[5] = y + sg * (int32_t) (my * hw / ml * r16 / 65536);
			q[6] = x + sg * bx, q[7] = y + sg * by;
			piece(c, q, 4);
			return;
		}
	}
	q[4] = x + sg * bx, q[5] = y + sg * by;
	piece(c, q, 3);
}

/* a cap at (x, y), the segment's end, going on (dx, dy) out of it */
static void cap(rs_canvas *c, const rs_stroke_style *s, int32_t x, int32_t y, int32_t nx, int32_t ny, int32_t hw,
		int out)
{
	int32_t q[8], ex, ey;
	if (s->cap == RS_ROUND_CAP) {
		disc(c, x, y, hw);
		return;
	}
	if (s->cap != RS_SQUARE)
		return;
	/* (the normal turned a quarter: along the segment, hw) */
	ex = ny * out, ey = -nx * out;
	q[0] = x + nx, q[1] = y + ny;
	q[2] = x + nx + ex, q[3] = y + ny + ey;
	q[4] = x - nx + ex, q[5] = y - ny + ey;
	q[6] = x - nx, q[7] = y - ny;
	piece(c, q, 4);
}

void rs_stroke(rs_canvas *c, const rs_stroke_style *st, const rs_paint *p)
{
	int32_t hw = st->width / 2;
	if (hw > (1 << 24))
		hw = 1 << 24;		/* (16384 pixels wide: enough) */
	long s;
	if (hw <= 0)
		hw = 1;
	c->ne = 0;
	for (s = 0; s < c->nsub; s++) {
		int closed = (c->sub[s] & CLOSED) != 0;
		long a = c->sub[s] & ~CLOSED, z = s + 1 < c->nsub ? (c->sub[s + 1] & ~CLOSED) : c->npt, i, n;
		int32_t fnx = 0, fny = 0, pnx = 0, pny = 0, fx0 = 0, fy0 = 0;
		int any = 0;
		n = (z - a) / 2;
		if (n == 1) {
			/* a lone point: a round or square cap shows it */
			if (st->cap != RS_BUTT && closed) {
				cap(c, st, c->pt[a], c->pt[a + 1], 0, hw, hw, 1);
				cap(c, st, c->pt[a], c->pt[a + 1], 0, hw, hw, -1);
			}
			continue;
		}
		for (i = 0; i < n - 1 + closed; i++) {
			long j = a + 2 * i, k = a + 2 * ((i + 1) % n);
			int32_t x0 = c->pt[j], y0 = c->pt[j + 1], x1 = c->pt[k], y1 = c->pt[k + 1], nx, ny, q[8];
			if (!normal(x1 - x0, y1 - y0, hw, &nx, &ny))
				continue;	/* (no length: no direction) */
			q[0] = x0 + nx, q[1] = y0 + ny;
			q[2] = x1 + nx, q[3] = y1 + ny;
			q[4] = x1 - nx, q[5] = y1 - ny;
			q[6] = x0 - nx, q[7] = y0 - ny;
			piece(c, q, 4);
			if (any)
				join(c, st, x0, y0, pnx, pny, nx, ny, x1 - x0, y1 - y0, hw);
			else {
				fnx = nx, fny = ny, fx0 = x0, fy0 = y0;
				any = 1;
			}
			pnx = nx, pny = ny;
		}
		if (!any) {
			if (st->cap != RS_BUTT) {
				cap(c, st, c->pt[a], c->pt[a + 1], 0, hw, hw, 1);
				cap(c, st, c->pt[a], c->pt[a + 1], 0, hw, hw, -1);
			}
			continue;
		}
		if (closed) {
			/* the last segment back to the first's start */
			join(c, st, fx0, fy0, pnx, pny, fnx, fny, c->pt[a + 2] - fx0, c->pt[a + 3] - fy0, hw);
		} else {
			cap(c, st, fx0, fy0, fnx, fny, hw, -1);
			cap(c, st, c->pt[z - 2], c->pt[z - 1], pnx, pny, hw, 1);
		}
	}
	if (!c->nomem)
		fill_edges(c, 0, p);
}
