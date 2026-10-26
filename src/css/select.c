/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Selection and the cascade: every element's matching rules, their
 * declarations in cascade order, and the computed style.
 *
 * The work an element costs is kept small four ways:
 *   - rules are found through the element's own atoms (its id, classes,
 *     tag and attribute names point at their rules), not searched for;
 *   - a counting filter of the ancestors' atoms rejects most descendant
 *     selectors without walking up the tree;
 *   - an element whose siblings before it match the same way takes their
 *     style without matching at all (style sharing);
 *   - elements with the same rules under parents with the same style
 *     take a style already computed (the style cache).
 * Declarations are applied from the most important down, each property
 * once, so nothing is written and then overwritten. */
#include "ec_int.h"

#define MAX_HITS	2048
#define CACHE_SIZE	1024
#define SHARE_CANDIDATES 8

/* an element matched a rule that looks at its siblings or children: its
 * style can't be shared */
#define MF_SIBLINGS	1

/* ------------------------------------------------------------ matching */

enum { M_YES, M_NO, M_FAIL_SIBS, M_FAIL_ALL };

static const ec_attr *find_attr(const ec_node *e, const ec_atom *name)
{
	const ec_attr *a = e->attr, *end = a + e->nattr;

	for (; a < end; a++)
		if (a->name == name)
			return a;
	return NULL;
}

static inline uint8_t lower(uint8_t ch)
{
	return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
}

static int bytes_eq(const char *a, const char *b, size_t n, int icase)
{
	size_t i;

	if (!icase)
		return memcmp(a, b, n) == 0;
	for (i = 0; i < n; i++)
		if (lower(a[i]) != lower(b[i]))
			return 0;
	return 1;
}

static int attr_match(const ec_simple *s, const ec_node *e)
{
	const ec_attr *a = find_attr(e, s->atom);
	const ec_str *v;
	int ic = s->flags & SF_ICASE;
	size_t i;

	if (a == NULL)
		return 0;
	if (s->op == S_ATTR)
		return 1;
	v = s->arg;
	switch (s->op) {
	case S_ATTR_EQ:
		return a->len == v->len && bytes_eq(a->val, v->s, v->len, ic);
	case S_ATTR_DASH:
		return a->len >= v->len && bytes_eq(a->val, v->s, v->len, ic) &&
			(a->len == v->len || a->val[v->len] == '-');
	case S_ATTR_PREFIX:
		return v->len > 0 && a->len >= v->len && bytes_eq(a->val, v->s, v->len, ic);
	case S_ATTR_SUFFIX:
		return v->len > 0 && a->len >= v->len &&
			bytes_eq(a->val + a->len - v->len, v->s, v->len, ic);
	case S_ATTR_SUBSTR:
		if (v->len == 0)
			return 0;
		for (i = 0; i + v->len <= a->len; i++)
			if (bytes_eq(a->val + i, v->s, v->len, ic))
				return 1;
		return 0;
	case S_ATTR_INCL: {
		const char *p = a->val, *end = a->val + a->len;
		if (v->len == 0 || memchr(v->s, ' ', v->len))
			return 0;
		while (p < end) {
			const char *w;
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
					*p == '\f'))
				p++;
			w = p;
			while (p < end && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
					*p == '\f'))
				p++;
			if ((size_t) (p - w) == v->len && bytes_eq(w, v->s, v->len, ic))
				return 1;
		}
		return 0;
	}
	}
	return 0;
}

/* an+b matches position p (from 1) */
static int nth(int a, int b, int p)
{
	if (a == 0)
		return p == b;
	return (p - b) / a >= 0 && (p - b) % a == 0;
}

static int of_type_pos(const ec_node *e, int last)
{
	int n = 1;
	const ec_node *s;

	for (s = last ? e->next : e->prev; s != NULL; s = last ? s->next : s->prev)
		if (s->name == e->name)
			n++;
	return n;
}

static int is_form(ec_ctx *c, const ec_node *e)
{
	return e->name == c->t_input || e->name == c->t_button || e->name == c->t_select ||
		e->name == c->t_textarea || e->name == c->t_option || e->name == c->t_optgroup ||
		e->name == c->t_fieldset;
}

static int text_input(ec_ctx *c, const ec_node *e)
{
	const ec_attr *t;

	if (e->name == c->t_textarea)
		return 1;
	if (e->name != c->t_input)
		return 0;
	t = find_attr(e, c->a_type);
	if (t == NULL)
		return 1;
	return (t->len == 4 && bytes_eq(t->val, "text", 4, 1)) ||
		(t->len == 6 && (bytes_eq(t->val, "search", 6, 1) || bytes_eq(t->val, "number", 6, 1))) ||
		(t->len == 5 && bytes_eq(t->val, "email", 5, 1)) ||
		(t->len == 3 && (bytes_eq(t->val, "url", 3, 1) || bytes_eq(t->val, "tel", 3, 1))) ||
		(t->len == 8 && bytes_eq(t->val, "password", 8, 1));
}

static int lang_match(ec_ctx *c, const ec_node *e, const ec_str *want)
{
	for (; e != NULL; e = e->parent) {
		const ec_attr *a = find_attr(e, c->a_lang);
		if (a == NULL)
			continue;
		return a->len >= want->len && bytes_eq(a->val, want->s, want->len, 1) &&
			(a->len == want->len || a->val[want->len] == '-');
	}
	return 0;
}

static int dir_match(ec_ctx *c, const ec_node *e, const ec_str *want)
{
	int rtl = 0;

	for (; e != NULL; e = e->parent) {
		const ec_attr *a = find_attr(e, c->a_dir);
		if (a != NULL && a->len == 3) {
			if (bytes_eq(a->val, "rtl", 3, 1)) {
				rtl = 1;
				break;
			}
			if (bytes_eq(a->val, "ltr", 3, 1))
				break;
		}
	}
	return want->len == 3 && bytes_eq(want->s, rtl ? "rtl" : "ltr", 3, 1);
}

static int match_from(ec_ctx *c, const ec_sel *s, int ci, const ec_node *e, int level, int *mf);

