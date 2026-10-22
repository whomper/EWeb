/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The CSS engine's insides, shared by its sources. */
#ifndef EC_INT_H
#define EC_INT_H

#include <stdlib.h>
#include <string.h>

#include "ecss.h"

#if defined(__GNUC__)
#define EC_LIKELY(x)	__builtin_expect(!!(x), 1)
#define EC_UNLIKELY(x)	__builtin_expect(!!(x), 0)
#else
#define EC_LIKELY(x)	(x)
#define EC_UNLIKELY(x)	(x)
#endif

/* 22.10 fixed point: a * b without 64-bit arithmetic (two 32-bit
 * multiplies on the 68030, where a 64-bit one is a library call) */
static inline ec_fx fx_mul(ec_fx a, ec_fx b)
{
	int neg = 0;
	uint32_t ua = (uint32_t) a, ub = (uint32_t) b, r;

	/* (in unsigned arithmetic: out of range wraps, it isn't undefined) */
	if (a < 0) { ua = 0u - ua; neg = 1; }
	if (b < 0) { ub = 0u - ub; neg ^= 1; }
	r = (ua >> 10) * ub + (((ua & 1023) * ub) >> 10);
	return (ec_fx) (neg ? 0u - r : r);
}

/* ------------------------------------------------------------ memory */

#define EC_ALIGN	(sizeof(void *) > 4 ? sizeof(void *) : 4)

typedef struct ec_chunk {
	struct ec_chunk *next;
} ec_chunk;

typedef struct ec_arena {
	char *p, *end;
	ec_chunk *chunks;
	size_t chunk_size;
	int failed;			/* a piece wasn't there (out of memory) */
} ec_arena;

void *ec_alloc_slow(ec_arena *a, size_t n);
void ec_arena_free(ec_arena *a);

static inline void *ec_alloc(ec_arena *a, size_t n)
{
	n = (n + EC_ALIGN - 1) & ~(EC_ALIGN - 1);
	if (EC_LIKELY((size_t) (a->end - a->p) >= n)) {
		void *r = a->p;
		a->p += n;
		return r;
	}
	return ec_alloc_slow(a, n);
}

const ec_str *ec_str_new(ec_arena *a, const char *s, size_t len);

/* ------------------------------------------------------------ atoms */

enum { EC_B_ID, EC_B_CLASS, EC_B_TAG, EC_B_ATTR, EC_NB };

typedef struct ec_rent {
	const struct ec_sel *sel;
	uint32_t prio;			/* origin, layer and specificity */
	uint32_t order;			/* source order */
	uint16_t bloom[4];		/* ancestors the selector needs, 0 for none */
	uint8_t sib;			/* whether it matches can turn on the element's
					   siblings or children (+, ~, :first-child, :empty) */
} ec_rent;

typedef struct ec_vec {
	ec_rent *e;
	uint32_t n, cap;
} ec_vec;

struct ec_atom {
	struct ec_atom *next;
	uint32_t hash;
	uint16_t len;
	uint16_t bloom;			/* its bit in the ancestor filter, never 0 */
	uint8_t ref;			/* an attribute a selector looks at */
	uint8_t shared;			/* a base context's (ec_share): its rules are
					   the base's, a context's own are in its shadow */
	uint32_t stamp;			/* the cascade's: a custom property seen */
	ec_vec *rules[EC_NB];		/* rules whose subject has it, by kind */
	ec_vec *sibrules[EC_NB];	/* those of them that look at siblings */
	char s[];
};

uint32_t ec_hash(const char *s, size_t len);

/* the cascade's stamps, from one counter for every context: atoms shared
 * between contexts never see the same stamp twice */
extern uint32_t ec_stamp;


/* ------------------------------------------------------------ tokens */

enum {
	T_EOF, T_WS, T_IDENT, T_FUNC, T_AT, T_HASH, T_STR, T_BADSTR, T_URL, T_BADURL,
	T_DELIM, T_NUM, T_PCT, T_DIM, T_CDO, T_CDC, T_COLON, T_SEMI, T_COMMA,
	T_LBRACK, T_RBRACK, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE
};

#define TF_ESC	1	/* the text has escapes: ec_tok_text() */
#define TF_INT	2	/* a number without . or e */
#define TF_ID	4	/* a hash that would be an identifier */
#define TF_WS	8	/* whitespace before it (in value token lists) */
#define TF_SIGN	16	/* a number with a sign */

typedef struct ec_tok {
	uint8_t type, flags;
	uint16_t ulen;			/* a dimension's unit length */
	const char *s;			/* the name, string, url or number text */
	uint32_t len;
	ec_fx num;			/* a number's value */
	const char *u;			/* a dimension's unit */
} ec_tok;

