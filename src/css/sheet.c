/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Stylesheets: rules, at-rules, media queries, @supports, selectors and
 * declarations (CSS Syntax 3, Selectors 4, Media Queries 4).
 *
 * Speed comes from doing less: @media blocks that don't apply are skipped
 * unread, and a rule's declarations are read only when the rule first
 * matches an element (ec_parse_block). Each selector is indexed under
 * its subject's rarest part: id, class, tag or attribute name. */
#include "ec_int.h"

#ifdef EW_ST
#define MAX_PRELUDE	1024	/* the ST edition: 20 KB, not 80, of a 1 MB ST */
#else
#define MAX_PRELUDE	4096
#endif
#ifdef EW_ST
#define MAX_VALUE	256
#define MAX_DECLS	256
#define MAX_NEST	4	/* levels of :not() and :is() */
#else
#define MAX_VALUE	512
#define MAX_DECLS	512
#define MAX_NEST	8
#endif

static ec_tok prelude[MAX_PRELUDE];

/* ------------------------------------------------------------ token lists */

/* tokens up to (not including) a { at depth 0 (or a ;, for an at-rule's
 * prelude), or the end; returns how many, or -1 if too many; *stop is the
 * token that ended it. A qualified rule's prelude runs on past a ; (CSS
 * Syntax 3, 5.5.3): it is then not a valid selector, and the rule goes. */
static int read_prelude(ec_lex *lx, ec_tok *t, int max, int *stop, int at)
{
	int n = 0, depth = 0;

	for (;;) {
		ec_tok k;
		int type = ec_next(lx, &k);
		if (type == T_EOF || (depth == 0 && (type == T_LBRACE || (at && type == T_SEMI)))) {
			*stop = type;
			break;
		}
		if (type == T_FUNC || type == T_LPAREN || type == T_LBRACK)
			depth++;
		else if ((type == T_RPAREN || type == T_RBRACK) && depth > 0)
			depth--;
		if (n < max)
			t[n] = k;
		n++;
	}
	return n <= max ? n : -1;
}

/* the value tokens from lx up to a ; at depth 0 or the end: whitespace
 * marked on the next token, not kept; *bad if there is a { at depth 0 */
static int read_value(ec_lex *lx, ec_tok *t, int max, const char **start, const char **end,
		int *hasvar, int *bad)
{
	int n = 0, depth = 0, ws = 0;

	*start = *end = NULL;
	*hasvar = 0;
	for (;;) {
		const char *before = lx->p;
		ec_tok k;
		int type = ec_next(lx, &k);
		if (type == T_EOF || (depth == 0 && type == T_SEMI))
			break;
		if (type == T_WS) {
			ws = 1;
			continue;
		}
		if (depth == 0 && (type == T_LBRACE || type == T_RBRACE || type == T_BADSTR ||
				type == T_BADURL))
			*bad = 1;
		if (type == T_LBRACE) {
			lx->p = ec_skip_block(lx->p, lx->end, '{');
			*end = lx->p;
			continue;
		}
		if (type == T_FUNC || type == T_LPAREN || type == T_LBRACK)
			depth++;
		else if ((type == T_RPAREN || type == T_RBRACK) && depth > 0)
			depth--;
		if (type == T_FUNC && (ec_tok_is(&k, "var") || ec_tok_is(&k, "env")))
			*hasvar = 1;
		if (*start == NULL)
			*start = before;
		*end = lx->p;
		if (ws)
			k.flags |= TF_WS;
		ws = 0;
		if (n < max)
			t[n] = k;
		n++;
	}
	return n <= max ? n : -1;
}

static int skip_ws(const ec_tok *t, int i, int n)
{
	while (i < n && t[i].type == T_WS)
		i++;
	return i;
}

/* the index past the ) matching the function or ( at t[i] */
static int close_of(const ec_tok *t, int i, int n)
{
	int depth = 0;

	for (; i < n; i++) {
		int ty = t[i].type;
		if (ty == T_FUNC || ty == T_LPAREN || ty == T_LBRACK)
			depth++;
		else if ((ty == T_RPAREN || ty == T_RBRACK) && --depth == 0)
			return i + 1;
	}
	return -1;
}

static inline int delim(const ec_tok *t, char ch)
{
	return t->type == T_DELIM && t->s[0] == ch;
}

/* an identifier's text, lower case, in buf */
static size_t ident_lower(const ec_tok *t, char *buf, size_t cap)
{
	size_t len, i;
	const char *s = ec_tok_text(t, buf, cap, &len);

	if (len >= cap)
		len = cap - 1;
	for (i = 0; i < len; i++) {
		char ch = s[i];
		buf[i] = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
	}
	buf[len] = 0;
	return len;
}

/* ------------------------------------------------------------ selectors */

typedef struct sbuild {
	ec_simple s[128];
	ec_compound comp[32];
	int ns, ncomp;
	uint32_t a, b, cc;		/* specificity */
	int pseudo, never;
} sbuild;

static const ec_sellist *sellist(ec_ctx *c, const ec_tok *t, int i, int n, int forgiving,
		uint32_t *maxspec, int *never_all);

/* an attribute a selector looks at: elements that share a style must agree
 * on it */
static void refattr(ec_ctx *c, const ec_atom *a)
{
	if (a != NULL)
		((ec_atom *) a)->ref = 1;
}

static ec_simple *add_simple(sbuild *b, int op)
{
	ec_simple *s;

	if (b->ns == 128)
		return NULL;
	s = &b->s[b->ns++];
	memset(s, 0, sizeof *s);
	s->op = op;
	return s;
}

/* An+B (CSS Syntax 3, 6), from the tokens' text with spaces taken out */
static int anb(const ec_tok *t, int i, int n, int16_t *a, int16_t *b)
{
	char buf[64];
	size_t len = 0;
	const char *p, *e;
	int sign = 1, va = 0, vb = 0, digits = 0;

	for (; i < n; i++) {
		const char *s = t[i].s, *se;
		if (t[i].type == T_WS)
			continue;
		if (t[i].type == T_DIM)
			se = t[i].u + t[i].ulen;
		else if (t[i].type == T_IDENT || t[i].type == T_NUM || t[i].type == T_DELIM)
			se = s + t[i].len;
		else
			return 0;
		if (t[i].flags & TF_ESC)
			return 0;
		while (s < se && len < sizeof buf - 1) {
			char ch = *s++;
			buf[len++] = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
		}
	}
	buf[len] = 0;
	if (strcmp(buf, "odd") == 0) {
		*a = 2;
		*b = 1;
		return 1;
	}
	if (strcmp(buf, "even") == 0) {
		*a = 2;
		*b = 0;
		return 1;
	}
	p = buf;
	e = buf + len;
	if (p < e && (*p == '+' || *p == '-'))
		sign = *p++ == '-' ? -1 : 1;
	while (p < e && *p >= '0' && *p <= '9') {
		va = va * 10 + (*p++ - '0');
		digits++;
		if (va > 30000)
			return 0;
	}
	if (p < e && *p == 'n') {
		p++;
		if (!digits)
			va = 1;
		*a = sign * va;
		if (p == e) {
			*b = 0;
			return 1;
		}
		if (*p != '+' && *p != '-')
			return 0;
		sign = *p++ == '-' ? -1 : 1;
		digits = 0;
		while (p < e && *p >= '0' && *p <= '9') {
			vb = vb * 10 + (*p++ - '0');
			digits++;
			if (vb > 30000)
				return 0;
		}
		if (!digits || p != e)
			return 0;
		*b = sign * vb;
		return 1;
	}
	if (!digits || p != e)
		return 0;
	*a = 0;
	*b = sign * va;
	return 1;
}

static const char *const never_pc[] = {
	"hover", "active", "focus", "focus-visible", "focus-within", "visited", "target",
	"target-within", "current", "past", "future", "playing", "paused", "autofill",
	"user-invalid", "user-valid", "modal", "fullscreen", "popover-open",
	"picture-in-picture", "valid", "invalid", "in-range", "out-of-range", "default",
	"indeterminate", "closed", "local-link", "blank", "user-error", NULL
};

static const struct { const char *s; int op; } simple_pc[] = {
	{ "root", S_ROOT }, { "scope", S_ROOT }, { "empty", S_EMPTY },
	{ "first-child", S_FIRST_CHILD }, { "last-child", S_LAST_CHILD },
	{ "only-child", S_ONLY_CHILD }, { "first-of-type", S_FIRST_OF_TYPE },
	{ "last-of-type", S_LAST_OF_TYPE }, { "only-of-type", S_ONLY_OF_TYPE },
	{ "link", S_LINK }, { "any-link", S_LINK }, { "enabled", S_ENABLED },
	{ "disabled", S_DISABLED }, { "checked", S_CHECKED }, { "required", S_REQUIRED },
	{ "optional", S_OPTIONAL }, { "read-write", S_READ_WRITE }, { "read-only", S_READ_ONLY },
	{ "placeholder-shown", S_PLACEHOLDER_SHOWN }, { "open", S_OPEN }, { "defined", -1 }, { NULL, 0 }
};