static int match_compound(ec_ctx *c, const ec_sel *s, int ci, const ec_node *e, int level,
		int *mf)
{
	const ec_simple *x = &s->simple[s->comp[ci].first], *end = x + s->comp[ci].n;

	for (; x < end; x++) {
		switch (x->op) {
		case S_TAG:
			if (e->name != x->atom)
				return 0;
			break;
		case S_ID:
			if (e->id != x->atom)
				return 0;
			break;
		case S_CLASS: {
			const ec_atom *const *k = e->cls, *const *ke = k + e->ncls;
			for (; k < ke && *k != x->atom; k++)
				;
			if (k == ke)
				return 0;
			break;
		}
		case S_ATTR: case S_ATTR_EQ: case S_ATTR_INCL: case S_ATTR_DASH:
		case S_ATTR_PREFIX: case S_ATTR_SUFFIX: case S_ATTR_SUBSTR:
			if (!attr_match(x, e))
				return 0;
			break;
		case S_ROOT:
			if (e->parent != NULL)
				return 0;
			break;
		case S_EMPTY:
			if (!(e->flags & EC_NODE_EMPTY))
				return 0;
			break;
		case S_FIRST_CHILD: case S_LAST_CHILD: case S_ONLY_CHILD:
		case S_NTH_CHILD: case S_NTH_LAST_CHILD:
		case S_FIRST_OF_TYPE: case S_LAST_OF_TYPE: case S_ONLY_OF_TYPE:
		case S_NTH_OF_TYPE: case S_NTH_LAST_OF_TYPE: {
			int ok;
			switch (x->op) {
			case S_FIRST_CHILD: ok = e->index == 1; break;
			case S_LAST_CHILD: ok = e->index == e->nsib; break;
			case S_ONLY_CHILD: ok = e->nsib == 1; break;
			case S_NTH_CHILD: ok = nth(x->a, x->b, e->index); break;
			case S_NTH_LAST_CHILD: ok = nth(x->a, x->b, e->nsib - e->index + 1); break;
			case S_FIRST_OF_TYPE: ok = of_type_pos(e, 0) == 1; break;
			case S_LAST_OF_TYPE: ok = of_type_pos(e, 1) == 1; break;
			case S_ONLY_OF_TYPE: ok = of_type_pos(e, 0) == 1 && of_type_pos(e, 1) == 1; break;
			case S_NTH_OF_TYPE: ok = nth(x->a, x->b, of_type_pos(e, 0)); break;
			default: ok = nth(x->a, x->b, of_type_pos(e, 1)); break;
			}
			if (!ok)
				return 0;
			break;
		}
		case S_VISITED:
			if ((e->flags & (EC_NODE_LINK | EC_NODE_VISITED)) != (EC_NODE_LINK | EC_NODE_VISITED))
				return 0;
			break;
		case S_LINK:		/* (:link is :any-link once parsed: sheet.c) */
		case S_ANY_LINK:
			if (!(e->flags & EC_NODE_LINK))
				return 0;
			break;
		case S_NEVER:
			return 0;
		case S_NOT:
			if (ec_match_list(c, x->arg, e, mf) ^ 1)
				break;
			return 0;
		case S_IS:
			if (!ec_match_list(c, x->arg, e, mf))
				return 0;
			break;
		case S_LANG:
			if (!lang_match(c, e, x->arg))
				return 0;
			break;
		case S_DIR:
			if (!dir_match(c, e, x->arg))
				return 0;
			break;
		case S_ENABLED:
			if (!is_form(c, e) || find_attr(e, c->a_disabled))
				return 0;
			break;
		case S_DISABLED:
			if (!is_form(c, e) || !find_attr(e, c->a_disabled))
				return 0;
			break;
		case S_CHECKED: {
			int ok = 0;
			if (e->name == c->t_input && find_attr(e, c->a_checked)) {
				const ec_attr *t = find_attr(e, c->a_type);
				ok = t != NULL && ((t->len == 8 && bytes_eq(t->val, "checkbox", 8, 1)) ||
					(t->len == 5 && bytes_eq(t->val, "radio", 5, 1)));
			} else if (e->name == c->t_option && find_attr(e, c->a_selected)) {
				ok = 1;
			}
			if (!ok)
				return 0;
			break;
		}
		case S_OPEN:
			/* a details or dialog with its open attribute (the HTML
			   Standard, 4.16.3; a select's picker never is, here) */
			if ((e->name != c->t_details && e->name != c->t_dialog) || !find_attr(e, c->a_open))
				return 0;
			break;
		case S_REQUIRED: case S_OPTIONAL: {
			int f = e->name == c->t_input || e->name == c->t_select || e->name == c->t_textarea;
			int r = f && find_attr(e, c->a_required) != NULL;
			if (!f || r != (x->op == S_REQUIRED))
				return 0;
			break;
		}
		case S_READ_WRITE: case S_READ_ONLY: {
			int rw = text_input(c, e) && !find_attr(e, c->a_readonly) &&
				!find_attr(e, c->a_disabled);
			if (rw != (x->op == S_READ_WRITE))
				return 0;
			break;
		}
		case S_PLACEHOLDER_SHOWN: {
			const ec_attr *v;
			if (!text_input(c, e) || !find_attr(e, c->a_placeholder))
				return 0;
			v = find_attr(e, c->a_value);
			if (v != NULL && v->len > 0)
				return 0;
			break;
		}
		}
	}
	return 1;
}

/* right to left from compound ci at e (Selectors 4; the failure kinds
 * stop the search early, as WebKit's SelectorChecker does) */
static int match_from(ec_ctx *c, const ec_sel *s, int ci, const ec_node *e, int level, int *mf)
{
	const ec_node *p;
	int r;

	if (!match_compound(c, s, ci, e, level, mf))
		return M_NO;
	if (ci + 1 == s->ncomp)
		return M_YES;
	switch (s->comp[ci].comb) {
	case C_CHILD:
		if (e->parent == NULL)
			return M_FAIL_ALL;
		return match_from(c, s, ci + 1, e->parent, level + 1, mf);
	case C_DESC:
		for (p = e->parent; p != NULL; p = p->parent) {
			r = match_from(c, s, ci + 1, p, level + 1, mf);
			if (r == M_YES || r == M_FAIL_ALL)
				return r;
		}
		return M_FAIL_ALL;
	case C_ADJ:
		if (e->prev == NULL)
			return M_FAIL_SIBS;
		return match_from(c, s, ci + 1, e->prev, level, mf);
	case C_SIB:
		for (p = e->prev; p != NULL; p = p->prev) {
			r = match_from(c, s, ci + 1, p, level, mf);
			if (r != M_NO)
				return r;
		}
		return M_FAIL_SIBS;
	}
	return M_NO;
}

int ec_match_list(ec_ctx *c, const ec_sellist *l, const ec_node *e, int *mf)
{
	int k;

	for (k = 0; k < l->n; k++)
		if (match_from(c, l->sel[k], 0, e, 0, mf) == M_YES)
			return 1;
	return 0;
}

/* ------------------------------------------------------------ the filter */

static inline void bloom_add(ec_ctx *c, const ec_atom *a)
{
	uint8_t *b = &c->bloom[a->bloom];
	if (*b != 255)
		(*b)++;
}

static inline void bloom_del(ec_ctx *c, const ec_atom *a)
{
	uint8_t *b = &c->bloom[a->bloom];
	if (*b != 255)
		(*b)--;
}

static void bloom_push(ec_ctx *c, const ec_node *e)
{
	uint16_t k;

	bloom_add(c, e->name);
	if (e->id)
		bloom_add(c, e->id);
	for (k = 0; k < e->ncls; k++)
		bloom_add(c, e->cls[k]);
}

static void bloom_pop(ec_ctx *c, const ec_node *e)
{
	uint16_t k;

	bloom_del(c, e->name);
	if (e->id)
		bloom_del(c, e->id);
	for (k = 0; k < e->ncls; k++)
		bloom_del(c, e->cls[k]);
}

/* ------------------------------------------------------------ collecting */

