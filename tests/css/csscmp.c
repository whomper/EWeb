/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* csscmp: EWeb's CSS engine against LibCSS, value by value.
 *
 *   csscmp [-v] [-V] [-n N] [-s SHEET.CSS]... PAGE.HTM...
 *
 * Each page is parsed once and styled by both engines, with the same UA
 * sheet and the same SHEET.CSS files; then every element's computed
 * value of every property both engines have is compared. It prints, per
 * property, how many differ and the first N (3) of them, with the
 * element. -V lets EWeb's engine use var() (LibCSS has none), so the
 * differences it makes show; without it var() is ignored, as LibCSS
 * does. -v prints every difference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/libcss.h>

#include "dom.h"
#include "style.h"
#include "ecstyle.h"
#include "ecss.h"
#include "plat.h"

static const struct { const char *prop; int val; const char *name; } lc[] = {
#include "lcnames.inc"
};

static const char *lcname(const char *prop, int v)
{
	size_t i;

	for (i = 0; i < sizeof lc / sizeof lc[0]; i++)
		if (lc[i].val == v && strcmp(lc[i].prop, prop) == 0 &&
				strcmp(lc[i].name, "inherit") != 0)
			return lc[i].name;
	return "?";
}

static const css_unit_ctx unit_ctx = {
	.viewport_width    = 640 * (1 << CSS_RADIX_POINT),
	.viewport_height   = 480 * (1 << CSS_RADIX_POINT),
	.font_size_default =  16 * (1 << CSS_RADIX_POINT),
	.font_size_minimum =   6 * (1 << CSS_RADIX_POINT),
	.device_dpi        =  96 * (1 << CSS_RADIX_POINT),
};

/* a value, either engine's: k is its kind */
typedef struct nv {
	char k;		/* k keyword, p px, % percent, n number, i integer, c colour,
			   C currentcolor, u url, f family, g gradient, x calc, ? none */
	int32_t v;
	char s[200];
	css_fixed rl;	/* a LibCSS length as LibCSS has it, before px */
	css_unit ru;
} nv;

static void kw(nv *o, const char *name)
{
	o->k = 'k';
	snprintf(o->s, sizeof o->s, "%s", name);
}

static void lc_len(const css_computed_style *s, css_fixed len, css_unit unit, nv *o)
{
	if (unit == CSS_UNIT_PCT) {
		o->k = '%';
		o->v = len;
	} else {
		o->k = 'p';
		o->v = css_unit_len2device_px(s, &unit_ctx, len, unit);
	}
	o->rl = len;
	o->ru = unit;
}

/* LibCSS's own length in px, done exactly: fs is the font size for em */
static int exact_px(const nv *a, int32_t fs, int32_t rfs, double *px)
{
	double v = a->rl / 1024.0, f = fs / 1024.0;

	switch (a->ru) {
	case CSS_UNIT_IN: *px = v * 96; return 1;
	case CSS_UNIT_CM: *px = v * 96 / 2.54; return 1;
	case CSS_UNIT_MM: *px = v * 96 / 25.4; return 1;
	case CSS_UNIT_Q: *px = v * 96 / 101.6; return 1;
	case CSS_UNIT_PT: *px = v * 96 / 72; return 1;
	case CSS_UNIT_PC: *px = v * 16; return 1;
	case CSS_UNIT_EM: *px = v * f; return 1;
	case CSS_UNIT_EX: *px = v * f * 0.6; return 1;
	case CSS_UNIT_CH: *px = v * f * 0.4; return 1;
	case CSS_UNIT_REM: *px = v * rfs / 1024.0; return 1;
	case CSS_UNIT_VW: *px = v * 6.4; return 1;
	case CSS_UNIT_VH: *px = v * 4.8; return 1;
	case CSS_UNIT_VMIN: *px = v * 4.8; return 1;
	case CSS_UNIT_VMAX: *px = v * 6.4; return 1;
	default: return 0;
	}
}

typedef uint8_t (*len_fn)(const css_computed_style *, css_fixed *, css_unit *);