static const char *const known_pe[] = {
	"first-line", "first-letter", "marker", "placeholder", "selection", "backdrop",
	"file-selector-button", "cue", "grammar-error", "spelling-error", "target-text",
	"details-content", NULL
};

static int str_in(const char *s, const char *const *list)
{
	for (; *list; list++)
		if (strcmp(s, *list) == 0)
			return 1;
	return 0;
}

/* the attributes the pseudo-classes look at, for sharing styles */
static void pc_attrs(ec_ctx *c, int op)
{
	switch (op) {
	case S_LINK: refattr(c, c->a_href); break;
	case S_ENABLED: case S_DISABLED: refattr(c, c->a_disabled); break;
	case S_CHECKED: refattr(c, c->a_checked); refattr(c, c->a_selected); refattr(c, c->a_type); break;
	case S_REQUIRED: case S_OPTIONAL: refattr(c, c->a_required); break;
	case S_READ_WRITE: case S_READ_ONLY: refattr(c, c->a_readonly); refattr(c, c->a_disabled); break;
	case S_PLACEHOLDER_SHOWN: refattr(c, c->a_placeholder); refattr(c, c->a_value); break;
	case S_OPEN: refattr(c, c->a_open); break;
	case S_LANG: refattr(c, c->a_lang); break;
	case S_DIR: refattr(c, c->a_dir); break;
	}
}

/* one pseudo-class or pseudo-element at t[*i] (the colon); 0 if invalid */
static int pseudo(ec_ctx *c, sbuild *b, const ec_tok *t, int *i, int n)
{
	char name[64];
	int k = *i + 1, elem = 0, op;
	ec_simple *s;

	if (k < n && t[k].type == T_COLON) {
		elem = 1;
		k++;
	}
	if (k >= n || (t[k].type != T_IDENT && t[k].type != T_FUNC))
		return 0;
	ident_lower(&t[k], name, sizeof name);
	if (t[k].type == T_IDENT) {
		*i = k + 1;
		if (elem || strcmp(name, "before") == 0 || strcmp(name, "after") == 0 ||
				strcmp(name, "first-line") == 0 || strcmp(name, "first-letter") == 0) {
			if (b->pseudo)
				return 0;
			if ((c->opts & EC_COMPAT_SELECTORS) && strcmp(name, "before") &&
					strcmp(name, "after") && strcmp(name, "first-line") &&
					strcmp(name, "first-letter"))
				return 0;
			if (strcmp(name, "before") == 0)
				b->pseudo = PE_BEFORE;
			else if (strcmp(name, "after") == 0)
				b->pseudo = PE_AFTER;
			else if (str_in(name, known_pe))
				b->pseudo = PE_OTHER;
			else
				return 0;
			b->cc++;
			return 1;
		}
		if (b->pseudo)
			return 0;
		/* :host (CSS Scoping 1, 3.2.1) matches only in a shadow tree,
		   which EWeb has none of; valid, so a list such as Tailwind's
		   ":root, :host" still matches the root (LibCSS doesn't know it) */
		if (strcmp(name, "host") == 0 && (c->opts & EC_COMPAT_SELECTORS))
			return 0;
		if (str_in(name, never_pc) || strcmp(name, "host") == 0) {
			if (!add_simple(b, S_NEVER))
				return 0;
			b->never = 1;
			b->b++;
			return 1;
		}
		for (op = 0; simple_pc[op].s; op++)
			if (strcmp(name, simple_pc[op].s) == 0)
				break;
		if (!simple_pc[op].s)
			return 0;
		if (c->opts & EC_COMPAT_SELECTORS) {
			/* what LibCSS 0.9 parses, and nsbench's handler answers */
			int o = simple_pc[op].op;
			if (o == S_ENABLED || o == S_DISABLED || o == S_CHECKED || o == S_OPEN) {
				b->b++;
				b->never = 1;
				return add_simple(b, S_NEVER) != NULL;
			}
			if (o < 0 || o == S_REQUIRED || o == S_OPTIONAL || o == S_READ_WRITE ||
					o == S_READ_ONLY || o == S_PLACEHOLDER_SHOWN ||
					!strcmp(name, "any-link") || !strcmp(name, "scope"))
				return 0;
		}
		b->b++;
		if (simple_pc[op].op < 0)
			return 1;		/* always true */
		pc_attrs(c, simple_pc[op].op);
		return add_simple(b, simple_pc[op].op) != NULL;
	}
	/* functional */
	{
		int end = close_of(t, k, n);
		if (end < 0 || elem || b->pseudo)
			return 0;
		*i = end;
		if (strcmp(name, "not") == 0 || strcmp(name, "is") == 0 || strcmp(name, "where") == 0 ||
				strcmp(name, "matches") == 0) {
			uint32_t spec = 0;
			int never_all = 0;
			int is = name[0] != 'n';
			const ec_sellist *l;
			if ((c->opts & EC_COMPAT_SELECTORS) && is)
				return 0;
			l = sellist(c, t, k + 1, end - 1, is, &spec, &never_all);
			if (l == NULL)
				return 0;
			if ((c->opts & EC_COMPAT_SELECTORS) && (l->n != 1 || l->sel[0]->ncomp != 1 ||
					l->sel[0]->comp[0].n > 1))
				return 0;	/* CSS 3's :not() takes one simple selector */
			s = add_simple(b, is ? S_IS : S_NOT);
			if (s == NULL)
				return 0;
			s->arg = l;
			if (name[0] != 'w') {
				b->a += spec >> 16;
				b->b += (spec >> 8) & 255;
				b->cc += spec & 255;
			}
			if (is && never_all)
				b->never = 1;
			return 1;
		}
		if (strncmp(name, "nth-", 4) == 0) {
			static const struct { const char *s; int op; } nth[] = {
				{ "nth-child", S_NTH_CHILD }, { "nth-last-child", S_NTH_LAST_CHILD },
				{ "nth-of-type", S_NTH_OF_TYPE }, { "nth-last-of-type", S_NTH_LAST_OF_TYPE },
				{ NULL, 0 }
			};
			int16_t a, bb;
			for (op = 0; nth[op].s && strcmp(name, nth[op].s) != 0; op++)
				;
			if (!nth[op].s || !anb(t, k + 1, end - 1, &a, &bb))
				return 0;
			s = add_simple(b, nth[op].op);
			if (s == NULL)
				return 0;
			s->a = a;
			s->b = bb;
			b->b++;
			return 1;
		}
		if (strcmp(name, "dir") == 0 && (c->opts & EC_COMPAT_SELECTORS))
			return 0;
		if (strcmp(name, "lang") == 0 && (c->opts & EC_COMPAT_SELECTORS)) {
			b->b++;
			b->never = 1;
			return add_simple(b, S_NEVER) != NULL;
		}
		if ((strcmp(name, "host") == 0 || strcmp(name, "host-context") == 0) &&
				!(c->opts & EC_COMPAT_SELECTORS)) {
			/* :host() and :host-context(): as :host, nothing outside a
			   shadow tree (their argument isn't looked at) */
			b->b++;
			b->never = 1;
			return add_simple(b, S_NEVER) != NULL;
		}
		if (strcmp(name, "lang") == 0 || strcmp(name, "dir") == 0) {
			int j = skip_ws(t, k + 1, end - 1);
			char buf[64];
			size_t len;
			const char *txt;
			if (j >= end - 1 || (t[j].type != T_IDENT && t[j].type != T_STR) ||
					skip_ws(t, j + 1, end - 1) != end - 1)
				return 0;
			txt = ec_tok_text(&t[j], buf, sizeof buf, &len);
			s = add_simple(b, name[0] == 'l' ? S_LANG : S_DIR);
			if (s == NULL)
				return 0;
			s->arg = ec_str_new(c->pmem, txt, len);
			pc_attrs(c, s->op);
			b->b++;
			return s->arg != NULL;
		}
		return 0;
	}
}