/* the properties a :visited rule may set (CSS Selectors 4, 8.2) */
static int colour_prop(int p)
{
	return p == EP_COLOR || p == EP_BACKGROUND_COLOR || p == EP_BORDER_TOP_COLOR || p == EP_BORDER_RIGHT_COLOR ||
	       p == EP_BORDER_BOTTOM_COLOR || p == EP_BORDER_LEFT_COLOR || p == EP_TEXT_STROKE_COLOR;
}

typedef struct hit {
	uint32_t prio, order;
	const ec_block *b;
	uint8_t pseudo;
	uint8_t only;			/* its selector's SV_*, then (style_element) what of it
					   counts: SV_VISITED colours, SV_LINK all but colours,
					   both nothing, 0 all */
} hit;

/* a declaration of a hit whose selector has :visited or :link counts */
static int counts(int only, int p)
{
	return only == SV_VISITED ? colour_prop(p) : only == SV_LINK ? !colour_prop(p) : 0;
}

static hit hits[MAX_HITS];
static int nhits;
static int link_hits;			/* the hits' SV_* together */

static void collect(ec_ctx *c, const ec_vec *v, const ec_node *e, int *mf)
{
	const ec_rent *r, *end;

	if (v == NULL)
		return;
	r = v->e;
	end = r + v->n;
	c->st.candidates += v->n;
	for (; r < end; r++) {
		const uint8_t *bl = c->bloom;
		if ((r->bloom[0] && !bl[r->bloom[0]]) || (r->bloom[1] && !bl[r->bloom[1]]) ||
				(r->bloom[2] && !bl[r->bloom[2]]) || (r->bloom[3] && !bl[r->bloom[3]])) {
			c->st.rejected++;
			continue;
		}
		if (match_from(c, r->sel, 0, e, 0, mf) != M_YES)
			continue;
		if (r->sib)
			*mf |= MF_SIBLINGS;
		if (nhits == MAX_HITS)
			return;
		hits[nhits].prio = r->prio;
		hits[nhits].order = r->order;
		hits[nhits].b = r->sel->block;
		hits[nhits].pseudo = r->sel->pseudo;
		hits[nhits].only = r->sel->visited;
		link_hits |= r->sel->visited;
		nhits++;
	}
}

static void sort_hits(void)
{
	int i, j;

	for (i = 1; i < nhits; i++) {
		hit h = hits[i];
		for (j = i; j > 0 && (hits[j - 1].prio > h.prio ||
				(hits[j - 1].prio == h.prio && hits[j - 1].order > h.order)); j--)
			hits[j] = hits[j - 1];
		hits[j] = h;
	}
}

/* ------------------------------------------------------------ inline styles */

typedef struct istyle {
	uint32_t hash, len;
	const char *s;
	ec_block *b;
} istyle;

static istyle icache[64];

/* a style attribute's or a hint's declarations as a block, the same
   block for the same text */
static ec_block *inline_block(ec_ctx *c, const char *val, uint32_t len)
{
	uint32_t h = ec_hash(val, len);
	istyle *e = &icache[h & 63];
	char *copy;
	ec_block *b;

	if (e->b != NULL && e->hash == h && e->len == len && memcmp(e->s, val, len) == 0)
		return e->b;
	copy = ec_alloc(&c->smem, len + 1);
	b = ec_alloc(&c->smem, sizeof *b);
	if (copy == NULL || b == NULL)
		return NULL;
	memcpy(copy, val, len);
	copy[len] = 0;
	memset(b, 0, sizeof *b);
	b->src = copy;
	b->len = len;
	ec_parse_block(c, b, &c->smem);
	e->hash = h;
	e->len = len;
	e->s = copy;
	e->b = b;
	return b;
}

/* ------------------------------------------------------------ custom properties */

typedef struct ec_cent {
	const ec_atom *name;
	const char *val;
	uint32_t len;
	uint8_t state;			/* 0 as written, 1 resolving, 2 done, 3 invalid */
} ec_cent;

typedef struct ec_custom ec_custom;
struct ec_custom {
	const struct ec_custom *parent;
	uint32_t n;
	ec_cent e[];
};

static ec_cent *custom_find(const ec_custom *cu, const ec_atom *name, int *own,
		const ec_custom *mine)
{
	for (; cu != NULL; cu = cu->parent) {
		uint32_t k;
		for (k = 0; k < cu->n; k++)
			if (cu->e[k].name == name) {
				*own = cu == mine;
				return (ec_cent *) &cu->e[k];
			}
	}
	return NULL;
}

const char *ec_custom_value(const ec_style *s, const ec_atom *name, size_t *len)
{
	int own;
	ec_cent *e = custom_find(s->custom, name, &own, NULL);

	if (e == NULL || e->state == 3)
		return NULL;
	*len = e->len;
	return e->val;
}

static int resolve_custom(ec_ctx *c, ec_custom *cu, ec_cent *e, int depth);

/* var() substituted in text into out (cap bytes); its length, or -1 if
 * invalid at computed-value time */
static int substitute(ec_ctx *c, ec_custom *cu, const char *s, size_t len, char *out, size_t cap,
		int depth)
{
	ec_lex lx;
	size_t n = 0;
	const char *copied = s;

	if (depth > 8)
		return -1;
	ec_lex_init(&lx, s, len);
	for (;;) {
		const char *at = lx.p;
		ec_tok t;
		int type = ec_next(&lx, &t);
		const char *fb = NULL, *fe = NULL, *end;
		ec_cent *v;
		int own, r;

		if (type == T_EOF)
			break;
		if (type != T_FUNC || !(ec_tok_is(&t, "var") || ec_tok_is(&t, "env")))
			continue;
		/* copy what came before */
		if (n + (at - copied) >= cap)
			return -1;
		memcpy(out + n, copied, at - copied);
		n += at - copied;
		end = ec_skip_block(lx.p, lx.end, '(');
		{
			ec_lex al;
			ec_tok nt;
			const char *body_end = end > lx.p && end[-1] == ')' ? end - 1 : end;
			char nb[128];
			size_t nl;
			const char *ns;
			int is_env = (t.s[0] | 32) == 'e';
			ec_lex_init(&al, lx.p, body_end - lx.p);
			while (ec_next(&al, &nt) == T_WS)
				;
			if (nt.type != T_IDENT)
				return -1;
			ns = ec_tok_text(&nt, nb, sizeof nb, &nl);
			while (ec_next(&al, &t) == T_WS)
				;
			if (t.type == T_COMMA) {
				fb = al.p;
				fe = body_end;
			} else if (t.type != T_EOF) {
				return -1;
			}
			v = is_env ? NULL : custom_find(cu, ec_intern(c, ns, nl), &own, cu);
			if (v != NULL && own && v->state != 2 && v->state != 3)
				resolve_custom(c, cu, v, depth + 1);
			if (v != NULL && v->state != 3 && (v->len > 0 || fb == NULL)) {
				if (n + v->len + 2 >= cap)
					return -1;
				/* a space either side keeps tokens apart, as substitution does */
				out[n++] = ' ';
				memcpy(out + n, v->val, v->len);
				n += v->len;
				out[n++] = ' ';
			} else if (fb != NULL) {
				r = substitute(c, cu, fb, fe - fb, out + n, cap - n, depth + 1);
				if (r < 0)
					return -1;
				n += r;
			} else {
				return -1;
			}
		}
		lx.p = end;
		copied = end;
	}
	if (n + (s + len - copied) >= cap)
		return -1;
	memcpy(out + n, copied, s + len - copied);
	n += s + len - copied;
	return n;
}