typedef struct ec_lex {
	const char *p, *end;
} ec_lex;

void ec_lex_init(ec_lex *lx, const char *s, size_t len);
int ec_next(ec_lex *lx, ec_tok *t);			/* returns t->type */
/* the token's text without escapes: in buf (cap bytes) if it had any */
const char *ec_tok_text(const ec_tok *t, char *buf, size_t cap, size_t *len);
/* the end of the block opened just before p ({, [ or (), past its closer */
const char *ec_skip_block(const char *p, const char *end, char open);
/* is the identifier token t this lower-case word? */
int ec_tok_is(const ec_tok *t, const char *word);

/* ------------------------------------------------------------ declarations */

#define EP_CUSTOM	255
#define EC_SH_BASE	128		/* shorthand ids: EC_SH_BASE + ES_* */

#define EC_S_(id, name) ES_##id,
enum { EC_SHORTHANDS(EC_S_) EC_NSH };
#undef EC_S_

typedef struct ec_decl {
	uint8_t prop;			/* EP_*, or EP_CUSTOM */
	uint8_t kind;			/* EK_* */
	uint8_t imp;			/* !important */
	uint8_t pad;
	ec_v v;
} ec_decl;

/* a custom property's declaration */
typedef struct ec_cdecl {
	const ec_atom *name;
	const char *val;
	uint32_t len;
	uint8_t hasvar;
} ec_cdecl;

/* a declaration waiting for var(): the text of the property's value */
typedef struct ec_pend {
	const char *txt;
	uint32_t len;
	uint16_t prop;			/* the property or shorthand written */
} ec_pend;

/* a specified calc(): per unit, then the operation (ec_calc's op) */
enum { CU_PX, CU_PCT, CU_EM, CU_EX, CU_CH, CU_REM, CU_VW, CU_VH, CU_VMIN, CU_VMAX, CU_N };
/* a relative unit in the unit table (absolute ones are their size in px) */
#define EC_UNIT_REL(cu)	(-1 - (cu))
#define EC_UNIT_CU(u)	(-1 - (u))
typedef struct ec_calcx {
	uint8_t op, n;
	ec_fx c[][CU_N];
} ec_calcx;

typedef struct ec_block {
	const char *src;		/* the declarations' text, parsed when first needed */
	const char *base;		/* its sheet's address (ec_sheet_at), or NULL */
	uint32_t len;
	ec_decl *d;
	uint16_t n;
	uint8_t parsed;
	uint8_t custom;			/* it has custom properties */
	uint8_t imp;			/* it has !important declarations */
} ec_block;

/* ------------------------------------------------------------ selectors */

enum {
	S_TAG, S_ID, S_CLASS,
	S_ATTR, S_ATTR_EQ, S_ATTR_INCL, S_ATTR_DASH, S_ATTR_PREFIX, S_ATTR_SUFFIX, S_ATTR_SUBSTR,
	S_ROOT, S_EMPTY, S_FIRST_CHILD, S_LAST_CHILD, S_ONLY_CHILD, S_NTH_CHILD, S_NTH_LAST_CHILD,
	S_FIRST_OF_TYPE, S_LAST_OF_TYPE, S_ONLY_OF_TYPE, S_NTH_OF_TYPE, S_NTH_LAST_OF_TYPE,
	S_LINK, S_NEVER, S_NOT, S_IS, S_LANG, S_DIR,
	S_ENABLED, S_DISABLED, S_CHECKED, S_REQUIRED, S_OPTIONAL, S_READ_WRITE, S_READ_ONLY,
	S_PLACEHOLDER_SHOWN
};

#define SF_ICASE	1		/* [attr=value i] */

typedef struct ec_simple {
	uint8_t op, flags;
	int16_t a, b;			/* :nth-*(an+b) */
	const ec_atom *atom;		/* the name, id, class or attribute */
	const void *arg;		/* ec_str (a value), ec_sellist (:not, :is) */
} ec_simple;

enum { C_NONE, C_DESC, C_CHILD, C_ADJ, C_SIB };

typedef struct ec_compound {
	uint16_t first;			/* its simple selectors */
	uint8_t n;
	uint8_t comb;			/* how the next compound (to the left) relates */
} ec_compound;

enum { PE_NONE, PE_BEFORE, PE_AFTER, PE_OTHER };

typedef struct ec_sel {
	const ec_block *block;
	uint32_t spec;
	uint8_t pseudo;			/* PE_* */
	uint8_t ncomp;
	uint8_t never;			/* :hover and the like: no match in a static page */
	const ec_compound *comp;	/* comp[0] is the subject's */
	const ec_simple *simple;
} ec_sel;

typedef struct ec_sellist {
	uint16_t n;
	const ec_sel *sel[];
} ec_sellist;