static int attribute(ec_ctx *c, sbuild *b, const ec_tok *t, int *i, int n)
{
	char name[128];
	int k = skip_ws(t, *i + 1, n), op = S_ATTR;
	size_t len;
	ec_simple *s;

	if (k >= n || t[k].type != T_IDENT)
		return 0;
	len = ident_lower(&t[k], name, sizeof name);
	k = skip_ws(t, k + 1, n);
	if (k < n && t[k].type == T_DELIM) {
		char ch = t[k].s[0];
		if (ch == '=') {
			op = S_ATTR_EQ;
			k++;
		} else if (k + 1 < n && delim(&t[k + 1], '=') && strchr("~|^$*", ch)) {
			op = ch == '~' ? S_ATTR_INCL : ch == '|' ? S_ATTR_DASH : ch == '^' ? S_ATTR_PREFIX :
				ch == '$' ? S_ATTR_SUFFIX : S_ATTR_SUBSTR;
			k += 2;
		} else {
			return 0;
		}
	}
	s = add_simple(b, op);
	if (s == NULL)
		return 0;
	s->atom = ec_intern(c, name, len);
	if (op != S_ATTR) {
		char vb[512];
		size_t vl;
		const char *v;
		k = skip_ws(t, k, n);
		if (k >= n || (t[k].type != T_IDENT && t[k].type != T_STR))
			return 0;
		v = ec_tok_text(&t[k], vb, sizeof vb, &vl);
		s->arg = ec_str_new(c->pmem, v, vl);
		k = skip_ws(t, k + 1, n);
		if (k < n && t[k].type == T_IDENT && t[k].len == 1) {
			if ((t[k].s[0] | 32) == 'i')
				s->flags |= SF_ICASE;
			else if ((t[k].s[0] | 32) != 's')
				return 0;
			k = skip_ws(t, k + 1, n);
		}
	}
	if (k >= n || t[k].type != T_RBRACK)
		return 0;
	*i = k + 1;
	refattr(c, s->atom);
	b->b++;
	return s->atom != NULL;
}

/* one compound selector at t[*i]; 0 if invalid or empty */
static int compound(ec_ctx *c, sbuild *b, const ec_tok *t, int *i, int n)
{
	int first = b->ns, any = 0;
	ec_compound *cp;

	if (b->ncomp == 32)
		return 0;
	if (*i < n && (t[*i].type == T_IDENT || delim(&t[*i], '*'))) {
		if (*i + 1 < n && delim(&t[*i + 1], '|'))
			return 0;		/* namespaces: not supported */
		if (t[*i].type == T_IDENT) {
			char name[128];
			size_t len = ident_lower(&t[*i], name, sizeof name);
			ec_simple *s = add_simple(b, S_TAG);
			if (s == NULL)
				return 0;
			s->atom = ec_intern(c, name, len);
			b->cc++;
		}
		(*i)++;
		any = 1;
	}
	while (*i < n) {
		const ec_tok *k = &t[*i];
		if (k->type == T_HASH) {
			char buf[256];
			size_t len;
			const char *s;
			ec_simple *sp;
			if (!(k->flags & TF_ID) || b->pseudo)
				return 0;
			s = ec_tok_text(k, buf, sizeof buf, &len);
			sp = add_simple(b, S_ID);
			if (sp == NULL)
				return 0;
			sp->atom = ec_intern(c, s, len);
			b->a++;
			(*i)++;
		} else if (delim(k, '.')) {
			char buf[256];
			size_t len;
			const char *s;
			ec_simple *sp;
			if (*i + 1 >= n || t[*i + 1].type != T_IDENT || b->pseudo)
				return 0;
			s = ec_tok_text(&t[*i + 1], buf, sizeof buf, &len);
			sp = add_simple(b, S_CLASS);
			if (sp == NULL)
				return 0;
			sp->atom = ec_intern(c, s, len);
			b->b++;
			*i += 2;
		} else if (k->type == T_LBRACK) {
			if (b->pseudo || !attribute(c, b, t, i, n))
				return 0;
		} else if (k->type == T_COLON) {
			if (!pseudo(c, b, t, i, n))
				return 0;
		} else {
			break;
		}
		any = 1;
	}
	if (!any)
		return 0;
	cp = &b->comp[b->ncomp++];
	cp->first = first;
	cp->n = b->ns - first;
	cp->comb = C_NONE;
	return 1;
}

/* a complex selector from t[i] to n into an ec_sel (no block yet) */
static sbuild builders[MAX_NEST];	/* one per level of :not() and :is() */
static int builder_depth;

static ec_sel *complex(ec_ctx *c, const ec_tok *t, int i, int n)
{
	sbuild *b;
	ec_sel *sel = NULL;
	ec_compound *comp;
	ec_simple *simple;
	int k, ns;

	if (builder_depth == MAX_NEST)
		return NULL;
	b = &builders[builder_depth++];
	b->ns = b->ncomp = 0;
	b->a = b->b = b->cc = 0;
	b->pseudo = b->never = 0;
	i = skip_ws(t, i, n);
	for (;;) {
		int comb = C_NONE, ws;
		if (b->pseudo || !compound(c, b, t, &i, n))
			goto out;
		ws = i < n && t[i].type == T_WS;
		i = skip_ws(t, i, n);
		if (i >= n)
			break;
		if (delim(&t[i], '>'))
			comb = C_CHILD, i++;
		else if (delim(&t[i], '+'))
			comb = C_ADJ, i++;
		else if (delim(&t[i], '~'))
			comb = C_SIB, i++;
		else if (ws)
			comb = C_DESC;
		else
			goto out;
		i = skip_ws(t, i, n);
		if (i >= n)
			goto out;
		/* the combinator says how this compound relates to the next */
		b->comp[b->ncomp - 1].comb = comb;
	}
	/* store them right to left: the subject first, each compound's comb
	 * saying how the one after it (to the left) relates */
	ns = b->ns;
	sel = ec_alloc(c->pmem, sizeof *sel + b->ncomp * sizeof *comp + ns * sizeof *simple + 8);
	if (sel == NULL)
		goto out;
	comp = (ec_compound *) (sel + 1);
	simple = (ec_simple *) ((char *) comp + ((b->ncomp * sizeof *comp + 7) & ~7u));
	for (k = 0; k < b->ncomp; k++) {
		const ec_compound *src = &b->comp[b->ncomp - 1 - k];
		comp[k] = *src;
		comp[k].comb = k + 1 < b->ncomp ? b->comp[b->ncomp - 2 - k].comb : C_NONE;
	}
	memcpy(simple, b->s, ns * sizeof *simple);
	sel->block = NULL;
	sel->spec = (b->a > 255 ? 255 : b->a) << 16 | (b->b > 255 ? 255 : b->b) << 8 |
		(b->cc > 255 ? 255 : b->cc);
	sel->pseudo = b->pseudo;
	sel->ncomp = b->ncomp;
	sel->never = b->never;
	sel->comp = comp;
	sel->simple = simple;
	for (k = 0; k < b->ncomp; k++)
		if (comp[k].comb == C_ADJ || comp[k].comb == C_SIB)
			c->sibling_rules = 1;
out:
	builder_depth--;
	return sel;
}

/* a selector list from t[i] to n; forgiving lists drop bad selectors */
static const ec_sellist *sellist(ec_ctx *c, const ec_tok *t, int i, int n, int forgiving,
		uint32_t *maxspec, int *never_all)
{
	const ec_sel *sels[256];
	int ns = 0, start = i, depth = 0, nnever = 0;
	ec_sellist *l;

	*maxspec = 0;
	if (EC_DEEP())
		return NULL;		/* :is() in :is() deeper than the stack holds: not valid */
	for (; i <= n; i++) {
		if (i < n) {
			int ty = t[i].type;
			if (ty == T_FUNC || ty == T_LPAREN || ty == T_LBRACK)
				depth++;
			else if (ty == T_RPAREN || ty == T_RBRACK)
				depth--;
			if (!(ty == T_COMMA && depth == 0))
				continue;
		}
		{
			ec_sel *s = complex(c, t, start, i);
			if (s == NULL) {
				if (!forgiving)
					return NULL;
			} else if (ns < 256) {
				if (s->spec > *maxspec)
					*maxspec = s->spec;
				if (s->never)
					nnever++;
				sels[ns++] = s;
			}
		}
		start = i + 1;
	}
	if (ns == 0 && !forgiving)
		return NULL;
	*never_all = ns > 0 && nnever == ns;
	l = ec_alloc(c->pmem, sizeof *l + ns * sizeof sels[0]);
	if (l == NULL)
		return NULL;
	l->n = ns;
	memcpy(l->sel, sels, ns * sizeof sels[0]);
	return l;
}

/* ------------------------------------------------------------ indexing */

static int vec_push(ec_arena *a, ec_vec *v, const ec_rent *e)
{
	if (v->n == v->cap) {
		uint32_t cap = v->cap ? v->cap * 2 : 4;
		ec_rent *ne = ec_alloc(a, cap * sizeof *ne);
		if (ne == NULL)
			return -1;
		if (v->n)
			memcpy(ne, v->e, v->n * sizeof *ne);
		v->e = ne;		/* the old array stays in the arena */
		v->cap = cap;
	}
	v->e[v->n++] = *e;
	return 0;
}