static const struct { int prop; const char *lcprop; len_fn fn; } lens[] = {
	{ EP_TOP, "top", css_computed_top }, { EP_RIGHT, "right", css_computed_right },
	{ EP_BOTTOM, "bottom", css_computed_bottom }, { EP_LEFT, "left", css_computed_left },
	{ EP_WIDTH, "width", css_computed_width }, { EP_HEIGHT, "height", css_computed_height },
	{ EP_MIN_WIDTH, "min-width", css_computed_min_width },
	{ EP_MIN_HEIGHT, "min-height", css_computed_min_height },
	{ EP_MAX_WIDTH, "max-width", css_computed_max_width },
	{ EP_MAX_HEIGHT, "max-height", css_computed_max_height },
	{ EP_MARGIN_TOP, "margin", css_computed_margin_top },
	{ EP_MARGIN_RIGHT, "margin", css_computed_margin_right },
	{ EP_MARGIN_BOTTOM, "margin", css_computed_margin_bottom },
	{ EP_MARGIN_LEFT, "margin", css_computed_margin_left },
	{ EP_PADDING_TOP, "padding", css_computed_padding_top },
	{ EP_PADDING_RIGHT, "padding", css_computed_padding_right },
	{ EP_PADDING_BOTTOM, "padding", css_computed_padding_bottom },
	{ EP_PADDING_LEFT, "padding", css_computed_padding_left },
	{ EP_TEXT_INDENT, "text-indent", css_computed_text_indent },
	{ EP_LETTER_SPACING, "letter-spacing", css_computed_letter_spacing },
	{ EP_WORD_SPACING, "word-spacing", css_computed_word_spacing },
	{ EP_VERTICAL_ALIGN, "vertical-align", css_computed_vertical_align },
	{ EP_FLEX_BASIS, "flex-basis", css_computed_flex_basis },
	{ EP_COLUMN_GAP, "column-gap", css_computed_column_gap },
	{ EP_BORDER_TOP_WIDTH, "border-width", css_computed_border_top_width },
	{ EP_BORDER_RIGHT_WIDTH, "border-width", css_computed_border_right_width },
	{ EP_BORDER_BOTTOM_WIDTH, "border-width", css_computed_border_bottom_width },
	{ EP_BORDER_LEFT_WIDTH, "border-width", css_computed_border_left_width },
};

typedef uint8_t (*kw_fn)(const css_computed_style *);

static const struct { int prop; const char *lcprop; kw_fn fn; } kws[] = {
	{ EP_FONT_STYLE, "font-style", css_computed_font_style },
	{ EP_FONT_VARIANT, "font-variant", css_computed_font_variant },
	{ EP_TEXT_TRANSFORM, "text-transform", css_computed_text_transform },
	{ EP_WHITE_SPACE, "white-space", css_computed_white_space },
	{ EP_LIST_STYLE_TYPE, "list-style-type", css_computed_list_style_type },
	{ EP_LIST_STYLE_POSITION, "list-style-position", css_computed_list_style_position },
	{ EP_VISIBILITY, "visibility", css_computed_visibility },
	{ EP_DIRECTION, "direction", css_computed_direction },
	{ EP_BORDER_COLLAPSE, "border-collapse", css_computed_border_collapse },
	{ EP_POSITION, "position", css_computed_position },
	{ EP_FLOAT, "float", css_computed_float },
	{ EP_CLEAR, "clear", css_computed_clear },
	{ EP_BORDER_TOP_STYLE, "border-style", css_computed_border_top_style },
	{ EP_BORDER_RIGHT_STYLE, "border-style", css_computed_border_right_style },
	{ EP_BORDER_BOTTOM_STYLE, "border-style", css_computed_border_bottom_style },
	{ EP_BORDER_LEFT_STYLE, "border-style", css_computed_border_left_style },
	{ EP_BACKGROUND_REPEAT, "background-repeat", css_computed_background_repeat },
	{ EP_OVERFLOW_X, "overflow", css_computed_overflow_x },
	{ EP_OVERFLOW_Y, "overflow", css_computed_overflow_y },
	{ EP_UNICODE_BIDI, "unicode-bidi", css_computed_unicode_bidi },
	{ EP_BOX_SIZING, "box-sizing", css_computed_box_sizing },
	{ EP_FLEX_DIRECTION, "flex-direction", css_computed_flex_direction },
	{ EP_FLEX_WRAP, "flex-wrap", css_computed_flex_wrap },
	{ EP_JUSTIFY_CONTENT, "justify-content", css_computed_justify_content },
	{ EP_ALIGN_ITEMS, "align-items", css_computed_align_items },
	{ EP_ALIGN_SELF, "align-self", css_computed_align_self },
	{ EP_ALIGN_CONTENT, "align-content", css_computed_align_content },
	{ EP_TABLE_LAYOUT, "table-layout", css_computed_table_layout },
	{ EP_TEXT_ALIGN, "text-align", css_computed_text_align },
};