static int resolve_custom(ec_ctx *c, ec_custom *cu, ec_cent *e, int depth)
{
	char *buf;
	int r;
	if (e->state == 1) {		/* a cycle */
		e->state = 3;
		return -1;
	}
	if (e->state >= 2)
		return 0;
	char tmp[1024];

	e->state = 1;
	r = substitute(c, cu, e->val, e->len, tmp, sizeof tmp, depth);
	if (e->state == 3 || r < 0) {
		e->state = 3;
		return -1;
	}
	buf = ec_alloc(&c->smem, r + 1);
	if (buf == NULL) {
		e->state = 3;
		return -1;
	}
	memcpy(buf, tmp, r);
	e->val = buf;
	e->len = r;
	e->state = 2;
	return 0;
}

/* ------------------------------------------------------------ computing */

static const ec_fx size_kw[] = {	/* xx-small to xxx-large, as LibCSS and browsers */
	576, 640, 832, 1024, 1152, 1536, 2048, 3072
};

typedef struct cascade {
	ec_ctx *c;
	ec_style *st;
	const ec_style *parent;
	uint32_t set[(EC_NPROP + 31) / 32];
	uint8_t todo[EC_NPROP];
	int ntodo;
	const ec_decl *var[EC_NPROP];
	int nvar;
	uint8_t varprop[EC_NPROP];
	const ec_cdecl *cust[256];
	int ncust;
} cascade;

static inline int is_set(cascade *k, int p)
{
	return k->set[p >> 5] & (1u << (p & 31));
}

static void parent_value(cascade *k, int p)
{
	if (k->parent != NULL) {
		k->st->v[p] = k->parent->v[p];
		k->st->k[p] = k->parent->k[p];
	} else {
		k->st->v[p] = k->c->initial.v[p];
		k->st->k[p] = k->c->initial.k[p];
	}
}

/* a declaration's value into the style, to be computed later if need be */
static void apply(cascade *k, const ec_decl *d)
{
	int p = d->prop;
	ec_style *st = k->st;

	if (d->kind == EK_KW) {
		int kw = d->v.i;
		if (kw == KW_INHERIT) {
			parent_value(k, p);
			return;
		}
		if (kw == KW_INITIAL) {
			st->v[p] = k->c->initial.v[p];
			st->k[p] = k->c->initial.k[p];
			return;
		}
		if (kw == KW_UNSET) {
			if (p < EC_NINHERITED)
				parent_value(k, p);
			else {
				st->v[p] = k->c->initial.v[p];
				st->k[p] = k->c->initial.k[p];
			}
			return;
		}
	}
	st->v[p] = d->v;
	st->k[p] = d->kind;
	if ((d->kind >= EK_EM && d->kind <= EK_CALCX) || p == EP_FONT_SIZE ||
			(d->kind == EK_KW && (p == EP_FONT_WEIGHT || p == EP_TEXT_ALIGN)) ||
			(p == EP_LINE_HEIGHT && d->kind == EK_PCT) ||
			(p == EP_COLOR && d->kind == EK_CURCOLOR) ||
			(d->kind == EK_PTR && (p == EP_GRID_TEMPLATE_COLUMNS || p == EP_GRID_TEMPLATE_ROWS ||
			 p == EP_GRID_AUTO_COLUMNS || p == EP_GRID_AUTO_ROWS) && ((const ec_tracks *) d->v.p)->rel) ||
			(d->kind == EK_IMAGE && ((const ec_grad *) d->v.p)->nrel))
		k->todo[k->ntodo++] = p;
}

static void take(cascade *k, const ec_decl *d)
{
	int p = d->prop;

	if (p == EP_CUSTOM) {
		const ec_cdecl *cd = d->v.p;
		ec_atom *a = (ec_atom *) cd->name;
		if (a->stamp == k->c->stamp || k->ncust == 256)
			return;
		a->stamp = k->c->stamp;
		k->cust[k->ncust++] = cd;
		return;
	}
	if (is_set(k, p))
		return;
	k->set[p >> 5] |= 1u << (p & 31);
	if (d->kind == EK_VAR) {
		k->var[k->nvar] = d;
		k->varprop[k->nvar++] = p;
		return;
	}
	apply(k, d);
}

/* a sum kept within 32 bits (1e9vw + 1e9px: no overflow) */
static ec_fx fx_clamp(int64_t v)
{
	return v > INT32_MAX ? INT32_MAX : v < -INT32_MAX ? -INT32_MAX : (ec_fx) v;
}

/* v vw (or vh...) of a viewport d px wide: v * d / 100, kept within 32
   bits (one of 512vw or more: its product in 64 bits, the division in
   32, which is a loop on the 68000). Not inlined: rare, and the callers'
   loops kept as they were */
static __attribute__((noinline)) ec_fx vw_px(ec_fx v, int32_t d)
{
	if (v < (1L << 19) && v > -(1L << 19) && d < 4096)
		return v * d / 100;
	return fx_clamp((int64_t) (v / 100) * d + (v % 100) * d / 100);
}

/* one calc() operand in px, its percentage left out */
static ec_fx calc_px(const ec_ctx *c, const ec_fx *u, ec_fx fs, ec_fx rfs)
{
	int32_t w = c->media.width, h = c->media.height;

	int64_t s = (int64_t) u[CU_PX] + fx_mul(u[CU_EM], fs) + fx_mul(fx_mul(u[CU_EX], fs), 614) +
		    fx_mul(fx_mul(u[CU_CH], fs), 410) + fx_mul(u[CU_REM], rfs);

	if (u[CU_VW] | u[CU_VH] | u[CU_VMIN] | u[CU_VMAX])
		s += (int64_t) vw_px(u[CU_VW], w) + vw_px(u[CU_VH], h) + vw_px(u[CU_VMIN], w < h ? w : h) +
		     vw_px(u[CU_VMAX], w > h ? w : h);
	return fx_clamp(s);
}

/* min(), max() or clamp() of operands in px */
static ec_fx calc_fold(int op, const ec_fx *v, int n)
{
	ec_fx r = v[0];
	int a;

	if (op == EC_CALC_CLAMP) {
		r = v[1] < v[2] ? v[1] : v[2];
		return r < v[0] ? v[0] : r;
	}
	for (a = 1; a < n; a++)
		if (op == EC_CALC_MIN ? v[a] < r : op == EC_CALC_MAX ? v[a] > r : 0)
			r = v[a];
	return r;
}

static ec_fx root_font(const ec_ctx *c)
{
	return c->root_style ? c->root_style->v[EP_FONT_SIZE].i : EC_FX(c->media.font_size);
}

