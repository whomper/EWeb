/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* EWeb's CSS engine: stylesheets in, a computed style for every element
 * out, written for a 68030 (see docs/CSS.md).
 *
 * The host gives the engine its document as ec_node elements, whose names,
 * ids, classes and attribute names are atoms from ec_intern(). Matching
 * reads them directly: there are no callbacks. Numbers are 22.10 fixed
 * point (ec_fx), lengths are in CSS pixels. */
#ifndef ECSS_H
#define ECSS_H

#include <stddef.h>
#include <stdint.h>

#include "ec_tables.h"

typedef int32_t ec_fx;			/* 22.10 fixed point */
#define EC_FX(n)	((ec_fx) ((n) * 1024))

/* the properties: EP_COLOR... (inherited ones first), EC_NPROP of them */
#define EC_P_(id, name, parser, flags, kind, val) EP_##id,
enum ec_prop { EC_PROPS_INHERITED(EC_P_) EC_PROPS_OTHER(EC_P_) EC_NPROP };
#define EC_PI_(id, name, parser, flags, kind, val) EC_I_##id,
enum { EC_PROPS_INHERITED(EC_PI_) EC_NINHERITED };
#undef EC_PI_

/* the keywords: KW_AUTO... (0 is none) */
#define EC_K_(id, text) KW_##id,
enum ec_keyword { KW_NONE_ = 0, EC_KEYWORDS(EC_K_) EC_NKW };
#undef EC_K_

/* what a computed value is: k[] of a style */
enum ec_kind {
	EK_KW,		/* v.i: a keyword */
	EK_PX,		/* v.i: a length in px, ec_fx */
	EK_PCT,		/* v.i: a percentage, ec_fx (50% is EC_FX(50)) */
	EK_NUM,		/* v.i: a number, ec_fx */
	EK_INT,		/* v.i: an integer (font-weight, z-index, order; text-decoration-line bits) */
	EK_COLOR,	/* v.i: 0xAARRGGBB */
	EK_CURCOLOR,	/* currentcolor: the element's color */
	EK_PTR,		/* v.p: ec_family, ec_str (a URL), ec_content */
	EK_CALC,	/* v.p: ec_calc, lengths that need a percentage's base */
	/* specified values only, gone once computed */
	EK_EM, EK_EX, EK_CH, EK_REM, EK_VW, EK_VH, EK_VMIN, EK_VMAX,
	EK_CALCX,	/* v.p: a calc() not yet computed */
	EK_VAR,		/* v.p: a value waiting for var() */
	EK_IMAGE	/* v.p: ec_str, a gradient: an image without a URL */
};

/* text-decoration-line's bits */
#define EC_DECO_UNDERLINE	1
#define EC_DECO_OVERLINE	2
#define EC_DECO_LINE_THROUGH	4
#define EC_DECO_BLINK		8

typedef union ec_v {
	int32_t i;
	const void *p;
} ec_v;

typedef struct ec_str {
	uint32_t len;
	char s[];			/* NUL-terminated */
} ec_str;

typedef struct ec_family {
	uint16_t n;			/* names before the generic one */
	uint16_t generic;		/* KW_SERIF..., or 0 */
	const ec_str *name[];
} ec_family;

/* a length that needs its percentage's base: px + pct% for each operand,
 * then min, max or clamp of them (op) */
enum { EC_CALC_SUM, EC_CALC_MIN, EC_CALC_MAX, EC_CALC_CLAMP };
typedef struct ec_calc {
	uint8_t op, n;
	struct { ec_fx px, pct; } a[];
} ec_calc;

enum { EC_CONTENT_STRING, EC_CONTENT_ATTR, EC_CONTENT_COUNTER, EC_CONTENT_QUOTE, EC_CONTENT_URL };
typedef struct ec_content {
	uint16_t n;
	struct { uint16_t type, kw; const ec_str *s; } item[];
} ec_content;

/* a grid's track sizes (CSS Grid 7.2): grid-template-columns and -rows,
 * grid-auto-columns and -rows. A size is a length (EC_TR_LEN: px once
 * computed; until then rel is the unit, EK_EM...), a percentage, fr, auto,
 * min-content or max-content; a track is minmax(mn, mx) (a plain size: the
 * same twice; fit-content(L): EC_TR_FIT with L in both). An auto repeat
 * (repeat(auto-fill or auto-fit, ...)) is the tracks from rep_at, rep_n
 * of them, rep its keyword */
enum { EC_TR_LEN, EC_TR_PCT, EC_TR_FR, EC_TR_AUTO, EC_TR_MIN, EC_TR_MAX, EC_TR_FIT };
typedef struct ec_tsize {
	uint8_t k, rel;
	ec_fx v;
} ec_tsize;
typedef struct ec_tracks {
	uint16_t n, rep_at, rep_n;
	uint8_t rep, rel;		/* rel: some length not yet in px */
	struct { ec_tsize mn, mx; } t[];
} ec_tracks;

/* grid-template-areas: rows x cols names (NULL for a cell of "."), by rows */
typedef struct ec_areas {
	uint16_t rows, cols;
	const ec_str *name[];
} ec_areas;

/* grid-row-start and the like: auto (EK_KW); EK_INT a line (negative:
   from the end), or EC_GRID_SPAN + n a span of n; EK_PTR an area's name
   (ec_str). grid-auto-flow: EK_INT, these bits */