/* whether a simple selector looks at siblings or children */
static int sib_simple(const ec_simple *x);

/* whether matching s at an element can turn on that element's siblings or
 * children: in the compounds from the subject to the first descendant or
 * child combinator */
static int sib_sel(const ec_sel *s)
{
	int k, j;

	for (k = 0; k < s->ncomp; k++) {
		for (j = 0; j < s->comp[k].n; j++)
			if (sib_simple(&s->simple[s->comp[k].first + j]))
				return 1;
		if (s->comp[k].comb == C_ADJ || s->comp[k].comb == C_SIB)
			return 1;
		if (s->comp[k].comb != C_NONE)
			break;		/* a descendant or child: ancestors from here */
	}
	return 0;
}

static int sib_simple(const ec_simple *x)
{
	if (x->op == S_NOT || x->op == S_IS) {
		const ec_sellist *l = x->arg;
		int k;
		for (k = 0; k < l->n; k++)
			if (sib_sel(l->sel[k]))
				return 1;
		return 0;
	}
	return x->op >= S_EMPTY && x->op <= S_NTH_LAST_OF_TYPE;
}

static void index_sel(ec_ctx *c, const ec_sel *s, int origin)
{
	const ec_compound *cp = &s->comp[0];
	const ec_atom *key = NULL, *anc[16];
	int kind = -1, k, j, na = 0;
	ec_rent e;
	ec_vec *v;

	/* the subject's key: id, then class, then tag, then attribute */
	for (k = 0; k < cp->n; k++) {
		const ec_simple *x = &s->simple[cp->first + k];
		int kk = x->op == S_ID ? EC_B_ID : x->op == S_CLASS ? EC_B_CLASS :
			x->op == S_TAG ? EC_B_TAG : (x->op >= S_ATTR && x->op <= S_ATTR_SUBSTR) ?
			EC_B_ATTR : -1;
		if (kk < 0)
			continue;
		if (kind < 0 || (kk == EC_B_ID) || (kk == EC_B_CLASS && kind > EC_B_CLASS) ||
				(kk == EC_B_TAG && kind == EC_B_ATTR)) {
			if (kind != EC_B_ID) {
				kind = kk;
				key = x->atom;
			}
		}
	}
	/* the ancestors' ids, classes and tags, for the filter: rarest first */
	for (k = 1; k < s->ncomp && na < 16; k++) {
		if (s->comp[k - 1].comb != C_DESC && s->comp[k - 1].comb != C_CHILD)
			continue;
		for (j = 0; j < s->comp[k].n && na < 16; j++) {
			const ec_simple *x = &s->simple[s->comp[k].first + j];
			if (x->op == S_ID || x->op == S_CLASS)
				anc[na++] = x->atom;
		}
	}
	for (k = 1; k < s->ncomp && na < 16; k++) {
		if (s->comp[k - 1].comb != C_DESC && s->comp[k - 1].comb != C_CHILD)
			continue;
		for (j = 0; j < s->comp[k].n && na < 16; j++) {
			const ec_simple *x = &s->simple[s->comp[k].first + j];
			if (x->op == S_TAG)
				anc[na++] = x->atom;
		}
	}
	memset(&e, 0, sizeof e);
	e.sel = s;
	e.prio = (uint32_t) origin << 28 | (uint32_t) !c->layer << 27 | s->spec;
	e.order = c->order++;
	for (k = 0; k < na && k < 4; k++)
		e.bloom[k] = anc[k]->bloom;
	e.sib = sib_sel(s);
	if (s->never) {
		vec_push(&c->rmem, &c->never, &e);
		return;
	}
	if (s->pseudo == PE_OTHER) {
		c->st.dropped++;
		return;
	}
	if (key == NULL) {
		vec_push(&c->rmem, &c->universal, &e);
		if (e.sib)
			vec_push(&c->rmem, &c->universal_sib, &e);
		return;
	}
	{
		/* a base's atom keeps the base's rules; this context's go in
		   its shadow */
		ec_vec **rules = ((ec_atom *) key)->rules, **sibrules = ((ec_atom *) key)->sibrules;
		if (key->shared && c->base != NULL) {
			ec_shadow *sh = ec_shadow_of(c, key, 1);
			if (sh == NULL)
				return;
			rules = sh->rules;
			sibrules = sh->sibrules;
		}
		v = rules[kind];
		if (v == NULL) {
			v = ec_alloc(&c->rmem, sizeof *v);
			if (v == NULL)
				return;
			memset(v, 0, sizeof *v);
			rules[kind] = v;
		}
		vec_push(&c->rmem, v, &e);
		if (e.sib) {
			v = sibrules[kind];
			if (v == NULL) {
				v = ec_alloc(&c->rmem, sizeof *v);
				if (v == NULL)
					return;
				memset(v, 0, sizeof *v);
				sibrules[kind] = v;
			}
			vec_push(&c->rmem, v, &e);
		}
	}
}

/* ------------------------------------------------------------ media queries */

typedef struct mq {
	ec_ctx *c;
	const ec_tok *t;
	int n;
} mq;

/* the widths a media query holds at as it does at c's (ec_media_span):
   each comparison the width takes part in narrows them */
static void span_narrow(ec_ctx *c, int64_t lo, int64_t hi)
{
	/* lo <= c's width <= hi: in 32 bits when they narrow */
	if (lo > c->span_lo)
		c->span_lo = (int32_t) lo;
	if (hi < c->span_hi)
		c->span_hi = (int32_t) hi;
}

/* only c's width itself */
static void span_pin(ec_ctx *c)
{
	span_narrow(c, c->media.width, c->media.width);
}

/* "width op x" (x in 22.10): the widths w (whole px) it goes the same
   way at; ge is the first w with w x 1024 >= x, le the last with
   w x 1024 <= x */
static void span_cmp(ec_ctx *c, int op, int64_t x)
{
	int64_t w = c->media.width, ge = -((-x) >> 10), le = x >> 10;

	switch (op) {
	case '<': if (w < ge) span_narrow(c, 0, ge - 1); else span_narrow(c, ge, INT32_MAX); break;
	case 'l': if (w <= le) span_narrow(c, 0, le); else span_narrow(c, le + 1, INT32_MAX); break;
	case '>': if (w > le) span_narrow(c, le + 1, INT32_MAX); else span_narrow(c, 0, le); break;
	case 'g': if (w >= ge) span_narrow(c, ge, INT32_MAX); else span_narrow(c, 0, ge - 1); break;
	default:
		if (w < ge)
			span_narrow(c, 0, ge - 1);
		else if (w > le)
			span_narrow(c, le + 1, INT32_MAX);
		else
			span_pin(c);
		break;
	}
}

/* a feature compared, as it goes with the width (feature_value's dep):
   the width itself, or the aspect ratio (only this width) */
static void span_of(ec_ctx *c, int dep, int op, int64_t x)
{
	if (dep == 'w')
		span_cmp(c, op, x);
	else if (dep == 'a')
		span_pin(c);
}

/* a length in a media query: px, with em and rem the default font size */
static int mq_length(ec_ctx *c, const ec_tok *t, int32_t *px)
{
	int32_t u;

	if (t->type == T_NUM && t->num == 0) {
		*px = 0;
		return 1;
	}
	if (t->type != T_DIM || !ec_names_lookup(KC_UNIT, t->u, t->ulen, &u))
		return 0;
	if (u > 0)
		*px = fx_mul(t->num, u);
	else if (u == EC_UNIT_REL(CU_EM) || u == EC_UNIT_REL(CU_REM))
		*px = t->num * c->media.font_size;
	else if (u == EC_UNIT_REL(CU_EX) || u == EC_UNIT_REL(CU_CH))
		*px = t->num * c->media.font_size / 2;
	else if (u == EC_UNIT_REL(CU_VW)) {
		*px = t->num * c->media.width / 100;
		span_pin(c);		/* a length that goes with the width */
	} else if (u == EC_UNIT_REL(CU_VH))
		*px = t->num * c->media.height / 100;
	else
		return 0;
	return 1;
}

static int cmp_ok(int64_t a, int op, int64_t b)
{
	switch (op) {
	case '<': return a < b;
	case 'l': return a <= b;
	case '>': return a > b;
	case 'g': return a >= b;
	default: return a == b;
	}
}

/* a feature's value for the Falcon: 1 if it has one of this kind; *dep
   how it goes with the viewport's width ('w' it is the width, 'a' the
   aspect ratio, 'o' the orientation, 0 not) */