/* LibCSS's value of property p */
static void lc_get(int p, const css_computed_style *s, int root, nv *o)
{
	css_fixed len, len2;
	css_unit unit, unit2;
	css_color col;
	lwc_string *url, **names;
	int32_t iv;
	uint8_t t;
	size_t i;

	memset(o, 0, sizeof *o);
	o->k = '?';
	for (i = 0; i < sizeof lens / sizeof lens[0]; i++)
		if (lens[i].prop == p) {
			const char *nm;
			t = lens[i].fn(s, &len, &unit);
			nm = lcname(lens[i].lcprop, t);
			if (!strcmp(nm, "set") || !strcmp(nm, "width"))
				lc_len(s, len, unit, o);
			else if (!strcmp(nm, "thin") || !strcmp(nm, "medium") || !strcmp(nm, "thick"))
				o->k = 'p', o->v = (nm[1] == 'h' ? 1 : nm[0] == 'm' ? 3 : 5) << 10;
			else
				kw(o, nm);
			return;
		}
	for (i = 0; i < sizeof kws / sizeof kws[0]; i++)
		if (kws[i].prop == p) {
			const char *nm = lcname(kws[i].lcprop, kws[i].fn(s));
			kw(o, !strcmp(nm, "default") ? "start" : nm);
			return;
		}
	switch (p) {
	case EP_COLOR:
		css_computed_color(s, &col);
		o->k = 'c';
		o->v = col;
		return;
	case EP_BORDER_TOP_COLOR: case EP_BORDER_RIGHT_COLOR: case EP_BORDER_BOTTOM_COLOR:
	case EP_BORDER_LEFT_COLOR: case EP_BACKGROUND_COLOR:
		t = p == EP_BACKGROUND_COLOR ? css_computed_background_color(s, &col) :
			p == EP_BORDER_TOP_COLOR ? css_computed_border_top_color(s, &col) :
			p == EP_BORDER_RIGHT_COLOR ? css_computed_border_right_color(s, &col) :
			p == EP_BORDER_BOTTOM_COLOR ? css_computed_border_bottom_color(s, &col) :
			css_computed_border_left_color(s, &col);
		if (t == CSS_BORDER_COLOR_CURRENT_COLOR)
			o->k = 'C';
		else
			o->k = 'c', o->v = col;
		return;
	case EP_FONT_FAMILY:
		t = css_computed_font_family(s, &names);
		o->k = 'f';
		for (i = 0; names != NULL && names[i] != NULL; i++) {
			size_t n = strlen(o->s);
			snprintf(o->s + n, sizeof o->s - n, "%s,", lwc_string_data(names[i]));
		}
		{
			size_t n = strlen(o->s);
			snprintf(o->s + n, sizeof o->s - n, "|%s", lcname("font-family", t));
		}
		for (i = 0; o->s[i]; i++)
			if (o->s[i] >= 'A' && o->s[i] <= 'Z')
				o->s[i] += 32;
		return;
	case EP_FONT_SIZE:
		css_computed_font_size(s, &len, &unit);
		lc_len(s, len, unit, o);
		return;
	case EP_FONT_WEIGHT: {
		const char *nm = lcname("font-weight", css_computed_font_weight(s));
		if (nm[0] >= '0' && nm[0] <= '9')
			o->k = 'i', o->v = atoi(nm);
		else if (!strcmp(nm, "normal"))
			o->k = 'i', o->v = 400;
		else if (!strcmp(nm, "bold"))
			o->k = 'i', o->v = 700;
		else
			kw(o, nm);
		return;
	}
	case EP_LINE_HEIGHT:
		t = css_computed_line_height(s, &len, &unit);
		if (t == CSS_LINE_HEIGHT_NUMBER)
			o->k = 'n', o->v = len;
		else if (t == CSS_LINE_HEIGHT_DIMENSION)
			lc_len(s, len, unit, o);
		else
			kw(o, "normal");
		return;
	case EP_LIST_STYLE_IMAGE: case EP_BACKGROUND_IMAGE:
		url = NULL;
		if (p == EP_LIST_STYLE_IMAGE)
			css_computed_list_style_image(s, &url);
		else
			css_computed_background_image(s, &url);
		if (url != NULL) {
			o->k = 'u';
			snprintf(o->s, sizeof o->s, "%s", lwc_string_data(url));
		} else {
			kw(o, "none");
		}
		return;
	case EP_BORDER_SPACING_H: case EP_BORDER_SPACING_V:
		css_computed_border_spacing(s, &len, &unit, &len2, &unit2);
		if (p == EP_BORDER_SPACING_H)
			lc_len(s, len, unit, o);
		else
			lc_len(s, len2, unit2, o);
		return;
	case EP_DISPLAY:
		kw(o, lcname("display", css_computed_display(s, root)));
		return;
	case EP_BACKGROUND_POSITION_X: case EP_BACKGROUND_POSITION_Y:
		css_computed_background_position(s, &len, &unit, &len2, &unit2);
		if (p == EP_BACKGROUND_POSITION_X)
			lc_len(s, len, unit, o);
		else
			lc_len(s, len2, unit2, o);
		return;
	case EP_Z_INDEX:
		if (css_computed_z_index(s, &iv) == CSS_Z_INDEX_AUTO)
			kw(o, "auto");
		else
			o->k = 'i', o->v = iv >> 10;	/* LibCSS keeps it in fixed point */
		return;
	case EP_TEXT_DECORATION_LINE:
		t = css_computed_text_decoration(s);
		o->k = 'i';
		o->v = t == CSS_TEXT_DECORATION_NONE ? 0 : t & 15;
		return;
	case EP_OPACITY:
		css_computed_opacity(s, &len);
		o->k = 'n';
		o->v = len;
		return;
	case EP_CONTENT: {
		const css_computed_content_item *ci;
		kw(o, lcname("content", css_computed_content(s, &ci)));
		return;
	}
	case EP_FLEX_GROW:
		css_computed_flex_grow(s, &len);
		o->k = 'n';
		o->v = len;
		return;
	case EP_FLEX_SHRINK:
		css_computed_flex_shrink(s, &len);
		o->k = 'n';
		o->v = len;
		return;
	case EP_ORDER:
		css_computed_order(s, &iv);
		o->k = 'i';
		o->v = iv;
		return;
	}
}

