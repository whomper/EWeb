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

static int calc_sum(const ec_tok *t, int n, int *i, lin *r, int depth);

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
		r->c[CU_PCT] = k->num;
		(*i)++;
		return 1;
	case T_DIM:
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
		if (k->type == T_FUNC && !ec_tok_is(k, "calc"))
			return 0;
		if (depth > 16)
			return 0;
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
		r->c[u] = divide ? (ec_fx) (((int64_t) r->c[u] << 10) / f) : fx_mul(r->c[u], f);
	r->num = divide ? (ec_fx) (((int64_t) r->num << 10) / f) : fx_mul(r->num, f);
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
			r->c[u] += neg ? -b.c[u] : b.c[u];
		r->num += neg ? -b.num : b.num;
	}
	return 1;
}

/* calc(), min(), max() or clamp() of lengths at t[*i]: into an ec_calcx,
 * or a plain length when that is all it is */
static int calc_value(ec_ctx *c, const ec_tok *t, int n, int *i, unsigned lf, ec_decl *d,
		int number)
{
	const ec_tok *f = &t[*i];
	int op, end = block_end(t, n, *i), j = *i + 1, na = 0, u, maxa;
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
	for (;;) {
		if (na == maxa || !calc_sum(t, end - 1, &j, &a[na], 0))
			return 0;
		if (a[na].isnum != number)
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
	if (number) {
		/* a number: min, max and clamp can be done now */
		ec_fx v = a[0].num;
		if (op == EC_CALC_MIN || op == EC_CALC_MAX) {
			int k;
			for (k = 1; k < na; k++)
				if (op == EC_CALC_MIN ? a[k].num < v : a[k].num > v)
					v = a[k].num;
		} else if (op == EC_CALC_CLAMP) {
			v = a[1].num < a[2].num ? a[1].num : a[2].num;
			if (v < a[0].num)
				v = a[0].num;
		}
		d->kind = EK_NUM;
		d->v.i = v;
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
		return calc_value(c, t, n, i, lf, d, 0);
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
	if (t[*i].type == T_FUNC && calc_value(c, t, n, i, 0, d, 1))
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

/* a gradient's first colour that shows (its alpha not 0), from its
   arguments t[from..to): EWeb draws no gradients, but text the page
   paints with one (background-clip: text) is drawn in it; 0: none */
static uint32_t grad_colour(const ec_tok *t, int from, int to)
{
	int j = from;

	while (j < to) {
		uint32_t argb;
		int used, cur;
		if (t[j].type != T_COMMA && ec_parse_color(&t[j], to - j, &used, &argb, &cur)) {
			if (!cur && (argb >> 24) != 0)
				return argb;
			j += used;
		} else if (t[j].type == T_FUNC) {
			int e = block_end(t, to, j);
			j = e < 0 ? to : e;
		} else {
			j++;
		}
	}
	return 0;
}

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
		if (ec_tok_is(k, "linear-gradient") || ec_tok_is(k, "radial-gradient") ||
				ec_tok_is(k, "conic-gradient") || ec_tok_is(k, "repeating-linear-gradient") ||
				ec_tok_is(k, "repeating-radial-gradient") ||
				ec_tok_is(k, "repeating-conic-gradient")) {
			uint32_t first = grad_colour(t, *i + 1, end - 1), *fp = NULL;
			if (first != 0 && (fp = ec_alloc(&c->mem, sizeof *fp)) != NULL)
				*fp = first;
			d->kind = EK_IMAGE;
			d->v.p = fp;
			*i = end;
			return 1;
		}
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
				kw == KW_MIDDLE || kw == KW_TEXT_BOTTOM || kw == KW_TOP || kw == KW_BOTTOM) {
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
				  (kw == KW_START || kw == KW_END || kw == KW_MATCH_PARENT)) ||
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
	if (!r || i != n)
		return -1;
	return 1;
}