static int feature_value(ec_ctx *c, const char *name, int *kind, int64_t *v, const char **word, int *dep)
{
	*word = NULL;
	*dep = 0;
	if (!strcmp(name, "width") || !strcmp(name, "device-width")) {
		*kind = 'L';
		*v = EC_FX(c->media.width);
		*dep = 'w';
	} else if (!strcmp(name, "height") || !strcmp(name, "device-height")) {
		*kind = 'L';
		*v = EC_FX(c->media.height);
	} else if (!strcmp(name, "aspect-ratio") || !strcmp(name, "device-aspect-ratio")) {
		*kind = 'R';
		*v = ((int64_t) c->media.width << 10) / c->media.height;
		*dep = 'a';
	} else if (!strcmp(name, "color")) {
		*kind = 'N';
		*v = EC_FX(5);		/* the Falcon's 16-bit colour: 5 bits */
	} else if (!strcmp(name, "monochrome") || !strcmp(name, "grid") ||
			!strcmp(name, "color-index")) {
		*kind = 'N';
		*v = 0;
	} else if (!strcmp(name, "resolution")) {
		*kind = 'D';
		*v = EC_FX(96);
	} else if (!strcmp(name, "orientation")) {
		*kind = 'W';
		*word = c->media.width >= c->media.height ? "landscape" : "portrait";
		*dep = 'o';
	} else if (!strcmp(name, "hover") || !strcmp(name, "any-hover")) {
		*kind = 'W';
		*word = "hover";
	} else if (!strcmp(name, "pointer") || !strcmp(name, "any-pointer")) {
		*kind = 'W';
		*word = "fine";
	} else if (!strcmp(name, "prefers-color-scheme")) {
		*kind = 'W';
		*word = "light";
	} else if (!strcmp(name, "prefers-reduced-motion")) {
		*kind = 'W';
		*word = "reduce";	/* EWeb doesn't animate */
	} else if (!strcmp(name, "prefers-reduced-transparency") ||
			!strcmp(name, "prefers-reduced-data") || !strcmp(name, "prefers-contrast") ||
			!strcmp(name, "forced-colors") || !strcmp(name, "inverted-colors")) {
		*kind = 'W';
		*word = !strcmp(name, "prefers-contrast") ? "no-preference" :
			!strcmp(name, "forced-colors") || !strcmp(name, "inverted-colors") ? "none" :
			"no-preference";
	} else if (!strcmp(name, "scripting")) {
		*kind = 'W';
		*word = "none";
	} else if (!strcmp(name, "update")) {
		*kind = 'W';
		*word = "slow";
	} else if (!strcmp(name, "color-gamut")) {
		*kind = 'W';
		*word = "srgb";
	} else if (!strcmp(name, "display-mode")) {
		*kind = 'W';
		*word = "browser";
	} else if (!strcmp(name, "overflow-block") || !strcmp(name, "overflow-inline")) {
		*kind = 'W';
		*word = "scroll";
	} else if (!strcmp(name, "dynamic-range") || !strcmp(name, "video-dynamic-range")) {
		*kind = 'W';
		*word = "standard";
	} else {
		return 0;
	}
	return 1;
}

/* a value of kind for a feature at t[*i] */
static int mq_operand(ec_ctx *c, const ec_tok *t, int *i, int n, int kind, int64_t *v)
{
	int32_t px;

	if (*i >= n)
		return 0;
	switch (kind) {
	case 'L':
		if (!mq_length(c, &t[*i], &px))
			return 0;
		*v = px;
		(*i)++;
		return 1;
	case 'N':
		if (t[*i].type != T_NUM)
			return 0;
		*v = t[*i].num;
		(*i)++;
		return 1;
	case 'R': {
		int64_t a, b;
		int j;
		if (t[*i].type != T_NUM)
			return 0;
		a = t[*i].num;
		j = skip_ws(t, *i + 1, n);
		if (j < n && delim(&t[j], '/')) {
			j = skip_ws(t, j + 1, n);
			if (j >= n || t[j].type != T_NUM || t[j].num <= 0)
				return 0;
			b = t[j].num;
			*i = j + 1;
		} else {
			b = 1024;
			(*i)++;
		}
		*v = (a << 10) / b;
		return 1;
	}
	case 'D': {
		const ec_tok *k = &t[*i];
		if (k->type != T_DIM)
			return 0;
		if (k->ulen == 3 && !memcmp(k->u, "dpi", 3))
			*v = k->num;
		else if (k->ulen == 4 && !memcmp(k->u, "dppx", 4))
			*v = (int64_t) k->num * 96;
		else if (k->ulen == 1 && (k->u[0] | 32) == 'x')
			*v = (int64_t) k->num * 96;
		else if (k->ulen == 4 && !memcmp(k->u, "dpcm", 4))
			*v = (int64_t) k->num * 254 / 100;
		else
			return 0;
		(*i)++;
		return 1;
	}
	}
	return 0;
}

/* a comparison operator at t[*i]: <, <=, >, >= or = */
static int mq_op(const ec_tok *t, int *i, int n)
{
	int op;

	if (*i >= n || t[*i].type != T_DELIM)
		return 0;
	op = t[*i].s[0];
	if (op != '<' && op != '>' && op != '=')
		return 0;
	(*i)++;
	if (op != '=' && *i < n && delim(&t[*i], '=')) {
		op = op == '<' ? 'l' : 'g';
		(*i)++;
	}
	return op;
}

static int flip(int op)
{
	return op == '<' ? '>' : op == '>' ? '<' : op == 'l' ? 'g' : op == 'g' ? 'l' : op;
}

/* a media feature inside ( ), from t[i] to n: 1 true, 0 false, -1 invalid */
static int mq_feature(ec_ctx *c, const ec_tok *t, int i, int n)
{
	char name[64];
	int kind, j, prefix = 0, dep;
	int64_t v, x;
	const char *word;

	i = skip_ws(t, i, n);
	if (i >= n)
		return -1;
	if (t[i].type == T_IDENT) {
		const char *nm = name;
		ident_lower(&t[i], name, sizeof name);
		j = skip_ws(t, i + 1, n);
		if (!strncmp(name, "min-", 4))
			prefix = 'g', nm = name + 4;
		else if (!strncmp(name, "max-", 4))
			prefix = 'l', nm = name + 4;
		if (!feature_value(c, nm, &kind, &v, &word, &dep))
			return 2;		/* unknown (Media Queries 4, 3) */
		/* LibCSS 0.9 knows width, height and prefers-color-scheme */
		if ((c->opts & EC_COMPAT_MEDIA) && strcmp(nm, "width") && strcmp(nm, "height") &&
				strcmp(nm, "prefers-color-scheme"))
			return 2;
		if (j >= n)			/* (feature): is it non-zero? */
			return prefix ? -1 : kind == 'W' ? strcmp(word, "none") && strcmp(word,
				"no-preference") : v != 0;
		if (t[j].type == T_COLON) {
			j = skip_ws(t, j + 1, n);
			if (kind == 'W') {
				char w[64];
				if (prefix || j >= n || t[j].type != T_IDENT ||
						skip_ws(t, j + 1, n) != n)
					return -1;
				ident_lower(&t[j], w, sizeof w);
				if (dep == 'o')	/* landscape: the width at least the height */
					span_cmp(c, 'g', EC_FX((int64_t) c->media.height));
				return strcmp(w, word) == 0;
			}
			if (!mq_operand(c, t, &j, n, kind, &x) || skip_ws(t, j, n) != n)
				return -1;
			span_of(c, dep, prefix ? prefix : '=', x);
			return cmp_ok(v, prefix ? prefix : '=', x);
		}
		/* feature op value */
		if (prefix || kind == 'W')
			return -1;
		{
			int op = mq_op(t, &j, n);
			if (!op)
				return -1;
			j = skip_ws(t, j, n);
			if (!mq_operand(c, t, &j, n, kind, &x) || skip_ws(t, j, n) != n)
				return -1;
			span_of(c, dep, op, x);
			return cmp_ok(v, op, x);
		}
	}
	/* value op feature [op value] */
	{
		int j2, op1, op2, r;
		int64_t a;
		const ec_tok *ft;
		/* find the feature name to know the kind */
		for (j2 = i; j2 < n && t[j2].type != T_IDENT; j2++)
			;
		if (j2 >= n)
			return -1;
		ft = &t[j2];
		ident_lower(ft, name, sizeof name);
		if (!feature_value(c, name, &kind, &v, &word, &dep))
			return 2;
		if (kind == 'W')
			return -1;
		j = i;
		if (!mq_operand(c, t, &j, n, kind, &a))
			return -1;
		j = skip_ws(t, j, n);
		op1 = mq_op(t, &j, n);
		j = skip_ws(t, j, n);
		if (!op1 || j != j2)
			return -1;
		r = cmp_ok(a, op1, v);
		j = skip_ws(t, j2 + 1, n);
		if (j < n) {
			op2 = mq_op(t, &j, n);
			j = skip_ws(t, j, n);
			if (!op2 || !mq_operand(c, t, &j, n, kind, &x) || skip_ws(t, j, n) != n)
				return -1;
			/* the second comparison only when the first held */
			span_of(c, dep, flip(op1), a);
			if (r)
				span_of(c, dep, op2, x);
			return r && cmp_ok(v, op2, x);
		}
		span_of(c, dep, flip(op1), a);
		return r;
	}
}