/* EWeb's value of property p */
static void ec_get(int p, const ec_style *s, nv *o)
{
	memset(o, 0, sizeof *o);
	switch (s->k[p]) {
	case EK_KW:
		kw(o, ec_kw_name(s->v[p].i));
		if (p == EP_CONTENT)
			kw(o, s->v[p].i == KW_NONE ? "none" : "normal");
		return;
	case EK_PX: o->k = 'p'; o->v = s->v[p].i; return;
	case EK_PCT: o->k = '%'; o->v = s->v[p].i; return;
	case EK_NUM: o->k = 'n'; o->v = s->v[p].i; return;
	case EK_INT: o->k = 'i'; o->v = s->v[p].i; return;
	case EK_COLOR: o->k = 'c'; o->v = s->v[p].i; return;
	case EK_CURCOLOR: o->k = 'C'; return;
	case EK_CALC: kw(o, "(calc)"); o->k = 'x'; return;
	case EK_IMAGE: kw(o, "(gradient)"); o->k = 'g'; return;
	case EK_PTR:
		if (p == EP_FONT_FAMILY) {
			const ec_family *f = s->v[p].p;
			int i;
			o->k = 'f';
			for (i = 0; i < f->n; i++) {
				size_t n = strlen(o->s);
				snprintf(o->s + n, sizeof o->s - n, "%s,", f->name[i]->s);
			}
			{
				size_t n = strlen(o->s);
				const char *g = f->generic ? ec_kw_name(f->generic) : "?";
				/* LibCSS knows five generic families; others are names to it */
				if (f->generic && f->generic != KW_SERIF && f->generic != KW_SANS_SERIF &&
						f->generic != KW_CURSIVE && f->generic != KW_FANTASY &&
						f->generic != KW_MONOSPACE) {
					snprintf(o->s + n, sizeof o->s - n, "%s,", g);
					n = strlen(o->s);
					g = "?";
				}
				snprintf(o->s + n, sizeof o->s - n, "|%s", g);
			}
			for (size_t i2 = 0; o->s[i2]; i2++)
				if (o->s[i2] >= 'A' && o->s[i2] <= 'Z')
					o->s[i2] += 32;
		} else if (p == EP_CONTENT) {
			kw(o, "set");
		} else if (p == EP_LIST_STYLE_TYPE) {
			kw(o, "(string)");
		} else {
			o->k = 'u';
			snprintf(o->s, sizeof o->s, "%s", ((const ec_str *) s->v[p].p)->s);
		}
		return;
	}
	o->k = '?';
}

/* equal, allowing for rounding and for names that mean the same */
static int same(int p, const nv *a, const nv *b)
{
	if (a->k != b->k)
		return 0;
	switch (a->k) {
	case 'p': {
		/* LibCSS rounds a font size to whole pixels when it turns ems
		 * into px (unit.c): up to half a pixel an em */
		int32_t d = a->v - b->v, m = (a->v < 0 ? -a->v : a->v) * 3 / 100;
		if (d < 0)
			d = -d;
		return d <= (m > 512 ? m : 512);
	}
	case '%': case 'n':
		return a->v - b->v <= 8 && b->v - a->v <= 8;
	case 'i':
		return a->v == b->v;
	case 'c': {
		/* alpha rounds either way: 0.7 is 178.5 */
		int32_t da = (int32_t) ((uint32_t) a->v >> 24) - (int32_t) ((uint32_t) b->v >> 24);
		return (a->v & 0xffffff) == (b->v & 0xffffff) && da >= -1 && da <= 1;
	}
	case 'C':
		return 1;
	default:
		if (strcmp(a->s, b->s) == 0)
			return 1;
		if (p == EP_FONT_FAMILY) {
			/* no generic family: LibCSS adds the UA's default, sans-serif */
			size_t la = strlen(a->s), lb = strlen(b->s);
			if (la > 11 && !strcmp(a->s + la - 11, "|sans-serif") && lb > 2 &&
					!strcmp(b->s + lb - 2, "|?") && la - 11 == lb - 2 &&
					!strncmp(a->s, b->s, lb - 2))
				return 1;
		}
		if (p == EP_TEXT_ALIGN && !strcmp(a->s, "left") && !strcmp(b->s, "start"))
			return 0;
		if (p == EP_JUSTIFY_CONTENT && ((!strcmp(a->s, "flex-start") && !strcmp(b->s, "normal")) ||
				(!strcmp(b->s, "flex-start") && !strcmp(a->s, "normal"))))
			return 1;
		if ((p == EP_ALIGN_ITEMS || p == EP_ALIGN_CONTENT) &&
				((!strcmp(a->s, "stretch") && !strcmp(b->s, "normal")) ||
				 (!strcmp(b->s, "stretch") && !strcmp(a->s, "normal"))))
			return 1;
		return 0;
	}
}