/* a length in v (kind) in px with font size fs, or an ec_calc */
static void compute_length(cascade *k, int p, ec_fx fs)
{
	ec_ctx *c = k->c;
	ec_style *st = k->st;
	ec_fx v = st->v[p].i, rfs = root_font(c);
	int32_t w = c->media.width, h = c->media.height;

	switch (st->k[p]) {
	case EK_EM: v = fx_mul(v, fs); break;
	case EK_EX: v = fx_mul(fx_mul(v, fs), 614); break;
	case EK_CH: v = fx_mul(fx_mul(v, fs), 410); break;
	case EK_REM: v = fx_mul(v, rfs); break;
	case EK_VW: v = vw_px(v, w); break;
	case EK_VH: v = vw_px(v, h); break;
	case EK_VMIN: v = vw_px(v, w < h ? w : h); break;
	case EK_VMAX: v = vw_px(v, w > h ? w : h); break;
	case EK_CALCX: {
		const ec_calcx *x = st->v[p].p;
		ec_calc *r;
		int a, allpx = 1;
		r = ec_alloc(&c->smem, sizeof *r + x->n * sizeof r->a[0]);
		if (r == NULL)
			return;
		ec_fx px[3];
		r->op = x->op;
		r->n = x->n;
		for (a = 0; a < x->n; a++) {
			r->a[a].px = px[a] = calc_px(c, x->c[a], fs, rfs);
			r->a[a].pct = x->c[a][CU_PCT];
			if (r->a[a].pct)
				allpx = 0;
		}
		if (allpx) {
			v = calc_fold(r->op, px, r->n);
			break;
		}
		st->k[p] = EK_CALC;
		st->v[p].p = r;
		return;
	}
	default:
		return;
	}
	st->k[p] = EK_PX;
	st->v[p].i = v;
}

/* a track list's lengths in em, rem, vw... made px: a copy, for this style */
static void compute_tracks(cascade *k, int p, ec_fx fs)
{
	ec_ctx *c = k->c;
	const ec_tracks *t = k->st->v[p].p;
	ec_tracks *r = ec_alloc(&c->smem, sizeof *r + t->n * sizeof r->t[0]);
	ec_fx rfs = root_font(c);
	int32_t w = c->media.width, h = c->media.height;
	int i, j;

	if (r == NULL)
		return;
	memcpy(r, t, sizeof *r + t->n * sizeof r->t[0]);
	for (i = 0; i < r->n; i++)
		for (j = 0; j < 2; j++) {
			ec_tsize *z = j ? &r->t[i].mx : &r->t[i].mn;
			switch (z->rel) {
			case EK_EM: z->v = fx_mul(z->v, fs); break;
			case EK_EX: z->v = fx_mul(fx_mul(z->v, fs), 614); break;
			case EK_CH: z->v = fx_mul(fx_mul(z->v, fs), 410); break;
			case EK_REM: z->v = fx_mul(z->v, rfs); break;
			case EK_VW: z->v = vw_px(z->v, w); break;
			case EK_VH: z->v = vw_px(z->v, h); break;
			case EK_VMIN: z->v = vw_px(z->v, w < h ? w : h); break;
			case EK_VMAX: z->v = vw_px(z->v, w > h ? w : h); break;
			default: break;
			}
			z->rel = 0;
		}
	r->rel = 0;
	k->st->v[p].p = r;
}

/* a gradient's lengths in em, rem, vw... made px: a copy, for this style */
static void compute_grad(cascade *k, int p, ec_fx fs)
{
	ec_ctx *c = k->c;
	const ec_grad *g = k->st->v[p].p;
	size_t size = sizeof *g + g->n * sizeof g->s[0];
	ec_grad *r = ec_alloc(&c->smem, size);
	ec_fx u[CU_N];
	int i;

	if (r == NULL)
		return;
	memcpy(r, g, size);
	memset(u, 0, sizeof u);
	for (i = 0; i < g->nrel; i++) {
		const ec_grel *e = &g->rel[i];
		ec_glen *l = e->at < g->n ? &r->s[e->at].at : e->at == EC_GL_X(g) ? &r->x :
			     e->at == EC_GL_Y(g) ? &r->y : e->at == EC_GL_RX(g) ? &r->rx : &r->ry;
		u[e->unit] = e->v;
		l->px = fx_clamp((int64_t) l->px + calc_px(c, u, fs, root_font(c)));
		u[e->unit] = 0;
	}
	if (r->extent == EC_EXT_LENGTH && r->circle)
		r->ry = r->rx;
	r->rel = NULL;
	r->nrel = 0;
	k->st->v[p].p = r;
}

static int bolder(int w)
{
	return w < 350 ? 400 : w < 550 ? 700 : 900;
}

static int lighter(int w)
{
	return w < 100 ? w : w < 550 ? 100 : w < 750 ? 400 : 700;
}

static int blockify(int d)
{
	switch (d) {
	case KW_INLINE_TABLE: return KW_TABLE;
	case KW_INLINE_FLEX: return KW_FLEX;
	case KW_INLINE_GRID: return KW_GRID;
	case KW_INLINE: case KW_RUN_IN: case KW_INLINE_BLOCK: case KW_TABLE_ROW_GROUP:
	case KW_TABLE_HEADER_GROUP: case KW_TABLE_FOOTER_GROUP: case KW_TABLE_ROW:
	case KW_TABLE_COLUMN_GROUP: case KW_TABLE_COLUMN: case KW_TABLE_CELL:
	case KW_TABLE_CAPTION:
		return KW_BLOCK;
	}
	return d;
}