static int mq_condition(ec_ctx *c, const ec_tok *t, int i, int n, int allow_or);

/* ( condition ) or ( feature ) at t[*i]; a condition nested deeper than
   a few is unknown (a page's could otherwise take the Atari's stack) */
static int mq_depth;

static int mq_in_parens(ec_ctx *c, const ec_tok *t, int *i, int n)
{
	int end, k, r;

	if (*i >= n)
		return -1;
	if (t[*i].type == T_FUNC) {	/* general enclosed: unknown */
		end = close_of(t, *i, n);
		if (end < 0)
			return -1;
		*i = end;
		return 2;
	}
	if (t[*i].type != T_LPAREN)
		return -1;
	end = close_of(t, *i, n);
	if (end < 0)
		return -1;
	k = skip_ws(t, *i + 1, end - 1);
	*i = end;
	if (k < end - 1 && (t[k].type == T_LPAREN || (t[k].type == T_IDENT &&
			ec_tok_is(&t[k], "not")))) {
		if (mq_depth >= 16)
			return 2;
		mq_depth++;
		r = mq_condition(c, t, k, end - 1, 1);
		mq_depth--;
	} else
		r = mq_feature(c, t, k, end - 1);
	return r < 0 ? 2 : r;		/* not understood: unknown */
}

/* three-valued logic: 1 true, 0 false, 2 unknown */
static int k_not(int a)
{
	return a == 2 ? 2 : !a;
}

static int k_and(int a, int b)
{
	return a == 0 || b == 0 ? 0 : a == 2 || b == 2 ? 2 : 1;
}

static int k_or(int a, int b)
{
	return a == 1 || b == 1 ? 1 : a == 2 || b == 2 ? 2 : 0;
}

/* not X | X and Y ... | X or Y ... */
static int mq_condition(ec_ctx *c, const ec_tok *t, int i, int n, int allow_or)
{
	int r, conj = 0;

	i = skip_ws(t, i, n);
	if (i < n && t[i].type == T_IDENT && ec_tok_is(&t[i], "not")) {
		i = skip_ws(t, i + 1, n);
		r = mq_in_parens(c, t, &i, n);
		if (r < 0 || skip_ws(t, i, n) != n)
			return -1;
		return k_not(r);
	}
	r = mq_in_parens(c, t, &i, n);
	if (r < 0)
		return -1;
	for (;;) {
		int x, which;
		i = skip_ws(t, i, n);
		if (i >= n)
			return r;
		if (t[i].type != T_IDENT)
			return -1;
		which = ec_tok_is(&t[i], "and") ? 'a' : ec_tok_is(&t[i], "or") ? 'o' : 0;
		if (!which || (which == 'o' && !allow_or) || (conj && conj != which))
			return -1;
		conj = which;
		i = skip_ws(t, i + 1, n);
		x = mq_in_parens(c, t, &i, n);
		if (x < 0)
			return -1;
		r = which == 'a' ? k_and(r, x) : k_or(r, x);
	}
}

/* one media query from t[i] to n */
static int media_query(ec_ctx *c, const ec_tok *t, int i, int n)
{
	int neg = 0, r = 1;
	char w[32];

	i = skip_ws(t, i, n);
	if (i >= n)
		return 0;
	if (t[i].type == T_LPAREN || (t[i].type == T_IDENT && ec_tok_is(&t[i], "not") &&
			skip_ws(t, i + 1, n) < n && t[skip_ws(t, i + 1, n)].type == T_LPAREN))
		return mq_condition(c, t, i, n, 1) == 1;
	if (t[i].type != T_IDENT)
		return 0;
	ident_lower(&t[i], w, sizeof w);
	if (!strcmp(w, "not") || !strcmp(w, "only")) {
		neg = w[0] == 'n';
		i = skip_ws(t, i + 1, n);
		if (i < n && t[i].type == T_LPAREN && neg) {
			r = mq_condition(c, t, i, n, 1);
			return r < 0 ? 0 : k_not(r) == 1;
		}
		if (i >= n || t[i].type != T_IDENT)
			return 0;
		ident_lower(&t[i], w, sizeof w);
	}
	if (!strcmp(w, "and") || !strcmp(w, "or") || !strcmp(w, "not") || !strcmp(w, "only"))
		return 0;
	r = !strcmp(w, "all") || !strcmp(w, "screen");
	i = skip_ws(t, i + 1, n);
	if (i < n) {
		int x;
		if (t[i].type != T_IDENT || !ec_tok_is(&t[i], "and"))
			return 0;
		x = mq_condition(c, t, i + 1, n, 0);
		if (x < 0)
			return 0;
		r = k_and(r, x);
	}
	/* unknown is false, and so is not unknown */
	return r == 2 ? 0 : neg ? !r : r;
}

/* a media query list: true if any query is */
static int media_list(ec_ctx *c, const ec_tok *t, int i, int n)
{
	int start = i, depth = 0, any = 0, empty = 1;

	for (; i <= n; i++) {
		if (i < n) {
			int ty = t[i].type;
			if (ty != T_WS)
				empty = 0;
			if (ty == T_FUNC || ty == T_LPAREN)
				depth++;
			else if (ty == T_RPAREN)
				depth--;
			if (!(ty == T_COMMA && depth == 0))
				continue;
		}
		if (media_query(c, t, start, i))
			any = 1;
		start = i + 1;
	}
	return empty || any;
}

int ec_media_ok(ec_ctx *c, const char *s, size_t len)
{
	ec_lex lx;
	int n, stop;

	ec_lex_init(&lx, s, len);
	n = read_prelude(&lx, prelude, MAX_PRELUDE, &stop, 0);
	return n >= 0 && stop == T_EOF && media_list(c, prelude, 0, n);
}

/* a length in sizes, t[*i] on: a dimension, or calc(), min(), max() or
   clamp() of them (+ - between lengths, * / by numbers); px x 1024, and
   in *num 1 if it is a number. depth: brackets inside brackets, a few
   (a page's could otherwise take the Atari's stack) */
#define SZ_DEPTH 8

static int sz_sum(ec_ctx *c, const ec_tok *t, int *i, int n, int64_t *v, int *num, int depth);

static int sz_value(ec_ctx *c, const ec_tok *t, int *i, int n, int64_t *v, int *num, int depth)
{
	int j = skip_ws(t, *i, n), end;
	int32_t px;

	if (j >= n || depth > SZ_DEPTH)
		return 0;
	*num = 0;
	if (t[j].type == T_NUM) {
		*v = t[j].num;
		*num = 1;
		*i = j + 1;
		return 1;
	}
	if (t[j].type == T_DIM) {
		if (!mq_length(c, &t[j], &px))
			return 0;
		*v = px;
		*i = j + 1;
		return 1;
	}
	if (t[j].type != T_FUNC && t[j].type != T_LPAREN)
		return 0;
	end = close_of(t, j, n);
	if (end < 0)
		return 0;
	if (t[j].type == T_LPAREN || ec_tok_is(&t[j], "calc")) {
		int k = j + 1;
		if (!sz_sum(c, t, &k, end - 1, v, num, depth + 1) || skip_ws(t, k, end - 1) != end - 1)
			return 0;
	} else {
		/* min(a, b ...), max(...), clamp(lo, v, hi) */
		int fn = ec_tok_is(&t[j], "min") ? 'n' : ec_tok_is(&t[j], "max") ? 'x' :
			ec_tok_is(&t[j], "clamp") ? 'c' : 0, k = j + 1, cnt = 0, nm;
		int64_t a[3] = { 0, 0, 0 }, x;
		if (!fn)
			return 0;
		for (;;) {
			if (!sz_sum(c, t, &k, end - 1, &x, &nm, depth + 1) || nm)
				return 0;
			if (fn == 'c') {
				if (cnt == 3)
					return 0;
				a[cnt] = x;
			} else if (cnt == 0 || (fn == 'n' ? x < a[0] : x > a[0])) {
				a[0] = x;
			}
			cnt++;
			k = skip_ws(t, k, end - 1);
			if (k >= end - 1)
				break;
			if (t[k].type != T_COMMA)
				return 0;
			k++;
		}
		if (fn == 'c') {
			if (cnt != 3)
				return 0;
			a[0] = a[1] < a[0] ? a[0] : a[1] > a[2] ? a[2] : a[1];
		}
		*v = a[0];
	}
	*i = end;
	return 1;
}