static void show(char *buf, size_t cap, const nv *v)
{
	switch (v->k) {
	case 'p':
		if (v->ru != CSS_UNIT_PX && v->rl != 0)
			snprintf(buf, cap, "%.3fpx (%.3f, unit %d)", v->v / 1024.0, v->rl / 1024.0,
				(int) v->ru);
		else
			snprintf(buf, cap, "%.3fpx", v->v / 1024.0);
		break;
	case '%': snprintf(buf, cap, "%.3f%%", v->v / 1024.0); break;
	case 'n': snprintf(buf, cap, "%.3f", v->v / 1024.0); break;
	case 'i': snprintf(buf, cap, "%ld", (long) v->v); break;
	case 'c': snprintf(buf, cap, "#%08lx", (unsigned long) (uint32_t) v->v); break;
	case 'C': snprintf(buf, cap, "currentcolor"); break;
	case '?': snprintf(buf, cap, "(none)"); break;
	default: snprintf(buf, cap, "%s", v->s); break;
	}
}

/* ------------------------------------------------------------ the tree */

#define NSAMPLES 3
static struct {
	unsigned long n, diff, explained;
	int shown;
} tally[EC_NPROP];

/* differences where LibCSS falls short of the specifications EWeb follows:
 * the first ones by rule, the rest found by styling the page again with
 * one of EWeb's EC_COMPAT_* switches on */
enum {
	R_NONE, R_MIN_AUTO, R_BORDER_NONE, R_FLEX_ITEM, R_GAP, R_CALC, R_GRADIENT, R_EM_INHERIT,
	R_USED_OFFSET, R_FLEX_BASIS, R_WEIGHT, R_MEDIUM, R_FLOAT, R_UNITS,
	R_V_CALC, R_V_GAP, R_V_START, R_V_SELECTORS, R_V_MEDIA, R_V_ATRULES, R_V_COLOR,
	R_V_INSET, R_V_KEYWORDS, R_V_GRADIENT, R_V_DECO, R_V_NOINLINE, R_V_ALL, R_LH_PCT, R_N
};
static struct { const char *why; unsigned long n; unsigned opt; } reasons[R_N] = {
	{ NULL, 0, 0 },
	{ "min-width and min-height start as auto (CSS Sizing 3), not 0 (CSS 2)", 0, 0 },
	{ "a border with no style has no width (CSS Backgrounds 3, 4.3)", 0, 0 },
	{ "a flex or grid item's display is blockified (CSS Display 3, 2.7)", 0, 0 },
	{ "gap, row-gap and column-gap: LibCSS has no gap", 0, 0 },
	{ "calc(), min(), max() and clamp() lengths: LibCSS drops them", 0, 0 },
	{ "gradients as background images: LibCSS has none", 0, 0 },
	{ "lengths in em inherit as px, not em (CSS Values 4, computed values)", 0, 0 },
	{ "top, right, bottom, left: LibCSS gives used values, CSS computed ones", 0, 0 },
	{ "flex: 1 is flex-basis 0% (CSS Flexbox 7.2): LibCSS says 0px", 0, 0 },
	{ "bolder and lighter compute to a weight (CSS Fonts 4, 2.2)", 0, 0 },
	{ "a medium border is 3px, as browsers draw it: LibCSS says 2px", 0, 0 },
	{ "float stays as given when display is none (CSS 2.1, 9.7)", 0, 0 },
	{ "LibCSS's px (unit.c) rounds the px in a unit down, and swaps vw and vh", 0, 0 },
	{ "calc(), min(), max(), clamp(): LibCSS drops them", 0, EC_COMPAT_CALC },
	{ "gap and row-gap: LibCSS has neither", 0, EC_COMPAT_GAP },
	{ "text-align: start, end, match-parent (CSS Text 3): LibCSS has none", 0, EC_COMPAT_START },
	{ "Selectors 4 (:is(), :where(), :not() of a list), form pseudo-classes, :lang()", 0,
		EC_COMPAT_SELECTORS },
	{ "media features other than width, height and prefers-color-scheme", 0, EC_COMPAT_MEDIA },
	{ "@supports and @layer: LibCSS skips them", 0, EC_COMPAT_ATRULES },
	{ "CSS Color 4 syntax: rgb() and hsl() with spaces and /", 0, EC_COMPAT_COLOR },
	{ "inset: LibCSS has no inset", 0, EC_COMPAT_INSET },
	{ "revert, display: flow-root and contents, other list styles", 0, EC_COMPAT_KEYWORDS },
	{ "gradients as images: LibCSS has none", 0, EC_COMPAT_GRADIENT },
	{ "text-decoration with a style or colour (CSS Text Decoration 3)", 0, EC_COMPAT_DECO },
	{ "LibCSS left out the element's style attribute (on a few pages: a LibCSS fault)", 0,
		EC_COMPAT_NOINLINE },
	{ "several of the switches above together", 0, EC_COMPAT_CALC | EC_COMPAT_GAP |
		EC_COMPAT_START | EC_COMPAT_SELECTORS | EC_COMPAT_MEDIA | EC_COMPAT_ATRULES |
		EC_COMPAT_COLOR | EC_COMPAT_INSET | EC_COMPAT_KEYWORDS | EC_COMPAT_GRADIENT |
		EC_COMPAT_DECO },
	{ "a line-height percentage computes to a length (CSS 2.1, 10.8.1)", 0, 0 },
};