static void compute(cascade *k)
{
	ec_ctx *c = k->c;
	ec_style *st = k->st;
	const ec_style *ps = k->parent;
	ec_fx pfs = ps ? ps->v[EP_FONT_SIZE].i : EC_FX(c->media.font_size), fs;
	int i, side;

	/* the font size first: the other lengths need it */
	if (is_set(k, EP_FONT_SIZE) && st->k[EP_FONT_SIZE] != EK_PX) {
		ec_fx v = st->v[EP_FONT_SIZE].i;
		switch (st->k[EP_FONT_SIZE]) {
		case EK_KW:
			if (v == KW_LARGER)
				v = fx_mul(pfs, 1229);
			else if (v == KW_SMALLER)
				v = (ec_fx) (((int64_t) pfs << 10) / 1229);
			else if (v == KW_MEDIUM)
				v = EC_FX(c->media.font_size);
			else
				v = c->media.font_size * size_kw[v - KW_XX_SMALL <= 2 ? v - KW_XX_SMALL :
					v - KW_XX_SMALL + 1];
			break;
		case EK_PCT:
			v = fx_mul(v, pfs) / 100;
			break;
		case EK_CALCX: {
			/* percentages are of the parent's size, so it all comes out in px */
			const ec_calcx *x = st->v[EP_FONT_SIZE].p;
			ec_fx px[3];
			int a;
			for (a = 0; a < x->n; a++)
				px[a] = fx_clamp((int64_t) calc_px(c, x->c[a], pfs, root_font(c)) +
						 fx_mul(x->c[a][CU_PCT], pfs) / 100);
			v = calc_fold(x->op, px, x->n);
			break;
		}
		default:
			compute_length(k, EP_FONT_SIZE, pfs);
			v = st->v[EP_FONT_SIZE].i;
			break;
		}
		if (v < 0)
			v = 0;
		st->k[EP_FONT_SIZE] = EK_PX;
		st->v[EP_FONT_SIZE].i = v;
	}
	fs = st->v[EP_FONT_SIZE].i;
	for (i = 0; i < k->ntodo; i++) {
		int p = k->todo[i];
		if (p == EP_FONT_SIZE)
			continue;
		switch (p) {
		case EP_FONT_WEIGHT:
			if (st->k[p] == EK_KW) {
				int pw = ps ? ps->v[EP_FONT_WEIGHT].i : 400;
				st->v[p].i = st->v[p].i == KW_BOLDER ? bolder(pw) : lighter(pw);
				st->k[p] = EK_INT;
			}
			continue;
		case EP_TEXT_ALIGN:
			/* match-parent, and th's centre where its parent's
			   isn't the initial start (the HTML Standard, 15.3.8):
			   the parent's; else th's is center */
			if (st->v[p].i == KW_MATCH_PARENT ||
			    (st->v[p].i == KW_EC_CENTER_OR_INHERIT && ps != NULL && ps->v[p].i != KW_START))
				parent_value(k, p);
			else if (st->v[p].i == KW_EC_CENTER_OR_INHERIT)
				st->v[p].i = KW_CENTER;
			continue;
		case EP_COLOR:
			if (st->k[p] == EK_CURCOLOR)
				parent_value(k, p);
			continue;
		case EP_GRID_TEMPLATE_COLUMNS: case EP_GRID_TEMPLATE_ROWS:
		case EP_GRID_AUTO_COLUMNS: case EP_GRID_AUTO_ROWS:
			if (st->k[p] == EK_PTR)
				compute_tracks(k, p, fs);
			continue;
		case EP_BACKGROUND_IMAGE: case EP_LIST_STYLE_IMAGE:
			if (st->k[p] == EK_IMAGE)
				compute_grad(k, p, fs);
			continue;
		case EP_LINE_HEIGHT:
			if (st->k[p] == EK_PCT) {
				st->v[p].i = fx_mul(st->v[p].i, fs) / 100;
				st->k[p] = EK_PX;
				continue;
			}
			break;
		}
		compute_length(k, p, fs);
	}
	/* a border without a style has no width (CSS Backgrounds 3, 4.3) */
	for (side = 0; side < 4; side++) {
		int s = st->v[EP_BORDER_TOP_STYLE + side].i;
		if (s == KW_NONE || s == KW_HIDDEN) {
			st->k[EP_BORDER_TOP_WIDTH + side] = EK_PX;
			st->v[EP_BORDER_TOP_WIDTH + side].i = 0;
		}
	}
	/* display, position and float (CSS 2.1, 9.7; flex items, CSS Flexbox 4) */
	{
		int d = st->v[EP_DISPLAY].i, pos = st->v[EP_POSITION].i;
		if (d != KW_NONE) {
			int pd = ps ? ps->v[EP_DISPLAY].i : 0;
			if (pos == KW_ABSOLUTE || pos == KW_FIXED) {
				st->v[EP_FLOAT].i = KW_NONE;
				d = blockify(d);
			} else if (st->v[EP_FLOAT].i != KW_NONE || ps == NULL ||
					pd == KW_FLEX || pd == KW_INLINE_FLEX ||
					pd == KW_GRID || pd == KW_INLINE_GRID) {
				d = blockify(d);
			}
			if (ps == NULL && d == KW_CONTENTS)
				d = KW_BLOCK;
			st->v[EP_DISPLAY].i = d;
		}
	}
}

/* a declaration waiting for var(): substituted, then read as if written so */
static void apply_var(cascade *k, const ec_decl *d, ec_custom *cu)
{
	static const ec_pend *last;
	static ec_decl got[16];
	static int ngot;
	static uint32_t last_stamp;
	const ec_pend *pd = d->v.p;
	int i;

	if (pd != last || last_stamp != k->c->stamp) {
		ec_tok v[256];
		ec_lex lx;
		int n = 0, ws = 0;
		int len = substitute(k->c, cu, pd->txt, pd->len, k->c->buf, sizeof k->c->buf, 0);
		last = pd;
		last_stamp = k->c->stamp;
		ngot = -1;
		if (len >= 0) {
			ec_lex_init(&lx, k->c->buf, len);
			for (;;) {
				int type = ec_next(&lx, &v[n]);
				if (type == T_EOF)
					break;
				if (type == T_WS) {
					ws = 1;
					continue;
				}
				if (ws)
					v[n].flags |= TF_WS;
				ws = 0;
				if (++n == 256)
					break;
			}
			if (n < 256) {
				/* the tokens point into buf: values that keep text copy it */
				ngot = ec_parse_value(k->c, pd->prop, v, n, got, 16);
			}
		}
	}
	for (i = 0; i < ngot; i++)
		if (got[i].prop == d->prop) {
			apply(k, &got[i]);
			return;
		}
	/* invalid at computed-value time: as unset */
	{
		ec_decl u;
		u.prop = d->prop;
		u.kind = EK_KW;
		u.v.i = KW_UNSET;
		apply(k, &u);
	}
}

/* the cascade for one pseudo-element (PE_NONE: the element itself) */
static ec_style *cascade_style(ec_ctx *c, const ec_style *parent, int pseudo)
{
	cascade k;
	ec_style *st = ec_alloc(&c->smem, sizeof *st);
	int i, o;
	ec_custom *cu = NULL;

	if (st == NULL)
		return NULL;
	c->stamp = ++ec_stamp;
	memset(k.set, 0, sizeof k.set);
	k.c = c;
	k.st = st;
	k.parent = parent;
	k.ntodo = k.nvar = k.ncust = 0;
	if (parent != NULL) {
		memcpy(st->v, parent->v, EC_NINHERITED * sizeof st->v[0]);
		memcpy(st->k, parent->k, EC_NINHERITED);
	} else {
		memcpy(st->v, c->initial.v, EC_NINHERITED * sizeof st->v[0]);
		memcpy(st->k, c->initial.k, EC_NINHERITED);
	}
	memcpy(st->v + EC_NINHERITED, c->initial.v + EC_NINHERITED,
		(EC_NPROP - EC_NINHERITED) * sizeof st->v[0]);
	memcpy(st->k + EC_NINHERITED, c->initial.k + EC_NINHERITED, EC_NPROP - EC_NINHERITED);
	st->before = st->after = NULL;

	/* !important: the user agent's first, then the user's, then the author's */
	for (o = EC_ORIGIN_UA; o <= EC_ORIGIN_AUTHOR; o++)
		for (i = nhits - 1; i >= 0; i--) {
			const ec_block *b = hits[i].b;
			int j;
			if (hits[i].pseudo != pseudo || (hits[i].prio >> 28) != (uint32_t) o || !b->imp)
				continue;
			for (j = b->n - 1; j >= 0; j--)
				if (b->d[j].imp && (!hits[i].only || counts(hits[i].only, b->d[j].prop)))
					take(&k, &b->d[j]);
		}
	/* then the rest, most specific first */
	for (i = nhits - 1; i >= 0; i--) {
		const ec_block *b = hits[i].b;
		int j;
		if (hits[i].pseudo != pseudo)
			continue;
		for (j = b->n - 1; j >= 0; j--)
			if (!b->d[j].imp && (!hits[i].only || counts(hits[i].only, b->d[j].prop)))
				take(&k, &b->d[j]);
	}
	/* custom properties: this element's over its parent's */
	if (k.ncust) {
		cu = ec_alloc(&c->smem, sizeof *cu + k.ncust * sizeof cu->e[0]);
		if (cu != NULL) {
			cu->parent = parent ? parent->custom : NULL;
			cu->n = k.ncust;
			for (i = 0; i < k.ncust; i++) {
				cu->e[i].name = k.cust[i]->name;
				cu->e[i].val = k.cust[i]->val;
				cu->e[i].len = k.cust[i]->len;
				cu->e[i].state = k.cust[i]->hasvar ? 0 : 2;
			}
			for (i = 0; i < k.ncust; i++)
				if (cu->e[i].state == 0)
					resolve_custom(c, cu, &cu->e[i], 0);
		}
		st->custom = cu;
	} else {
		st->custom = parent ? parent->custom : NULL;
	}
	for (i = 0; i < k.nvar; i++)
		apply_var(&k, k.var[i], (ec_custom *) st->custom);
	compute(&k);
	return st;
}

