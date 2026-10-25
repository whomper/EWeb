/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * svgread.c - an SVG picture drawn from EWeb's own document nodes (fresh
 * code, in whole numbers: fixmath.h; DESIGN.md, SVG): the shapes (path,
 * rect with its rounded corners, circle, ellipse, line, polyline,
 * polygon), groups, nested <svg>, <use> of an element or a <symbol>, the
 * viewBox and preserveAspectRatio; fill and stroke with colours,
 * currentColor and linear and radial gradients, their opacities, the fill
 * rule, stroke widths, caps, joins and the miter limit; the transform
 * attribute; presentation attributes, the style attribute and the
 * picture's own <style> sheet (simple selectors). Not: text, clipping
 * paths, masks, filters, patterns, markers, dashes, animation.
 */
#include <stdlib.h>
#include <string.h>

#include "svg.h"
#include "raster.h"
#include "fixmath.h"

/* user units to the canvas: X = (a x + c y) >> 24 + e, a raster point
   (24.8); a, b, c, d 16.16 */
typedef struct {
	int32_t a, b, c, d, e, f;
} MAT;

enum { PK_NONE, PK_COLOR, PK_CURRENT, PK_URL };
typedef struct {
	uint8_t kind;
	uint32_t argb;			/* PK_COLOR; a PK_URL's fallback (0: none) */
	const char *id;			/* PK_URL: the id, idl long */
	size_t idl;
} PAINT;

/* what an element inherits */
typedef struct {
	MAT m;
	PAINT fill, stroke;
	fx fill_op, stroke_op, op;	/* 0 .. FX_ONE */
	fx sw;				/* stroke-width, user units */
	uint8_t evenodd, cap, join, hidden;
	fx miter;
	uint32_t color;			/* currentColor: 0xRRGGBB */
	fx vw, vh;			/* the viewport, for percentages */
} ST;

/* a rule of the picture's own sheet */
typedef struct {
	const char *tag, *id, *cls;	/* (cls: classes joined by '.') */
	size_t tagl, idl, clsl;
	int spec;
	const char *decl;
	size_t decll;
} RULE;

typedef struct {
	rs_canvas cv;
	const dnode *top;		/* the document, for ids */
	RULE *rules;
	int nrules, rcap;
	int depth, uses;
	/* the shape's bounds in its user units, for a gradient's
	   objectBoundingBox */
	fx bx0, by0, bx1, by1;
	int bany;
	/* the path's pen in user units */
	fx px, py;
	MAT pm;
} R;

#define MAXDEPTH 48

/* user units as far as they go: 8192 either way, so that a few added are
   still a number (a picture's own units are hardly ever past it) */
#define UMAX ((fx) 1 << 29)

static fx sat(int64_t v)
{
	return v > UMAX ? UMAX : v < -UMAX ? -UMAX : (fx) v;
}