/* the styles each switch gave, in the order walk() meets elements */
#define MAX_VARIANTS R_N
static const ec_style **vstyles[MAX_VARIANTS];
static unsigned long elem_index;

static void ec_get(int p, const ec_style *s, nv *o);
static int same(int p, const nv *a, const nv *b);

static int by_variant(int p, int pseudo, const nv *a)
{
	int r;

	for (r = R_V_CALC; r < R_N; r++) {
		const ec_style *v;
		if (!reasons[r].opt)
			continue;
		v = vstyles[r] ? vstyles[r][elem_index] : NULL;
		nv b;
		if (v != NULL && pseudo == 1)
			v = v->before;
		else if (v != NULL && pseudo == 2)
			v = v->after;
		if (v == NULL)
			continue;
		ec_get(p, v, &b);
		if (b.k == 'C')
			continue;
		if (same(p, a, &b))
			return r;
	}
	return R_NONE;
}

static int blockified(const char *d)
{
	return !strcmp(d, "block") || !strcmp(d, "table") || !strcmp(d, "flex") ||
		!strcmp(d, "grid") || !strcmp(d, "list-item") || !strcmp(d, "none") ||
		!strcmp(d, "flow-root");
}

static int explain(const node *n, int p, const css_computed_style *ls, const ec_style *es,
		const nv *a, const nv *b, int pseudo)
{
	css_fixed len;
	css_unit unit;
	int r = by_variant(p, pseudo, a);
	double px;

	if (r)
		return r;
	/* LibCSS's value, converted exactly, is EWeb's */
	if (a->k == 'p' && b->k == 'p' && a->ru != CSS_UNIT_PX) {
		const ec_node *e = n->ecn;
		int32_t fs = p == EP_FONT_SIZE && n->parent && n->parent->type == N_ELEMENT ?
			((const ec_node *) n->parent->ecn)->style->v[EP_FONT_SIZE].i :
			es->v[EP_FONT_SIZE].i;
		const ec_node *rt = e;
		while (rt->parent != NULL)
			rt = rt->parent;
		if (exact_px(a, fs, rt->style->v[EP_FONT_SIZE].i, &px)) {
			double d = px * 1024 - b->v;
			if (d < 0)
				d = -d;
			if (d <= 16 || d <= (b->v < 0 ? -b->v : b->v) / 100.0)
				return R_UNITS;
		}
	}
	switch (p) {
	case EP_FONT_WEIGHT:
		if (a->k == 'k' && (!strcmp(a->s, "bolder") || !strcmp(a->s, "lighter")))
			return R_WEIGHT;
		break;
	case EP_FLOAT:
		if (es->v[EP_DISPLAY].i == KW_NONE && a->k == 'k' && !strcmp(a->s, "none"))
			return R_FLOAT;
		break;
	case EP_MIN_WIDTH: case EP_MIN_HEIGHT:
		if (a->k == 'p' && a->v == 0 && b->k == 'k' && !strcmp(b->s, "auto"))
			return R_MIN_AUTO;
		break;
	case EP_BORDER_TOP_WIDTH: case EP_BORDER_RIGHT_WIDTH:
	case EP_BORDER_BOTTOM_WIDTH: case EP_BORDER_LEFT_WIDTH: {
		int st = es->v[p - EP_BORDER_TOP_WIDTH + EP_BORDER_TOP_STYLE].i;
		if (b->k == 'p' && b->v == 0 && (st == KW_NONE || st == KW_HIDDEN))
			return R_BORDER_NONE;
		if (a->k == 'p' && a->v == EC_FX(2) && b->k == 'p' && b->v == EC_FX(3))
			return R_MEDIUM;
		break;
	}
	case EP_DISPLAY: {
		const node *pn = n->parent;
		const ec_node *pe = pn != NULL && pn->type == N_ELEMENT ? pn->ecn : NULL;
		int pd = pseudo ? es->v[EP_DISPLAY].i : pe != NULL ? pe->style->v[EP_DISPLAY].i : 0;
		if (pseudo) {
			const ec_node *self = n->ecn;
			pd = self->style->v[EP_DISPLAY].i;
		}
		if ((pd == KW_FLEX || pd == KW_INLINE_FLEX || pd == KW_GRID || pd == KW_INLINE_GRID) &&
				b->k == 'k' && blockified(b->s))
			return R_FLEX_ITEM;
		break;
	}
	case EP_COLUMN_GAP:
		if (a->k == 'k' && !strcmp(a->s, "normal") && b->k != 'k')
			return R_GAP;
		break;
	case EP_BACKGROUND_IMAGE:
		if (b->k == 'g' && a->k == 'k' && !strcmp(a->s, "none"))
			return R_GRADIENT;
		break;
	case EP_LINE_HEIGHT:
		if (a->ru == CSS_UNIT_EM || a->ru == CSS_UNIT_EX || a->ru == CSS_UNIT_CH)
			return R_EM_INHERIT;
		if (a->k == '%' && b->k == 'p')
			return R_LH_PCT;
		break;
	case EP_LETTER_SPACING: case EP_WORD_SPACING: case EP_TEXT_INDENT:
		if ((p == EP_LETTER_SPACING ? css_computed_letter_spacing(ls, &len, &unit) :
				p == EP_WORD_SPACING ? css_computed_word_spacing(ls, &len, &unit) :
				css_computed_text_indent(ls, &len, &unit)) != 0 &&
				(unit == CSS_UNIT_EM || unit == CSS_UNIT_EX || unit == CSS_UNIT_CH))
			return R_EM_INHERIT;
		break;
	case EP_TOP: case EP_RIGHT: case EP_BOTTOM: case EP_LEFT: {
		int pos = es->v[EP_POSITION].i;
		if (pos == KW_STATIC || pos == KW_RELATIVE || pos == KW_STICKY)
			return R_USED_OFFSET;
		break;
	}
	case EP_FLEX_BASIS:
		if (a->k == 'p' && a->v == 0 && b->k == '%' && b->v == 0)
			return R_FLEX_BASIS;
		break;
	}
	if (b->k == 'x')
		return R_CALC;
	return R_NONE;
}
static int verbose, nsamples = NSAMPLES, variants = 1;
static const char *page_name;