static int sz_product(ec_ctx *c, const ec_tok *t, int *i, int n, int64_t *v, int *num, int depth)
{
	int64_t x;
	int nm;

	if (!sz_value(c, t, i, n, v, num, depth))
		return 0;
	for (;;) {
		int j = skip_ws(t, *i, n), op;
		if (j >= n || !(delim(&t[j], '*') || delim(&t[j], '/')))
			return 1;
		op = t[j].s[0];
		*i = j + 1;
		if (!sz_value(c, t, i, n, &x, &nm, depth))
			return 0;
		if (op == '*') {
			if (!*num && !nm)
				return 0;
			*v = *v * x >> 10;
			*num = *num && nm;
		} else {
			if (!nm || x == 0)
				return 0;
			*v = (*v << 10) / x;
		}
	}
}

static int sz_sum(ec_ctx *c, const ec_tok *t, int *i, int n, int64_t *v, int *num, int depth)
{
	int64_t x;
	int nm;

	if (!sz_product(c, t, i, n, v, num, depth))
		return 0;
	for (;;) {
		int j = *i, op;
		/* + and - need white space round them */
		if (j >= n || t[j].type != T_WS)
			return 1;
		j = skip_ws(t, j, n);
		if (j >= n || !(delim(&t[j], '+') || delim(&t[j], '-')))
			return 1;
		op = t[j].s[0];
		*i = j + 1;
		if (*i >= n || t[*i].type != T_WS || !sz_product(c, t, i, n, &x, &nm, depth) || nm != *num)
			return 0;
		*v = op == '+' ? *v + x : *v - x;
	}
}

int32_t ec_sizes(ec_ctx *c, const char *s, size_t len)
{
	ec_lex lx;
	int n, stop, a = 0;

	ec_lex_init(&lx, s, len);
	n = read_prelude(&lx, prelude, MAX_PRELUDE, &stop, 0);
	if (n < 0 || stop != T_EOF)
		return -1;
	while (a < n) {
		int b = a, depth = 0, e, l0, k, num;
		int64_t v;
		/* this entry: to a comma outside brackets */
		for (; b < n; b++) {
			int ty = prelude[b].type;
			if (ty == T_FUNC || ty == T_LPAREN)
				depth++;
			else if (ty == T_RPAREN)
				depth--;
			else if (ty == T_COMMA && depth == 0)
				break;
		}
		e = b;
		while (e > a && prelude[e - 1].type == T_WS)
			e--;
		/* its length: the last component, after the media condition */
		l0 = e - 1;
		if (l0 >= a && prelude[l0].type == T_RPAREN) {
			for (depth = 0; l0 >= a; l0--) {
				int ty = prelude[l0].type;
				if (ty == T_RPAREN)
					depth++;
				else if ((ty == T_FUNC || ty == T_LPAREN) && --depth == 0)
					break;
			}
		}
		k = l0;
		if (l0 >= a && prelude[l0].type != T_LPAREN && sz_value(c, prelude, &k, e, &v, &num, 0) &&
		    k == e && !num && v >= 0) {
			int cond = skip_ws(prelude, a, l0) >= l0 ? 1 : mq_condition(c, prelude, a, l0, 1);
			if (cond == 1)
				return (int32_t) (v >> 10);
		}
		a = b + 1;
	}
	return -1;
}

/* ------------------------------------------------------------ @supports */

static int supports_cond(ec_ctx *c, const ec_tok *t, int i, int n);

static int supports_in_parens(ec_ctx *c, const ec_tok *t, int *i, int n)
{
	int end, k;

	if (*i >= n || EC_DEEP())
		return -1;
	if (t[*i].type == T_FUNC) {
		end = close_of(t, *i, n);
		if (end < 0)
			return -1;
		if (ec_tok_is(&t[*i], "selector")) {
			uint32_t spec;
			int never;
			int ok = sellist(c, t, *i + 1, end - 1, 0, &spec, &never) != NULL;
			*i = end;
			return ok;
		}
		*i = end;
		return 0;
	}
	if (t[*i].type != T_LPAREN)
		return -1;
	end = close_of(t, *i, n);
	if (end < 0)
		return -1;
	k = skip_ws(t, *i + 1, end - 1);
	*i = end;
	if (k < end - 1 && t[k].type == T_IDENT) {
		int j = skip_ws(t, k + 1, end - 1);
		if (j < end - 1 && t[j].type == T_COLON) {
			/* a declaration: does EWeb take it? */
			ec_tok v[64];
			ec_decl d[16];
			int nv = 0, m, prop, ws = 0;
			char name[64];
			size_t len = ident_lower(&t[k], name, sizeof name);
			if (len > 2 && name[0] == '-' && name[1] == '-')
				return 1;
			prop = ec_prop_lookup(name, len);
			if (prop < 0)
				return 0;
			for (m = j + 1; m < end - 1 && nv < 64; m++) {
				if (t[m].type == T_WS) {
					ws = 1;
					continue;
				}
				v[nv] = t[m];
				if (ws)
					v[nv].flags |= TF_WS;
				ws = 0;
				nv++;
			}
			if (nv > 1 && delim(&v[nv - 2], '!') && ec_tok_is(&v[nv - 1], "important"))
				nv -= 2;
			return ec_parse_value(c, prop, v, nv, d, 16) > 0;
		}
	}
	{
		int r = supports_cond(c, t, k, end - 1);
		return r < 0 ? 0 : r;
	}
}

static int supports_cond(ec_ctx *c, const ec_tok *t, int i, int n)
{
	int r, conj = 0;

	i = skip_ws(t, i, n);
	if (i < n && t[i].type == T_IDENT && ec_tok_is(&t[i], "not")) {
		i = skip_ws(t, i + 1, n);
		r = supports_in_parens(c, t, &i, n);
		if (r < 0 || skip_ws(t, i, n) != n)
			return -1;
		return !r;
	}
	r = supports_in_parens(c, t, &i, n);
	if (r < 0)
		return -1;
	for (;;) {
		int x, which;
		i = skip_ws(t, i, n);
		if (i >= n)
			return r;
		if (t[i].type != T_IDENT)
			return -1;
		which = ec_tok_is(&t[i], "and") ? 'a' : ec_tok_is(&t[i], "or") ? 'o' : 0;
		if (!which || (conj && conj != which))
			return -1;
		conj = which;
		i = skip_ws(t, i + 1, n);
		x = supports_in_parens(c, t, &i, n);
		if (x < 0)
			return -1;
		r = which == 'a' ? r && x : r || x;
	}
}

/* ------------------------------------------------------------ rules */

static void rules(ec_ctx *c, const char *p, const char *end, int origin, int depth);

/* a qualified rule: its prelude in prelude[0..n), its block from p */
static const char *style_rule(ec_ctx *c, int n, const char *p, const char *end, int origin)
{
	const char *bend = ec_skip_block(p, end, '{');
	const ec_sellist *l;
	ec_block *b;
	uint32_t spec;
	int never, k;
	size_t blen = bend - p;

	if (bend > p && bend[-1] == '}')
		blen--;
	c->st.rules++;
	l = n >= 0 ? sellist(c, prelude, 0, n, 0, &spec, &never) : NULL;
	if (l == NULL) {
		c->st.dropped++;
		return bend;
	}
	b = ec_alloc(&c->rmem, sizeof *b);
	if (b == NULL)
		return bend;
	memset(b, 0, sizeof *b);
	b->src = p;
	b->base = c->sheet_base;
	b->len = blen;
	c->st.blocks++;
	for (k = 0; k < l->n; k++) {
		((ec_sel *) l->sel[k])->block = b;
		c->st.selectors++;
		index_sel(c, l->sel[k], origin);
	}
	return bend;
}