#define EC_GRID_SPAN	0x10000
#define EC_FLOW_COLUMN	1
#define EC_FLOW_DENSE	2

typedef struct ec_style {
	ec_v v[EC_NPROP];
	uint8_t k[EC_NPROP];
	const struct ec_custom *custom;		/* custom properties (--name) */
	const struct ec_style *before, *after;	/* ::before and ::after, if any rule made them */
} ec_style;

/* ------------------------------------------------------------ the document */

typedef struct ec_atom ec_atom;

typedef struct ec_attr {
	const ec_atom *name;		/* lower case */
	const char *val;
	uint32_t len;
} ec_attr;

typedef struct ec_node {
	struct ec_node *parent, *prev, *next, *first;	/* elements only */
	const ec_atom *name;		/* lower case */
	const ec_atom *id;		/* or NULL */
	const ec_atom *const *cls;	/* the class attribute, split */
	const ec_attr *attr;
	uint16_t ncls, nattr;
	uint16_t index, nsib;		/* place among its element siblings from 1; their number */
	uint16_t flags;			/* EC_NODE_* */
	const ec_style *style;		/* ec_select()'s result */
	/* presentational hints: declarations made from the element's HTML
	   attributes (width, bgcolor, align...), or NULL. They cascade as
	   author rules of specificity zero before every author sheet. */
	const char *hint;
	uint32_t nhint;
} ec_node;

#define EC_NODE_LINK	1		/* an a or area with an href */
#define EC_NODE_EMPTY	2		/* no element children and no text */

/* ------------------------------------------------------------ the engine */

typedef struct ec_ctx ec_ctx;

typedef struct ec_media {
	int32_t width, height;		/* the viewport, px */
	int32_t font_size;		/* the default font size, px */
} ec_media;

enum { EC_ORIGIN_UA, EC_ORIGIN_USER, EC_ORIGIN_AUTHOR };

#define EC_OPT_NO_VAR	1	/* var() makes a declaration invalid, as in LibCSS (for tests) */
/* For tests (tests/css/csscmp.c): each leaves out one thing LibCSS lacks,
 * so that a difference between the two engines can be put down to it. */
#define EC_COMPAT_CALC		0x0002	/* calc(), min(), max(), clamp() */
#define EC_COMPAT_GAP		0x0004	/* gap, grid-gap, row-gap */
#define EC_COMPAT_START		0x0008	/* text-align: start, end, match-parent */
#define EC_COMPAT_SELECTORS	0x0010	/* Selectors 4, and pseudo-classes nsbench's LibCSS handler says no to */
#define EC_COMPAT_MEDIA		0x0020	/* media features but width, height and prefers-color-scheme */
#define EC_COMPAT_ATRULES	0x0040	/* @supports and @layer */
#define EC_COMPAT_COLOR		0x0080	/* CSS Color 4's syntax: spaces and / in rgb() and hsl() */
#define EC_COMPAT_INSET		0x0100	/* inset */
#define EC_COMPAT_KEYWORDS	0x0200	/* revert, flow-root, contents, unknown list styles */
#define EC_COMPAT_GRADIENT	0x0400	/* gradients as images */
#define EC_COMPAT_DECO		0x0800	/* text-decoration with a style or colour */
#define EC_COMPAT_NOINLINE	0x1000	/* style attributes left out */

typedef struct ec_stats {
	uint32_t sheets, rules, selectors, dropped;	/* dropped: invalid or never matching */
	uint32_t blocks, blocks_parsed, decls;
	uint32_t elements, shared, cached, candidates, rejected, matched;
} ec_stats;

ec_ctx *ec_create(const ec_media *media, unsigned opts);
void ec_destroy(ec_ctx *c);

/* A base context: sheets that every document has (the UA sheet), parsed
 * once. ec_share() freezes c as one: no sheet may be added to it after,
 * and it must outlive the contexts made on it. ec_create_on() makes a
 * context that has base's rules and atoms as if its sheets had been
 * added to it first (its own sheets cascade after them, as they would). */
void ec_share(ec_ctx *c);
ec_ctx *ec_create_on(const ec_media *media, unsigned opts, const ec_ctx *base);

/* an atom: the same pointer for the same bytes, until ec_destroy() */
const ec_atom *ec_intern(ec_ctx *c, const char *s, size_t len);
const char *ec_atom_text(const ec_atom *a, size_t *len);

/* Adds a stylesheet (the text is copied). Sheets cascade in the order
 * they are added, within their origin. Returns 0, or -1 if out of memory. */
int ec_sheet(ec_ctx *c, const char *css, size_t len, int origin);

/* Styles root and every element under it: sets each node's style. The
 * styles last until ec_unstyle() or ec_destroy(). */
void ec_select(ec_ctx *c, ec_node *root);
void ec_unstyle(ec_ctx *c);

const ec_stats *ec_get_stats(const ec_ctx *c);
const char *ec_prop_name(int prop);
const char *ec_kw_name(int kw);

/* For scripts (querySelector, matches, closest): a selector list read
 * with c's atoms, in c's memory until ec_destroy(); NULL if it is not a
 * valid one (or out of memory). ec_matches: does e match it. */
struct ec_sellist;
const struct ec_sellist *ec_selector(ec_ctx *c, const char *s, size_t len);
int ec_matches(ec_ctx *c, const struct ec_sellist *l, const ec_node *e);

/* a custom property's computed value, or NULL */
const char *ec_custom_value(const ec_style *s, const ec_atom *name, size_t *len);

#endif