static void path(const node *n, char *buf, size_t cap)
{
	char tmp[512];
	size_t len;

	buf[0] = 0;
	for (; n != NULL && n->type == N_ELEMENT; n = n->parent) {
		snprintf(tmp, sizeof tmp, "%s%s%s%s%s%s", lwc_string_data(n->name),
			n->id ? "#" : "", n->id ? lwc_string_data(n->id) : "",
			n->nclass ? "." : "", n->nclass ? lwc_string_data(n->classes[0]) : "",
			buf[0] ? ">" : "");
		len = strlen(tmp);
		if (len + strlen(buf) + 1 >= cap)
			break;
		memmove(buf + len, buf, strlen(buf) + 1);
		memcpy(buf, tmp, len);
	}
}

static void compare(const node *n, const css_computed_style *ls, const ec_style *es, int root,
		const char *what)
{
	int p;
	nv lcol, ecol;

	lc_get(EP_COLOR, ls, root, &lcol);
	ec_get(EP_COLOR, es, &ecol);
	for (p = 0; p < EC_NPROP; p++) {
		nv a, b;
		if (p == EP_OVERFLOW_WRAP || p == EP_ROW_GAP)
			continue;		/* LibCSS has neither */
		if (p >= EP_GRID_TEMPLATE_COLUMNS && p <= EP_JUSTIFY_SELF)
			continue;		/* nor grid's */
		if (p == EP_BACKGROUND_SIZE_W || p == EP_BACKGROUND_SIZE_H)
			continue;		/* nor background-size */
		lc_get(p, ls, root, &a);
		ec_get(p, es, &b);

		if (a.k == 'C')
			a.k = 'c', a.v = lcol.v;
		if (b.k == 'C')
			b.k = 'c', b.v = ecol.v;
		tally[p].n++;
		if (same(p, &a, &b))
			continue;
		{
			int r = explain(n, p, ls, es, &a, &b, !strcmp(what, " (::before)") ? 1 :
				what[0] ? 2 : 0);
			if (r) {
				reasons[r].n++;
				tally[p].explained++;
				continue;
			}
		}
		tally[p].diff++;
		if (verbose || tally[p].shown < nsamples) {
			char pa[1024], sa[256], sb[256];
			tally[p].shown++;
			path(n, pa, sizeof pa);
			show(sa, sizeof sa, &a);
			show(sb, sizeof sb, &b);
			printf("  %s: LibCSS %s, EWeb %s%s\t%s %s\n", ec_prop_name(p), sa, sb, what,
				page_name, pa);
		}
	}
}

static unsigned long pseudo_diff, elements;

/* each element's style from the last ecs_select(), in walk()'s order */
static void collect(const node *n, const ec_style **out, unsigned long *k)
{
	const node *c;

	if (n->type == N_ELEMENT && n->ecn != NULL)
		out[(*k)++] = ((const ec_node *) n->ecn)->style;
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == N_ELEMENT)
			collect(c, out, k);
}

static unsigned long count_elements(const node *n)
{
	const node *c;
	unsigned long k = n->type == N_ELEMENT;

	for (c = n->first; c != NULL; c = c->next)
		k += count_elements(c);
	return k;
}