/* an at-rule whose name is at; returns where parsing goes on */
static const char *at_rule(ec_ctx *c, const ec_tok *at, ec_lex *lx, int origin, int depth)
{
	char name[64];
	int stop, n;
	const char *bstart, *bend;

	ident_lower(at, name, sizeof name);
	n = read_prelude(lx, prelude, MAX_PRELUDE, &stop, 1);
	if (stop != T_LBRACE)
		return lx->p;			/* a statement: @import, @charset, @layer a; */
	bstart = lx->p;
	bend = ec_skip_block(bstart, lx->end, '{');
	if (n < 0 || depth > 16)
		return bend;
	if (!strcmp(name, "media")) {
		if (media_list(c, prelude, 0, n))
			rules(c, bstart, bend > bstart && bend[-1] == '}' ? bend - 1 : bend, origin,
				depth + 1);
	} else if (!strcmp(name, "supports")) {
		if (!(c->opts & EC_COMPAT_ATRULES) && supports_cond(c, prelude, 0, n) == 1)
			rules(c, bstart, bend > bstart && bend[-1] == '}' ? bend - 1 : bend, origin,
				depth + 1);
	} else if (!strcmp(name, "layer") && !(c->opts & EC_COMPAT_ATRULES)) {
		uint8_t save = c->layer;
		c->layer = 1;
		rules(c, bstart, bend > bstart && bend[-1] == '}' ? bend - 1 : bend, origin, depth + 1);
		c->layer = save;
	}
	/* @font-face, @keyframes, @page, @container, ... are not for EWeb yet */
	return bend;
}

static void rules(ec_ctx *c, const char *p, const char *end, int origin, int depth)
{
	ec_lex lx;

	ec_lex_init(&lx, p, end - p);
	for (;;) {
		const char *start = lx.p;
		ec_tok t;
		int type = ec_next(&lx, &t), n, stop;

		if (type == T_EOF)
			break;
		if (type == T_WS || type == T_CDO || type == T_CDC || type == T_SEMI)
			continue;
		if (type == T_AT) {
			lx.p = at_rule(c, &t, &lx, origin, depth);
			continue;
		}
		lx.p = start;
		n = read_prelude(&lx, prelude, MAX_PRELUDE, &stop, 0);
		if (stop != T_LBRACE)
			break;			/* the end, in the middle of a rule */
		lx.p = style_rule(c, n, lx.p, lx.end, origin);
	}
}

#ifndef EW_ST	/* scripts' selectors: the ST edition runs no scripts */
const ec_sellist *ec_selector(ec_ctx *c, const char *s, size_t len)
{
	/* kept by the scripts as long as the page: not with the sheets */
	char *copy = ec_alloc(&c->mem, len + 1);
	uint32_t spec;
	int never, stop, n;
	ec_arena *pm = c->pmem;
	const ec_sellist *l;
	ec_lex lx;

	if (copy == NULL)
		return NULL;
	memcpy(copy, s, len);
	copy[len] = 0;
	ec_lex_init(&lx, copy, len);
	n = read_prelude(&lx, prelude, MAX_PRELUDE, &stop, 0);
	if (n <= 0 || stop != T_EOF)
		return NULL;
	c->pmem = &c->mem;
	l = sellist(c, prelude, 0, n, 0, &spec, &never);
	c->pmem = pm;
	return l;
}

int ec_matches(ec_ctx *c, const ec_sellist *l, const ec_node *e)
{
	int mf = 0;
	return ec_match_list(c, l, e, &mf);
}

#endif

int ec_sheet(ec_ctx *c, const char *css, size_t len, int origin)
{
	char *copy = ec_alloc(&c->rmem, len + 1);

	if (copy == NULL)
		return -1;
	memcpy(copy, css, len);
	copy[len] = 0;
	c->st.sheets++;
	rules(c, copy, copy + len, origin, 0);
	return 0;
}

void ec_set_resolver(ec_ctx *c, int (*resolve)(const char *base, const char *rel, char *out, int size))
{
	c->resolve = resolve;
}

int ec_sheet_at(ec_ctx *c, const char *css, size_t len, int origin, const char *base)
{
	int r;
	if (base != NULL) {
		size_t n = strlen(base);
		char *b = ec_alloc(&c->rmem, n + 1);
		if (b == NULL)
			return -1;
		memcpy(b, base, n + 1);
		c->sheet_base = b;
	}
	r = ec_sheet(c, css, len, origin);
	c->sheet_base = NULL;
	return r;
}

/* ------------------------------------------------------------ declarations */

int ec_parse_decls(ec_ctx *c, const char *s, size_t len, ec_decl **out, int *custom)
{
	static ec_tok v[MAX_VALUE];
	static ec_decl d[MAX_DECLS];
	ec_lex lx;
	int nd = 0;

	*custom = 0;
	ec_lex_init(&lx, s, len);
	for (;;) {
		ec_tok t;
		int type = ec_next(&lx, &t), prop, nv, hasvar, bad = 0, imp = 0, r, k;
		ec_tok nt;
		const char *vs, *ve;
		char name[128];
		size_t nlen;

		if (type == T_EOF)
			break;
		if (type == T_WS || type == T_SEMI)
			continue;
		if (type == T_AT) {
			int stop;
			read_prelude(&lx, prelude, 0, &stop, 1);
			if (stop == T_LBRACE)
				lx.p = ec_skip_block(lx.p, lx.end, '{');
			continue;
		}
		if (type != T_IDENT) {
			/* not a declaration: skip to the next ; (a nested rule's block too) */
			read_value(&lx, v, 0, &vs, &ve, &hasvar, &bad);
			continue;
		}
		nlen = ident_lower(&t, name, sizeof name);
		nt = t;
		do
			type = ec_next(&lx, &t);
		while (type == T_WS);
		if (type != T_COLON) {
			if (type != T_SEMI && type != T_EOF)
				read_value(&lx, v, 0, &vs, &ve, &hasvar, &bad);
			continue;
		}
		nv = read_value(&lx, v, MAX_VALUE, &vs, &ve, &hasvar, &bad);
		if (nv > 1 && delim(&v[nv - 2], '!') && ec_tok_is(&v[nv - 1], "important")) {
			imp = 1;
			nv -= 2;
			/* the text ends before the ! */
			ve = v[nv].s;
			while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\n' ||
					ve[-1] == '\r' || ve[-1] == '\f'))
				ve--;
		}
		if (nd + 16 > MAX_DECLS)
			break;
		if (nlen > 2 && name[0] == '-' && name[1] == '-') {
			/* a custom property: its name as written, its value as text */
			char nb[128];
			size_t nl;
			const char *ns = ec_tok_text(&nt, nb, sizeof nb, &nl);
			ec_cdecl *cd;
			if (bad)
				continue;
			cd = ec_alloc(c->pmem, sizeof *cd);
			if (cd == NULL)
				continue;
			cd->name = ec_intern(c, ns, nl);
			cd->val = vs ? vs : "";
			cd->len = vs ? (uint32_t) (ve - vs) : 0;
			cd->hasvar = hasvar;
			if (c->opts & EC_OPT_NO_VAR)
				continue;
			d[nd].prop = EP_CUSTOM;
			d[nd].kind = 0;
			d[nd].imp = imp;
			d[nd].v.p = cd;
			nd++;
			*custom = 1;
			continue;
		}
		prop = ec_prop_lookup(name, nlen);
		if (prop < 0 || nv <= 0 || bad)
			continue;
		if (hasvar) {
			uint8_t p[16];
			int np;
			ec_pend *pd;
			if (c->opts & EC_OPT_NO_VAR)
				continue;
			pd = ec_alloc(c->pmem, sizeof *pd);
			if (pd == NULL)
				continue;
			pd->txt = vs;
			pd->len = ve - vs;
			pd->prop = prop;
			if (prop < EC_NPROP) {
				p[0] = prop;
				np = 1;
			} else {
				np = ec_shorthand_props(prop - EC_SH_BASE, p);
			}
			for (k = 0; k < np; k++) {
				d[nd].prop = p[k];
				d[nd].kind = EK_VAR;
				d[nd].imp = imp;
				d[nd].v.p = pd;
				nd++;
			}
			continue;
		}
		if (nv < 0)
			continue;
		r = ec_parse_value(c, prop, v, nv, &d[nd], MAX_DECLS - nd);
		if (r <= 0)
			continue;
		for (k = 0; k < r; k++)
			d[nd + k].imp = imp;
		nd += r;
	}
	c->st.decls += nd;
	*out = NULL;
	if (nd) {
		*out = ec_alloc(c->pmem, nd * sizeof d[0]);
		if (*out == NULL)
			return 0;
		memcpy(*out, d, nd * sizeof d[0]);
	}
	return nd;
}

void ec_parse_block(ec_ctx *c, ec_block *b, ec_arena *a)
{
	int custom;
	int k;
	ec_arena *pm = c->pmem;

	c->parse_base = b->base;
	c->pmem = a;
	b->n = ec_parse_decls(c, b->src, b->len, &b->d, &custom);
	c->pmem = pm;
	c->parse_base = NULL;
	b->custom = custom;
	for (k = 0; k < b->n; k++)
		if (b->d[k].imp)
			b->imp = 1;
	b->parsed = 1;
	c->st.blocks_parsed++;
}