static int is_ws(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int same(const char *a, size_t al, const char *b)
{
	size_t i;
	for (i = 0; i < al && b[i]; i++) {
		unsigned char x = (unsigned char) a[i], y = (unsigned char) b[i];
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
	}
	return i == al && b[i] == 0;
}

static const char *name_of(const dnode *n, size_t *len)
{
	return ec_atom_text(n->e.name, len);
}

static int is(const dnode *n, const char *name)
{
	size_t l;
	const char *s;
	if (n->type != DN_ELEMENT || n->ns != DN_NS_SVG)
		return 0;
	s = name_of(n, &l);
	return same(s, l, name);
}

/* ------------------------------------------------------------ numbers */

static const int32_t pow10[5] = { 1, 10, 100, 1000, 10000 };

/* a number (16.16, saturated) at s, up to end: its end, or NULL */
static const char *number(const char *s, const char *end, fx *out)
{
	int neg = 0, any = 0, k = 0;
	int32_t ip = 0, fp = 0;
	int64_t v;
	if (s < end && (*s == '+' || *s == '-'))
		neg = *s++ == '-';
	for (; s < end && *s >= '0' && *s <= '9'; s++, any = 1)
		if (ip < 1000000)
			ip = ip * 10 + (*s - '0');
	if (s < end && *s == '.') {
		for (s++; s < end && *s >= '0' && *s <= '9'; s++, any = 1)
			if (k < 4) {
				fp = fp * 10 + (*s - '0');
				k++;
			}
	}
	if (!any)
		return NULL;
	v = ((int64_t) ip << 16) + (((int64_t) fp << 16) + pow10[k] / 2) / pow10[k];
	if (s < end && (*s == 'e' || *s == 'E') && s + 1 < end &&
	    ((s[1] >= '0' && s[1] <= '9') || ((s[1] == '-' || s[1] == '+') && s + 2 < end && s[2] >= '0' && s[2] <= '9'))) {
		int en = 0, e = 0;
		s++;
		if (*s == '-' || *s == '+')
			en = *s++ == '-';
		for (; s < end && *s >= '0' && *s <= '9'; s++)
			if (e < 100)
				e = e * 10 + (*s - '0');
		while (e-- > 0 && v != 0) {
			if (en)
				v /= 10;
			else if ((v *= 10) > 0x7fffffffLL)
				break;
		}
	}
	*out = sat(neg ? -v : v);
	return s;
}

static const char *skip_sep(const char *s, const char *end)
{
	while (s < end && (is_ws((unsigned char) *s) || *s == ','))
		s++;
	return s;
}

/* a length: a number and its unit, in user units (percentages of ref) */
static int length(const char *s, size_t n, fx ref, fx *out)
{
	const char *end = s + n, *e;
	fx v;
	while (s < end && is_ws((unsigned char) *s))
		s++;
	if ((e = number(s, end, &v)) == NULL)
		return 0;
	if (e + 2 <= end) {
		if ((e[0] | 32) == 'p' && (e[1] | 32) == 't')
			v = sat((int64_t) v * 4 / 3);
		else if ((e[0] | 32) == 'p' && (e[1] | 32) == 'c')
			v = sat((int64_t) v * 16);
		else if ((e[0] | 32) == 'm' && (e[1] | 32) == 'm')
			v = sat((int64_t) v * 960 / 254);
		else if ((e[0] | 32) == 'c' && (e[1] | 32) == 'm')
			v = sat((int64_t) v * 9600 / 254);
		else if ((e[0] | 32) == 'i' && (e[1] | 32) == 'n')
			v = sat((int64_t) v * 96);
		else if ((e[0] | 32) == 'e' && (e[1] | 32) == 'm')
			v = sat((int64_t) v * 16);
		else if ((e[0] | 32) == 'e' && (e[1] | 32) == 'x')
			v = sat((int64_t) v * 8);
	}
	if (e < end && *e == '%')
		v = sat((int64_t) v * ref / (100 * FX_ONE));
	*out = sat(v);
	return 1;
}

static const char *attr(const dnode *n, const char *name)
{
	return dn_attr(n, name);
}

static fx attr_len(const dnode *n, const char *name, fx ref, fx dflt)
{
	const char *v = attr(n, name);
	fx r;
	if (v != NULL && length(v, strlen(v), ref, &r))
		return r;
	return dflt;
}

/* ------------------------------------------------------------ transforms */

/* a canvas point (24.8) as far as the rasteriser takes one */
static int64_t rclamp(int64_t v)
{
	return v > (1 << 28) ? (1 << 28) : v < -(1 << 28) ? -(1 << 28) : v;
}

/* m then the local matrix (la .. lf, 16.16; le, lf user units) */
static void mat_then(MAT *m, fx la, fx lb, fx lc, fx ld, fx le, fx lf)
{
	MAT r;
	r.a = (int32_t) (((int64_t) m->a * la + (int64_t) m->c * lb) >> 16);
	r.b = (int32_t) (((int64_t) m->b * la + (int64_t) m->d * lb) >> 16);
	r.c = (int32_t) (((int64_t) m->a * lc + (int64_t) m->c * ld) >> 16);
	r.d = (int32_t) (((int64_t) m->b * lc + (int64_t) m->d * ld) >> 16);
	r.e = (int32_t) rclamp((int64_t) m->e + (((int64_t) m->a * le + (int64_t) m->c * lf) >> 24));
	r.f = (int32_t) rclamp((int64_t) m->f + (((int64_t) m->b * le + (int64_t) m->d * lf) >> 24));
	*m = r;
}

/* the transform attribute's list, each after the one before */
static void transform(MAT *m, const char *s, size_t n)
{
	const char *end = s + n;
	while (s < end) {
		const char *name;
		size_t nl;
		fx a[6];
		int k = 0;
		s = skip_sep(s, end);
		name = s;
		while (s < end && ((*s | 32) >= 'a' && (*s | 32) <= 'z'))
			s++;
		nl = (size_t) (s - name);
		while (s < end && is_ws((unsigned char) *s))
			s++;
		if (nl == 0 || s >= end || *s != '(')
			return;
		s++;
		while (k < 6) {
			const char *e;
			s = skip_sep(s, end);
			if ((e = number(s, end, &a[k])) == NULL)
				break;
			s = e;
			k++;
		}
		s = skip_sep(s, end);
		if (s >= end || *s != ')')
			return;
		s++;
		if (same(name, nl, "matrix") && k == 6) {
			mat_then(m, a[0], a[1], a[2], a[3], a[4], a[5]);
		} else if (same(name, nl, "translate") && k >= 1) {
			mat_then(m, FX_ONE, 0, 0, FX_ONE, a[0], k > 1 ? a[1] : 0);
		} else if (same(name, nl, "scale") && k >= 1) {
			mat_then(m, a[0], 0, 0, k > 1 ? a[1] : a[0], 0, 0);
		} else if (same(name, nl, "rotate") && k >= 1) {
			uint32_t ang = fx_deg(a[0]);
			fx c = fx_cos(ang), sn = fx_sin(ang);
			if (k >= 3)
				mat_then(m, FX_ONE, 0, 0, FX_ONE, a[1], a[2]);
			mat_then(m, c, sn, -sn, c, 0, 0);
			if (k >= 3)
				mat_then(m, FX_ONE, 0, 0, FX_ONE, -a[1], -a[2]);
		} else if (same(name, nl, "skewx") && k >= 1) {
			uint32_t ang = fx_deg(a[0]);
			mat_then(m, FX_ONE, 0, fx_div(fx_sin(ang), fx_cos(ang)), FX_ONE, 0, 0);
		} else if (same(name, nl, "skewy") && k >= 1) {
			uint32_t ang = fx_deg(a[0]);
			mat_then(m, FX_ONE, fx_div(fx_sin(ang), fx_cos(ang)), 0, FX_ONE, 0, 0);
		}
	}
}

/* a viewBox's four numbers; 0 if it hasn't them (or a size not above 0) */
static int view_box(const char *s, fx v[4])
{
	const char *end;
	int k;
	if (s == NULL)
		return 0;
	end = s + strlen(s);
	for (k = 0; k < 4; k++) {
		s = skip_sep(s, end);
		if ((s = number(s, end, &v[k])) == NULL)
			return 0;
	}
	return v[2] > 0 && v[3] > 0;
}

/* the viewBox (vb) fitted into a viewport x, y, w, h (user units of m)
   as preserveAspectRatio (par) says */
static void fit(MAT *m, const fx vb[4], const char *par, fx x, fx y, fx w, fx h)
{
	fx sx = fx_div(w, vb[2]), sy = fx_div(h, vb[3]), tx, ty;
	int ax = 1, ay = 1;		/* xMid, yMid */
	if (par != NULL) {
		const char *p = par;
		while (is_ws((unsigned char) *p))
			p++;
		if (!strncmp(p, "defer", 5))
			for (p += 5; is_ws((unsigned char) *p); p++)
				;
		if (!strncmp(p, "none", 4)) {
			ax = -1;
		} else if (strlen(p) >= 8 && p[0] == 'x' && p[4] == 'Y') {
			ax = p[2] == 'i' ? 0 : p[2] == 'a' && p[1] == 'M' && p[3] == 'x' ? 2 : 1;
			ay = p[6] == 'i' ? 0 : p[6] == 'a' && p[5] == 'M' && p[7] == 'x' ? 2 : 1;
			if (!strncmp(p + 1, "Min", 3)) ax = 0;
			else if (!strncmp(p + 1, "Mid", 3)) ax = 1;
			else if (!strncmp(p + 1, "Max", 3)) ax = 2;
			if (!strncmp(p + 5, "Min", 3)) ay = 0;
			else if (!strncmp(p + 5, "Mid", 3)) ay = 1;
			else if (!strncmp(p + 5, "Max", 3)) ay = 2;
		}
		if (ax >= 0 && strstr(p, "slice") != NULL) {
			if (sx < sy) sx = sy; else sy = sx;
		} else if (ax >= 0) {
			if (sx > sy) sx = sy; else sy = sx;
		}
	} else {
		if (sx > sy) sx = sy; else sy = sx;
	}
	tx = sat((int64_t) x - fx_mul(vb[0], sx) +
		 (ax > 0 ? ((int64_t) w - fx_mul(vb[2], sx)) * ax / 2 : 0));
	ty = sat((int64_t) y - fx_mul(vb[1], sy) +
		 (ax >= 0 && ay > 0 ? ((int64_t) h - fx_mul(vb[3], sy)) * ay / 2 : 0));
	mat_then(m, sx, 0, 0, sy, tx, ty);
}

/* ------------------------------------------------------------ the path */

static void bound(R *r, fx x, fx y)
{
	if (!r->bany) {
		r->bx0 = r->bx1 = x;
		r->by0 = r->by1 = y;
		r->bany = 1;
		return;
	}
	if (x < r->bx0) r->bx0 = x;
	if (x > r->bx1) r->bx1 = x;
	if (y < r->by0) r->by0 = y;
	if (y > r->by1) r->by1 = y;
}

static int32_t tx(const MAT *m, fx x, fx y)
{
	return (int32_t) rclamp((((int64_t) m->a * x + (int64_t) m->c * y) >> 24) + m->e);
}

static int32_t ty(const MAT *m, fx x, fx y)
{
	return (int32_t) rclamp((((int64_t) m->b * x + (int64_t) m->d * y) >> 24) + m->f);
}

static void p_move(R *r, fx x, fx y)
{
	bound(r, x, y);
	rs_move(&r->cv, tx(&r->pm, x, y), ty(&r->pm, x, y));
	r->px = x, r->py = y;
}

static void p_line(R *r, fx x, fx y)
{
	bound(r, x, y);
	rs_line(&r->cv, tx(&r->pm, x, y), ty(&r->pm, x, y));
	r->px = x, r->py = y;
}

static void p_cubic(R *r, fx x1, fx y1, fx x2, fx y2, fx x, fx y)
{
	const MAT *m = &r->pm;
	bound(r, x1, y1);
	bound(r, x2, y2);
	bound(r, x, y);
	rs_cubic(&r->cv, tx(m, x1, y1), ty(m, x1, y1), tx(m, x2, y2), ty(m, x2, y2), tx(m, x, y), ty(m, x, y));
	r->px = x, r->py = y;
}

/* (x0 + 2/3 (x1 - x0)) */
static fx third2(fx a, fx b)
{
	return a + (fx) (((int64_t) (b - a) * 2) / 3);
}

static void p_quad(R *r, fx qx, fx qy, fx x, fx y)
{
	fx x0 = r->px, y0 = r->py;
	p_cubic(r, third2(x0, qx), third2(y0, qy), third2(x, qx), third2(y, qy), x, y);
}

/* atan2 of two 64-bit numbers, brought into fx_atan2's range */
static uint32_t atan2_64(int64_t y, int64_t x)
{
	while (y > 0x3fffffff || y < -0x3fffffff || x > 0x3fffffff || x < -0x3fffffff) {
		y >>= 1;
		x >>= 1;
	}
	return fx_atan2((fx) y, (fx) x);
}

/* an elliptical arc from the pen to (x, y) (SVG 1.1, F.6.5: its centre
   from its ends), as cubic curves of a quarter turn at most. The ends'
   places on the unit circle in 2.29 and their sums in 64 bits: an arc
   whose radius is hundreds of times its chord (icon optimisers write
   them) has its centre far away, which 16.16 can't place */
static void p_arc(R *r, fx rx, fx ry, fx rot, int large, int sweep, fx x, fx y)
{
	fx x0 = r->px, y0 = r->py, cp, sp, dx2, dy2, x1p, y1p, coef = 0, cxp, cyp, cx, cy;
	int64_t ux, uy, lam, one = (int64_t) 1 << 58, v1x, v1y, v2x, v2y;
	int32_t dt, seg;
	uint32_t a, t1, t2;
	int nseg, i;
	if (x == x0 && y == y0)
		return;
	if (rx < 0) rx = -rx;
	if (ry < 0) ry = -ry;
	if (rx == 0 || ry == 0) {
		p_line(r, x, y);
		return;
	}
	a = fx_deg(rot);
	cp = fx_cos(a);
	sp = fx_sin(a);
	dx2 = (fx) (((int64_t) x0 - x) / 2);
	dy2 = (fx) (((int64_t) y0 - y) / 2);
	x1p = fx_mul(cp, dx2) + fx_mul(sp, dy2);
	y1p = -fx_mul(sp, dx2) + fx_mul(cp, dy2);
	if ((x1p < 0 ? -x1p : x1p) >= rx || (y1p < 0 ? -y1p : y1p) >= ry) {
		/* the radii too small to reach (or just): made big enough */
		fx ux16 = fx_div(x1p, rx), uy16 = fx_div(y1p, ry);
		fx s = (fx) isqrt64((uint64_t) ((int64_t) ux16 * ux16 + (int64_t) uy16 * uy16));
		if (s > FX_ONE) {
			rx = fx_mul(rx, s);
			ry = fx_mul(ry, s);
		}
	}
	ux = (int64_t) x1p * (1 << 29) / rx;		/* 2.29 */
	uy = (int64_t) y1p * (1 << 29) / ry;
	lam = ux * ux + uy * uy;			/* 58 bits a whole */
	if (lam < (one >> 10) && !large) {
		/* a flat arc (a radius over 32 times the chord's half): one
		   curve from its chord and how far it bows (its sagitta,
		   1 - sqrt(1 - lam) on the unit circle, about lam / 2), the
		   control points a third along the chord and 4/3 of that out:
		   no far centre to place */
		int64_t ul = isqrt64((uint64_t) lam), sag = lam >> 30;	/* 2.29 */
		int64_t bx, by;
		fx px, py, ox, oy;
		if (ul == 0) {
			p_line(r, x, y);
			return;
		}
		/* out from the chord, to the side the sweep goes */
		bx = (sweep ? -uy : uy) * sag / ul;
		by = (sweep ? ux : -ux) * sag / ul;
		px = (fx) ((rx * bx) >> 29);
		py = (fx) ((ry * by) >> 29);
		ox = fx_mul(cp, px) - fx_mul(sp, py);
		oy = fx_mul(sp, px) + fx_mul(cp, py);
		ox = (fx) (((int64_t) ox * 4) / 3);
		oy = (fx) (((int64_t) oy * 4) / 3);
		p_cubic(r, sat(x0 + ((int64_t) x - x0) / 3 + ox), sat(y0 + ((int64_t) y - y0) / 3 + oy),
			sat(x0 + ((int64_t) x - x0) * 2 / 3 + ox), sat(y0 + ((int64_t) y - y0) * 2 / 3 + oy), x, y);
		return;
	}
	if (lam >= one) {
		/* (at the ends of a diameter) */
		uint32_t s29 = isqrt64((uint64_t) lam);
		ux = ux * (1 << 29) / s29;
		uy = uy * (1 << 29) / s29;
	} else {
		int64_t q26 = (one - lam) / (lam >> 26);	/* (1 - lam) / lam, 26 bits a whole */
		coef = (fx) isqrt64((uint64_t) q26 << 6);	/* its root, 16.16 */
		if (large == sweep)
			coef = -coef;
	}
	/* the centre: coef (rx uy, -ry ux), turned and moved to the middle */
	cxp = (fx) (((int64_t) coef * ((rx * uy) >> 29)) >> 16);
	cyp = (fx) -(((int64_t) coef * ((ry * ux) >> 29)) >> 16);
	cx = sat((int64_t) fx_mul(cp, cxp) - fx_mul(sp, cyp) + x0 / 2 + x / 2);
	cy = sat((int64_t) fx_mul(sp, cxp) + fx_mul(cp, cyp) + y0 / 2 + y / 2);
	/* the ends' angles on the unit circle */
	v1x = ux - ((coef * uy) >> 16);
	v1y = uy + ((coef * ux) >> 16);
	v2x = -ux - ((coef * uy) >> 16);
	v2y = -uy + ((coef * ux) >> 16);
	t1 = atan2_64(v1y, v1x);
	t2 = atan2_64(v2y, v2x);
	dt = (int16_t) (uint16_t) (t2 - t1);
	if (!sweep && dt > 0)
		dt -= 65536;
	else if (sweep && dt < 0)
		dt += 65536;
	nseg = (int) ((dt < 0 ? -dt : dt) + 16383) / 16384;
	if (nseg < 1)
		nseg = 1;
	seg = dt / nseg;
	for (i = 0; i < nseg; i++) {
		uint32_t s0 = t1 + (uint32_t) (seg * i), s1 = i + 1 == nseg ? t1 + (uint32_t) dt : s0 + (uint32_t) seg;
		uint32_t q4 = (uint32_t) (seg / 4);
		fx k = fx_div(fx_sin(q4) * 4 / 3, fx_cos(q4));
		fx c0 = fx_cos(s0), n0 = fx_sin(s0), c1 = fx_cos(s1), n1 = fx_sin(s1);
		/* on the unit circle, then onto the ellipse */
		fx ex1 = c0 - fx_mul(k, n0), ey1 = n0 + fx_mul(k, c0);
		fx ex2 = c1 + fx_mul(k, n1), ey2 = n1 - fx_mul(k, c1);
		fx e1x = fx_mul(rx, ex1), e1y = fx_mul(ry, ey1), e2x = fx_mul(rx, ex2), e2y = fx_mul(ry, ey2);
		fx e3x = fx_mul(rx, c1), e3y = fx_mul(ry, n1);
		fx X3 = sat((int64_t) fx_mul(cp, e3x) - fx_mul(sp, e3y) + cx);
		fx Y3 = sat((int64_t) fx_mul(sp, e3x) + fx_mul(cp, e3y) + cy);
		if (i + 1 == nseg)
			X3 = x, Y3 = y;
		p_cubic(r, sat((int64_t) fx_mul(cp, e1x) - fx_mul(sp, e1y) + cx),
			sat((int64_t) fx_mul(sp, e1x) + fx_mul(cp, e1y) + cy),
			sat((int64_t) fx_mul(cp, e2x) - fx_mul(sp, e2y) + cx),
			sat((int64_t) fx_mul(sp, e2x) + fx_mul(cp, e2y) + cy), X3, Y3);
	}
}

/* a path's d (SVG 1.1, 8.3; SVG 2's arcs' flags without separators):
   drawn up to the first error, as the standard says */
static void path_data(R *r, const char *s, size_t n)
{
	const char *end = s + n;
	fx sx = 0, sy = 0, qx = 0, qy = 0;
	char cmd = 0, last = 0;
	r->px = r->py = 0;
	for (;;) {
		fx v[7];
		int k, need, rel;
		char c;
		s = skip_sep(s, end);
		if (s >= end)
			break;
		if (((*s | 32) >= 'a' && (*s | 32) <= 'z')) {
			cmd = *s++;
			if ((cmd | 32) == 'z') {
				rs_close(&r->cv);
				r->px = sx, r->py = sy;
				last = 'z';
				continue;
			}
		} else if (cmd == 0 || (cmd | 32) == 'z') {
			break;
		}
		c = (char) (cmd | 32);
		rel = cmd >= 'a';
		switch (c) {
		case 'm': case 'l': case 't': need = 2; break;
		case 'h': case 'v': need = 1; break;
		case 's': case 'q': need = 4; break;
		case 'c': need = 6; break;
		case 'a': need = 7; break;
		default: return;
		}
		for (k = 0; k < need; k++) {
			const char *e;
			s = skip_sep(s, end);
			if (c == 'a' && (k == 3 || k == 4)) {
				/* a flag: one digit, nothing between it and the next */
				if (s >= end || (*s != '0' && *s != '1'))
					return;
				v[k] = *s++ - '0';
				continue;
			}
			if ((e = number(s, end, &v[k])) == NULL)
				return;
			s = e;
		}
		if (rel) {
			for (k = 0; k < need; k++) {
				if (c == 'a') {
					if (k >= 5)
						v[k] = sat((int64_t) v[k] + (k == 5 ? r->px : r->py));
					continue;
				}
				if (c == 'h')
					v[k] = sat((int64_t) v[k] + r->px);
				else if (c == 'v')
					v[k] = sat((int64_t) v[k] + r->py);
				else
					v[k] = sat((int64_t) v[k] + ((k & 1) ? r->py : r->px));
			}
		}
		switch (c) {
		case 'm':
			p_move(r, v[0], v[1]);
			sx = v[0], sy = v[1];
			cmd = rel ? 'l' : 'L';	/* (pairs after a moveto are lines) */
			break;
		case 'l':
			p_line(r, v[0], v[1]);
			break;
		case 'h':
			p_line(r, v[0], r->py);
			break;
		case 'v':
			p_line(r, r->px, v[0]);
			break;
		case 'c':
			p_cubic(r, v[0], v[1], v[2], v[3], v[4], v[5]);
			qx = v[2], qy = v[3];
			break;
		case 's': {
			fx x1 = r->px, y1 = r->py;
			if (last == 'c' || last == 's')
				x1 = sat(2 * (int64_t) r->px - qx), y1 = sat(2 * (int64_t) r->py - qy);
			p_cubic(r, x1, y1, v[0], v[1], v[2], v[3]);
			qx = v[0], qy = v[1];
			break;
		}
		case 'q':
			p_quad(r, v[0], v[1], v[2], v[3]);
			qx = v[0], qy = v[1];
			break;
		case 't': {
			fx x1 = r->px, y1 = r->py;
			if (last == 'q' || last == 't')
				x1 = sat(2 * (int64_t) r->px - qx), y1 = sat(2 * (int64_t) r->py - qy);
			p_quad(r, x1, y1, v[0], v[1]);
			qx = x1, qy = y1;
			break;
		}
		case 'a':
			p_arc(r, v[0], v[1], v[2], (int) v[3], (int) v[4], v[5], v[6]);
			break;
		}
		last = c == 'm' ? 'l' : c;
	}
}

/* KAPPA: a quarter circle's control points, 0.5523 of its radius */
#define KAPPA 36195

static void ellipse_path(R *r, fx cx, fx cy, fx rx, fx ry)
{
	fx kx = fx_mul(rx, KAPPA), ky = fx_mul(ry, KAPPA);
	p_move(r, cx + rx, cy);
	p_cubic(r, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
	p_cubic(r, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
	p_cubic(r, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
	p_cubic(r, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
	rs_close(&r->cv);
}

/* ------------------------------------------------------------ properties */

static void paint_of(PAINT *p, const char *s, size_t n)
{
	uint32_t argb;
	int cur;
	while (n > 0 && is_ws((unsigned char) *s))
		s++, n--;
	while (n > 0 && is_ws((unsigned char) s[n - 1]))
		n--;
	if (n >= 4 && !strncmp(s, "url(", 4)) {
		const char *e = memchr(s, ')', n), *h;
		if (e == NULL)
			return;
		h = memchr(s, '#', (size_t) (e - s));
		p->kind = PK_URL;
		p->argb = 0;
		p->id = h != NULL ? h + 1 : s + 4;
		p->idl = (size_t) (e - p->id);
		while (p->idl > 0 && (p->id[p->idl - 1] == '"' || p->id[p->idl - 1] == '\'' || is_ws((unsigned char) p->id[p->idl - 1])))
			p->idl--;
		/* a fallback colour after it */
		{
			const char *t = e + 1, *te = s + n;
			while (t < te && is_ws((unsigned char) *t))
				t++;
			if (t < te && !(te - t == 4 && !strncmp(t, "none", 4)) && ec_color_text(t, (size_t) (te - t), &argb, &cur) &&
			    !cur)
				p->argb = argb;
		}
		return;
	}
	if (n == 4 && !strncmp(s, "none", 4)) {
		p->kind = PK_NONE;
		return;
	}
	if (ec_color_text(s, n, &argb, &cur)) {
		if (cur) {
			p->kind = PK_CURRENT;
		} else {
			p->kind = PK_COLOR;
			p->argb = argb;
		}
	}
}

static fx opacity_of(const char *s, size_t n)
{
	fx v;
	const char *e;
	while (n > 0 && is_ws((unsigned char) *s))
		s++, n--;
	if ((e = number(s, s + n, &v)) == NULL)
		return -1;
	if (e < s + n && *e == '%')
		v /= 100;
	return v < 0 ? 0 : v > FX_ONE ? FX_ONE : v;
}

static int word(const char *s, size_t n, const char *w)
{
	while (n > 0 && is_ws((unsigned char) *s))
		s++, n--;
	while (n > 0 && is_ws((unsigned char) s[n - 1]))
		n--;
	return same(s, n, w);
}

/* one property (name: value) of an element; *none: display none */
static void prop(ST *st, const char *nm, size_t nl, const char *v, size_t vl, int *none)
{
	fx f;
	if (same(nm, nl, "fill")) {
		paint_of(&st->fill, v, vl);
	} else if (same(nm, nl, "stroke")) {
		paint_of(&st->stroke, v, vl);
	} else if (same(nm, nl, "stroke-width")) {
		if (length(v, vl, st->vw, &f) && f >= 0)
			st->sw = f;
	} else if (same(nm, nl, "fill-rule")) {
		st->evenodd = word(v, vl, "evenodd");
	} else if (same(nm, nl, "fill-opacity")) {
		if ((f = opacity_of(v, vl)) >= 0)
			st->fill_op = f;
	} else if (same(nm, nl, "stroke-opacity")) {
		if ((f = opacity_of(v, vl)) >= 0)
			st->stroke_op = f;
	} else if (same(nm, nl, "opacity")) {
		if ((f = opacity_of(v, vl)) >= 0)
			st->op = fx_mul(st->op, f);
	} else if (same(nm, nl, "stroke-linecap")) {
		st->cap = word(v, vl, "round") ? RS_ROUND_CAP : word(v, vl, "square") ? RS_SQUARE : RS_BUTT;
	} else if (same(nm, nl, "stroke-linejoin")) {
		st->join = word(v, vl, "round") ? RS_ROUND_JOIN : word(v, vl, "bevel") ? RS_BEVEL : RS_MITER;
	} else if (same(nm, nl, "stroke-miterlimit")) {
		const char *e = number(v, v + vl, &f);
		if (e != NULL && f >= FX_ONE)
			st->miter = f > 100 * FX_ONE ? 100 * FX_ONE : f;
	} else if (same(nm, nl, "color")) {
		uint32_t argb;
		int cur;
		if (ec_color_text(v, vl, &argb, &cur) && !cur)
			st->color = argb & 0xffffff;
	} else if (same(nm, nl, "display")) {
		if (word(v, vl, "none"))
			*none = 1;
	} else if (same(nm, nl, "visibility")) {
		st->hidden = word(v, vl, "hidden") || word(v, vl, "collapse");
	}
}

/* declarations (name: value; ...) */
static void decls(ST *st, const char *s, size_t n, int *none)
{
	const char *end = s + n;
	while (s < end) {
		const char *nm, *v, *e;
		size_t nl, vl;
		while (s < end && (is_ws((unsigned char) *s) || *s == ';'))
			s++;
		nm = s;
		while (s < end && *s != ':' && *s != ';')
			s++;
		if (s >= end || *s != ':') {
			while (s < end && *s != ';')
				s++;
			continue;
		}
		nl = (size_t) (s - nm);
		while (nl > 0 && is_ws((unsigned char) nm[nl - 1]))
			nl--;
		v = ++s;
		while (s < end && *s != ';')
			s++;
		e = s;
		vl = (size_t) (e - v);
		/* (!important: as any other) */
		{
			const char *b = memchr(v, '!', vl);
			if (b != NULL)
				vl = (size_t) (b - v);
		}
		prop(st, nm, nl, v, vl, none);
	}
}

static const char *const pres[] = {
	"fill", "stroke", "stroke-width", "fill-rule", "fill-opacity", "stroke-opacity", "opacity",
	"stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "color", "display", "visibility"
};

/* does the class attribute have cls (len l)? */
static int has_class(const char *list, const char *cls, size_t l)
{
	const char *s = list;
	while (*s) {
		const char *b;
		while (*s && is_ws((unsigned char) *s))
			s++;
		b = s;
		while (*s && !is_ws((unsigned char) *s))
			s++;
		if ((size_t) (s - b) == l && !memcmp(b, cls, l))
			return 1;
	}
	return 0;
}

static int rule_matches(const RULE *u, const dnode *n)
{
	if (u->tagl) {
		size_t l;
		const char *s = name_of(n, &l);
		if (!(u->tagl == 1 && u->tag[0] == '*') && (l != u->tagl || strncmp(s, u->tag, l)))
			return 0;
	}
	if (u->idl) {
		const char *id = attr(n, "id");
		if (id == NULL || strlen(id) != u->idl || strncmp(id, u->id, u->idl))
			return 0;
	}
	if (u->clsl) {
		const char *list = attr(n, "class"), *c = u->cls, *ce = u->cls + u->clsl;
		if (list == NULL)
			return 0;
		while (c < ce) {
			const char *d = memchr(c, '.', (size_t) (ce - c));
			size_t l = d != NULL ? (size_t) (d - c) : (size_t) (ce - c);
			if (!has_class(list, c, l))
				return 0;
			c += l + 1;
		}
	}
	return 1;
}

/* an element's style: inherited, then its attributes, the sheet's rules
   (by specificity), its style attribute. 0: display none */
static int style(R *r, const dnode *n, ST *st)
{
	int none = 0, i, spec;
	const char *v;
	for (i = 0; i < (int) (sizeof pres / sizeof pres[0]); i++)
		if ((v = attr(n, pres[i])) != NULL)
			prop(st, pres[i], strlen(pres[i]), v, strlen(v), &none);
	if (r->nrules > 0) {
		/* (specificities are below 1000: each in turn, the lowest first) */
		int lo = 0;
		for (;;) {
			int next = 100000;
			for (i = 0; i < r->nrules; i++) {
				spec = r->rules[i].spec;
				if (spec == lo && rule_matches(&r->rules[i], n))
					decls(st, r->rules[i].decl, r->rules[i].decll, &none);
				if (spec > lo && spec < next)
					next = spec;
			}
			if (next == 100000)
				break;
			lo = next;
		}
	}
	if ((v = attr(n, "style")) != NULL)
		decls(st, v, strlen(v), &none);
	return !none;
}

/* the picture's own <style> sheets: simple selectors, the last compound
   of each (tag, #id, .classes) */
static void sheet(R *r, const char *s, size_t n)
{
	const char *end = s + n;
	while (s < end) {
		const char *sel, *selend, *body, *bend;
		while (s < end && is_ws((unsigned char) *s))
			s++;
		if (s + 4 <= end && !strncmp(s, "/*", 2)) {
			const char *e = strstr(s + 2, "*/");
			s = e != NULL && e < end ? e + 2 : end;
			continue;
		}
		sel = s;
		while (s < end && *s != '{')
			s++;
		if (s >= end)
			return;
		selend = s++;
		body = s;
		{
			int depth = 1;
			while (s < end && depth > 0) {
				if (*s == '{')
					depth++;
				else if (*s == '}')
					depth--;
				s++;
			}
			bend = depth == 0 ? s - 1 : s;	/* (a sheet that ends in the block: to its end) */
		}
		if (*sel == '@')
			continue;	/* (an at-rule: its block skipped) */
		/* each selector of the list */
		while (sel < selend) {
			const char *a = sel, *b, *c;
			RULE u;
			while (sel < selend && *sel != ',')
				sel++;
			b = sel;
			if (sel < selend)
				sel++;
			while (b > a && is_ws((unsigned char) b[-1]))
				b--;
			/* its last compound */
			for (c = b; c > a && !is_ws((unsigned char) c[-1]) && c[-1] != '>' && c[-1] != '+' &&
			     c[-1] != '~'; c--)
				;
			if (c == b)
				continue;
			memset(&u, 0, sizeof u);
			u.tag = c;
			while (c < b && *c != '.' && *c != '#' && *c != ':' && *c != '[')
				c++;
			u.tagl = (size_t) (c - u.tag);
			if (u.tagl)
				u.spec += 1;
			while (c < b) {
				const char *k = c + 1;
				if (*c == ':' || *c == '[')
					break;	/* a pseudo-class or an attribute: not this */
				while (k < b && *k != '.' && *k != '#' && *k != ':' && *k != '[')
					k++;
				if (*c == '#') {
					u.id = c + 1;
					u.idl = (size_t) (k - c - 1);
					u.spec += 100;
				} else {
					if (u.cls == NULL)
						u.cls = c + 1;
					u.clsl = (size_t) (k - u.cls);
					u.spec += 10;
				}
				c = k;
			}
			if (c < b)
				continue;
			if (u.cls != NULL && (u.id != NULL && u.id > u.cls))
				continue;	/* (#id between classes: too clever) */
			u.decl = body;
			u.decll = (size_t) (bend - body);
			if (r->nrules == r->rcap) {
				RULE *t = realloc(r->rules, (size_t) (r->rcap = r->rcap ? r->rcap * 2 : 16) * sizeof *t);
				if (t == NULL)
					return;
				r->rules = t;
			}
			r->rules[r->nrules++] = u;
		}
	}
}

/* the next node in tree order under top, or NULL */
static const dnode *next_in(const dnode *top, const dnode *n)
{
	if (n->first != NULL)
		return n->first;
	while (n != top && n->next == NULL)
		n = n->parent;
	return n == top ? NULL : n->next;
}

static const dnode *by_id(R *r, const char *id, size_t l)
{
	const dnode *k;
	for (k = r->top; k != NULL; k = next_in(r->top, k)) {
		const char *v;
		if (k->type == DN_ELEMENT && (v = attr(k, "id")) != NULL && strlen(v) == l && !strncmp(v, id, l))
			return k;
	}
	return NULL;
}

/* an element's href (or xlink:href): the element named, or NULL */
static const dnode *href(R *r, const dnode *n)
{
	const char *h = attr(n, "href");
	uint16_t i;
	if (h == NULL)
		for (i = 0; i < n->nattr && h == NULL; i++) {
			size_t al;
			const char *an = ec_atom_text(n->attrs[i].name, &al);
			if (al == 10 && !strncmp(an, "xlink:href", 10))
				h = n->attrs[i].val;
		}
	if (h == NULL || *h != '#')
		return NULL;
	return by_id(r, h + 1, strlen(h + 1));
}

/* ------------------------------------------------------------ painting */

static uint32_t mix(uint32_t a, uint32_t b, unsigned t)
{
	uint32_t r = 0;
	int k;
	for (k = 0; k < 32; k += 8) {
		unsigned x = (a >> k) & 255, y = (b >> k) & 255;
		r |= (uint32_t) ((x * (256 - t) + y * t) >> 8) << k;
	}
	return r;
}

/* a gradient's stops into its ramp; 0 if it has none */
static int stops(R *r, const dnode *g, const ST *st, uint32_t ramp[256])
{
	fx off[32];
	uint32_t col[32];
	int n = 0, i, k;
	const dnode *c;
	for (c = g->first; c != NULL && n < 32; c = c->next) {
		ST s;
		int none = 0;
		const char *v;
		fx o = 0;
		uint32_t argb = 0xff000000u;
		fx sop = FX_ONE;
		if (!is(c, "stop"))
			continue;
		s = *st;
		if ((v = attr(c, "offset")) != NULL) {
			const char *e = number(v, v + strlen(v), &o);
			if (e != NULL && *e == '%')
				o /= 100;
		}
		if (o < 0) o = 0;
		if (o > FX_ONE) o = FX_ONE;
		if (n > 0 && o < off[n - 1])
			o = off[n - 1];
		/* its colour and opacity: attributes, rules, style */
		{
			const char *sc = attr(c, "stop-color"), *so = attr(c, "stop-opacity"), *sty = attr(c, "style");
			int cur;
			uint32_t a2;
			if (sc != NULL && ec_color_text(sc, strlen(sc), &a2, &cur))
				argb = cur ? 0xff000000u | st->color : a2;
			if (so != NULL)
				sop = opacity_of(so, strlen(so));
			for (i = 0; i < r->nrules; i++)
				if (rule_matches(&r->rules[i], c)) {
					const char *p = r->rules[i].decl, *pe = p + r->rules[i].decll, *q;
					if ((q = strstr(p, "stop-color")) != NULL && q < pe && (q = memchr(q, ':', (size_t) (pe - q))) != NULL) {
						const char *qe = memchr(q, ';', (size_t) (pe - q));
						if (qe == NULL)
							qe = pe;
						if (ec_color_text(q + 1, (size_t) (qe - q - 1), &a2, &cur))
							argb = cur ? 0xff000000u | st->color : a2;
					}
				}
			if (sty != NULL) {
				const char *q = strstr(sty, "stop-color"), *p2;
				if (q != NULL && (q = strchr(q, ':')) != NULL) {
					const char *qe = strchr(q, ';');
					if (qe == NULL)
						qe = q + strlen(q);
					if (ec_color_text(q + 1, (size_t) (qe - q - 1), &a2, &cur))
						argb = cur ? 0xff000000u | st->color : a2;
				}
				if ((p2 = strstr(sty, "stop-opacity")) != NULL && (p2 = strchr(p2, ':')) != NULL) {
					const char *qe = strchr(p2, ';');
					fx f = opacity_of(p2 + 1, qe != NULL ? (size_t) (qe - p2 - 1) : strlen(p2 + 1));
					if (f >= 0)
						sop = f;
				}
			}
		}
		(void) none;
		(void) s;
		argb = (argb & 0xffffff) | (uint32_t) ((((argb >> 24) * (uint32_t) sop) >> 16) << 24);
		off[n] = o;
		col[n] = argb;
		n++;
	}
	if (n == 0)
		return 0;
	for (i = 0, k = 0; i < 256; i++) {
		fx t = (fx) i * 257;		/* (255: 1.0) */
		while (k < n - 1 && off[k + 1] <= t)
			k++;
		if (t <= off[0])
			ramp[i] = col[0];
		else if (k >= n - 1)
			ramp[i] = col[n - 1];
		else {
			fx span = off[k + 1] - off[k];
			unsigned f = span > 0 ? (unsigned) (((int64_t) (t - off[k]) << 8) / span) : 256;
			ramp[i] = mix(col[k], col[k + 1], f > 256 ? 256 : f);
		}
	}
	return 1;
}

/* num * 65536 / den with both brought into range first */
static int32_t ratio16(int64_t num, int64_t den)
{
	int64_t q;
	if (den == 0)
		return 0;
	while (num > ((int64_t) 1 << 46) || num < -((int64_t) 1 << 46)) {
		num >>= 1;
		den >>= 1;
		if (den == 0)
			return num > 0 ? 0x7fffffff : -0x7fffffff;
	}
	q = num * 65536 / den;
	return q > 0x7fffffff ? 0x7fffffff : q < -0x7fffffff ? -0x7fffffff : (int32_t) q;
}

/* a gradient paint: 0 if it can't be (no stops, a bounding box of no size) */
static int gradient(R *r, const dnode *g, const ST *st, rs_paint *p)
{
	const dnode *chain[8], *k;
	int nc = 0, i, linear = is(g, "lineargradient"), obb = 1;
	MAT G, T;
	fx ga, gb, gc, gd, ge, gf;
	int64_t det;
	int32_t ia, ib, ic, id, ie, iff;
	fx x1, y1, x2, y2, cx, cy, rr;
	const char *a_x1 = NULL, *a_y1 = NULL, *a_x2 = NULL, *a_y2 = NULL, *a_cx = NULL, *a_cy = NULL, *a_r = NULL;
	const char *a_units = NULL, *a_tf = NULL, *a_spread = NULL;
	const dnode *stopper = NULL;
	for (k = g; k != NULL && nc < 8 && (is(k, "lineargradient") || is(k, "radialgradient")); k = href(r, k))
		chain[nc++] = k;
	for (i = 0; i < nc; i++) {
		const dnode *c = chain[i];
		const dnode *s;
#define TAKE(var, name) if (var == NULL) var = attr(c, name)
		TAKE(a_x1, "x1"); TAKE(a_y1, "y1"); TAKE(a_x2, "x2"); TAKE(a_y2, "y2");
		TAKE(a_cx, "cx"); TAKE(a_cy, "cy"); TAKE(a_r, "r");
		TAKE(a_units, "gradientunits"); TAKE(a_tf, "gradienttransform"); TAKE(a_spread, "spreadmethod");
#undef TAKE
		if (stopper == NULL)
			for (s = c->first; s != NULL; s = s->next)
				if (is(s, "stop")) {
					stopper = c;
					break;
				}
	}
	if (stopper == NULL || !stops(r, stopper, st, p->ramp))
		return 0;
	if (a_units != NULL && !strcmp(a_units, "userSpaceOnUse"))
		obb = 0;
	p->spread = a_spread == NULL ? RS_PAD : !strcmp(a_spread, "reflect") ? RS_REFLECT :
		    !strcmp(a_spread, "repeat") ? RS_REPEAT : RS_PAD;
	/* user units to pixels (16.16), then the bounding box, then the
	   gradient's own transform */
	G = st->m;
	if (obb) {
		if (!r->bany || r->bx1 == r->bx0 || r->by1 == r->by0)
			return 0;
		mat_then(&G, r->bx1 - r->bx0, 0, 0, r->by1 - r->by0, r->bx0, r->by0);
	}
	if (a_tf != NULL) {
		T = G;
		transform(&T, a_tf, strlen(a_tf));
		G = T;
	}
	ga = G.a, gb = G.b, gc = G.c, gd = G.d, ge = G.e * 256, gf = G.f * 256;
	det = (int64_t) ga * gd - (int64_t) gb * gc;	/* 32.32 */
	if (det == 0)
		return 0;
	/* its inverse: pixels (16.16) to the gradient's units (16.16) */
	ia = ratio16((int64_t) gd * 65536, det);
	ib = ratio16(-(int64_t) gb * 65536, det);
	ic = ratio16(-(int64_t) gc * 65536, det);
	id = ratio16((int64_t) ga * 65536, det);
	ie = -(int32_t) (((int64_t) ia * ge + (int64_t) ic * gf) >> 16);
	iff = -(int32_t) (((int64_t) ib * ge + (int64_t) id * gf) >> 16);
	{
		fx ref_w = obb ? FX_ONE : st->vw, ref_h = obb ? FX_ONE : st->vh;
		fx ref_r = obb ? FX_ONE : (fx) (isqrt64(((uint64_t) ((int64_t) st->vw * st->vw) +
							  (uint64_t) ((int64_t) st->vh * st->vh)) / 2));
#define LEN(s, ref, d) ((s) != NULL && length((s), strlen(s), (ref), &tmp) ? tmp : (d))
		fx tmp;
		if (linear) {
			int64_t dx, dy, l2;
			x1 = LEN(a_x1, ref_w, 0);
			y1 = LEN(a_y1, ref_h, 0);
			x2 = LEN(a_x2, ref_w, obb ? FX_ONE : st->vw);
			y2 = LEN(a_y2, ref_h, 0);
			dx = x2 - x1;
			dy = y2 - y1;
			l2 = dx * dx + dy * dy;		/* 32.32 */
			if (l2 == 0)
				return 0;
			p->kind = RS_LINEAR;
			p->m[0] = ratio16((int64_t) ia * dx + (int64_t) ib * dy, l2);
			p->m[1] = ratio16((int64_t) ic * dx + (int64_t) id * dy, l2);
			p->m[2] = ratio16((int64_t) (ie - x1) * dx + (int64_t) (iff - y1) * dy, l2);
		} else {
			cx = LEN(a_cx, ref_w, obb ? FX_ONE / 2 : st->vw / 2);
			cy = LEN(a_cy, ref_h, obb ? FX_ONE / 2 : st->vh / 2);
			rr = LEN(a_r, ref_r, obb ? FX_ONE / 2 : ref_r / 2);
			if (rr <= 0)
				return 0;
			p->kind = RS_RADIAL;
			p->m[0] = ratio16(ia, rr);
			p->m[1] = ratio16(ic, rr);
			p->m[2] = ratio16(ie - cx, rr);
			p->m[3] = ratio16(ib, rr);
			p->m[4] = ratio16(id, rr);
			p->m[5] = ratio16(iff - cy, rr);
		}
#undef LEN
	}
	return 1;
}

/* a paint and its opacity into p; 0: nothing painted */
static int paint(R *r, const PAINT *pa, fx op, const ST *st, rs_paint *p)
{
	unsigned a = (unsigned) ((fx_mul(op, st->op) * 255 + 32768) >> 16);
	p->kind = RS_SOLID;
	if (pa->kind == PK_NONE || a == 0)
		return 0;
	if (pa->kind == PK_URL) {
		const dnode *g = by_id(r, pa->id, pa->idl);
		if (g != NULL && (is(g, "lineargradient") || is(g, "radialgradient")) && gradient(r, g, st, p)) {
			p->alpha = a;
			return 1;
		}
		if (pa->argb == 0)
			return 0;	/* (a pattern, or nothing: and no fallback) */
		p->rgb = pa->argb & 0xffffff;
		a = a * (pa->argb >> 24) / 255;
	} else if (pa->kind == PK_CURRENT) {
		p->rgb = st->color;
	} else {
		p->rgb = pa->argb & 0xffffff;
		a = a * (pa->argb >> 24) / 255;
	}
	p->alpha = a;
	return a > 0;
}

/* the path made, filled and stroked */
static void shape_done(R *r, const ST *st)
{
	static rs_paint p;		/* (a ramp of 1 KB: not on the stack) */
	if (st->hidden) {
		rs_clear(&r->cv);
		return;
	}
	if (paint(r, &st->fill, st->fill_op, st, &p))
		rs_fill(&r->cv, st->evenodd, &p);
	if (st->sw > 0 && paint(r, &st->stroke, st->stroke_op, st, &p)) {
		rs_stroke_style ss;
		/* the width as the transform scales it on average */
		int64_t det = (int64_t) st->m.a * st->m.d - (int64_t) st->m.b * st->m.c;
		fx scale = (fx) isqrt64((uint64_t) (det < 0 ? -det : det));
		int64_t wd = ((int64_t) st->sw * scale) >> 24;
		ss.width = wd < 1 ? 1 : wd > (1 << 25) ? (1 << 25) : (int32_t) wd;
		ss.cap = st->cap;
		ss.join = st->join;
		ss.miter = st->miter;
		rs_stroke(&r->cv, &ss, &p);
	}
	rs_clear(&r->cv);
}

/* ------------------------------------------------------------ elements */

static void children(R *r, const dnode *n, const ST *st);
static void element(R *r, const dnode *n, const ST *parent);

/* a viewport (nested <svg>, a <symbol> used): x, y, w, h in the parent's
   units and its viewBox */
static void viewport(R *r, const dnode *n, ST *st, fx x, fx y, fx w, fx h)
{
	fx vb[4];
	if (view_box(attr(n, "viewbox"), vb)) {
		fit(&st->m, vb, attr(n, "preserveaspectratio"), x, y, w, h);
		st->vw = vb[2];
		st->vh = vb[3];
	} else {
		mat_then(&st->m, FX_ONE, 0, 0, FX_ONE, x, y);
		st->vw = w;
		st->vh = h;
	}
}

static void element(R *r, const dnode *n, const ST *parent)
{
	ST st;
	size_t l;
	const char *nm, *v;
	if (n->type != DN_ELEMENT || n->ns != DN_NS_SVG || r->depth >= MAXDEPTH || r->cv.nomem)
		return;
	nm = name_of(n, &l);
	/* what is never drawn where it is */
	if (same(nm, l, "defs") || same(nm, l, "symbol") || same(nm, l, "lineargradient") ||
	    same(nm, l, "radialgradient") || same(nm, l, "clippath") || same(nm, l, "mask") ||
	    same(nm, l, "pattern") || same(nm, l, "marker") || same(nm, l, "style") || same(nm, l, "title") ||
	    same(nm, l, "desc") || same(nm, l, "metadata") || same(nm, l, "script") ||
	    same(nm, l, "foreignobject") || same(nm, l, "filter") || same(nm, l, "text"))
		return;
	st = *parent;
	st.op = parent->op;
	if (!same(nm, l, "svg") && (v = attr(n, "transform")) != NULL)
		transform(&st.m, v, strlen(v));
	if (!style(r, n, &st))
		return;
	r->depth++;
	r->pm = st.m;
	r->bany = 0;
	if (same(nm, l, "g") || same(nm, l, "a") || same(nm, l, "switch")) {
		children(r, n, &st);
	} else if (same(nm, l, "svg")) {
		fx x = attr_len(n, "x", parent->vw, 0), y = attr_len(n, "y", parent->vh, 0);
		fx w = attr_len(n, "width", parent->vw, parent->vw), h = attr_len(n, "height", parent->vh, parent->vh);
		if (w > 0 && h > 0) {
			viewport(r, n, &st, x, y, w, h);
			children(r, n, &st);
		}
	} else if (same(nm, l, "use")) {
		const dnode *t = href(r, n);
		fx x = attr_len(n, "x", st.vw, 0), y = attr_len(n, "y", st.vh, 0);
		if (t != NULL && r->uses < 8) {
			const dnode *k;
			for (k = n; k != NULL && k != t; k = k->parent)
				;
			if (k == NULL) {	/* (not one of its own ancestors) */
				r->uses++;
				mat_then(&st.m, FX_ONE, 0, 0, FX_ONE, x, y);
				if (is(t, "symbol") || is(t, "svg")) {
					ST s2 = st;
					fx w = attr_len(n, "width", st.vw, attr_len(t, "width", st.vw, st.vw));
					fx h = attr_len(n, "height", st.vh, attr_len(t, "height", st.vh, st.vh));
					if (style(r, t, &s2) && w > 0 && h > 0) {
						viewport(r, t, &s2, 0, 0, w, h);
						children(r, t, &s2);
					}
				} else {
					element(r, t, &st);
				}
				r->uses--;
			}
		}
	} else if (same(nm, l, "path")) {
		if ((v = attr(n, "d")) != NULL) {
			path_data(r, v, strlen(v));
			shape_done(r, &st);
		}
	} else if (same(nm, l, "rect")) {
		fx x = attr_len(n, "x", st.vw, 0), y = attr_len(n, "y", st.vh, 0);
		fx w = attr_len(n, "width", st.vw, 0), h = attr_len(n, "height", st.vh, 0);
		fx rx = attr_len(n, "rx", st.vw, -1), ry = attr_len(n, "ry", st.vh, -1);
		if (w > 0 && h > 0) {
			if (rx < 0 && ry < 0)
				rx = ry = 0;
			else if (rx < 0)
				rx = ry;
			else if (ry < 0)
				ry = rx;
			if (rx > w / 2) rx = w / 2;
			if (ry > h / 2) ry = h / 2;
			if (rx > 0 && ry > 0) {
				fx kx = fx_mul(rx, FX_ONE - KAPPA), ky = fx_mul(ry, FX_ONE - KAPPA);
				p_move(r, x + rx, y);
				p_line(r, x + w - rx, y);
				p_cubic(r, x + w - kx, y, x + w, y + ky, x + w, y + ry);
				p_line(r, x + w, y + h - ry);
				p_cubic(r, x + w, y + h - ky, x + w - kx, y + h, x + w - rx, y + h);
				p_line(r, x + rx, y + h);
				p_cubic(r, x + kx, y + h, x, y + h - ky, x, y + h - ry);
				p_line(r, x, y + ry);
				p_cubic(r, x, y + ky, x + kx, y, x + rx, y);
			} else {
				p_move(r, x, y);
				p_line(r, x + w, y);
				p_line(r, x + w, y + h);
				p_line(r, x, y + h);
			}
			rs_close(&r->cv);
			shape_done(r, &st);
		}
	} else if (same(nm, l, "circle") || same(nm, l, "ellipse")) {
		fx cx = attr_len(n, "cx", st.vw, 0), cy = attr_len(n, "cy", st.vh, 0), rx, ry;
		if (same(nm, l, "circle")) {
			rx = ry = attr_len(n, "r", st.vw, 0);
		} else {
			rx = attr_len(n, "rx", st.vw, -1);
			ry = attr_len(n, "ry", st.vh, -1);
			if (rx < 0) rx = ry;
			if (ry < 0) ry = rx;
		}
		if (rx > 0 && ry > 0) {
			ellipse_path(r, cx, cy, rx, ry);
			shape_done(r, &st);
		}
	} else if (same(nm, l, "line")) {
		p_move(r, attr_len(n, "x1", st.vw, 0), attr_len(n, "y1", st.vh, 0));
		p_line(r, attr_len(n, "x2", st.vw, 0), attr_len(n, "y2", st.vh, 0));
		st.fill.kind = PK_NONE;		/* (a line has no inside) */
		shape_done(r, &st);
	} else if (same(nm, l, "polyline") || same(nm, l, "polygon")) {
		if ((v = attr(n, "points")) != NULL) {
			const char *end = v + strlen(v), *e;
			fx px, py;
			int k = 0;
			for (;;) {
				v = skip_sep(v, end);
				if ((e = number(v, end, &px)) == NULL)
					break;
				v = skip_sep(e, end);
				if ((e = number(v, end, &py)) == NULL)
					break;
				v = e;
				if (k++ == 0)
					p_move(r, px, py);
				else
					p_line(r, px, py);
			}
			if (k > 0) {
				if (same(nm, l, "polygon"))
					rs_close(&r->cv);
				shape_done(r, &st);
			}
		}
	}
	r->depth--;
}

static void children(R *r, const dnode *n, const ST *st)
{
	const dnode *c;
	for (c = n->first; c != NULL && !r->cv.nomem; c = c->next)
		element(r, c, st);
}

PIC *svg_render(const dnode *svg, const dnode *top, int w, int h, uint32_t color, int *why)
{
	R r;
	ST st;
	fx vb[4], ow, oh;
	const dnode *k;
	PIC *p;
	long i, n = (long) w * h;
	int any_clear = 0;

	memset(&r, 0, sizeof r);
	r.top = top;
	if (!rs_init(&r.cv, w, h)) {
		*why = PIC_MEMORY;
		return NULL;
	}
	/* the picture's own sheets */
	for (k = svg; k != NULL; k = next_in(svg, k))
		if (is(k, "style")) {
			const dnode *t;
			for (t = k->first; t != NULL; t = t->next)
				if (t->type == DN_TEXT)
					sheet(&r, t->text, t->len);
		}
	memset(&st, 0, sizeof st);
	st.fill.kind = PK_COLOR;
	st.fill.argb = 0xff000000u;
	st.stroke.kind = PK_NONE;
	st.fill_op = st.stroke_op = st.op = FX_ONE;
	st.sw = FX_ONE;
	st.miter = 4 * FX_ONE;
	st.color = color & 0xffffff;
	/* the root: its viewBox onto the picture, else its own size */
	st.m.a = st.m.d = FX_ONE;
	ow = attr_len(svg, "width", 0, 0);
	oh = attr_len(svg, "height", 0, 0);
	if (view_box(attr(svg, "viewbox"), vb)) {
		fit(&st.m, vb, attr(svg, "preserveaspectratio"), 0, 0, FX(w), FX(h));
		st.vw = vb[2];
		st.vh = vb[3];
	} else {
		if (ow <= 0) ow = FX(w);
		if (oh <= 0) oh = FX(h);
		mat_then(&st.m, fx_div(FX(w), ow), 0, 0, fx_div(FX(h), oh), 0, 0);
		st.vw = ow;
		st.vh = oh;
	}
	if ((st.vw <= 0 || st.vh <= 0) || !style(&r, svg, &st)) {
		st.hidden = 1;
	} else {
		const char *v = attr(svg, "transform");
		if (v != NULL)
			transform(&st.m, v, strlen(v));
		children(&r, svg, &st);
	}
	free(r.rules);
	if (r.cv.nomem) {
		rs_free(&r.cv);
		*why = PIC_MEMORY;
		return NULL;
	}
	p = calloc(1, sizeof *p);
	if (p != NULL) {
		p->w = w;
		p->h = h;
		p->how = PIC_SVG;
		p->px = malloc((size_t) n * sizeof *p->px);
		p->alpha = malloc((size_t) n);
	}
	if (p == NULL || p->px == NULL || p->alpha == NULL) {
		pic_free(p);
		rs_free(&r.cv);
		*why = PIC_MEMORY;
		return NULL;
	}
	for (i = 0; i < n; i++) {
		const uint8_t *q = r.cv.px + i * 4;
		unsigned a = q[3], cr = 0, cg = 0, cb = 0;
		if (a == 255) {
			cr = q[0], cg = q[1], cb = q[2];
		} else if (a > 0) {
			/* (premultiplied: the colour again) */
			cr = (q[0] * 255u + a / 2) / a;
			cg = (q[1] * 255u + a / 2) / a;
			cb = (q[2] * 255u + a / 2) / a;
			if (cr > 255) cr = 255;
			if (cg > 255) cg = 255;
			if (cb > 255) cb = 255;
		}
		if (a < 255)
			any_clear = 1;
		p->px[i] = (unsigned short) ((cr >> 3) << 11 | (cg >> 2) << 5 | (cb >> 3));
		p->alpha[i] = (unsigned char) a;
	}
	if (!any_clear) {
		free(p->alpha);
		p->alpha = NULL;
	}
	rs_free(&r.cv);
	*why = PIC_OK;
	return p;
}