static void walk(const node *n, int root)
{
	const node *c;

	if (n->type == N_ELEMENT && n->style != NULL && n->ecn != NULL ) {
		const css_select_results *r = n->style;
		const ec_node *e = n->ecn;
		elements++;
		elem_index++;
		compare(n, r->styles[CSS_PSEUDO_ELEMENT_NONE], e->style, root, "");
		if ((r->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL) != (e->style->before != NULL) ||
				(r->styles[CSS_PSEUDO_ELEMENT_AFTER] != NULL) != (e->style->after != NULL))
			pseudo_diff++;
		if (r->styles[CSS_PSEUDO_ELEMENT_BEFORE] && e->style->before)
			compare(n, r->styles[CSS_PSEUDO_ELEMENT_BEFORE], e->style->before, 0, " (::before)");
		if (r->styles[CSS_PSEUDO_ELEMENT_AFTER] && e->style->after)
			compare(n, r->styles[CSS_PSEUDO_ELEMENT_AFTER], e->style->after, 0, " (::after)");
		root = 0;
	}
	for (c = n->first; c != NULL; c = c->next)
		if (c->type == N_ELEMENT)
			walk(c, root);
}

/* plat_posix.c's main() calls it */
int nsbench(int argc, char **argv)
{
	const char *sheet[32];
	size_t sheet_len[32];
	int nsheets = 0, i, pages = 0, p;
	unsigned long total = 0, diff = 0;

	ecs_opts = EC_OPT_NO_VAR;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-V")) {
			ecs_opts = 0;
		} else if (!strcmp(argv[i], "-1")) {
			variants = 0;
		} else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
			nsamples = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-s") && i + 1 < argc && nsheets < 32) {
			sheet[nsheets] = load(argv[++i], &sheet_len[nsheets]);
			if (sheet[nsheets] == NULL) {
				fprintf(stderr, "csscmp: can't read %s\n", argv[i]);
				return 1;
			}
			nsheets++;
		} else {
			size_t len;
			char *page = load(argv[i], &len);
			node *doc;
			style_stats ss;
			struct css_select_ctx *ctx;
			struct ec_ctx *ec;
			if (page == NULL) {
				fprintf(stderr, "csscmp: can't read %s\n", argv[i]);
				return 1;
			}
			page_name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
			doc = dom_parse((const uint8_t *) page, len, 4096);
			if (doc == NULL) {
				free(page);
				continue;
			}
			struct ec_ctx *vctx[MAX_VARIANTS];
			unsigned long ne = count_elements(doc) + 1, k;
			int r;
			unsigned base = ecs_opts;
			memset(&ss, 0, sizeof ss);
			ctx = style_sheets(doc, sheet, sheet_len, nsheets, &ss);
			style_select(ctx, doc, &ss);
			/* the page with each switch on, then as it is */
			for (r = 0; r < R_N; r++) {
				vctx[r] = NULL;
				vstyles[r] = NULL;
				if (!reasons[r].opt || !variants)
					continue;
				ecs_opts = base | reasons[r].opt;
				vctx[r] = ecs_sheets(doc, sheet, sheet_len, nsheets, &ss);
				ecs_select(vctx[r], doc, &ss);
				vstyles[r] = calloc(ne, sizeof *vstyles[r]);
				k = 0;
				collect(doc, vstyles[r], &k);
			}
			ecs_opts = base;
			ec = ecs_sheets(doc, sheet, sheet_len, nsheets, &ss);
			ecs_select(ec, doc, &ss);
			elem_index = (unsigned long) -1;
			walk(doc, 1);
			ecs_free(ec, doc);
			for (r = 0; r < R_N; r++) {
				if (vctx[r] != NULL)
					ecs_free(vctx[r], doc);
				free(vstyles[r]);
				vstyles[r] = NULL;
			}
			style_free(ctx, doc);
			dom_free(doc);
			free(page);
			pages++;
		}
	}
	printf("%d pages, %lu elements; ::before or ::after made by one engine only: %lu\n",
		pages, elements, pseudo_diff);
	{
		unsigned long expl = 0;
		for (p = 0; p < EC_NPROP; p++) {
			total += tally[p].n;
			diff += tally[p].diff;
			expl += tally[p].explained;
			if (tally[p].diff)
				printf("%-24s %8lu of %8lu differ\n", ec_prop_name(p), tally[p].diff,
					tally[p].n);
		}
		printf("where LibCSS falls short of the specifications (%lu values):\n", expl);
		for (p = 1; p < R_N; p++)
			if (reasons[p].n)
				printf("  %8lu  %s\n", reasons[p].n, reasons[p].why);
		printf("all: %lu of %lu values differ for no known reason (%.3f%%)\n", diff, total,
			total ? 100.0 * diff / total : 0.0);
	}
	for (i = 0; i < nsheets; i++)
		free((char *) sheet[i]);
	return diff != 0;
}