/* ------------------------------------------------------------ the context */

#define EC_BLOOM_BITS	12
#define EC_BLOOM_SIZE	(1 << EC_BLOOM_BITS)

typedef struct ec_kwent ec_kwent;

/* a context's rules on a base context's atom (ec_create_on) */
typedef struct ec_shadow {
	struct ec_shadow *next;
	const ec_atom *a;
	ec_vec *rules[EC_NB], *sibrules[EC_NB];
} ec_shadow;

#define EC_SHADOW_SIZE 256

/* a context's own rules on an atom it shares with its base (NULL: none);
   make: create the entry */
ec_shadow *ec_shadow_of(struct ec_ctx *c, const ec_atom *a, int make);

struct ec_ctx {
	ec_media media;
	unsigned opts;
	const struct ec_ctx *base;	/* ec_create_on's, or NULL */
	ec_shadow **shadow;		/* EC_SHADOW_SIZE lists, or NULL: none yet */
	ec_arena mem;			/* atoms, sheets, rules */
	ec_arena smem;			/* styles */
	ec_atom **atoms;
	uint32_t natoms, atom_mask;
	ec_vec universal;		/* rules with no id, class, tag or attribute to index */
	ec_vec universal_sib;		/* those of them that look at siblings */
	ec_vec never;			/* rules for :hover and the like, kept for later */
	uint32_t order;
	uint8_t layer;			/* inside @layer: below unlayered rules */
	ec_stats st;

	int sibling_rules;		/* any selector looks at siblings */

	/* url()s in a sheet from another address (ec_sheet_at) */
	int (*resolve)(const char *base, const char *rel, char *out, int size);
	const char *sheet_base;		/* the sheet being added's */
	const char *parse_base;		/* the block being parsed's */

	/* names the matcher and the cascade look for */
	const ec_atom *a_style, *a_href, *a_lang, *a_dir, *a_type, *a_disabled, *a_checked,
		*a_selected, *a_required, *a_readonly, *a_placeholder, *a_value, *a_hidden;
	const ec_atom *t_a, *t_area, *t_input, *t_button, *t_select, *t_textarea,
		*t_option, *t_optgroup, *t_fieldset, *t_html;

	ec_style initial;		/* every property's initial value */
	const ec_style *root_style;	/* for rem */
	uint8_t bloom[EC_BLOOM_SIZE];	/* the ancestors' atoms, counted */
	uint32_t stamp;			/* one per element styled */
	char buf[4096];			/* scratch for var() substitution */
};

/* the keyword, property, shorthand and colour names */
enum { KC_KW = 1, KC_PROP, KC_COLOR, KC_UNIT };
int ec_names_lookup(int cls, const char *s, size_t len, int32_t *val);
int ec_kw(const ec_tok *t);			/* KW_*, or 0 */
int ec_prop_lookup(const char *s, size_t len);	/* EP_*, EC_SH_BASE + ES_*, or -1 */

/* values (value.c) */
#define LF_AUTO		1
#define LF_NONE		2
#define LF_NORMAL	4
#define LF_NN		8		/* not negative */
#define LF_NOPCT	16
#define LF_CONTENT	32

enum {
	VP_KW, VP_LEN, VP_COLOR, VP_FAMILY, VP_FSIZE, VP_WEIGHT, VP_LHEIGHT, VP_LISTTYPE,
	VP_IMAGE, VP_BWIDTH, VP_BGPOS, VP_INT, VP_NUM, VP_ALPHA, VP_VALIGN, VP_DECO, VP_CONTENT,
	VP_TRACKS, VP_AREAS, VP_AUTOFLOW, VP_GRIDLINE, VP_SHADOW, VP_ANIM
};

/* Parses a property's (or shorthand's) value from its tokens (no
 * whitespace tokens, TF_WS marks them) into declarations. Returns how
 * many, or -1 if the value is invalid. */
int ec_parse_value(ec_ctx *c, int prop, const ec_tok *t, int n, ec_decl *out, int max);
int ec_parse_color(const ec_tok *t, int n, int *used, uint32_t *argb, int *cur);
/* the longhands a shorthand sets; returns their number */
int ec_shorthand_props(int sh, uint8_t *props);

/* sheets (sheet.c) */
void ec_parse_block(ec_ctx *c, ec_block *b);
int ec_parse_decls(ec_ctx *c, const char *s, size_t len, ec_decl **out, int *custom);
const ec_sellist *ec_parse_sellist(ec_ctx *c, ec_lex *lx, int forgiving, int *ok);

/* matching (select.c) */
int ec_match_list(ec_ctx *c, const ec_sellist *l, const ec_node *e, int *flags);

#endif
