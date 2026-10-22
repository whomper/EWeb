/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Property values: from a declaration's tokens to compact declarations,
 * shorthands expanded. Absolute units become px here, so computing a
 * style only has the relative ones left to do. */
#include "ec_int.h"

/* the keyword lists, each ending in 0 */
#define EC_L_ENUM(id, ...) KL_##id,
enum { KL_NONE_ = 0, EC_KWLISTS(EC_L_ENUM) KL_N };
#undef EC_L_ENUM
#define EC_L_ARR(id, ...) static const uint8_t kl_##id[] = { __VA_ARGS__, 0 };
EC_KWLISTS(EC_L_ARR)
#undef EC_L_ARR
#define EC_L_PTR(id, ...) kl_##id,
static const uint8_t *const kwlists[KL_N] = { NULL, EC_KWLISTS(EC_L_PTR) };
#undef EC_L_PTR

#define EC_P_PARSER(id, nm, parser, flags, kind, val) { parser, flags },
static const struct { uint8_t parser, flags; } ptab[EC_NPROP] = {
	EC_PROPS_INHERITED(EC_P_PARSER) EC_PROPS_OTHER(EC_P_PARSER)
};
#undef EC_P_PARSER

static int in_list(int kl, int kw)
{
	const uint8_t *l = kwlists[kl];

	if (kw == 0)
		return 0;
	for (; *l; l++)
		if (*l == kw)
			return 1;
	return 0;
}

static inline int is_delim(const ec_tok *t, char ch)
{
	return t->type == T_DELIM && t->s[0] == ch;
}

/* the index just past the function or ( block that starts at t[i] */
static int block_end(const ec_tok *t, int n, int i)
{
	int depth = 0;

	for (; i < n; i++) {
		if (t[i].type == T_FUNC || t[i].type == T_LPAREN)
			depth++;
		else if (t[i].type == T_RPAREN && --depth == 0)
			return i + 1;
	}
	return -1;
}

/* ------------------------------------------------------------ lengths and calc() */

/* a dimension's unit: px value of 1 unit (absolute), or EC_UNIT_REL(CU_*) */
static int unit_of(const ec_tok *t, int32_t *u)
{
	return ec_names_lookup(KC_UNIT, t->u, t->ulen, u);
}

static const uint8_t cu_kind[CU_N] = {
	EK_PX, EK_PCT, EK_EM, EK_EX, EK_CH, EK_REM, EK_VW, EK_VH, EK_VMIN, EK_VMAX
};

/* a calc() operand: a sum of units, or a number */
typedef struct lin {
	ec_fx c[CU_N];
	ec_fx num;
	int isnum;
} lin;

static int unit_is(const ec_tok *t, const char *u)
{
	uint16_t k;

	for (k = 0; k < t->ulen; k++)
		if (u[k] == 0 || (t->u[k] | 32) != u[k])
			return 0;
	return u[k] == 0;
}

/* a dimension that is an angle (CSS Values 4: deg, grad, rad, turn), in
   degrees (22.10), in 32 bits: more than 5000 turns only where in the
   turn they end, more than 30000 rad as 30000 */
static int angle_unit(const ec_tok *t, ec_fx *deg)
{
	ec_fx n = t->num;

	if (unit_is(t, "deg"))
		*deg = n;
	else if (unit_is(t, "turn"))
		*deg = (n > EC_FX(5000) || n < -EC_FX(5000) ? n % 1024 : n) * 360;
	else if (unit_is(t, "rad"))
		*deg = fx_mul(n > EC_FX(30000) ? EC_FX(30000) : n < -EC_FX(30000) ? -EC_FX(30000) : n, 58671);
	else if (unit_is(t, "grad"))
		*deg = n - n / 10;
	else
		return 0;
	return 1;
}

/* a + b (or a - b), kept within 32 bits (calc() of huge values: no
   overflow) */
static ec_fx fx_sum(ec_fx a, ec_fx b, int neg)
{
	if (neg)
		b = b < -INT32_MAX ? INT32_MAX : -b;
	if (b > 0 ? a > INT32_MAX - b : a < -INT32_MAX - b)
		return b > 0 ? INT32_MAX : -INT32_MAX;
	return a + b;
}

/* an angle's degrees and a percentage of a turn's, together */
static ec_fx turn_pct(ec_fx deg, ec_fx pct)
{
	pct = pct > (1 << 26) ? 1 << 26 : pct < -(1 << 26) ? -(1 << 26) : pct;
	return fx_sum(deg, pct * 18 / 5, 0);
}

/* what calc() is of: 0 lengths and numbers; 1 angles (in degrees, as
   CU_PX), 2 angles and percentages (of a turn: a conic gradient's) */
static int calc_ang;
/* min(), max() and clamp() taken inside calc() (a gradient's), and one
   of lengths of more than one unit met there (its value lost: the
   gradient is drawn as its average); the ST edition works none of them
   out (600 bytes): each one's value is lost */
static int calc_nested, calc_lost;

static int calc_sum(const ec_tok *t, int n, int *i, lin *r, int depth);
#ifndef EW_ST
static int calc_fn(const ec_tok *t, int n, int *i, lin *r, int depth);
#endif