/* ------------------------------------------------------------ the style cache */

typedef struct centry {
	uint32_t hash;
	uint16_t n;
	const ec_style *parent;
	const uintptr_t *key;
	const ec_style *style;
} centry;

static centry cache[CACHE_SIZE];
static uintptr_t keybuf[2 * MAX_HITS];

static int build_key(void)
{
	int i, n = 0;

	for (i = 0; i < nhits; i++) {
		keybuf[n++] = (uintptr_t) hits[i].b | hits[i].pseudo;
		if (hits[i].only)
			keybuf[n++] = hits[i].only;	/* (what of it counts: no block is at 1, 2 or 3) */
	}
	return n;
}

static uint32_t key_hash(const ec_style *parent, int n)
{
	uint32_t h = (uint32_t) (uintptr_t) parent;
	int i;

	for (i = 0; i < n; i++)
		h = (h << 5) + h + (uint32_t) (keybuf[i] >> 2) + (uint32_t) (keybuf[i] & 3);
	return h ^ (h >> 15);
}

/* ------------------------------------------------------------ the tree */

/* does e match any rule that looks at siblings or children? */
static int sib_matches(ec_ctx *c, const ec_vec *v, const ec_node *e)
{
	const ec_rent *r, *end;
	int mf = 0;

	if (v == NULL)
		return 0;
	for (r = v->e, end = r + v->n; r < end; r++) {
		const uint8_t *bl = c->bloom;
		if ((r->bloom[0] && !bl[r->bloom[0]]) || (r->bloom[1] && !bl[r->bloom[1]]) ||
				(r->bloom[2] && !bl[r->bloom[2]]) || (r->bloom[3] && !bl[r->bloom[3]]))
			continue;
		if (match_from(c, r->sel, 0, e, 0, &mf) == M_YES)
			return 1;
	}
	return 0;
}

/* an atom's rules that look at siblings: its own (or its base's), and
   for a base's atom this context's too */
static int atom_sib_matches(ec_ctx *c, const ec_atom *a, int kind, const ec_node *e)
{
	const ec_shadow *sh;
	if (sib_matches(c, a->sibrules[kind], e))
		return 1;
	return a->shared && c->shadow != NULL && (sh = ec_shadow_of(c, a, 0)) != NULL &&
	       sib_matches(c, sh->sibrules[kind], e);
}

static int any_sib_match(ec_ctx *c, const ec_node *e)
{
	uint16_t k;

	if (e->id && atom_sib_matches(c, e->id, EC_B_ID, e))
		return 1;
	for (k = 0; k < e->ncls; k++)
		if (atom_sib_matches(c, e->cls[k], EC_B_CLASS, e))
			return 1;
	if (atom_sib_matches(c, e->name, EC_B_TAG, e))
		return 1;
	for (k = 0; k < e->nattr; k++)
		if (atom_sib_matches(c, e->attr[k].name, EC_B_ATTR, e))
			return 1;
	if (c->base != NULL && sib_matches(c, &c->base->universal_sib, e))
		return 1;
	return sib_matches(c, &c->universal_sib, e);
}

/* an atom's rules: its own (or its base's), and for a base's atom this
   context's too */
static void collect_atom(ec_ctx *c, const ec_atom *a, int kind, const ec_node *e, int *mf)
{
	const ec_shadow *sh;
	if (a->rules[kind])
		collect(c, a->rules[kind], e, mf);
	if (a->shared && c->shadow != NULL && (sh = ec_shadow_of(c, a, 0)) != NULL && sh->rules[kind])
		collect(c, sh->rules[kind], e, mf);
}

/* the attributes selectors look at, in short: elements that differ here
 * can't share a style. A small hash of their names, lengths and end bytes. */
static uint32_t ref_sig(const ec_node *e)
{
	const ec_attr *a = e->attr, *end = a + e->nattr;
	uint32_t h = 1;

	for (; a < end; a++)
		if (a->name->ref) {
			h = (h << 5) + h + (uint32_t) (uintptr_t) a->name + a->len;
			if (a->len)
				h = (h << 5) + h + (uint8_t) a->val[0] + ((uint8_t) a->val[a->len - 1] << 8);
		}
	return h;
}

/* the same attributes, with the same values, that selectors look at */
static int same_ref_attrs(const ec_node *a, const ec_node *b)
{
	const ec_attr *x, *y, *ae = a->attr + a->nattr, *be = b->attr + b->nattr;
	int na = 0, nb = 0;

	for (x = a->attr; x < ae; x++) {
		if (!x->name->ref)
			continue;
		na++;
		for (y = b->attr; y < be && y->name != x->name; y++)
			;
		if (y == be || y->len != x->len || memcmp(x->val, y->val, x->len) != 0)
			return 0;
	}
	for (y = b->attr; y < be; y++)
		nb += y->name->ref != 0;
	return na == nb;
}

typedef struct cand {
	const ec_node *n;
	uint32_t sig;
} cand;

static const ec_style *shared_from(ec_ctx *c, const ec_node *e, uint32_t sig, const cand *cd,
		int ncand)
{
	int i, k;

	if (e->id != NULL)
		return NULL;
	for (i = ncand - 1; i >= 0; i--) {
		const ec_node *s = cd[i].n;
		if (cd[i].sig != sig || s->name != e->name || s->ncls != e->ncls ||
				s->flags != e->flags)
			continue;
		for (k = 0; k < e->ncls && s->cls[k] == e->cls[k]; k++)
			;
		if (k < e->ncls || !same_ref_attrs(s, e) || s->nhint != e->nhint ||
				(e->nhint > 0 && memcmp(s->hint, e->hint, e->nhint) != 0))
			continue;
		/* s matched no rule that looks at siblings; nor must e */
		if (any_sib_match(c, e))
			return NULL;
		return s->style;
	}
	return NULL;
}