static int calc_unit(const ec_tok *t, int n, int *i, lin *r, int depth)
{
	const ec_tok *k;
	int32_t u;

	if (*i >= n)
		return 0;
	k = &t[*i];
	memset(r, 0, sizeof *r);
	switch (k->type) {
	case T_NUM:
		r->isnum = 1;
		r->num = k->num;
		(*i)++;
		return 1;
	case T_PCT:
		if (calc_ang == 1)
			return 0;
		r->c[CU_PCT] = k->num;
		(*i)++;
		return 1;
	case T_DIM:
		if (calc_ang) {
			if (!angle_unit(k, &r->c[CU_PX]))
				return 0;
			(*i)++;
			return 1;
		}
		if (!unit_of(k, &u))
			return 0;
		if (u < 0)
			r->c[EC_UNIT_CU(u)] = k->num;
		else
			r->c[CU_PX] = fx_mul(k->num, u);
		(*i)++;
		return 1;
	case T_LPAREN:
	case T_FUNC:
		if (depth > 16)
			return 0;
#ifndef EW_ST
		if (k->type == T_FUNC && !ec_tok_is(k, "calc"))
			return calc_nested && calc_fn(t, n, i, r, depth);
#else
		if (k->type == T_FUNC && !ec_tok_is(k, "calc")) {
			/* (min(), max() or clamp() in a gradient's: its value lost,
			   the gradient its average and the colour with it kept, as
			   EWEB.PRG has one of more units) */
			int end = block_end(t, n, *i);
			if (!calc_nested || end < 0 ||
			    !(ec_tok_is(k, "min") || ec_tok_is(k, "max") || ec_tok_is(k, "clamp")))
				return 0;
			*i = end;
			calc_lost = 1;
			return 1;
		}
#endif
		(*i)++;
		if (!calc_sum(t, n, i, r, depth + 1))
			return 0;
		if (*i >= n || t[*i].type != T_RPAREN)
			return 0;
		(*i)++;
		return 1;
	case T_IDENT:
		if (ec_tok_is(k, "pi")) {
			r->isnum = 1;
			r->num = 3217;
			(*i)++;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

static void lin_scale(lin *r, ec_fx f, int divide)
{
	int u;

	for (u = 0; u < CU_N; u++)
		r->c[u] = divide ? (ec_fx) ((int64_t) r->c[u] * 1024 / f) : fx_mul(r->c[u], f);
	r->num = divide ? (ec_fx) ((int64_t) r->num * 1024 / f) : fx_mul(r->num, f);
}

static int calc_product(const ec_tok *t, int n, int *i, lin *r, int depth)
{
	if (!calc_unit(t, n, i, r, depth))
		return 0;
	while (*i < n && (is_delim(&t[*i], '*') || is_delim(&t[*i], '/'))) {
		int div = t[*i].s[0] == '/';
		lin b;
		(*i)++;
		if (!calc_unit(t, n, i, &b, depth))
			return 0;
		if (b.isnum) {
			if (div && b.num == 0)
				return 0;
			lin_scale(r, b.num, div);
		} else if (r->isnum && !div) {
			ec_fx f = r->num;
			*r = b;
			lin_scale(r, f, 0);
		} else {
			return 0;
		}
	}
	return 1;
}

static int calc_sum(const ec_tok *t, int n, int *i, lin *r, int depth)
{
	if (!calc_product(t, n, i, r, depth))
		return 0;
	while (*i < n && (is_delim(&t[*i], '+') || is_delim(&t[*i], '-'))) {
		int neg = t[*i].s[0] == '-', u;
		lin b;
		/* CSS wants whitespace on both sides of + and - */
		if (!(t[*i].flags & TF_WS) || *i + 1 >= n || !(t[*i + 1].flags & TF_WS))
			return 0;
		(*i)++;
		if (!calc_product(t, n, i, &b, depth))
			return 0;
		if (b.isnum != r->isnum)
			return 0;
		for (u = 0; u < CU_N; u++)
			r->c[u] = fx_sum(r->c[u], b.c[u], neg);
		r->num = fx_sum(r->num, b.num, neg);
	}
	return 1;
}

#ifndef EW_ST
/* min(), max() or clamp() at t[*i] inside calc() (calc_nested): worked
   out when its arguments are numbers, angles, or lengths of one unit
   (each unit's size is 0 or more: their order is their numbers'); of
   lengths of more, 0 and calc_lost */
static int calc_fn(const ec_tok *t, int n, int *i, lin *r, int depth)
{
	const ec_tok *f = &t[*i];
	int op, end = block_end(t, n, *i), j = *i + 1, na = 0, k, u, unit = -1;
	ec_fx v[3], x;
	lin a[3];

	if (end < 0)
		return 0;
	if (ec_tok_is(f, "min"))
		op = EC_CALC_MIN;
	else if (ec_tok_is(f, "max"))
		op = EC_CALC_MAX;
	else if (ec_tok_is(f, "clamp"))
		op = EC_CALC_CLAMP;
	else
		return 0;
	for (;;) {
		if (na == 3 || !calc_sum(t, end - 1, &j, &a[na], depth + 1) || a[na].isnum != a[0].isnum)
			return 0;
		na++;
		if (j < end - 1 && t[j].type == T_COMMA) {
			j++;
			continue;
		}
		break;
	}
	if (j != end - 1 || (op == EC_CALC_CLAMP && na != 3))
		return 0;
	*i = end;
	memset(r, 0, sizeof *r);
	r->isnum = a[0].isnum;
	for (k = 0; k < na; k++) {
		if (a[k].isnum || calc_ang) {
			v[k] = a[k].isnum ? a[k].num : turn_pct(a[k].c[CU_PX], a[k].c[CU_PCT]);
			unit = CU_PX;
			continue;
		}
		v[k] = 0;
		for (u = 0; u < CU_N; u++)
			if (a[k].c[u] != 0) {
				if (unit >= 0 && u != unit) {
					calc_lost = 1;
					return 1;
				}
				unit = u;
				v[k] = a[k].c[u];
			}
	}
	x = v[0];
	if (op == EC_CALC_CLAMP) {
		x = v[1] < v[2] ? v[1] : v[2];
		if (x < v[0])
			x = v[0];
	} else {
		for (k = 1; k < na; k++)
			if (op == EC_CALC_MIN ? v[k] < x : v[k] > x)
				x = v[k];
	}
	if (r->isnum)
		r->num = x;
	else if (unit >= 0)
		r->c[unit] = x;
	return 1;
}
#endif

/* what calc_value() reads: lengths, a number, an angle, or an angle or
   a percentage of a turn */
enum { CV_LEN, CV_NUM, CV_ANG, CV_ANGPCT };

/* calc(), min(), max() or clamp() at t[*i]: of lengths (CV_LEN) into an
 * ec_calcx, or a plain length when that is all it is; of numbers or
 * angles worked out now, EK_NUM (an angle in degrees) */
static int calc_value(ec_ctx *c, const ec_tok *t, int n, int *i, unsigned lf, ec_decl *d,
		int what)
{
	const ec_tok *f = &t[*i];
	int op, end = block_end(t, n, *i), j = *i + 1, na = 0, u, maxa, ok;
	lin a[3];
	ec_calcx *x;

	if (end < 0 || (c->opts & EC_COMPAT_CALC))
		return 0;
	if (ec_tok_is(f, "calc"))
		op = EC_CALC_SUM, maxa = 1;
	else if (ec_tok_is(f, "min"))
		op = EC_CALC_MIN, maxa = 3;
	else if (ec_tok_is(f, "max"))
		op = EC_CALC_MAX, maxa = 3;
	else if (ec_tok_is(f, "clamp"))
		op = EC_CALC_CLAMP, maxa = 3;
	else
		return 0;
	calc_ang = what == CV_ANG ? 1 : what == CV_ANGPCT ? 2 : 0;
	for (;;) {
		ok = na < maxa && calc_sum(t, end - 1, &j, &a[na], 0) && a[na].isnum == (what == CV_NUM);
		if (!ok)
			break;
		na++;
		if (j < end - 1 && t[j].type == T_COMMA) {
			j++;
			continue;
		}
		break;
	}
	calc_ang = 0;
	if (!ok || j != end - 1 || (op == EC_CALC_CLAMP && na != 3))
		return 0;
	*i = end;
	if (what != CV_LEN) {
		/* a number or an angle: min, max and clamp can be done now */
		ec_fx v[3], r;
		int k;
		for (k = 0; k < na; k++)
			v[k] = what == CV_NUM ? a[k].num : turn_pct(a[k].c[CU_PX], a[k].c[CU_PCT]);
		r = v[0];
		if (op == EC_CALC_MIN || op == EC_CALC_MAX) {
			for (k = 1; k < na; k++)
				if (op == EC_CALC_MIN ? v[k] < r : v[k] > r)
					r = v[k];
		} else if (op == EC_CALC_CLAMP) {
			r = v[1] < v[2] ? v[1] : v[2];
			if (r < v[0])
				r = v[0];
		}
		d->kind = EK_NUM;
		d->v.i = r;
		return 1;
	}
	if (lf & LF_NOPCT)
		for (u = 0; u < na; u++)
			if (a[u].c[CU_PCT] != 0)
				return 0;
	/* one operand with one unit is just a length */
	if (na == 1) {
		int units = 0, which = 0;
		for (u = 0; u < CU_N; u++)
			if (a[0].c[u] != 0) {
				units++;
				which = u;
			}
		if (units <= 1) {
			d->kind = cu_kind[which];
			d->v.i = a[0].c[which];
			return 1;
		}
	}
	x = ec_alloc(&c->mem, sizeof(ec_calcx) + na * sizeof x->c[0]);
	if (x == NULL)
		return 0;
	x->op = op;
	x->n = na;
	for (u = 0; u < na; u++)
		memcpy(x->c[u], a[u].c, sizeof x->c[u]);
	d->kind = EK_CALCX;
	d->v.p = x;
	return 1;
}

/* one length or percentage (lf says what else) at t[*i] */
static int length(ec_ctx *c, const ec_tok *t, int n, int *i, unsigned lf, ec_decl *d)
{
	const ec_tok *k = &t[*i];
	int32_t u;

	switch (k->type) {
	case T_DIM:
		if (!unit_of(k, &u) || ((lf & LF_NN) && k->num < 0))
			return 0;
		if (u < 0) {
			d->kind = cu_kind[EC_UNIT_CU(u)];
			d->v.i = k->num;
		} else {
			d->kind = EK_PX;
			d->v.i = fx_mul(k->num, u);
		}
		(*i)++;
		return 1;
	case T_PCT:
		if ((lf & LF_NOPCT) || ((lf & LF_NN) && k->num < 0))
			return 0;
		d->kind = EK_PCT;
		d->v.i = k->num;
		(*i)++;
		return 1;
	case T_NUM:
		if (k->num != 0)
			return 0;
		d->kind = EK_PX;
		d->v.i = 0;
		(*i)++;
		return 1;
	case T_IDENT: {
		int kw = ec_kw(k);
		if ((kw == KW_AUTO && (lf & LF_AUTO)) || (kw == KW_NONE && (lf & LF_NONE)) ||
				(kw == KW_NORMAL && (lf & LF_NORMAL)) ||
				((kw == KW_CONTENT || kw == KW_MAX_CONTENT || kw == KW_MIN_CONTENT ||
				  kw == KW_FIT_CONTENT) && (lf & LF_CONTENT))) {
			d->kind = EK_KW;
			d->v.i = kw == KW_CONTENT ? KW_CONTENT : kw;
			(*i)++;
			return 1;
		}
		return 0;
	}
	case T_FUNC:
		return calc_value(c, t, n, i, lf, d, CV_LEN);
	default:
		return 0;
	}
}

static int number(ec_ctx *c, const ec_tok *t, int n, int *i, int nn, ec_decl *d)
{
	if (t[*i].type == T_NUM) {
		if (nn && t[*i].num < 0)
			return 0;
		d->kind = EK_NUM;
		d->v.i = t[*i].num;
		(*i)++;
		return 1;
	}
	if (t[*i].type == T_FUNC && calc_value(c, t, n, i, 0, d, CV_NUM))
		return !nn || d->v.i >= 0;
	return 0;
}

/* ------------------------------------------------------------ colours */

static int hexval(char h)
{
	if (h >= '0' && h <= '9')
		return h - '0';
	h |= 32;
	if (h >= 'a' && h <= 'f')
		return h - 'a' + 10;
	return -1;
}

static int clamp255(int32_t v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* a percentage (fixed point) of 255, rounded: 30% is 77 */
static int pct255(ec_fx p)
{
	if (p <= 0)
		return 0;
	if (p >= EC_FX(100))
		return 255;
	return (p * 255 + 51200) / 102400;
}

/* hsl to rgb, h in degrees, s and l 0..1, all fixed point (CSS Color 4) */
static uint32_t hsl_rgb(ec_fx h, ec_fx s, ec_fx l)
{
	static const int ns[3] = { 0, 8, 4 };
	ec_fx a, h30;
	uint32_t rgb = 0;
	int i;

	h %= EC_FX(360);
	if (h < 0)
		h += EC_FX(360);
	s = s < 0 ? 0 : s > 1024 ? 1024 : s;
	l = l < 0 ? 0 : l > 1024 ? 1024 : l;
	h30 = h / 30;
	a = fx_mul(s, l < 1024 - l ? l : 1024 - l);
	for (i = 0; i < 3; i++) {
		ec_fx k = (EC_FX(ns[i]) + h30) % EC_FX(12), m, v;
		m = k - EC_FX(3);
		if (EC_FX(9) - k < m)
			m = EC_FX(9) - k;
		if (m > 1024)
			m = 1024;
		if (m < -1024)
			m = -1024;
		v = l - fx_mul(a, m);
		rgb = (rgb << 8) | clamp255((v * 255 + 512) >> 10);
	}
	return rgb;
}

/* rgb(), rgba(), hsl(), hsla(): legacy (commas) and modern (spaces, /) */
static unsigned color_opts;	/* the context's options, for color_fn */

static int color_fn(const ec_tok *t, int n, uint32_t *argb)
{
	int hsl = ec_tok_is(&t[0], "hsl") || ec_tok_is(&t[0], "hsla");
	int rgb = ec_tok_is(&t[0], "rgb") || ec_tok_is(&t[0], "rgba");
	int end = block_end(t, n, 0), i, nv = 0, commas = 0, slash = 0;
	const ec_tok *v[4];
	int32_t ch[3], alpha = 255;

	if ((!hsl && !rgb) || end < 0)
		return 0;
	for (i = 1; i < end - 1; i++) {
		const ec_tok *k = &t[i];
		if (k->type == T_COMMA) {
			commas++;
			continue;
		}
		if (is_delim(k, '/')) {
			if (slash || nv != 3)
				return 0;
			slash = 1;
			continue;
		}
		if (nv == 4 || !(k->type == T_NUM || k->type == T_PCT || k->type == T_DIM ||
				(k->type == T_IDENT && ec_tok_is(k, "none"))))
			return 0;
		v[nv++] = k;
	}
	if (nv < 3 || (commas && (slash || commas != nv - 1)) || (nv == 4 && !commas && !slash))
		return 0;
	if ((color_opts & EC_COMPAT_COLOR) && !commas)
		return 0;
	for (i = 0; i < 3; i++) {
		const ec_tok *k = v[i];
		if (k->type == T_IDENT) {
			ch[i] = 0;
			continue;
		}
		if (rgb) {
			if (k->type == T_DIM)
				return 0;
			ch[i] = k->type == T_PCT ? pct255(k->num) : clamp255((k->num + 512) >> 10);
		} else if (i == 0) {
			ec_fx h = k->num;
			if (k->type == T_PCT)
				return 0;
			if (k->type == T_DIM) {
				if (k->ulen == 3 && (k->u[0] | 32) == 'd')
					;
				else if (k->ulen == 4 && (k->u[0] | 32) == 't')
					h = fx_mul(h, EC_FX(360));
				else if (k->ulen == 3 && (k->u[0] | 32) == 'r')
					h = fx_mul(h, 58671);	/* 180 / pi */
				else if (k->ulen == 4 && (k->u[0] | 32) == 'g')
					h = fx_mul(h, 922);	/* 0.9 */
				else
					return 0;
			}
			ch[0] = h;
		} else {
			if (k->type == T_DIM)
				return 0;
			ch[i] = k->num / 100;	/* percent, or a number 0..100 */
		}
	}
	if (nv == 4 && v[3]->type != T_IDENT) {
		const ec_tok *k = v[3];
		if (k->type == T_DIM)
			return 0;
		alpha = k->type == T_PCT ? pct255(k->num) : clamp255((k->num * 255 + 512) >> 10);
	}
	if (hsl)
		*argb = ((uint32_t) alpha << 24) | hsl_rgb(ch[0], ch[1], ch[2]);
	else
		*argb = ((uint32_t) alpha << 24) | ((uint32_t) ch[0] << 16) | ((uint32_t) ch[1] << 8) |
			(uint32_t) ch[2];
	return end;
}

int ec_parse_color(const ec_tok *t, int n, int *used, uint32_t *argb, int *cur)
{
	int32_t v;

	*cur = 0;
	if (n <= 0)
		return 0;
	switch (t->type) {
	case T_HASH: {
		int d[8], i;
		if (t->len != 3 && t->len != 4 && t->len != 6 && t->len != 8)
			return 0;
		for (i = 0; i < (int) t->len; i++)
			if ((d[i] = hexval(t->s[i])) < 0)
				return 0;
		if (t->len <= 4) {
			*argb = (t->len == 4 ? (uint32_t) (d[3] * 17) << 24 : 0xff000000u) |
				(uint32_t) (d[0] * 17) << 16 | (uint32_t) (d[1] * 17) << 8 | d[2] * 17;
		} else {
			*argb = (t->len == 8 ? (uint32_t) (d[6] * 16 + d[7]) << 24 : 0xff000000u) |
				(uint32_t) (d[0] * 16 + d[1]) << 16 | (uint32_t) (d[2] * 16 + d[3]) << 8 |
				(uint32_t) (d[4] * 16 + d[5]);
		}
		*used = 1;
		return 1;
	}
	case T_IDENT: {
		int kw = ec_kw(t);
		if (kw == KW_CURRENTCOLOR) {
			*cur = 1;
			*used = 1;
			return 1;
		}
		if (kw == KW_TRANSPARENT) {
			*argb = 0;
			*used = 1;
			return 1;
		}
		if (t->flags & TF_ESC)
			return 0;
		if (!ec_names_lookup(KC_COLOR, t->s, t->len, &v))
			return 0;
		*argb = (uint32_t) v;
		*used = 1;
		return 1;
	}
	case T_FUNC: {
		int end = color_fn(t, n, argb);
		if (end <= 0)
			return 0;
		*used = end;
		return 1;
	}
	default:
		return 0;
	}
}

static int color(const ec_tok *t, int n, int *i, ec_decl *d)
{
	uint32_t argb;
	int used, cur;

	if (!ec_parse_color(t + *i, n - *i, &used, &argb, &cur))
		return 0;
	*i += used;
	if (cur) {
		d->kind = EK_CURCOLOR;
		d->v.i = 0;
	} else {
		d->kind = EK_COLOR;
		d->v.i = (int32_t) argb;
	}
	return 1;
}

/* ------------------------------------------------------------ other values */

static const ec_str *tok_str(ec_ctx *c, const ec_tok *t)
{
	char buf[1024];
	size_t len;
	const char *s = ec_tok_text(t, buf, sizeof buf, &len);
	return ec_str_new(&c->mem, s, len);
}

/* a url(): made absolute when its sheet has an address of its own */
static const ec_str *url_str(ec_ctx *c, const ec_tok *t)
{
	char buf[1024], abs[2048];
	size_t len;
	const char *s = ec_tok_text(t, buf, sizeof buf, &len);
	if (c->parse_base != NULL && c->resolve != NULL && len < sizeof buf) {
		char rel[1024];
		memcpy(rel, s, len);
		rel[len] = 0;
		if (c->resolve(c->parse_base, rel, abs, sizeof abs))
			return ec_str_new(&c->mem, abs, strlen(abs));
	}
	return ec_str_new(&c->mem, s, len);
}

/* ------------------------------------------------------------ gradients */

/* CSS Images 3 and 4: linear-, radial- and conic-gradient() and their
   repeating- forms, kept as ec_grad: what the layout and the canvas
   need to draw them; and the Compatibility Standard's
   -webkit-linear-gradient() and -webkit-gradient(), kept as the same.
   A gradient that is valid but whose stops EWeb can't place (a min() of
   px and %, more stops than GRAD_MAX: none of the page set's has more
   than 12) is kept as its colours, drawn as their average (approx) */
#ifdef EW_ST
#define GRAD_MAX 32
#else
#define GRAD_MAX 64
#endif
#define GRAD_REL GRAD_MAX
/* the prelude's lengths' rel numbers until the stops are counted */
#define GL_PRE 0xfc

static int position(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *x, ec_decl *y);

typedef struct gb {
	ec_gstop s[GRAD_MAX];
	ec_grel rel[GRAD_REL];
	int n, nrel, approx;
} gb;

/* an angle at t[*j], in degrees (22.10): a dimension, or calc(), min(),
   max() or clamp() of them (CSS Values 4, 10); a unitless 0 where zero
   says, a percentage of a turn in calc() where pct does */
static int angle(ec_ctx *c, const ec_tok *t, int e, int *j, int zero, int pct, ec_fx *deg)
{
	const ec_tok *k = &t[*j];
	ec_decl d;

	if (*j >= e)
		return 0;
	if (k->type == T_NUM && zero && k->num == 0) {
		*deg = 0;
		(*j)++;
		return 1;
	}
	if (k->type == T_DIM) {
		if (!angle_unit(k, deg))
			return 0;
		(*j)++;
		return 1;
	}
	if (k->type != T_FUNC || !calc_value(c, t, e, j, 0, &d, pct ? CV_ANGPCT : CV_ANG))
		return 0;
	*deg = d.v.i;
	return 1;
}

/* a direction, within a turn (the canvas's sums of it can't overflow) */
static ec_fx turn_of(ec_fx deg)
{
	return deg % (360 * 1024);
}

/* a length's part in a relative unit, for the style to compute (more
   than are kept: the gradient its average) */
static void grel(gb *b, int at, int unit, ec_fx v)
{
	if (b->nrel == GRAD_REL) {
		b->approx = 1;
		return;
	}
	b->rel[b->nrel].at = (uint8_t) at;
	b->rel[b->nrel].unit = (uint8_t) unit;
	b->rel[b->nrel++].v = v;
}

/* a length or percentage as length() read it, as px + pct% and its
   relative parts; min(), max() and clamp() worked out when their
   arguments are of one unit (each unit's size is 0 or more, so their
   order is their numbers'), and of more units the gradient its average
   (in the ST edition any min(), max() or clamp(): 250 bytes less) */
static void glen(gb *b, const ec_decl *d, int at, ec_glen *l)
{
	int u;

	l->px = l->pct = 0;
	switch (d->kind) {
	case EK_PX:
		l->px = d->v.i;
		return;
	case EK_PCT:
		l->pct = d->v.i;
		return;
	case EK_CALCX: {
		const ec_calcx *x = d->v.p;
#ifndef EW_ST
		ec_fx v[3] = { 0, 0, 0 }, r;
		int k, unit = -1;
#endif
		if (x->op == EC_CALC_SUM) {
			l->px = x->c[0][CU_PX];
			l->pct = x->c[0][CU_PCT];
			for (u = CU_EM; u < CU_N; u++)
				if (x->c[0][u] != 0)
					grel(b, at, u, x->c[0][u]);
			return;
		}
#ifdef EW_ST
		b->approx = 1;
#else
		for (k = 0; k < x->n && k < 3; k++)
			for (u = 0; u < CU_N; u++)
				if (x->c[k][u] != 0) {
					if (unit >= 0 && u != unit) {
						b->approx = 1;
						return;
					}
					unit = u;
					v[k] = x->c[k][u];
				}
		r = v[0];
		if (x->op == EC_CALC_CLAMP && x->n == 3) {
			r = v[1] < v[2] ? v[1] : v[2];
			if (r < v[0])
				r = v[0];
		} else {
			for (k = 1; k < x->n && k < 3; k++)
				if (x->op == EC_CALC_MIN ? v[k] < r : v[k] > r)
					r = v[k];
		}
		if (unit == CU_PCT)
			l->pct = r;
		else if (unit > CU_PCT)
			grel(b, at, unit, r);
		else
			l->px = r;
#endif
		return;
	}
	default:
		if (d->kind >= EK_EM && d->kind <= EK_VMAX)
			grel(b, at, CU_EM + (d->kind - EK_EM), d->v.i);
		return;
	}
}

/* a percentage in it (a circle's size is a length only) */
static int has_pct(const ec_decl *d)
{
	const ec_calcx *x;
	int k;

	if (d->kind == EK_PCT)
		return 1;
	if (d->kind != EK_CALCX)
		return 0;
	x = d->v.p;
	for (k = 0; k < x->n; k++)
		if (x->c[k][CU_PCT] != 0)
			return 1;
	return 0;
}

/* the end of the argument at t[j]: its comma, or e; -1 if broken */
static int arg_end(const ec_tok *t, int j, int e)
{
	while (j < e && t[j].type != T_COMMA) {
		if (t[j].type == T_FUNC || t[j].type == T_LPAREN) {
			j = block_end(t, e, j);
			if (j < 0)
				return -1;
		} else {
			j++;
		}
	}
	return j;
}

/* in <colour space> [<hue method> hue] (CSS Color 4, 12.1): read, and
   drawn interpolated in sRGB all the same, as browsers do between
   EWeb's colours (sRGB ones) when none is named */
static int interp(const ec_tok *t, int e, int *j)
{
	static const char *const spaces[] = {
		"srgb", "srgb-linear", "display-p3", "a98-rgb", "prophoto-rgb", "rec2020", "lab", "oklab",
		"xyz", "xyz-d50", "xyz-d65", "hsl", "hwb", "lch", "oklch", NULL
	};
	int k = *j, s;

	if (k + 1 >= e || t[k].type != T_IDENT || !ec_tok_is(&t[k], "in") || t[k + 1].type != T_IDENT)
		return 0;
	for (s = 0; spaces[s] != NULL && !ec_tok_is(&t[k + 1], spaces[s]); s++)
		;
	if (spaces[s] == NULL)
		return 0;
	k += 2;
	if (s >= 11 && k + 1 < e && t[k].type == T_IDENT && t[k + 1].type == T_IDENT &&
	    ec_tok_is(&t[k + 1], "hue") && (ec_tok_is(&t[k], "shorter") || ec_tok_is(&t[k], "longer") ||
					      ec_tok_is(&t[k], "increasing") || ec_tok_is(&t[k], "decreasing")))
		k += 2;
	*j = k;
	return 1;
}

static int extent_kw(const ec_tok *t)
{
	if (t->type != T_IDENT)
		return 0;
	if (ec_tok_is(t, "closest-side"))
		return EC_EXT_CLOSEST_SIDE;
	if (ec_tok_is(t, "closest-corner"))
		return EC_EXT_CLOSEST_CORNER;
	if (ec_tok_is(t, "farthest-side"))
		return EC_EXT_FARTHEST_SIDE;
	if (ec_tok_is(t, "farthest-corner"))
		return EC_EXT_FARTHEST_CORNER;
	return 0;
}

/* at <position>: the centre */
static int at_pos(ec_ctx *c, gb *b, const ec_tok *t, int a, int *j, ec_glen *x, ec_glen *y)
{
	ec_decl px, py;

	(*j)++;
	if (!position(c, t, a, j, &px, &py))
		return 0;
	glen(b, &px, GL_PRE, x);
	glen(b, &py, GL_PRE + 1, y);
	return 1;
}

/* a side or a corner: one or two of left, right, top and bottom, as
   EC_TO_* bits (0: none); legacy (-webkit-) names where it starts */
static int sides(const ec_tok *t, int a, int *j, int legacy)
{
	int h = 0, v = 0;

	for (; *j < a && t[*j].type == T_IDENT; (*j)++) {
		int kw = ec_kw(&t[*j]);
		if ((kw == KW_LEFT || kw == KW_RIGHT) && !h)
			h = (kw == KW_LEFT) != legacy ? EC_TO_LEFT : EC_TO_RIGHT;
		else if ((kw == KW_TOP || kw == KW_BOTTOM) && !v)
			v = (kw == KW_TOP) != legacy ? EC_TO_TOP : EC_TO_BOTTOM;
		else
			break;	/* (in <colour space>, or not valid) */
	}
	return h | v;
}

/* the first argument when it isn't a colour stop: a linear gradient's
   direction, a radial one's shape, size and centre, a conic one's
   start and centre, and any's interpolation (CSS Images 4, 3.1-3.3);
   legacy: -webkit-linear-gradient()'s, a side it starts from or an
   angle from east, counter-clockwise (the Compatibility Standard) */
static int prelude(ec_ctx *c, gb *b, ec_grad *g, const ec_tok *t, int j, int a, int legacy)
{
	int have_in = 0, have_at = 0, have = 0, shape = 0, nl = 0;
	ec_fx deg;
	ec_decl l[2];

	while (j < a) {
		const ec_tok *k = &t[j];
		if (!have_in && !legacy && interp(t, a, &j)) {
			have_in = 1;
			continue;
		}
		if (have_at)
			return 0;
		if (g->type == EC_GRAD_LINEAR) {
			if (have)
				return 0;
			if (k->type == T_IDENT && (legacy || ec_tok_is(k, "to"))) {
				/* to a side or a corner */
				if (!legacy)
					j++;
				if ((g->to = (uint8_t) sides(t, a, &j, legacy)) == 0)
					return 0;
			} else if (angle(c, t, a, &j, 1, 0, &deg)) {
				g->angle = turn_of(legacy ? 90 * 1024 - turn_of(deg) : deg);
			} else {
				return 0;
			}
			have = 1;
		} else if (k->type == T_IDENT && ec_tok_is(k, "at")) {
			if (!at_pos(c, b, t, a, &j, &g->x, &g->y))
				return 0;
			have_at = 1;
		} else if (g->type == EC_GRAD_CONIC) {
			if (have || !(k->type == T_IDENT && ec_tok_is(k, "from")))
				return 0;
			j++;
			if (!angle(c, t, a, &j, 1, 0, &deg))
				return 0;
			g->angle = turn_of(deg);
			have = 1;
		} else if (k->type == T_IDENT && !shape && (ec_tok_is(k, "circle") || ec_tok_is(k, "ellipse"))) {
			shape = ec_tok_is(k, "circle") ? 1 : 2;
			j++;
		} else if (!g->extent && !nl && (g->extent = (uint8_t) extent_kw(k)) != 0) {
			j++;
		} else if (!g->extent && !nl && length(c, t, a, &j, LF_NN, &l[0])) {
			/* its radius, or its two radii */
			nl = 1;
			if (j < a && length(c, t, a, &j, LF_NN, &l[1]))
				nl = 2;
		} else {
			return 0;
		}
	}
	if (g->type == EC_GRAD_RADIAL) {
		/* a circle's size a length, an ellipse's two (CSS Images 3, 3.2.1) */
		if (shape == 0)
			shape = nl == 1 ? 1 : 2;
		if ((shape == 1 && nl == 2) || (shape == 2 && nl == 1) || (nl == 1 && has_pct(&l[0])))
			return 0;
		if (nl) {
			glen(b, &l[0], GL_PRE + 2, &g->rx);
			if (nl == 2)
				glen(b, &l[1], GL_PRE + 3, &g->ry);
			else
				g->ry = g->rx;
		} else if (!g->extent) {
			g->extent = EC_EXT_FARTHEST_CORNER;
		}
		g->circle = (uint8_t) (shape == 1);
	}
	return have_in || have_at || have || shape || nl || g->extent;
}

/* a stop's or a hint's position (stop at): a length or percentage of
   the gradient line or ray; a conic gradient's an angle or a
   percentage, kept as a percentage of the turn */
static int stop_at(ec_ctx *c, gb *b, int conic, const ec_tok *t, int a, int *j, ec_glen *l, int at)
{
	ec_decl d;
	ec_fx deg;

	if (!conic) {
		if (!length(c, t, a, j, 0, &d))
			return 0;
		glen(b, &d, at, l);
		return 1;
	}
	l->px = 0;
	if (t[*j].type == T_PCT) {
		l->pct = t[*j].num;
		(*j)++;
		return 1;
	}
	if (!angle(c, t, a, j, 1, 1, &deg))
		return 0;
	l->pct = deg / 18 * 5 + deg % 18 * 5 / 18;
	return 1;
}

/* the gradient h with b's stops into the sheet's memory, d's value */
static int grad_keep(ec_ctx *c, const ec_grad *h, gb *b, ec_decl *d)
{
	ec_grad *g;
	ec_grel *rel = NULL;
	int k;

	if (b->approx) {
		/* its colours alone */
		int n = 0;
		for (k = 0; k < b->n; k++)
			if (!(b->s[k].flags & EC_GS_HINT)) {
				b->s[n] = b->s[k];
				b->s[n].flags &= EC_GS_CUR;
				b->s[n].at.px = b->s[n].at.pct = 0;
				n++;
			}
		b->n = n;
		b->nrel = 0;
	}
	g = ec_alloc(&c->mem, sizeof *g + b->n * sizeof g->s[0]);
	if (b->nrel)
		rel = ec_alloc(&c->mem, b->nrel * sizeof *rel);
	if (g == NULL || (b->nrel && rel == NULL))
		return 0;
	*g = *h;
	g->approx = (uint8_t) !!b->approx;
	g->n = (uint8_t) b->n;
	memcpy(g->s, b->s, b->n * sizeof g->s[0]);
	for (k = 0; k < b->nrel; k++) {
		rel[k] = b->rel[k];
		if (rel[k].at >= GL_PRE)
			rel[k].at = (uint8_t) (b->n + rel[k].at - GL_PRE);
	}
	g->rel = rel;
	g->nrel = (uint8_t) b->nrel;
	d->kind = EK_IMAGE;
	d->v.p = g;
	return 1;
}

/* a gradient function at t[*i], whose block ends at end */
static int grad_fn(ec_ctx *c, const ec_tok *t, int *i, int end, ec_decl *d)
{
	static const struct {
		const char *name;
		uint8_t type, repeat, legacy;
	} kinds[] = {
		{ "linear-gradient", EC_GRAD_LINEAR, 0, 0 }, { "radial-gradient", EC_GRAD_RADIAL, 0, 0 },
		{ "conic-gradient", EC_GRAD_CONIC, 0, 0 }, { "repeating-linear-gradient", EC_GRAD_LINEAR, 1, 0 },
		{ "repeating-radial-gradient", EC_GRAD_RADIAL, 1, 0 }, { "repeating-conic-gradient", EC_GRAD_CONIC, 1, 0 },
		{ "-webkit-linear-gradient", EC_GRAD_LINEAR, 0, 1 },
		{ "-webkit-repeating-linear-gradient", EC_GRAD_LINEAR, 1, 1 }
	};
	ec_grad h;
	gb b;
	int j = *i + 1, e = end - 1, k, a, stops = 0, hint = 0, used, cur, nk = (int) (sizeof kinds / sizeof kinds[0]);
	uint32_t argb;

	for (k = 0; k < nk && !ec_tok_is(&t[*i], kinds[k].name); k++)
		;
	if (k == nk)
		return 0;
	memset(&h, 0, sizeof h);
	h.type = kinds[k].type;
	h.repeat = kinds[k].repeat;
	h.angle = h.type == EC_GRAD_LINEAR ? EC_FX(180) : 0;	/* to bottom */
	h.x.pct = h.y.pct = EC_FX(50);
	b.n = b.nrel = b.approx = 0;
	a = arg_end(t, j, e);
	if (a <= j)
		return 0;
	if (!ec_parse_color(&t[j], a - j, &used, &argb, &cur)) {
		if (!prelude(c, &b, &h, t, j, a, kinds[k].legacy) || a == e)
			return 0;
		j = a + 1;
	} else if (h.type == EC_GRAD_RADIAL) {
		h.extent = EC_EXT_FARTHEST_CORNER;
	}
	/* the stops: a colour and up to two positions, or a hint between
	   two; more than GRAD_MAX read and left out (the gradient the
	   average of those kept) */
	while (j < e) {
		ec_gstop s[2];
		int ns = 1;
		a = arg_end(t, j, e);
		if (a <= j)
			return 0;
		memset(s, 0, sizeof s);
		if (ec_parse_color(&t[j], a - j, &used, &argb, &cur)) {
			j += used;
			s[0].argb = cur ? 0 : argb;
			s[0].flags = cur ? EC_GS_CUR : 0;
			if (j < a) {
				if (!stop_at(c, &b, h.type == EC_GRAD_CONIC, t, a, &j, &s[0].at, b.n))
					return 0;
				s[0].flags |= EC_GS_AT;
				if (j < a) {
					/* two positions: two stops of the colour */
					s[1] = s[0];
					ns = 2;
					if (!stop_at(c, &b, h.type == EC_GRAD_CONIC, t, a, &j, &s[1].at, b.n + 1))
						return 0;
				}
			}
			stops++;
			hint = 0;
		} else {
			if (!stops || hint || !stop_at(c, &b, h.type == EC_GRAD_CONIC, t, a, &j, &s[0].at, b.n))
				return 0;
			s[0].flags = EC_GS_AT | EC_GS_HINT;
			hint = 1;
		}
		if (j != a)
			return 0;
		if (b.n + ns <= GRAD_MAX) {
			memcpy(&b.s[b.n], s, ns * sizeof s[0]);
			b.n += ns;
		} else {
			b.approx = 1;
		}
		j = a + 1;
		if (a < e && j == e)
			return 0;	/* a comma with nothing after it */
	}
	if (!stops || hint)
		return 0;
	if (calc_lost)
		b.approx = 1;
	if (!grad_keep(c, &h, &b, d))
		return 0;
	*i = end;
	return 1;
}

/* (min(), max() and clamp() taken inside its calc()s) */
static int gradient(ec_ctx *c, const ec_tok *t, int *i, int end, ec_decl *d)
{
	int r;

	calc_nested = 1;
	calc_lost = 0;
	r = grad_fn(c, t, i, end, d);
	calc_nested = calc_lost = 0;
	return r;
}

#ifndef EW_ST
/* (-webkit-gradient(), 2 KB of 68000 code, isn't the ST edition's: the
   1 MB ST has no more to spare) */

/* -webkit-gradient()'s point: x then y, each a number (px), a
   percentage or a keyword */
static int wk_point(const ec_tok *t, int e, int *j, ec_glen *x, ec_glen *y)
{
	int k, kw;

	for (k = 0; k < 2; k++, (*j)++) {
		const ec_tok *p = &t[*j];
		ec_glen *l = k ? y : x;
		l->px = l->pct = 0;
		if (*j >= e)
			return 0;
		if (p->type == T_NUM)
			l->px = p->num;
		else if (p->type == T_PCT)
			l->pct = p->num;
		else if (p->type == T_IDENT && ((kw = ec_kw(p)) == KW_CENTER || kw == (k ? KW_TOP : KW_LEFT) ||
						 kw == (k ? KW_BOTTOM : KW_RIGHT)))
			l->pct = kw == KW_CENTER ? EC_FX(50) : kw == KW_LEFT || kw == KW_TOP ? 0 : EC_FX(100);
		else
			return 0;
	}
	return 1;
}

/* the sign of a length: 0 none, 1 more than none, -1 less, 2 either
   (its px one way, its % the other) */
static int glen_sign(const ec_glen *l)
{
	int a = l->px > 0 ? 1 : l->px < 0 ? -1 : 0, b = l->pct > 0 ? 1 : l->pct < 0 ? -1 : 0;
	return !a ? b : !b || a == b ? a : 2;
}

/* -webkit-gradient(linear, <point>, <point>, <stop>...) or (radial,
   <point>, <radius>, <point>, <radius>, <stop>...), WebKit's first
   gradients (the Compatibility Standard): a linear one along a side,
   as most are, kept as linear-gradient() to that side with its stops
   (from(), to(), color-stop(), sorted by their places) placed on its
   line; any other kept as its average */
static int wk_gradient(ec_ctx *c, const ec_tok *t, int *i, int end, ec_decl *d)
{
	ec_grad h;
	gb b;
	ec_glen x[2], y[2], from, by;
	ec_fx f[GRAD_MAX];
	int j = *i + 1, e = end - 1, radial, k, way = 0;

	memset(&h, 0, sizeof h);
	from.px = from.pct = by.px = by.pct = 0;
	b.n = b.nrel = b.approx = 0;
	if (j >= e || t[j].type != T_IDENT)
		return 0;
	radial = ec_tok_is(&t[j], "radial");
	if (!radial && !ec_tok_is(&t[j], "linear"))
		return 0;
	j++;
	/* its points (and radii) */
	for (k = 0; k < 2; k++) {
		if (j >= e || t[j].type != T_COMMA)
			return 0;
		j++;
		if (!wk_point(t, e, &j, &x[k], &y[k]))
			return 0;
		if (radial) {
			if (j + 1 >= e || t[j].type != T_COMMA || t[j + 1].type != T_NUM || t[j + 1].num < 0)
				return 0;
			j += 2;
		}
	}
	/* its stops */
	while (j < e) {
		const ec_tok *s;
		ec_fx at;
		int fe, used, cur;
		uint32_t argb;
		if (t[j].type != T_COMMA || j + 1 >= e || t[j + 1].type != T_FUNC)
			return 0;
		s = &t[++j];
		fe = block_end(t, e, j);
		if (fe < 0)
			return 0;
		j++;
		if (ec_tok_is(s, "from") || ec_tok_is(s, "to")) {
			at = ec_tok_is(s, "to") ? EC_FX(1) : 0;
		} else if (ec_tok_is(s, "color-stop") && j + 1 < fe && (t[j].type == T_NUM || t[j].type == T_PCT) &&
			   t[j + 1].type == T_COMMA) {
			at = t[j].type == T_PCT ? t[j].num / 100 : t[j].num;
			at = at < 0 ? 0 : at > EC_FX(1) ? EC_FX(1) : at;
			j += 2;
		} else {
			return 0;
		}
		if (!ec_parse_color(&t[j], fe - 1 - j, &used, &argb, &cur) || j + used != fe - 1)
			return 0;
		j = fe;
		if (b.n == GRAD_MAX) {
			b.approx = 1;
			continue;
		}
		/* in order of place, as WebKit sorts them */
		for (k = b.n; k > 0 && f[k - 1] > at; k--) {
			f[k] = f[k - 1];
			b.s[k] = b.s[k - 1];
		}
		f[k] = at;
		memset(&b.s[k], 0, sizeof b.s[k]);
		b.s[k].argb = cur ? 0 : argb;
		b.s[k].flags = cur ? EC_GS_CUR : 0;
		b.n++;
	}
	h.type = radial ? EC_GRAD_RADIAL : EC_GRAD_LINEAR;
	h.angle = EC_FX(180);
	h.x.pct = h.y.pct = EC_FX(50);
	h.extent = radial ? EC_EXT_FARTHEST_CORNER : 0;
	if (!radial) {
		/* down or up, across one way or the other: its line is the
		   gradient line's part from one point to the other */
		ec_glen dx, dy;
		int sx, sy;
		dx.px = fx_sum(x[1].px, x[0].px, 1);
		dx.pct = fx_sum(x[1].pct, x[0].pct, 1);
		dy.px = fx_sum(y[1].px, y[0].px, 1);
		dy.pct = fx_sum(y[1].pct, y[0].pct, 1);
		sx = glen_sign(&dx);
		sy = glen_sign(&dy);
		if (!sx && (sy == 1 || sy == -1)) {
			way = sy > 0 ? EC_TO_BOTTOM : EC_TO_TOP;
			from = y[0];
			by = dy;
		} else if (!sy && (sx == 1 || sx == -1)) {
			way = sx > 0 ? EC_TO_RIGHT : EC_TO_LEFT;
			from = x[0];
			by = dx;
		}
	}
	if (way) {
		h.to = (uint8_t) way;
		for (k = 0; k < b.n; k++) {
			ec_glen *l = &b.s[k].at;
			l->px = fx_sum(from.px, (ec_fx) ((int64_t) f[k] * by.px / 1024), 0);
			l->pct = fx_sum(from.pct, (ec_fx) ((int64_t) f[k] * by.pct / 1024), 0);
			if (way == EC_TO_TOP || way == EC_TO_LEFT) {
				/* (from the other side) */
				l->px = fx_sum(0, l->px, 1);
				l->pct = fx_sum(EC_FX(100), l->pct, 1);
			}
			b.s[k].flags |= EC_GS_AT;
		}
	} else {
		b.approx = 1;
	}
	if (!grad_keep(c, &h, &b, d))
		return 0;
	*i = end;
	return 1;
}
#endif

static int image(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	const ec_tok *k = &t[*i];

	if (k->type == T_URL) {
		d->kind = EK_PTR;
		d->v.p = url_str(c, k);
		(*i)++;
		return d->v.p != NULL;
	}
	if (k->type == T_IDENT && ec_kw(k) == KW_NONE) {
		d->kind = EK_KW;
		d->v.i = KW_NONE;
		(*i)++;
		return 1;
	}
	if (k->type == T_FUNC) {
		int end = block_end(t, n, *i);
		if (end < 0)
			return 0;
		if (ec_tok_is(k, "url")) {
			if (end != *i + 3 || t[*i + 1].type != T_STR)
				return 0;
			d->kind = EK_PTR;
			d->v.p = url_str(c, &t[*i + 1]);
			*i = end;
			return d->v.p != NULL;
		}
		if (c->opts & EC_COMPAT_GRADIENT)
			return 0;
#ifndef EW_ST
		if (ec_tok_is(k, "-webkit-gradient"))
			return wk_gradient(c, t, i, end, d);
#endif
		return gradient(c, t, i, end, d);
	}
	return 0;
}

static int font_size(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	if (t[*i].type == T_IDENT) {
		int kw = ec_kw(&t[*i]);
		if ((kw >= KW_XX_SMALL && kw <= KW_SMALLER) || kw == KW_MEDIUM) {
			d->kind = EK_KW;
			d->v.i = kw;
			(*i)++;
			return 1;
		}
		return 0;
	}
	return length(c, t, n, i, LF_NN, d);
}

static int font_weight(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	if (t[*i].type == T_IDENT) {
		int kw = ec_kw(&t[*i]);
		d->kind = EK_INT;
		if (kw == KW_NORMAL)
			d->v.i = 400;
		else if (kw == KW_BOLD)
			d->v.i = 700;
		else if (kw == KW_BOLDER || kw == KW_LIGHTER) {
			d->kind = EK_KW;
			d->v.i = kw;
		} else
			return 0;
		(*i)++;
		return 1;
	}
	if (!number(c, t, n, i, 1, d) || d->v.i < EC_FX(1) || d->v.i > EC_FX(1000))
		return 0;
	d->kind = EK_INT;
	d->v.i >>= 10;
	return 1;
}

static int line_height(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	if (t[*i].type == T_NUM)		/* line-height: 0 is the number 0 */
		return number(c, t, n, i, 1, d);
	if (t[*i].type == T_FUNC) {
		int j = *i;
		if (number(c, t, n, &j, 1, d)) {
			*i = j;
			return 1;
		}
	}
	return length(c, t, n, i, LF_NN | LF_NORMAL, d);
}

static int border_width(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	if (t[*i].type == T_IDENT) {
		int kw = ec_kw(&t[*i]);
		d->kind = EK_PX;
		if (kw == KW_THIN)
			d->v.i = EC_FX(1);
		else if (kw == KW_MEDIUM)
			d->v.i = EC_FX(3);
		else if (kw == KW_THICK)
			d->v.i = EC_FX(5);
		else
			return 0;
		(*i)++;
		return 1;
	}
	return length(c, t, n, i, LF_NN | LF_NOPCT, d);
}

/* one axis of background-position: axis 0 is x */
static int bg_axis(ec_ctx *c, const ec_tok *t, int n, int *i, int axis, ec_decl *d)
{
	if (t[*i].type == T_IDENT) {
		int kw = ec_kw(&t[*i]);
		d->kind = EK_PCT;
		if (kw == KW_CENTER)
			d->v.i = EC_FX(50);
		else if (kw == (axis ? KW_TOP : KW_LEFT))
			d->v.i = 0;
		else if (kw == (axis ? KW_BOTTOM : KW_RIGHT))
			d->v.i = EC_FX(100);
		else
			return 0;
		(*i)++;
		return 1;
	}
	return length(c, t, n, i, 0, d);
}

static int integer(ec_ctx *c, const ec_tok *t, int n, int *i, unsigned lf, ec_decl *d)
{
	if (t[*i].type == T_NUM && (t[*i].flags & TF_INT)) {
		d->kind = EK_INT;
		d->v.i = t[*i].num >> 10;
		(*i)++;
		return 1;
	}
	if ((lf & LF_AUTO) && t[*i].type == T_IDENT && ec_kw(&t[*i]) == KW_AUTO) {
		d->kind = EK_KW;
		d->v.i = KW_AUTO;
		(*i)++;
		return 1;
	}
	if (t[*i].type == T_FUNC && number(c, t, n, i, 0, d)) {
		d->kind = EK_INT;
		d->v.i = (d->v.i + 512) >> 10;
		return 1;
	}
	return 0;
}

static int valign(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *d)
{
	if (t[*i].type == T_IDENT) {
		int kw = ec_kw(&t[*i]);
		if (kw == KW_BASELINE || kw == KW_SUB || kw == KW_SUPER || kw == KW_TEXT_TOP ||
				kw == KW_MIDDLE || kw == KW_TEXT_BOTTOM || kw == KW_TOP || kw == KW_BOTTOM ||
				(kw == KW_WEBKIT_BASELINE_MIDDLE && !(c->opts & EC_COMPAT_START))) {
			d->kind = EK_KW;
			d->v.i = kw;
			(*i)++;
			return 1;
		}
		return 0;
	}
	return length(c, t, n, i, 0, d);
}

static int deco_bit(int kw)
{
	switch (kw) {
	case KW_UNDERLINE: return EC_DECO_UNDERLINE;
	case KW_OVERLINE: return EC_DECO_OVERLINE;
	case KW_LINE_THROUGH: return EC_DECO_LINE_THROUGH;
	case KW_BLINK: return EC_DECO_BLINK;
	}
	return 0;
}

/* one value for a longhand at t[*i] */
static int one(ec_ctx *c, int prop, const ec_tok *t, int n, int *i, ec_decl *d)
{
	unsigned lf = ptab[prop].flags;

	if (*i >= n)
		return 0;
	d->prop = prop;
	switch (ptab[prop].parser) {
	case VP_KW: {
		int kw = ec_kw(&t[*i]);
		if (!in_list(lf, kw))
			return 0;
		if (EC_UNLIKELY(c->opts & (EC_COMPAT_START | EC_COMPAT_KEYWORDS)) &&
				(((c->opts & EC_COMPAT_START) && prop == EP_TEXT_ALIGN &&
				  (kw == KW_START || kw == KW_END || kw == KW_MATCH_PARENT ||
				   (kw >= KW_WEBKIT_LEFT && kw <= KW_EC_CENTER_OR_INHERIT))) ||
				 ((c->opts & EC_COMPAT_KEYWORDS) && prop == EP_DISPLAY &&
				  (kw == KW_FLOW_ROOT || kw == KW_CONTENTS))))
			return 0;
		d->kind = EK_KW;
		d->v.i = kw;
		(*i)++;
		return 1;
	}
	case VP_LEN:
		return length(c, t, n, i, lf, d);
	case VP_COLOR:
		return color(t, n, i, d);
	case VP_FSIZE:
		return font_size(c, t, n, i, d);
	case VP_WEIGHT:
		return font_weight(c, t, n, i, d);
	case VP_LHEIGHT:
		return line_height(c, t, n, i, d);
	case VP_IMAGE:
		return image(c, t, n, i, d);
	case VP_BWIDTH:
		return border_width(c, t, n, i, d);
	case VP_BGPOS:
		return bg_axis(c, t, n, i, lf, d);
	case VP_INT:
		return integer(c, t, n, i, lf, d);
	case VP_NUM:
		return number(c, t, n, i, lf & LF_NN, d);
	case VP_ALPHA:
		if (t[*i].type == T_PCT) {
			d->kind = EK_NUM;
			d->v.i = t[*i].num / 100;
			(*i)++;
		} else if (!number(c, t, n, i, 0, d)) {
			return 0;
		}
		if (d->v.i < 0)
			d->v.i = 0;
		if (d->v.i > 1024)
			d->v.i = 1024;
		return 1;
	case VP_VALIGN:
		return valign(c, t, n, i, d);
	case VP_LISTTYPE: {
		int kw;
		if (t[*i].type == T_STR) {
			d->kind = EK_PTR;
			d->v.p = tok_str(c, &t[*i]);
			(*i)++;
			return d->v.p != NULL;
		}
		if (t[*i].type != T_IDENT)
			return 0;
		kw = ec_kw(&t[*i]);
		if (kw == KW_INHERIT || kw == KW_INITIAL || kw == KW_UNSET || kw == KW_REVERT ||
				kw == KW_REVERT_LAYER)
			return 0;
		/* a counter style EWeb doesn't know is decimal (CSS Counter Styles 3) */
		if ((c->opts & EC_COMPAT_KEYWORDS) && !in_list(KL_LIST_TYPE, kw))
			return 0;
		d->kind = EK_KW;
		d->v.i = in_list(KL_LIST_TYPE, kw) ? kw : KW_DECIMAL;
		(*i)++;
		return 1;
	}
	default:
		return 0;
	}
}

static int family(ec_ctx *c, const ec_tok *t, int n, ec_decl *d)
{
	const ec_str *names[32];
	int nn = 0, generic = 0, i = 0;
	ec_family *f;

	while (i < n) {
		if (t[i].type == T_STR) {
			if (nn < 32 && !generic)
				names[nn++] = tok_str(c, &t[i]);
			i++;
		} else if (t[i].type == T_IDENT) {
			int kw = ec_kw(&t[i]), j = i + 1;
			if (j == n || t[j].type == T_COMMA) {
				if (kw == KW_INHERIT || kw == KW_INITIAL || kw == KW_UNSET ||
						kw == KW_REVERT || ec_tok_is(&t[i], "default"))
					return -1;
				if (kw >= KW_SERIF && kw <= KW_EMOJI) {
					if (!generic)
						generic = kw;
					i++;
					goto next;
				}
			}
			/* identifiers in a row make one name, a space between each */
			{
				char buf[256];
				size_t len = 0;
				for (j = i; j < n && t[j].type == T_IDENT; j++) {
					size_t l;
					char tb[128];
					const char *s = ec_tok_text(&t[j], tb, sizeof tb, &l);
					if (len + l + 1 >= sizeof buf)
						return -1;
					if (len)
						buf[len++] = ' ';
					memcpy(buf + len, s, l);
					len += l;
				}
				if (nn < 32 && !generic)
					names[nn++] = ec_str_new(&c->mem, buf, len);
				i = j;
			}
		} else {
			return -1;
		}
next:
		if (i < n) {
			if (t[i].type != T_COMMA || i + 1 == n)
				return -1;
			i++;
		}
	}
	if (nn == 0 && !generic)
		return -1;
	f = ec_alloc(&c->mem, sizeof(ec_family) + nn * sizeof f->name[0]);
	if (f == NULL)
		return -1;
	f->n = nn;
	f->generic = generic;
	memcpy(f->name, names, nn * sizeof names[0]);
	d->prop = EP_FONT_FAMILY;
	d->kind = EK_PTR;
	d->v.p = f;
	return 1;
}

static int content(ec_ctx *c, const ec_tok *t, int n, ec_decl *d)
{
	ec_content *ct;
	int i, k = 0;

	d->prop = EP_CONTENT;
	if (n == 1 && t[0].type == T_IDENT) {
		int kw = ec_kw(&t[0]);
		if (kw == KW_NORMAL || kw == KW_NONE) {
			d->kind = EK_KW;
			d->v.i = kw;
			return 1;
		}
	}
	ct = ec_alloc(&c->mem, sizeof(ec_content) + n * sizeof ct->item[0]);
	if (ct == NULL)
		return -1;
	for (i = 0; i < n; i++) {
		const ec_tok *x = &t[i];
		if (is_delim(x, '/'))		/* alternative text: not shown */
			break;
		if (x->type == T_STR) {
			ct->item[k].type = EC_CONTENT_STRING;
			ct->item[k].kw = 0;
			ct->item[k++].s = tok_str(c, x);
		} else if (x->type == T_URL) {
			ct->item[k].type = EC_CONTENT_URL;
			ct->item[k].kw = 0;
			ct->item[k++].s = tok_str(c, x);
		} else if (x->type == T_IDENT) {
			int kw = ec_kw(x);
			if (kw != KW_OPEN_QUOTE && kw != KW_CLOSE_QUOTE && kw != KW_NO_OPEN_QUOTE &&
					kw != KW_NO_CLOSE_QUOTE)
				return -1;
			ct->item[k].type = EC_CONTENT_QUOTE;
			ct->item[k].kw = kw;
			ct->item[k++].s = NULL;
		} else if (x->type == T_FUNC) {
			int end = block_end(t, n, i);
			if (end < 0 || end < i + 3 || t[i + 1].type != T_IDENT)
				return -1;
			if (ec_tok_is(x, "attr"))
				ct->item[k].type = EC_CONTENT_ATTR;
			else if (ec_tok_is(x, "counter") || ec_tok_is(x, "counters"))
				ct->item[k].type = EC_CONTENT_COUNTER;
			else
				return -1;
			ct->item[k].kw = 0;
			ct->item[k++].s = tok_str(c, &t[i + 1]);
			i = end - 1;
		} else {
			return -1;
		}
	}
	if (k == 0)
		return -1;
	ct->n = k;
	d->kind = EK_PTR;
	d->v.p = ct;
	return 1;
}

/* ------------------------------------------------------------ shorthands */

static const uint8_t sh_margin[] = { EP_MARGIN_TOP, EP_MARGIN_RIGHT, EP_MARGIN_BOTTOM, EP_MARGIN_LEFT };
static const uint8_t sh_padding[] = { EP_PADDING_TOP, EP_PADDING_RIGHT, EP_PADDING_BOTTOM, EP_PADDING_LEFT };
static const uint8_t sh_inset[] = { EP_TOP, EP_RIGHT, EP_BOTTOM, EP_LEFT };
static const uint8_t sh_bwidth[] = { EP_BORDER_TOP_WIDTH, EP_BORDER_RIGHT_WIDTH,
	EP_BORDER_BOTTOM_WIDTH, EP_BORDER_LEFT_WIDTH };
static const uint8_t sh_bstyle[] = { EP_BORDER_TOP_STYLE, EP_BORDER_RIGHT_STYLE,
	EP_BORDER_BOTTOM_STYLE, EP_BORDER_LEFT_STYLE };
static const uint8_t sh_bcolor[] = { EP_BORDER_TOP_COLOR, EP_BORDER_RIGHT_COLOR,
	EP_BORDER_BOTTOM_COLOR, EP_BORDER_LEFT_COLOR };

static void put(ec_decl *d, int prop, int kind, int32_t v)
{
	d->prop = prop;
	d->kind = kind;
	d->v.i = v;
}

/* 1 to 4 values: top, right, bottom, left */
static int four(ec_ctx *c, const uint8_t *props, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl v[4];
	int nv = 0, i = 0, k;
	static const uint8_t from[4][4] = {
		{ 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 }
	};

	while (i < n) {
		if (nv == 4 || !one(c, props[0], t, n, &i, &v[nv]))
			return -1;
		nv++;
	}
	if (nv == 0)
		return -1;
	for (k = 0; k < 4; k++) {
		out[k] = v[from[nv - 1][k]];
		out[k].prop = props[k];
	}
	return 4;
}

/* border and border-*: width || style || color */
static int border(ec_ctx *c, const ec_tok *t, int n, const int *sides, int nsides, ec_decl *out)
{
	ec_decl w, s, col;
	int hw = 0, hs = 0, hc = 0, i = 0, k, o = 0;

	put(&w, 0, EK_PX, EC_FX(3));
	put(&s, 0, EK_KW, KW_NONE);
	put(&col, 0, EK_CURCOLOR, 0);
	while (i < n) {
		int j = i;
		if (!hw && one(c, EP_BORDER_TOP_WIDTH, t, n, &j, &w))
			hw = 1;
		else if (j = i, !hs && one(c, EP_BORDER_TOP_STYLE, t, n, &j, &s))
			hs = 1;
		else if (j = i, !hc && one(c, EP_BORDER_TOP_COLOR, t, n, &j, &col))
			hc = 1;
		else
			return -1;
		i = j;
	}
	for (k = 0; k < nsides; k++) {
		out[o] = w;
		out[o++].prop = sh_bwidth[sides[k]];
		out[o] = s;
		out[o++].prop = sh_bstyle[sides[k]];
		out[o] = col;
		out[o++].prop = sh_bcolor[sides[k]];
	}
	return o;
}

/* a position keyword on one axis (0 is x) as a percentage */
static int kw_axis(int kw, int axis, ec_decl *d)
{
	d->kind = EK_PCT;
	if (kw == KW_CENTER)
		d->v.i = EC_FX(50);
	else if (kw == (axis ? KW_TOP : KW_LEFT))
		d->v.i = 0;
	else if (kw == (axis ? KW_BOTTOM : KW_RIGHT))
		d->v.i = EC_FX(100);
	else
		return 0;
	return 1;
}

/* a <bg-position> of 1 to 4 parts into x and y (CSS Backgrounds 3, 3.6) */
static int position(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *x, ec_decl *y)
{
	int kws[4], np = 0, j = *i;
	ec_decl v[4];

	while (np < 4 && j < n) {
		int save = j, kw = t[j].type == T_IDENT ? ec_kw(&t[j]) : 0;
		if (kw == KW_LEFT || kw == KW_RIGHT || kw == KW_TOP || kw == KW_BOTTOM ||
				kw == KW_CENTER) {
			kws[np] = kw;
			j++;
		} else if (length(c, t, n, &j, 0, &v[np])) {
			kws[np] = 0;
		} else {
			j = save;
			break;
		}
		np++;
	}
	if (np == 0)
		return 0;
	put(x, EP_BACKGROUND_POSITION_X, EK_PCT, EC_FX(50));
	put(y, EP_BACKGROUND_POSITION_Y, EK_PCT, EC_FX(50));
	if (np == 1) {
		if (kws[0] == KW_TOP || kws[0] == KW_BOTTOM)
			kw_axis(kws[0], 1, y);
		else if (kws[0])
			kw_axis(kws[0], 0, x);
		else
			*x = v[0];
	} else if (np == 2) {
		/* x then y, unless keywords say y then x */
		int swap = kws[0] == KW_TOP || kws[0] == KW_BOTTOM || kws[1] == KW_LEFT ||
			kws[1] == KW_RIGHT;
		int a = swap, b = !swap;
		if (swap && (!kws[0] || !kws[1]))
			return 0;
		if (kws[a] ? !kw_axis(kws[a], 0, x) : (*x = v[a], 0))
			return 0;
		if (kws[b] ? !kw_axis(kws[b], 1, y) : (*y = v[b], 0))
			return 0;
	} else {
		/* 3 or 4 parts: each keyword maybe followed by an offset from that edge */
		int k, have_x = 0, have_y = 0;
		for (k = 0; k < np; k++) {
			int kw = kws[k], axis, far;
			ec_decl *dst;
			if (!kw)
				return 0;
			axis = kw == KW_TOP || kw == KW_BOTTOM;
			if (kw == KW_CENTER)
				axis = have_x;
			if (axis ? have_y : have_x)
				return 0;
			dst = axis ? y : x;
			far = kw == KW_RIGHT || kw == KW_BOTTOM;
			if (k + 1 < np && !kws[k + 1]) {
				ec_decl off = v[k + 1];
				if (kw == KW_CENTER)
					return 0;
				if (!far) {
					*dst = off;
				} else if (off.kind == EK_PCT) {
					put(dst, 0, EK_PCT, EC_FX(100) - off.v.i);
				} else if (off.kind == EK_PX) {
					ec_calcx *cx = ec_alloc(&c->mem, sizeof(ec_calcx) + sizeof cx->c[0]);
					if (cx == NULL)
						return 0;
					memset(cx, 0, sizeof(ec_calcx) + sizeof cx->c[0]);
					cx->op = EC_CALC_SUM;
					cx->n = 1;
					cx->c[0][CU_PCT] = EC_FX(100);
					cx->c[0][CU_PX] = -off.v.i;
					dst->kind = EK_CALCX;
					dst->v.p = cx;
				} else {
					return 0;
				}
				k++;
			} else {
				kw_axis(kw, axis, dst);
			}
			if (axis)
				have_y = 1;
			else
				have_x = 1;
		}
	}
	x->prop = EP_BACKGROUND_POSITION_X;
	y->prop = EP_BACKGROUND_POSITION_Y;
	*i = j;
	return 1;
}

/* background-size's first layer from t[*i]: cover | contain | a width
   and maybe a height (auto when missing) */
static int bg_size(ec_ctx *c, const ec_tok *t, int n, int *i, ec_decl *w, ec_decl *h)
{
	int j = *i, kw = t[j].type == T_IDENT ? ec_kw(&t[j]) : 0;

	put(h, EP_BACKGROUND_SIZE_H, EK_KW, KW_AUTO);
	if (kw == KW_COVER || kw == KW_CONTAIN) {
		put(w, EP_BACKGROUND_SIZE_W, EK_KW, kw);
		*i = j + 1;
		return 1;
	}
	if (!length(c, t, n, &j, LF_AUTO | LF_NN, w))
		return 0;
	if (j < n && t[j].type != T_COMMA && !length(c, t, n, &j, LF_AUTO | LF_NN, h))
		put(h, EP_BACKGROUND_SIZE_H, EK_KW, KW_AUTO);	/* one value: the height is auto */
	w->prop = EP_BACKGROUND_SIZE_W;
	h->prop = EP_BACKGROUND_SIZE_H;
	*i = j;
	return 1;
}

static int background(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl col, img, rep, px, py, sw, sh, clip;
	int i = 0, layer = 0;

	put(&col, EP_BACKGROUND_COLOR, EK_COLOR, 0);
	put(&img, EP_BACKGROUND_IMAGE, EK_KW, KW_NONE);
	put(&rep, EP_BACKGROUND_REPEAT, EK_KW, KW_REPEAT);
	put(&px, EP_BACKGROUND_POSITION_X, EK_PCT, 0);
	put(&py, EP_BACKGROUND_POSITION_Y, EK_PCT, 0);
	put(&sw, EP_BACKGROUND_SIZE_W, EK_KW, KW_AUTO);
	put(&sh, EP_BACKGROUND_SIZE_H, EK_KW, KW_AUTO);
	put(&clip, EP_BACKGROUND_CLIP, EK_KW, KW_BORDER_BOX);
	while (i <= n) {
		int hcol = 0, himg = 0, hrep = 0, hpos = 0, hatt = 0, hbox = 0, hsize = 0;
		int r1 = 0, r2 = 0, box = 0;
		ec_decl lcol, limg, lx, ly, lw, lh;
		while (i < n && t[i].type != T_COMMA) {
			int j = i, kw = t[i].type == T_IDENT ? ec_kw(&t[i]) : 0;
			if (!himg && image(c, t, n, &j, &limg)) {
				himg = 1;
			} else if (!hrep && (kw == KW_REPEAT || kw == KW_REPEAT_X || kw == KW_REPEAT_Y ||
					kw == KW_NO_REPEAT || kw == KW_SPACE || kw == KW_ROUND)) {
				hrep = 1;
				r1 = kw;
				j++;
				if (kw != KW_REPEAT_X && kw != KW_REPEAT_Y && j < n &&
						t[j].type == T_IDENT) {
					int k2 = ec_kw(&t[j]);
					if (k2 == KW_REPEAT || k2 == KW_NO_REPEAT || k2 == KW_SPACE ||
							k2 == KW_ROUND) {
						r2 = k2;
						j++;
					}
				}
			} else if (!hatt && (kw == KW_SCROLL || kw == KW_FIXED || kw == KW_LOCAL)) {
				hatt = 1;
				j++;
			} else if (hbox < 2 && (kw == KW_BORDER_BOX || kw == KW_PADDING_BOX ||
					kw == KW_CONTENT_BOX || kw == KW_TEXT)) {
				/* one: origin and clip; two: origin, then clip */
				hbox++;
				box = kw;
				j++;
			} else if (!hpos && position(c, t, n, &j, &lx, &ly)) {
				hpos = 1;
				if (j < n && is_delim(&t[j], '/')) {
					j++;
					if (j >= n || !bg_size(c, t, n, &j, &lw, &lh))
						return -1;
					hsize = 1;
				}
			} else if (!hcol && color(t, n, &j, &lcol)) {
				hcol = 1;
			} else {
				return -1;
			}
			i = j;
		}
		if (hcol) {
			if (i < n)		/* a colour only in the last layer */
				return -1;
			col = lcol;
			col.prop = EP_BACKGROUND_COLOR;
		}
		if (layer == 0) {
			if (himg) {
				img = limg;
				img.prop = EP_BACKGROUND_IMAGE;
			}
			if (hrep) {
				int x = r1, y = r2 ? r2 : r1;
				if (r1 == KW_REPEAT_X || r1 == KW_REPEAT_Y)
					put(&rep, EP_BACKGROUND_REPEAT, EK_KW, r1);
				else {
					int xr = x != KW_NO_REPEAT, yr = y != KW_NO_REPEAT;
					put(&rep, EP_BACKGROUND_REPEAT, EK_KW,
						xr && yr ? KW_REPEAT : xr ? KW_REPEAT_X :
						yr ? KW_REPEAT_Y : KW_NO_REPEAT);
				}
			}
			if (hpos) {
				px = lx;
				py = ly;
			}
			if (hsize) {
				sw = lw;
				sh = lh;
			}
			if (hbox)
				put(&clip, EP_BACKGROUND_CLIP, EK_KW, box);
		}
		if (i >= n)
			break;
		i++;			/* the comma */
		layer++;
		if (i >= n)
			return -1;
	}
	out[0] = col;
	out[1] = img;
	out[2] = rep;
	out[3] = px;
	out[4] = py;
	out[5] = sw;
	out[6] = sh;
	out[7] = clip;
	return 8;
}

/* -webkit-text-stroke: <line-width> || <color> */
static int text_stroke(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl w, col;
	int i = 0, hw = 0, hc = 0;

	put(&w, EP_TEXT_STROKE_WIDTH, EK_PX, 0);
	put(&col, EP_TEXT_STROKE_COLOR, EK_CURCOLOR, 0);
	while (i < n) {
		int j = i;
		ec_decl v;
		if (!hw && one(c, EP_TEXT_STROKE_WIDTH, t, n, &j, &v)) {
			hw = 1;
			w = v;
		} else if (j = i, !hc && one(c, EP_TEXT_STROKE_COLOR, t, n, &j, &v)) {
			hc = 1;
			col = v;
		} else {
			return -1;
		}
		i = j;
	}
	out[0] = w;
	out[1] = col;
	return 2;
}

/* text-shadow: none, or shadows ([<color>? && <length>{2,3}]#). EWeb
   draws no shadows; it keeps the first one's colour (currentcolor when
   it has none), as text whose own colour is transparent is shown in it */
static int text_shadow(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl first;
	int i = 0, k = 0;

	put(&first, EP_TEXT_SHADOW, EK_CURCOLOR, 0);
	if (n == 1 && ec_kw(&t[0]) == KW_NONE) {
		put(out, EP_TEXT_SHADOW, EK_KW, KW_NONE);
		return 1;
	}
	while (i < n) {
		ec_decl col, len;
		int nlen = 0, hcol = 0;
		put(&col, EP_TEXT_SHADOW, EK_CURCOLOR, 0);
		while (i < n && t[i].type != T_COMMA) {
			int j = i;
			/* the colour before the lengths or after them */
			if (!hcol && (nlen == 0 || nlen >= 2) && color(t, n, &j, &col))
				hcol = 1;
			else if (j = i, nlen < 3 && length(c, t, n, &j, LF_NOPCT, &len) &&
				 !(nlen == 2 && len.kind == EK_PX && len.v.i < 0))
				nlen++;
			else
				return -1;
			i = j;
		}
		if (nlen < 2)
			return -1;
		if (k++ == 0)
			first = col;
		if (i < n && ++i >= n)
			return -1;	/* a comma at the end */
	}
	*out = first;
	out->prop = EP_TEXT_SHADOW;
	return 1;
}

/* animation-name: none, or names (identifiers or strings), a comma
   between each; and the animation shorthand's names (in each animation
   the identifier that is none of its other parts' keywords). EWeb runs
   no animations: it keeps whether there is one (EK_INT 1) or none, as
   the layout draws an animated element as its animation leaves it
   (DESIGN.md, Translucency) */
static int animation(const ec_tok *t, int n, int shorthand, ec_decl *out)
{
	static const char *const parts[] = {
		"ease", "linear", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end",
		"infinite", "normal", "reverse", "alternate", "alternate-reverse", "forwards",
		"backwards", "both", "running", "paused", "none"
	};
	int i = 0, named = 0;

	while (i < n) {
		int name = 0, k = 0;
		while (i < n && t[i].type != T_COMMA) {
			const ec_tok *a = &t[i];
			size_t w = 0;
			if (a->type == T_IDENT) {
				int kw = ec_kw(a);
				if (kw == KW_INHERIT || kw == KW_INITIAL || kw == KW_UNSET || kw == KW_REVERT ||
						kw == KW_REVERT_LAYER || ec_tok_is(a, "default"))
					return -1;
				for (; w < sizeof parts / sizeof parts[0] && !ec_tok_is(a, parts[w]); w++)
					;
				if (!shorthand && w < sizeof parts / sizeof parts[0] - 1)
					w = sizeof parts / sizeof parts[0];	/* a name, in the longhand */
			} else if (a->type != T_STR) {
				int e = a->type == T_FUNC ? block_end(t, n, i) : i + 1;
				if (!shorthand || e < 0 || !(a->type == T_FUNC || a->type == T_DIM ||
							     a->type == T_NUM))
					return -1;
				i = e;
				k++;
				continue;
			}
			if (w == sizeof parts / sizeof parts[0]) {	/* a name */
				if (name)
					return -1;
				name = 1;
			}
			i++;
			k++;
		}
		if (k == 0 || (!shorthand && k != 1))
			return -1;
		named |= name;
		if (i < n && ++i >= n)
			return -1;
	}
	put(out, EP_ANIMATION_NAME, named ? EK_INT : EK_KW, named ? 1 : KW_NONE);
	return 1;
}

static int font(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl style, variant, weight, size, lh, fam;
	int i = 0, k, nprefix = 0, hs = 0, hv = 0, hw = 0;

	if (n == 1 && t[0].type == T_IDENT) {
		int kw = ec_kw(&t[0]);
		if (kw >= KW_CAPTION && kw <= KW_STATUS_BAR) {
			static const ec_family sys = { 0, KW_SANS_SERIF };
			put(&out[0], EP_FONT_STYLE, EK_KW, KW_NORMAL);
			put(&out[1], EP_FONT_VARIANT, EK_KW, KW_NORMAL);
			put(&out[2], EP_FONT_WEIGHT, EK_INT, 400);
			put(&out[3], EP_FONT_SIZE, EK_KW, KW_MEDIUM);
			put(&out[4], EP_LINE_HEIGHT, EK_KW, KW_NORMAL);
			out[5].prop = EP_FONT_FAMILY;
			out[5].kind = EK_PTR;
			out[5].v.p = &sys;
			return 6;
		}
	}
	put(&style, EP_FONT_STYLE, EK_KW, KW_NORMAL);
	put(&variant, EP_FONT_VARIANT, EK_KW, KW_NORMAL);
	put(&weight, EP_FONT_WEIGHT, EK_INT, 400);
	put(&lh, EP_LINE_HEIGHT, EK_KW, KW_NORMAL);
	/* style, variant, weight and stretch, in any order, then the size */
	for (; i < n && nprefix < 4; nprefix++) {
		int kw = t[i].type == T_IDENT ? ec_kw(&t[i]) : 0, j = i;
		if (kw == KW_NORMAL) {
			i++;
		} else if (!hs && (kw == KW_ITALIC || kw == KW_OBLIQUE)) {
			put(&style, EP_FONT_STYLE, EK_KW, kw);
			hs = 1;
			i++;
			if (kw == KW_OBLIQUE && i < n && t[i].type == T_DIM)
				i++;		/* an angle */
		} else if (!hv && kw == KW_SMALL_CAPS) {
			put(&variant, EP_FONT_VARIANT, EK_KW, kw);
			hv = 1;
			i++;
		} else if (kw >= KW_CONDENSED && kw <= KW_ULTRA_EXPANDED) {
			i++;			/* font-stretch: not kept */
		} else if (!hw && (kw == KW_BOLD || kw == KW_BOLDER || kw == KW_LIGHTER ||
				(t[i].type == T_NUM && font_weight(c, t, n, &j, &weight)))) {
			if (kw)
				font_weight(c, t, n, &j, &weight);
			weight.prop = EP_FONT_WEIGHT;
			hw = 1;
			i = j;
		} else {
			break;
		}
	}
	if (i >= n || !font_size(c, t, n, &i, &size))
		return -1;
	size.prop = EP_FONT_SIZE;
	if (i < n && is_delim(&t[i], '/')) {
		i++;
		if (i >= n || !line_height(c, t, n, &i, &lh))
			return -1;
		lh.prop = EP_LINE_HEIGHT;
	}
	if (i >= n || family(c, t + i, n - i, &fam) != 1)
		return -1;
	k = 0;
	out[k++] = style;
	out[k++] = variant;
	out[k++] = weight;
	out[k++] = size;
	out[k++] = lh;
	out[k++] = fam;
	return k;
}

static int list_style(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl type, pos, img;
	int i = 0, ht = 0, hp = 0, hi = 0, nones = 0;

	put(&type, EP_LIST_STYLE_TYPE, EK_KW, KW_DISC);
	put(&pos, EP_LIST_STYLE_POSITION, EK_KW, KW_OUTSIDE);
	put(&img, EP_LIST_STYLE_IMAGE, EK_KW, KW_NONE);
	while (i < n) {
		int j = i, kw = t[i].type == T_IDENT ? ec_kw(&t[i]) : 0;
		if (kw == KW_NONE) {
			nones++;
			i++;
			continue;
		}
		if (!hp && (kw == KW_INSIDE || kw == KW_OUTSIDE)) {
			put(&pos, EP_LIST_STYLE_POSITION, EK_KW, kw);
			hp = 1;
			i++;
		} else if (!hi && image(c, t, n, &j, &img)) {
			img.prop = EP_LIST_STYLE_IMAGE;
			hi = 1;
			i = j;
		} else if (!ht && one(c, EP_LIST_STYLE_TYPE, t, n, &j, &type)) {
			ht = 1;
			i = j;
		} else {
			return -1;
		}
	}
	if (nones > 2 || nones + ht + hi > 2)
		return -1;
	if (nones && !ht)
		put(&type, EP_LIST_STYLE_TYPE, EK_KW, KW_NONE);
	out[0] = type;
	out[1] = pos;
	out[2] = img;
	return 3;
}

static int flex(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	ec_decl grow, shrink, basis;
	int i = 0, hg = 0, hb = 0;

	put(&grow, EP_FLEX_GROW, EK_NUM, EC_FX(1));
	put(&shrink, EP_FLEX_SHRINK, EK_NUM, EC_FX(1));
	put(&basis, EP_FLEX_BASIS, EK_PCT, 0);
	if (n == 1 && t[0].type == T_IDENT) {
		int kw = ec_kw(&t[0]);
		if (kw == KW_NONE || kw == KW_AUTO) {
			put(&out[0], EP_FLEX_GROW, EK_NUM, kw == KW_NONE ? 0 : EC_FX(1));
			put(&out[1], EP_FLEX_SHRINK, EK_NUM, kw == KW_NONE ? 0 : EC_FX(1));
			put(&out[2], EP_FLEX_BASIS, EK_KW, KW_AUTO);
			return 3;
		}
	}
	while (i < n) {
		int j = i;
		if (!hg && number(c, t, n, &j, 1, &grow)) {
			hg = 1;
			grow.prop = EP_FLEX_GROW;
			i = j;
			if (i < n && number(c, t, n, &j, 1, &shrink)) {
				shrink.prop = EP_FLEX_SHRINK;
				i = j;
			}
		} else if (j = i, !hb && one(c, EP_FLEX_BASIS, t, n, &j, &basis)) {
			hb = 1;
			i = j;
		} else {
			return -1;
		}
	}
	out[0] = grow;
	out[1] = shrink;
	out[2] = basis;
	return 3;
}

static int pair(ec_ctx *c, int p1, int p2, int any_order, const ec_tok *t, int n, ec_decl *out)
{
	int i = 0;

	if (any_order) {
		/* a || b, each defaulting to its initial value */
		int h1 = 0, h2 = 0;
		ec_decl a, b;
		while (i < n) {
			int j = i;
			if (!h1 && one(c, p1, t, n, &j, &a))
				h1 = 1;
			else if (j = i, !h2 && one(c, p2, t, n, &j, &b))
				h2 = 1;
			else
				return -1;
			i = j;
		}
		if (!h1 && !h2)
			return -1;
		if (!h1)
			put(&a, p1, p1 == EP_FLEX_DIRECTION ? EK_KW : EK_KW,
				p1 == EP_FLEX_DIRECTION ? KW_ROW : KW_NORMAL);
		if (!h2)
			put(&b, p2, EK_KW, p2 == EP_FLEX_WRAP ? KW_NOWRAP : KW_NORMAL);
		out[0] = a;
		out[1] = b;
		return 2;
	}
	/* a [b]: b is a when missing */
	if (!one(c, p1, t, n, &i, &out[0]))
		return -1;
	if (i < n) {
		if (!one(c, p2, t, n, &i, &out[1]) || i != n)
			return -1;
	} else {
		out[1] = out[0];
	}
	out[0].prop = p1;
	out[1].prop = p2;
	return 2;
}

static int text_decoration(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	int i = 0, bits = 0, none = 0, hs = 0, hc = 0, ht = 0;

	while (i < n) {
		int kw = t[i].type == T_IDENT ? ec_kw(&t[i]) : 0, j = i;
		ec_decl tmp;
		if (kw == KW_NONE && !none && !bits) {
			none = 1;
			i++;
		} else if (deco_bit(kw) && !none && !(bits & deco_bit(kw))) {
			bits |= deco_bit(kw);
			i++;
		} else if (c->opts & EC_COMPAT_DECO) {
			return -1;
		} else if (!hs && (kw == KW_SOLID || kw == KW_DOUBLE || kw == KW_DOTTED ||
				kw == KW_DASHED || kw == KW_WAVY)) {
			hs = 1;
			i++;
		} else if (!ht && (kw == KW_AUTO || kw == KW_FROM_FONT)) {
			ht = 1;
			i++;
		} else if (!hc && color(t, n, &j, &tmp)) {
			hc = 1;
			i = j;
		} else if (!ht && length(c, t, n, &j, 0, &tmp)) {
			ht = 1;
			i = j;
		} else {
			return -1;
		}
	}
	put(&out[0], EP_TEXT_DECORATION_LINE, EK_INT, bits);
	return 1;
}

/* ------------------------------------------------------------ grid (CSS Grid 1) */

/* a track size at t[*i]: a length, a percentage, fr, auto, min-content
   or max-content (calc() is not taken) */
static int tsize(ec_ctx *c, const ec_tok *t, int n, int *i, ec_tsize *z)
{
	const ec_tok *k;
	ec_decl d;

	if (*i >= n)
		return 0;
	k = &t[*i];
	z->rel = 0;
	z->v = 0;
	if (k->type == T_DIM && k->ulen == 2 && (k->u[0] | 32) == 'f' && (k->u[1] | 32) == 'r') {
		if (k->num < 0)
			return 0;
		z->k = EC_TR_FR;
		z->v = k->num;
		(*i)++;
		return 1;
	}
	if (k->type == T_IDENT) {
		int kw = ec_kw(k);
		if (kw == KW_AUTO)
			z->k = EC_TR_AUTO;
		else if (kw == KW_MIN_CONTENT)
			z->k = EC_TR_MIN;
		else if (kw == KW_MAX_CONTENT)
			z->k = EC_TR_MAX;
		else
			return 0;
		(*i)++;
		return 1;
	}
	if (!length(c, t, n, i, LF_NN, &d))
		return 0;
	if (d.kind == EK_PCT || d.kind == EK_PX)
		z->k = d.kind == EK_PCT ? EC_TR_PCT : EC_TR_LEN;
	else if (d.kind >= EK_EM && d.kind <= EK_VMAX)
		z->k = EC_TR_LEN, z->rel = d.kind;
	else
		return 0;
	z->v = d.v.i;
	return 1;
}

/* a track: a size, minmax(min, max) or fit-content(length) */
static int track(ec_ctx *c, const ec_tok *t, int n, int *i, ec_tsize *mn, ec_tsize *mx)
{
	if (*i < n && t[*i].type == T_FUNC) {
		int end = block_end(t, n, *i), j = *i + 1;
		if (end < 0)
			return 0;
		if (ec_tok_is(&t[*i], "minmax")) {
			if (!tsize(c, t, end - 1, &j, mn) || mn->k == EC_TR_FR || j >= end - 1 || t[j].type != T_COMMA)
				return 0;
			j++;
			if (!tsize(c, t, end - 1, &j, mx) || j != end - 1)
				return 0;
		} else if (ec_tok_is(&t[*i], "fit-content")) {
			if (!tsize(c, t, end - 1, &j, mn) || j != end - 1 || mn->k != EC_TR_LEN)
				return 0;
			mn->k = EC_TR_FIT;
			*mx = *mn;
		} else {
			return 0;
		}
		*i = end;
		return 1;
	}
	if (!tsize(c, t, n, i, mn))
		return 0;
	*mx = *mn;
	return 1;
}

/* line names ([a b]) are read past: EWeb places by numbers and areas */
static int line_names(const ec_tok *t, int n, int i)
{
	while (i < n && t[i].type != T_RBRACK)
		i++;
	return i < n ? i + 1 : -1;
}

#define EC_MAXTRACKS 256

/* grid-template-columns and -rows (none, or a track list with repeat()),
   grid-auto-columns and -rows (sizes only: the flag) */
static int tracks(ec_ctx *c, int prop, const ec_tok *t, int n, ec_decl *d)
{
	struct { ec_tsize mn, mx; } tmp[EC_MAXTRACKS];
	int i = 0, nt = 0, rep = 0, rep_at = 0, rep_n = 0, sizes_only = ptab[prop].flags, k;
	ec_tracks *r;

	d->prop = prop;
	if (!sizes_only && n == 1 && t[0].type == T_IDENT && ec_kw(&t[0]) == KW_NONE) {
		d->kind = EK_KW;
		d->v.i = KW_NONE;
		return 1;
	}
	while (i < n) {
		if (t[i].type == T_LBRACK && !sizes_only) {
			if ((i = line_names(t, n, i)) < 0)
				return -1;
			continue;
		}
		if (t[i].type == T_FUNC && ec_tok_is(&t[i], "repeat") && !sizes_only) {
			int end = block_end(t, n, i), j = i + 1, count = 1, k0 = nt, m, q;
			if (end < 0 || j >= end - 1)
				return -1;
			if (t[j].type == T_IDENT && (ec_kw(&t[j]) == KW_AUTO_FILL || ec_kw(&t[j]) == KW_AUTO_FIT)) {
				if (rep)
					return -1;
				rep = ec_kw(&t[j]);
			} else if (t[j].type == T_NUM && (t[j].flags & TF_INT) && t[j].num >= 1024) {
				count = t[j].num >> 10;
			} else {
				return -1;
			}
			if (++j >= end - 1 || t[j].type != T_COMMA)
				return -1;
			j++;
			while (j < end - 1) {
				if (t[j].type == T_LBRACK) {
					if ((j = line_names(t, end - 1, j)) < 0)
						return -1;
					continue;
				}
				if (nt >= EC_MAXTRACKS || !track(c, t, end - 1, &j, &tmp[nt].mn, &tmp[nt].mx))
					return -1;
				nt++;
			}
			if (nt == k0)
				return -1;
			m = nt - k0;
			if (rep && t[i + 1].type == T_IDENT) {
				rep_at = k0;
				rep_n = m;
			} else {
				for (k = 1; k < count && nt + m <= EC_MAXTRACKS; k++)
					for (q = 0; q < m; q++)
						tmp[nt++] = tmp[k0 + q];
			}
			i = end;
			continue;
		}
		if (nt >= EC_MAXTRACKS || !track(c, t, n, &i, &tmp[nt].mn, &tmp[nt].mx))
			return -1;
		nt++;
	}
	if (nt == 0)
		return -1;
	r = ec_alloc(&c->mem, sizeof *r + (size_t) nt * sizeof r->t[0]);
	if (r == NULL)
		return -1;
	r->n = (uint16_t) nt;
	r->rep = (uint8_t) (rep ? rep - KW_AUTO_FILL + 1 : 0);
	r->rep_at = (uint16_t) rep_at;
	r->rep_n = (uint16_t) rep_n;
	r->rel = 0;
	for (k = 0; k < nt; k++) {
		r->t[k].mn = tmp[k].mn;
		r->t[k].mx = tmp[k].mx;
		r->rel |= tmp[k].mn.rel | tmp[k].mx.rel;
	}
	d->kind = EK_PTR;
	d->v.p = r;
	return 1;
}

/* grid-template-areas: none, or a string a row, each cell a name or "."
   (one or more), every row as many */
static int areas(ec_ctx *c, const ec_tok *t, int n, ec_decl *d)
{
	char buf[1024];
	const ec_str *name[1024];
	int k, cols = 0, nn = 0;
	ec_areas *a;

	d->prop = EP_GRID_TEMPLATE_AREAS;
	if (n == 1 && t[0].type == T_IDENT && ec_kw(&t[0]) == KW_NONE) {
		d->kind = EK_KW;
		d->v.i = KW_NONE;
		return 1;
	}
	for (k = 0; k < n; k++) {
		size_t len, p = 0;
		const char *s;
		int row = 0;
		if (t[k].type != T_STR)
			return -1;
		s = ec_tok_text(&t[k], buf, sizeof buf, &len);
		while (p < len) {
			size_t q;
			if (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r' || s[p] == '\f') {
				p++;
				continue;
			}
			q = p;
			if (s[p] == '.') {
				while (q < len && s[q] == '.')
					q++;
			} else {
				while (q < len && s[q] != ' ' && s[q] != '\t' && s[q] != '\n' && s[q] != '\r' &&
				       s[q] != '\f' && s[q] != '.')
					q++;
			}
			if (nn >= 1024)
				return -1;
			name[nn] = s[p] == '.' ? NULL : ec_str_new(&c->mem, s + p, q - p);
			if (s[p] != '.' && name[nn] == NULL)
				return -1;
			nn++;
			row++;
			p = q;
		}
		if (row == 0 || (k > 0 && row != cols))
			return -1;
		cols = row;
	}
	if (n == 0 || n > 1000)
		return -1;
	a = ec_alloc(&c->mem, sizeof *a + (size_t) nn * sizeof a->name[0]);
	if (a == NULL)
		return -1;
	a->rows = (uint16_t) n;
	a->cols = (uint16_t) cols;
	memcpy(a->name, name, (size_t) nn * sizeof a->name[0]);
	d->kind = EK_PTR;
	d->v.p = a;
	return 1;
}

/* grid-auto-flow: row or column, and dense */
static int autoflow(const ec_tok *t, int n, ec_decl *d)
{
	int k, bits = 0, dir = 0, dense = 0;

	for (k = 0; k < n; k++) {
		int kw = t[k].type == T_IDENT ? ec_kw(&t[k]) : 0;
		if ((kw == KW_ROW || kw == KW_COLUMN) && !dir) {
			dir = 1;
			if (kw == KW_COLUMN)
				bits |= EC_FLOW_COLUMN;
		} else if (kw == KW_DENSE && !dense) {
			dense = 1;
			bits |= EC_FLOW_DENSE;
		} else {
			return -1;
		}
	}
	if (n == 0)
		return -1;
	put(d, EP_GRID_AUTO_FLOW, EK_INT, bits);
	return 1;
}

/* a grid line, t[0..n): auto, an integer, span and an integer, or a name */
static int gridline(ec_ctx *c, int prop, const ec_tok *t, int n, ec_decl *d)
{
	const ec_tok *name = NULL;
	int k, span = 0, num = 0;

	d->prop = prop;
	if (n == 1 && t[0].type == T_IDENT && ec_kw(&t[0]) == KW_AUTO) {
		d->kind = EK_KW;
		d->v.i = KW_AUTO;
		return 1;
	}
	for (k = 0; k < n; k++) {
		int kw = t[k].type == T_IDENT ? ec_kw(&t[k]) : -1;
		if (kw == KW_SPAN && !span)
			span = 1;
		else if (t[k].type == T_NUM && (t[k].flags & TF_INT) && t[k].num != 0 && !num)
			num = t[k].num / 1024;
		else if (t[k].type == T_IDENT && kw != KW_AUTO && kw != KW_SPAN && kw != KW_INHERIT &&
			 kw != KW_INITIAL && kw != KW_UNSET && !name)
			name = &t[k];
		else
			return 0;
	}
	if (span) {
		/* span and a name: not taken (EWeb doesn't name lines) */
		if (num <= 0 || name)
			return 0;
		d->kind = EK_INT;
		d->v.i = EC_GRID_SPAN + (num > 1000 ? 1000 : num);
		return 1;
	}
	if (name) {
		if (num)
			return 0;
		d->kind = EK_PTR;
		d->v.p = tok_str(c, name);
		return d->v.p != NULL;
	}
	if (num == 0)
		return 0;
	d->kind = EK_INT;
	d->v.i = num > 1000 ? 1000 : num < -1000 ? -1000 : num;
	return 1;
}

/* the parts of a value between /s: their starts in at[], n of them */
static int slashes(const ec_tok *t, int n, int *at, int max)
{
	int k, np = 1;

	at[0] = 0;
	for (k = 0; k < n; k++)
		if (is_delim(&t[k], '/')) {
			if (np == max)
				return -1;
			at[np++] = k + 1;
		}
	at[np] = n + 1;
	return np;
}

/* grid-row, grid-column: start [/ end]; grid-area: row-start [/
   column-start [/ row-end [/ column-end]]], a missing one the name
   before it (when it is one) or auto */
static int grid_lines(ec_ctx *c, const uint8_t *p, int max, const ec_tok *t, int n, ec_decl *out)
{
	int at[5], np = slashes(t, n, at, max), k;

	if (np < 0)
		return -1;
	for (k = 0; k < np; k++)
		if (at[k + 1] - 1 - at[k] < 1 ||
		    gridline(c, p[k], t + at[k], at[k + 1] - 1 - at[k], &out[k]) != 1)
			return -1;
	for (; k < max; k++) {
		/* the start's name for the end; for grid-area's second its first's */
		int from = max == 4 ? (k == 1 ? 0 : k - 2) : 0;
		if (out[from].kind == EK_PTR)
			out[k] = out[from];
		else
			put(&out[k], p[k], EK_KW, KW_AUTO);
		out[k].prop = p[k];
	}
	return max;
}

/* grid-template: none, or rows / columns */
static int grid_template(ec_ctx *c, const ec_tok *t, int n, ec_decl *out)
{
	int at[3], np;

	if (n == 1 && t[0].type == T_IDENT && ec_kw(&t[0]) == KW_NONE) {
		put(&out[0], EP_GRID_TEMPLATE_ROWS, EK_KW, KW_NONE);
		put(&out[1], EP_GRID_TEMPLATE_COLUMNS, EK_KW, KW_NONE);
		put(&out[2], EP_GRID_TEMPLATE_AREAS, EK_KW, KW_NONE);
		return 3;
	}
	np = slashes(t, n, at, 2);
	if (np != 2 || tracks(c, EP_GRID_TEMPLATE_ROWS, t, at[1] - 1, &out[0]) != 1 ||
	    tracks(c, EP_GRID_TEMPLATE_COLUMNS, t + at[1], n - at[1], &out[1]) != 1)
		return -1;
	put(&out[2], EP_GRID_TEMPLATE_AREAS, EK_KW, KW_NONE);
	return 3;
}

static const uint8_t sh_grid_row[2] = { EP_GRID_ROW_START, EP_GRID_ROW_END };
static const uint8_t sh_grid_col[2] = { EP_GRID_COLUMN_START, EP_GRID_COLUMN_END };
static const uint8_t sh_grid_area[4] = { EP_GRID_ROW_START, EP_GRID_COLUMN_START, EP_GRID_ROW_END,
					 EP_GRID_COLUMN_END };

int ec_shorthand_props(int sh, uint8_t *p)
{
	int k;

	switch (sh) {
	case ES_MARGIN: memcpy(p, sh_margin, 4); return 4;
	case ES_PADDING: memcpy(p, sh_padding, 4); return 4;
	case ES_INSET: memcpy(p, sh_inset, 4); return 4;
	case ES_BORDER_WIDTH: memcpy(p, sh_bwidth, 4); return 4;
	case ES_BORDER_STYLE: memcpy(p, sh_bstyle, 4); return 4;
	case ES_BORDER_COLOR: memcpy(p, sh_bcolor, 4); return 4;
	case ES_BORDER:
		for (k = 0; k < 4; k++) {
			p[k * 3] = sh_bwidth[k];
			p[k * 3 + 1] = sh_bstyle[k];
			p[k * 3 + 2] = sh_bcolor[k];
		}
		return 12;
	case ES_BORDER_TOP: case ES_BORDER_RIGHT: case ES_BORDER_BOTTOM: case ES_BORDER_LEFT:
		k = sh - ES_BORDER_TOP;
		p[0] = sh_bwidth[k];
		p[1] = sh_bstyle[k];
		p[2] = sh_bcolor[k];
		return 3;
	case ES_BACKGROUND:
		p[0] = EP_BACKGROUND_COLOR; p[1] = EP_BACKGROUND_IMAGE; p[2] = EP_BACKGROUND_REPEAT;
		p[3] = EP_BACKGROUND_POSITION_X; p[4] = EP_BACKGROUND_POSITION_Y;
		p[5] = EP_BACKGROUND_SIZE_W; p[6] = EP_BACKGROUND_SIZE_H; p[7] = EP_BACKGROUND_CLIP;
		return 8;
	case ES_BACKGROUND_POSITION:
		p[0] = EP_BACKGROUND_POSITION_X; p[1] = EP_BACKGROUND_POSITION_Y;
		return 2;
	case ES_BACKGROUND_SIZE:
		p[0] = EP_BACKGROUND_SIZE_W; p[1] = EP_BACKGROUND_SIZE_H;
		return 2;
	case ES_FONT:
		p[0] = EP_FONT_STYLE; p[1] = EP_FONT_VARIANT; p[2] = EP_FONT_WEIGHT;
		p[3] = EP_FONT_SIZE; p[4] = EP_LINE_HEIGHT; p[5] = EP_FONT_FAMILY;
		return 6;
	case ES_LIST_STYLE:
		p[0] = EP_LIST_STYLE_TYPE; p[1] = EP_LIST_STYLE_POSITION; p[2] = EP_LIST_STYLE_IMAGE;
		return 3;
	case ES_FLEX:
		p[0] = EP_FLEX_GROW; p[1] = EP_FLEX_SHRINK; p[2] = EP_FLEX_BASIS;
		return 3;
	case ES_FLEX_FLOW:
		p[0] = EP_FLEX_DIRECTION; p[1] = EP_FLEX_WRAP;
		return 2;
	case ES_GAP: case ES_GRID_GAP:
		p[0] = EP_ROW_GAP; p[1] = EP_COLUMN_GAP;
		return 2;
	case ES_OVERFLOW:
		p[0] = EP_OVERFLOW_X; p[1] = EP_OVERFLOW_Y;
		return 2;
	case ES_TEXT_DECORATION:
		p[0] = EP_TEXT_DECORATION_LINE;
		return 1;
	case ES_BORDER_SPACING:
		p[0] = EP_BORDER_SPACING_H; p[1] = EP_BORDER_SPACING_V;
		return 2;
	case ES_WORD_WRAP:
		p[0] = EP_OVERFLOW_WRAP;
		return 1;
	case ES_GRID_ROW: memcpy(p, sh_grid_row, 2); return 2;
	case ES_GRID_COLUMN: memcpy(p, sh_grid_col, 2); return 2;
	case ES_GRID_AREA: memcpy(p, sh_grid_area, 4); return 4;
	case ES_GRID_TEMPLATE:
		p[0] = EP_GRID_TEMPLATE_ROWS; p[1] = EP_GRID_TEMPLATE_COLUMNS; p[2] = EP_GRID_TEMPLATE_AREAS;
		return 3;
	case ES_WEBKIT_BACKGROUND_CLIP:
		p[0] = EP_BACKGROUND_CLIP;
		return 1;
	case ES_WEBKIT_TEXT_STROKE:
		p[0] = EP_TEXT_STROKE_WIDTH; p[1] = EP_TEXT_STROKE_COLOR;
		return 2;
	case ES_ANIMATION:
		p[0] = EP_ANIMATION_NAME;
		return 1;
	}
	return 0;
}

static int shorthand(ec_ctx *c, int sh, const ec_tok *t, int n, ec_decl *out)
{
	static const int all[4] = { 0, 1, 2, 3 };
	int side;

	switch (sh) {
	case ES_MARGIN: return four(c, sh_margin, t, n, out);
	case ES_PADDING: return four(c, sh_padding, t, n, out);
	case ES_INSET: return four(c, sh_inset, t, n, out);
	case ES_BORDER_WIDTH: return four(c, sh_bwidth, t, n, out);
	case ES_BORDER_STYLE: return four(c, sh_bstyle, t, n, out);
	case ES_BORDER_COLOR: return four(c, sh_bcolor, t, n, out);
	case ES_BORDER: return border(c, t, n, all, 4, out);
	case ES_BORDER_TOP: case ES_BORDER_RIGHT: case ES_BORDER_BOTTOM: case ES_BORDER_LEFT:
		side = sh - ES_BORDER_TOP;
		return border(c, t, n, &all[side], 1, out);
	case ES_BACKGROUND: return background(c, t, n, out);
	case ES_BACKGROUND_POSITION: {
		int i = 0;
		/* the first layer only */
		if (!position(c, t, n, &i, &out[0], &out[1]) || (i < n && t[i].type != T_COMMA))
			return -1;
		return 2;
	}
	case ES_BACKGROUND_SIZE: {
		int i = 0;
		/* the first layer only */
		if (!bg_size(c, t, n, &i, &out[0], &out[1]) || (i < n && t[i].type != T_COMMA))
			return -1;
		return 2;
	}
	case ES_FONT: return font(c, t, n, out);
	case ES_LIST_STYLE: return list_style(c, t, n, out);
	case ES_FLEX: return flex(c, t, n, out);
	case ES_FLEX_FLOW: return pair(c, EP_FLEX_DIRECTION, EP_FLEX_WRAP, 1, t, n, out);
	case ES_GAP: case ES_GRID_GAP: return pair(c, EP_ROW_GAP, EP_COLUMN_GAP, 0, t, n, out);
	case ES_OVERFLOW: return pair(c, EP_OVERFLOW_X, EP_OVERFLOW_Y, 0, t, n, out);
	case ES_TEXT_DECORATION: return text_decoration(c, t, n, out);
	case ES_BORDER_SPACING:
		return pair(c, EP_BORDER_SPACING_H, EP_BORDER_SPACING_V, 0, t, n, out);
	case ES_GRID_ROW: return grid_lines(c, sh_grid_row, 2, t, n, out);
	case ES_GRID_COLUMN: return grid_lines(c, sh_grid_col, 2, t, n, out);
	case ES_GRID_AREA: return grid_lines(c, sh_grid_area, 4, t, n, out);
	case ES_GRID_TEMPLATE: return grid_template(c, t, n, out);
	case ES_WORD_WRAP: case ES_WEBKIT_BACKGROUND_CLIP: {
		int i = 0;
		if (!one(c, sh == ES_WORD_WRAP ? EP_OVERFLOW_WRAP : EP_BACKGROUND_CLIP, t, n, &i, &out[0]) ||
		    i != n)
			return -1;
		return 1;
	}
	case ES_WEBKIT_TEXT_STROKE: return text_stroke(c, t, n, out);
	case ES_ANIMATION: return animation(t, n, 1, out);
	}
	return -1;
}

int ec_parse_value(ec_ctx *c, int prop, const ec_tok *t, int n, ec_decl *out, int max)
{
	int i = 0, r;

	if (n <= 0 || max < 12)
		return -1;
	color_opts = c->opts;
	if (EC_UNLIKELY(c->opts & ~EC_OPT_NO_VAR)) {
		if ((c->opts & EC_COMPAT_GAP) && (prop == EP_ROW_GAP ||
				prop == EC_SH_BASE + ES_GAP || prop == EC_SH_BASE + ES_GRID_GAP))
			return -1;
		if ((c->opts & EC_COMPAT_INSET) && prop == EC_SH_BASE + ES_INSET)
			return -1;
		if ((c->opts & EC_COMPAT_KEYWORDS) && n == 1 && t[0].type == T_IDENT &&
				(ec_kw(&t[0]) == KW_REVERT || ec_kw(&t[0]) == KW_REVERT_LAYER))
			return -1;
	}
	/* inherit, initial, unset, revert: for every longhand */
	if (n == 1 && t[0].type == T_IDENT) {
		int kw = ec_kw(&t[0]);
		if (kw == KW_INHERIT || kw == KW_INITIAL || kw == KW_UNSET || kw == KW_REVERT ||
				kw == KW_REVERT_LAYER) {
			uint8_t p[16];
			int np, k;
			if (kw == KW_REVERT || kw == KW_REVERT_LAYER)
				kw = KW_UNSET;
			if (prop < EC_NPROP) {
				p[0] = prop;
				np = 1;
			} else {
				np = ec_shorthand_props(prop - EC_SH_BASE, p);
			}
			for (k = 0; k < np; k++)
				put(&out[k], p[k], EK_KW, kw);
			return np;
		}
	}
	if (prop >= EC_SH_BASE)
		return shorthand(c, prop - EC_SH_BASE, t, n, out);
	switch (ptab[prop].parser) {
	case VP_FAMILY:
		return family(c, t, n, out);
	case VP_CONTENT:
		return content(c, t, n, out);
	case VP_TRACKS:
		return tracks(c, prop, t, n, out);
	case VP_AREAS:
		return areas(c, t, n, out);
	case VP_AUTOFLOW:
		return autoflow(t, n, out);
	case VP_GRIDLINE:
		return gridline(c, prop, t, n, out) == 1 ? 1 : -1;
	case VP_SHADOW:
		return text_shadow(c, t, n, out);
	case VP_ANIM:
		return animation(t, n, 0, out);
	case VP_DECO: {
		int bits = 0;
		if (n == 1 && ec_kw(&t[0]) == KW_NONE) {
			put(out, prop, EK_INT, 0);
			return 1;
		}
		for (i = 0; i < n; i++) {
			int b = deco_bit(ec_kw(&t[i]));
			if (!b || (bits & b))
				return -1;
			bits |= b;
		}
		put(out, prop, EK_INT, bits);
		return 1;
	}
	}
	r = one(c, prop, t, n, &i, out);
	if (r && prop == EP_BACKGROUND_IMAGE && !(c->opts & EC_COMPAT_LAYERS)) {
		/* layers (CSS Backgrounds 3, 2.1): the first, the top one, is
		   kept, as the background shorthand keeps it; the others are
		   read, each an image */
		ec_decl more;
		while (i < n && t[i].type == T_COMMA) {
			i++;
			if (i >= n || !image(c, t, n, &i, &more))
				return -1;
		}
	}
	if (!r || i != n)
		return -1;
	return 1;
}