static const ec_style *style_element(ec_ctx *c, ec_node *e, const ec_style *parent, int *mf)
{
	uint16_t k;
	int n, i, any_before = 0, any_after = 0;
	uint32_t h;
	centry *ce;
	ec_style *st;
	const ec_attr *sa;

	nhits = link_hits = 0;
	*mf = 0;
	if (e->id)
		collect_atom(c, e->id, EC_B_ID, e, mf);
	for (k = 0; k < e->ncls; k++)
		collect_atom(c, e->cls[k], EC_B_CLASS, e, mf);
	collect_atom(c, e->name, EC_B_TAG, e, mf);
	for (k = 0; k < e->nattr; k++)
		collect_atom(c, e->attr[k].name, EC_B_ATTR, e, mf);
	if (c->base != NULL)
		collect(c, &c->base->universal, e, mf);
	collect(c, &c->universal, e, mf);
	sa = find_attr(e, c->a_style);
	if (sa != NULL && nhits < MAX_HITS && !(c->opts & EC_COMPAT_NOINLINE)) {
		ec_block *b = inline_block(c, sa->val, sa->len);
		if (b != NULL) {
			hits[nhits].prio = (uint32_t) EC_ORIGIN_AUTHOR << 28 | 1u << 27 | 0x1000000;
			hits[nhits].order = 0xffffffff;
			hits[nhits].b = b;
			hits[nhits].pseudo = PE_NONE;
			hits[nhits].only = 0;
			nhits++;
		}
	}
	if (e->hint != NULL && e->nhint > 0 && nhits < MAX_HITS) {
		/* author level, below every author rule: specificity zero, first */
		ec_block *b = inline_block(c, e->hint, e->nhint);
		if (b != NULL) {
			hits[nhits].prio = (uint32_t) EC_ORIGIN_AUTHOR << 28;
			hits[nhits].order = 0;
			hits[nhits].b = b;
			hits[nhits].pseudo = PE_NONE;
			hits[nhits].only = 0;
			nhits++;
		}
	}
	c->st.matched += nhits;
	for (i = 0; (link_hits & SV_LINK) && i < nhits; i++)
		if (hits[i].only & SV_LINK) {
			/* :link on the link this element is or is in (the
			   nearest): on a visited one, as if it weren't, but for
			   colours, which are :visited's (CSS Selectors 4, 8.2) */
			const ec_node *l = e;
			int k;
			while (l != NULL && !(l->flags & EC_NODE_LINK))
				l = l->parent;
			if (l == NULL || !(l->flags & EC_NODE_VISITED))
				for (k = i; k < nhits; k++)
					hits[k].only &= (uint8_t) ~SV_LINK;
			break;
		}
	for (i = 0; i < nhits; i++) {
		ec_block *b = (ec_block *) hits[i].b;
		if (!b->parsed)
			ec_parse_block(c, b, &c->rmem);	/* a sheet's: kept with it */
		if (hits[i].pseudo == PE_BEFORE)
			any_before = 1;
		else if (hits[i].pseudo == PE_AFTER)
			any_after = 1;
	}
	sort_hits();
	n = build_key();
	h = key_hash(parent, n);
	ce = &cache[h & (CACHE_SIZE - 1)];
	if (ce->style != NULL && ce->hash == h && ce->parent == parent && ce->n == n &&
			memcmp(ce->key, keybuf, n * sizeof keybuf[0]) == 0) {
		c->st.cached++;
		return ce->style;
	}
	st = cascade_style(c, parent, PE_NONE);
	if (st == NULL)
		return parent;
	if (any_before)
		st->before = cascade_style(c, st, PE_BEFORE);
	if (any_after)
		st->after = cascade_style(c, st, PE_AFTER);
	{
		uintptr_t *key = ec_alloc(&c->smem, n * sizeof *key + 1);
		if (key != NULL) {
			memcpy(key, keybuf, n * sizeof *key);
			ce->hash = h;
			ce->n = n;
			ce->parent = parent;
			ce->key = key;
			ce->style = st;
		}
	}
	return st;
}

/* the cascade stops this much above the stack's floor: under an element
   it styles can go 12.5 KB more (cascade_style's 6.7 KB and a var() or
   grid-template-areas value's), while its own recursion takes 176 bytes a
   level, so it still goes deeper than the layout can */
#define EC_CASCADE_ROOM (14L * 1024)

/* below the stack's floor: every element under top takes its style,
   without going deeper on the stack */
static void style_flat(ec_node *top, const ec_style *st)
{
	ec_node *e = top->first;
	while (e != NULL) {
		e->style = st;
		if (e->first != NULL) {
			e = e->first;
			continue;
		}
		while (e != top && e->next == NULL)
			e = e->parent;
		e = e == top ? NULL : e->next;
	}
}

/* styles e and, unless one, its siblings after it; then their children */
static void style_list(ec_ctx *c, ec_node *e, const ec_style *parent, int one)
{
	cand cd[SHARE_CANDIDATES];
	int ncand = 0;

	for (; e != NULL; e = one ? NULL : e->next) {
		uint32_t sig;
		const ec_style *st;
		if (c->stopped || ((c->st.elements & 31) == 0 && ec_poll != NULL && !ec_poll() && (c->stopped = 1)))
			return;
		sig = e->id == NULL ? ref_sig(e) : 0;
		st = e->id == NULL ? shared_from(c, e, sig, cd, ncand) : NULL;
		c->st.elements++;
		if (st != NULL) {
			c->st.shared++;
			e->style = st;
		} else {
			int mf;
			e->style = st = style_element(c, e, parent, &mf);
			if (!(mf & MF_SIBLINGS) && e->id == NULL) {
				if (ncand == SHARE_CANDIDATES) {
					memmove(cd, cd + 1, (SHARE_CANDIDATES - 1) * sizeof cd[0]);
					ncand--;
				}
				cd[ncand].n = e;
				cd[ncand++].sig = sig;
			}
		}
		if (e->parent == NULL)
			c->root_style = st;
		if (e->first != NULL && EC_DEEP_BY(EC_CASCADE_ROOM)) {
			style_flat(e, st);
		} else if (e->first != NULL) {
			bloom_push(c, e);
			style_list(c, e->first, st, 0);
			bloom_pop(c, e);
		}
	}
}

int (*ec_poll)(void);

int ec_select(ec_ctx *c, ec_node *root)
{
	ec_arena *pm = c->pmem;
	c->smem.nomem = 0;
	c->stopped = 0;
	memset(cache, 0, sizeof cache);
	memset(icache, 0, sizeof icache);
	memset(c->bloom, 0, sizeof c->bloom);
	/* what var() makes parsed goes with the styles (ec_unstyle gives
	   it back), not with the sheets */
	c->pmem = &c->smem;
	if (root->parent != NULL) {
		/* a subtree: its ancestors go in the filter */
		ec_node *p;
		for (p = root->parent; p != NULL; p = p->parent)
			bloom_push(c, p);
		style_list(c, root, root->parent->style, 1);
		for (p = root->parent; p != NULL; p = p->parent)
			bloom_pop(c, p);
	} else {
		c->root_style = NULL;
		style_list(c, root, NULL, 1);
	}
	c->pmem = pm;
	return c->smem.nomem || c->stopped ? -1 : 0;
}
