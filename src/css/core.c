/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The CSS engine's memory, atoms and name tables, and its context. */
#include "ec_int.h"

/* ------------------------------------------------------------ memory */

/* a context's memory comes in pieces of this: in the ST edition small
   ones, as a 1 MB ST has 200 KB for everything, and there an arena's
   first pieces smaller still (ec_alloc_slow) */
#ifdef EW_ST
#define EC_CHUNK 4096
#define EC_FIRST 512
#else
#define EC_CHUNK 65536
#endif

void *ec_alloc_slow(ec_arena *a, size_t n)
{
	size_t size = a->chunk_size ? a->chunk_size : 32768;
	ec_chunk *ch;

#ifdef EC_FIRST
	/* from EC_FIRST, each piece as big as those before it together: a
	   small page's names, sheets and styles (a context has an arena for
	   each) take about what they need, not a piece each. Only in the ST
	   edition: on the Falcon the pieces more cost more time than the
	   memory they save is worth (+0.08% of the page set's 68030
	   instructions) */
	if (a->size < size)
		size = a->size > EC_FIRST ? a->size : EC_FIRST;
#endif
	if (n + sizeof(ec_chunk) + EC_ALIGN > size)
		size = n + sizeof(ec_chunk) + EC_ALIGN;
	ch = malloc(size);
	if (ch == NULL) {
		a->nomem = 1;
		return NULL;
	}
	ch->next = a->chunks;
	a->chunks = ch;
	a->size += size;
	a->p = (char *) ch + ((sizeof(ec_chunk) + EC_ALIGN - 1) & ~(EC_ALIGN - 1));
	a->end = (char *) ch + size;
	return ec_alloc(a, n);
}

void ec_arena_free(ec_arena *a)
{
	ec_chunk *ch, *next;

	for (ch = a->chunks; ch != NULL; ch = next) {
		next = ch->next;
		free(ch);
	}
	a->chunks = NULL;
	a->p = a->end = NULL;
	a->size = 0;
	a->nomem = 0;
}

const ec_str *ec_str_new(ec_arena *a, const char *s, size_t len)
{
	ec_str *r = ec_alloc(a, sizeof(ec_str) + len + 1);

	if (r == NULL)
		return NULL;
	r->len = len;
	memcpy(r->s, s, len);
	r->s[len] = 0;
	return r;
}

/* ------------------------------------------------------------ atoms */

uint32_t ec_hash(const char *s, size_t len)
{
	uint32_t h = 5381;

	while (len--)
		h = ((h << 5) + h) ^ (uint8_t) *s++;
	return h;
}

static int atoms_grow(ec_ctx *c)
{
	uint32_t size = c->atom_mask ? (c->atom_mask + 1) * 2 : 1024, i;
	ec_atom **t = calloc(size, sizeof *t);

	if (t == NULL)
		return -1;
	for (i = 0; c->atoms != NULL && i <= c->atom_mask; i++) {
		ec_atom *a = c->atoms[i], *next;
		for (; a != NULL; a = next) {
			next = a->next;
			a->next = t[a->hash & (size - 1)];
			t[a->hash & (size - 1)] = a;
		}
	}
	free(c->atoms);
	c->atoms = t;
	c->atom_mask = size - 1;
	return 0;
}

uint32_t ec_stamp;
char *ec_stack_floor;

const ec_atom *ec_intern(ec_ctx *c, const char *s, size_t len)
{
	uint32_t h = ec_hash(s, len), b;
	ec_atom *a;

	if (c->base != NULL)
		/* the base's atom, if it has one: its rules match here */
		for (a = c->base->atoms[h & c->base->atom_mask]; a != NULL; a = a->next)
			if (a->hash == h && a->len == len && memcmp(a->s, s, len) == 0)
				return a;
	for (a = c->atoms[h & c->atom_mask]; a != NULL; a = a->next)
		if (a->hash == h && a->len == len && memcmp(a->s, s, len) == 0)
			return a;
	if (c->natoms >= c->atom_mask && atoms_grow(c) != 0)
		return NULL;
	a = ec_alloc(&c->mem, sizeof(ec_atom) + len + 1);
	if (a == NULL)
		return NULL;
	memset(a, 0, sizeof *a);
	a->hash = h;
	a->len = len;
	b = (h ^ (h >> 12) ^ (h >> 24)) & (EC_BLOOM_SIZE - 1);
	a->bloom = b ? b : 1;
	memcpy(a->s, s, len);
	a->s[len] = 0;
	a->next = c->atoms[h & c->atom_mask];
	c->atoms[h & c->atom_mask] = a;
	c->natoms++;
	return a;
}

const char *ec_atom_text(const ec_atom *a, size_t *len)
{
	if (len != NULL)
		*len = a->len;
	return a->s;
}

/* ------------------------------------------------------------ names */

typedef struct name {
	const char *s;
	uint8_t len, cls;
	int32_t val;
} name;

#ifdef EW_ST
#define NAMES_SIZE 1024		/* 530 names: room enough, 10 KB less for a 1 MB ST */
#else
#define NAMES_SIZE 2048
#endif
static name names[NAMES_SIZE];
static int names_ready;

#define EC_P_NAME(id, nm, parser, flags, kind, val) nm,
static const char *const prop_names[EC_NPROP] = {
	EC_PROPS_INHERITED(EC_P_NAME) EC_PROPS_OTHER(EC_P_NAME)
};
#undef EC_P_NAME
#define EC_S_NAME(id, nm) nm,
static const char *const sh_names[EC_NSH] = { EC_SHORTHANDS(EC_S_NAME) };
#undef EC_S_NAME
#define EC_K_NAME(id, text) text,
static const char *const kw_names[EC_NKW] = { "", EC_KEYWORDS(EC_K_NAME) };
#undef EC_K_NAME

static const struct { const char *s; uint32_t rgb; } colors[] = {
	{ "aliceblue", 0xf0f8ff }, { "antiquewhite", 0xfaebd7 }, { "aqua", 0x00ffff },
	{ "aquamarine", 0x7fffd4 }, { "azure", 0xf0ffff }, { "beige", 0xf5f5dc },
	{ "bisque", 0xffe4c4 }, { "black", 0x000000 }, { "blanchedalmond", 0xffebcd },
	{ "blue", 0x0000ff }, { "blueviolet", 0x8a2be2 }, { "brown", 0xa52a2a },
	{ "burlywood", 0xdeb887 }, { "cadetblue", 0x5f9ea0 }, { "chartreuse", 0x7fff00 },
	{ "chocolate", 0xd2691e }, { "coral", 0xff7f50 }, { "cornflowerblue", 0x6495ed },
	{ "cornsilk", 0xfff8dc }, { "crimson", 0xdc143c }, { "cyan", 0x00ffff },
	{ "darkblue", 0x00008b }, { "darkcyan", 0x008b8b }, { "darkgoldenrod", 0xb8860b },
	{ "darkgray", 0xa9a9a9 }, { "darkgreen", 0x006400 }, { "darkgrey", 0xa9a9a9 },
	{ "darkkhaki", 0xbdb76b }, { "darkmagenta", 0x8b008b }, { "darkolivegreen", 0x556b2f },
	{ "darkorange", 0xff8c00 }, { "darkorchid", 0x9932cc }, { "darkred", 0x8b0000 },
	{ "darksalmon", 0xe9967a }, { "darkseagreen", 0x8fbc8f }, { "darkslateblue", 0x483d8b },
	{ "darkslategray", 0x2f4f4f }, { "darkslategrey", 0x2f4f4f }, { "darkturquoise", 0x00ced1 },
	{ "darkviolet", 0x9400d3 }, { "deeppink", 0xff1493 }, { "deepskyblue", 0x00bfff },
	{ "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "dodgerblue", 0x1e90ff },
	{ "firebrick", 0xb22222 }, { "floralwhite", 0xfffaf0 }, { "forestgreen", 0x228b22 },
	{ "fuchsia", 0xff00ff }, { "gainsboro", 0xdcdcdc }, { "ghostwhite", 0xf8f8ff },
	{ "gold", 0xffd700 }, { "goldenrod", 0xdaa520 }, { "gray", 0x808080 },
	{ "green", 0x008000 }, { "greenyellow", 0xadff2f }, { "grey", 0x808080 },
	{ "honeydew", 0xf0fff0 }, { "hotpink", 0xff69b4 }, { "indianred", 0xcd5c5c },
	{ "indigo", 0x4b0082 }, { "ivory", 0xfffff0 }, { "khaki", 0xf0e68c },
	{ "lavender", 0xe6e6fa }, { "lavenderblush", 0xfff0f5 }, { "lawngreen", 0x7cfc00 },
	{ "lemonchiffon", 0xfffacd }, { "lightblue", 0xadd8e6 }, { "lightcoral", 0xf08080 },
	{ "lightcyan", 0xe0ffff }, { "lightgoldenrodyellow", 0xfafad2 }, { "lightgray", 0xd3d3d3 },
	{ "lightgreen", 0x90ee90 }, { "lightgrey", 0xd3d3d3 }, { "lightpink", 0xffb6c1 },
	{ "lightsalmon", 0xffa07a }, { "lightseagreen", 0x20b2aa }, { "lightskyblue", 0x87cefa },
	{ "lightslategray", 0x778899 }, { "lightslategrey", 0x778899 }, { "lightsteelblue", 0xb0c4de },
	{ "lightyellow", 0xffffe0 }, { "lime", 0x00ff00 }, { "limegreen", 0x32cd32 },
	{ "linen", 0xfaf0e6 }, { "magenta", 0xff00ff }, { "maroon", 0x800000 },
	{ "mediumaquamarine", 0x66cdaa }, { "mediumblue", 0x0000cd }, { "mediumorchid", 0xba55d3 },
	{ "mediumpurple", 0x9370db }, { "mediumseagreen", 0x3cb371 }, { "mediumslateblue", 0x7b68ee },
	{ "mediumspringgreen", 0x00fa9a }, { "mediumturquoise", 0x48d1cc },
	{ "mediumvioletred", 0xc71585 }, { "midnightblue", 0x191970 }, { "mintcream", 0xf5fffa },
	{ "mistyrose", 0xffe4e1 }, { "moccasin", 0xffe4b5 }, { "navajowhite", 0xffdead },
	{ "navy", 0x000080 }, { "oldlace", 0xfdf5e6 }, { "olive", 0x808000 },
	{ "olivedrab", 0x6b8e23 }, { "orange", 0xffa500 }, { "orangered", 0xff4500 },
	{ "orchid", 0xda70d6 }, { "palegoldenrod", 0xeee8aa }, { "palegreen", 0x98fb98 },
	{ "paleturquoise", 0xafeeee }, { "palevioletred", 0xdb7093 }, { "papayawhip", 0xffefd5 },
	{ "peachpuff", 0xffdab9 }, { "peru", 0xcd853f }, { "pink", 0xffc0cb },
	{ "plum", 0xdda0dd }, { "powderblue", 0xb0e0e6 }, { "purple", 0x800080 },
	{ "rebeccapurple", 0x663399 }, { "red", 0xff0000 }, { "rosybrown", 0xbc8f8f },
	{ "royalblue", 0x4169e1 }, { "saddlebrown", 0x8b4513 }, { "salmon", 0xfa8072 },
	{ "sandybrown", 0xf4a460 }, { "seagreen", 0x2e8b57 }, { "seashell", 0xfff5ee },
	{ "sienna", 0xa0522d }, { "silver", 0xc0c0c0 }, { "skyblue", 0x87ceeb },
	{ "slateblue", 0x6a5acd }, { "slategray", 0x708090 }, { "slategrey", 0x708090 },
	{ "snow", 0xfffafa }, { "springgreen", 0x00ff7f }, { "steelblue", 0x4682b4 },
	{ "tan", 0xd2b48c }, { "teal", 0x008080 }, { "thistle", 0xd8bfd8 },
	{ "tomato", 0xff6347 }, { "turquoise", 0x40e0d0 }, { "violet", 0xee82ee },
	{ "wheat", 0xf5deb3 }, { "white", 0xffffff }, { "whitesmoke", 0xf5f5f5 },
	{ "yellow", 0xffff00 }, { "yellowgreen", 0x9acd32 },
	/* system colours, as a light theme draws them */
	{ "canvas", 0xffffff }, { "canvastext", 0x000000 }, { "linktext", 0x0000ee },
	{ "visitedtext", 0x551a8b }, { "activetext", 0xff0000 }, { "buttonface", 0xefefef },
	{ "buttontext", 0x000000 }, { "buttonborder", 0x767676 }, { "field", 0xffffff },
	{ "fieldtext", 0x000000 }, { "highlight", 0x3399ff }, { "highlighttext", 0xffffff },
	{ "graytext", 0x808080 }, { "mark", 0xffff00 }, { "marktext", 0x000000 },
};

/* the length units: what one is in px (absolute), or EC_UNIT_REL(CU_*) */
static const struct { const char *s; int32_t v; } units[] = {
	{ "px", EC_FX(1) }, { "in", EC_FX(96) }, { "cm", 38702 }, { "mm", 3870 },
	{ "q", 968 }, { "pt", 1365 }, { "pc", EC_FX(16) },
	{ "em", EC_UNIT_REL(CU_EM) }, { "rem", EC_UNIT_REL(CU_REM) }, { "ex", EC_UNIT_REL(CU_EX) },
	{ "ch", EC_UNIT_REL(CU_CH) }, { "vw", EC_UNIT_REL(CU_VW) }, { "vh", EC_UNIT_REL(CU_VH) },
	{ "vmin", EC_UNIT_REL(CU_VMIN) }, { "vmax", EC_UNIT_REL(CU_VMAX) },
	{ "svw", EC_UNIT_REL(CU_VW) }, { "lvw", EC_UNIT_REL(CU_VW) }, { "dvw", EC_UNIT_REL(CU_VW) },
	{ "svh", EC_UNIT_REL(CU_VH) }, { "lvh", EC_UNIT_REL(CU_VH) }, { "dvh", EC_UNIT_REL(CU_VH) },
};

/* other engines' names for a keyword: Gecko's for the legacy alignments */
static const struct { const char *s; int kw; } kw_aliases[] = {
	{ "-moz-left", KW_WEBKIT_LEFT }, { "-moz-right", KW_WEBKIT_RIGHT },
	{ "-moz-center", KW_WEBKIT_CENTER }, { "-moz-middle-with-baseline", KW_WEBKIT_BASELINE_MIDDLE },
};

static inline uint8_t lower(uint8_t ch)
{
	return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
}

static uint32_t hash_lower(const char *s, size_t len, int cls)
{
	uint32_t h = 5381 + cls;

	while (len--)
		h = ((h << 5) + h) ^ lower(*s++);
	return h;
}

static void name_add(const char *s, int cls, int32_t val)
{
	size_t len = strlen(s);
	uint32_t i = hash_lower(s, len, cls) & (NAMES_SIZE - 1);

	while (names[i].s != NULL)
		i = (i + 1) & (NAMES_SIZE - 1);
	names[i].s = s;
	names[i].len = len;
	names[i].cls = cls;
	names[i].val = val;
}

static void names_init(void)
{
	size_t i;

	if (names_ready)
		return;
	for (i = 1; i < EC_NKW; i++)
		name_add(kw_names[i], KC_KW, i);
	for (i = 0; i < sizeof kw_aliases / sizeof kw_aliases[0]; i++)
		name_add(kw_aliases[i].s, KC_KW, kw_aliases[i].kw);
	for (i = 0; i < EC_NPROP; i++)
		name_add(prop_names[i], KC_PROP, i);
	for (i = 0; i < EC_NSH; i++)
		name_add(sh_names[i], KC_PROP, EC_SH_BASE + i);
	for (i = 0; i < sizeof colors / sizeof colors[0]; i++)
		name_add(colors[i].s, KC_COLOR, (int32_t) (0xff000000u | colors[i].rgb));
	for (i = 0; i < sizeof units / sizeof units[0]; i++)
		name_add(units[i].s, KC_UNIT, units[i].v);
	names_ready = 1;
}

/* case-insensitive (ASCII), as CSS's names are */
int ec_names_lookup(int cls, const char *s, size_t len, int32_t *val)
{
	uint32_t i;

	if (len == 0 || len > 255)
		return 0;
	i = hash_lower(s, len, cls) & (NAMES_SIZE - 1);
	for (; names[i].s != NULL; i = (i + 1) & (NAMES_SIZE - 1)) {
		size_t k;
		if (names[i].cls != cls || names[i].len != len)
			continue;
		for (k = 0; k < len && (uint8_t) names[i].s[k] == lower(s[k]); k++)
			;
		if (k == len) {
			*val = names[i].val;
			return 1;
		}
	}
	return 0;
}

int ec_kw(const ec_tok *t)
{
	int32_t v;
	const char *s;
	size_t len;
	char buf[64];

	if (t->type != T_IDENT)
		return 0;
	s = t->s;
	len = t->len;
	if (EC_UNLIKELY(t->flags & TF_ESC))
		s = ec_tok_text(t, buf, sizeof buf, &len);
	return ec_names_lookup(KC_KW, s, len, &v) ? v : 0;
}

int ec_prop_lookup(const char *s, size_t len)
{
	int32_t v;
	return ec_names_lookup(KC_PROP, s, len, &v) ? v : -1;
}

const char *ec_prop_name(int prop)
{
	if (prop >= 0 && prop < EC_NPROP)
		return prop_names[prop];
	if (prop >= EC_SH_BASE && prop < EC_SH_BASE + EC_NSH)
		return sh_names[prop - EC_SH_BASE];
	return "?";
}

const char *ec_kw_name(int kw)
{
	return kw > 0 && kw < EC_NKW ? kw_names[kw] : "?";
}

/* ------------------------------------------------------------ the context */

static const ec_family family_default = { 0, KW_SANS_SERIF };

#define EC_P_INIT(id, nm, parser, flags, kind, val) { kind, val },
static const struct { uint8_t kind; int32_t val; } initial[EC_NPROP] = {
	EC_PROPS_INHERITED(EC_P_INIT) EC_PROPS_OTHER(EC_P_INIT)
};
#undef EC_P_INIT

ec_ctx *ec_create(const ec_media *media, unsigned opts)
{
	return ec_create_on(media, opts, NULL);
}

ec_ctx *ec_create_on(const ec_media *media, unsigned opts, const ec_ctx *base)
{
	ec_ctx *c = calloc(1, sizeof *c);
	int i;

	if (c == NULL)
		return NULL;
	names_init();
	c->base = base;
	if (base != NULL) {
		/* its rules come first, as if its sheets had been added here */
		c->order = base->order;
		c->sibling_rules = base->sibling_rules;
	}
	c->media = *media;
	c->opts = opts;
	c->span_lo = 0;
	c->span_hi = INT32_MAX;
	c->mem.chunk_size = EC_CHUNK;
	c->rmem.chunk_size = EC_CHUNK;
	c->smem.chunk_size = EC_CHUNK;
	c->pmem = &c->rmem;
	if (atoms_grow(c) != 0) {
		free(c);
		return NULL;
	}
	for (i = 0; i < EC_NPROP; i++) {
		c->initial.k[i] = initial[i].kind;
		c->initial.v[i].i = initial[i].val;
	}
	c->initial.v[EP_FONT_FAMILY].p = &family_default;
	c->initial.v[EP_FONT_SIZE].i = EC_FX(media->font_size);

#define A(field, s) c->field = ec_intern(c, s, sizeof s - 1)
	A(a_style, "style"); A(a_href, "href"); A(a_lang, "lang"); A(a_dir, "dir");
	A(a_type, "type"); A(a_disabled, "disabled"); A(a_checked, "checked");
	A(a_selected, "selected"); A(a_required, "required"); A(a_readonly, "readonly");
	A(a_placeholder, "placeholder"); A(a_value, "value"); A(a_hidden, "hidden"); A(a_open, "open");
	A(t_a, "a"); A(t_area, "area"); A(t_input, "input"); A(t_button, "button");
	A(t_select, "select"); A(t_textarea, "textarea"); A(t_option, "option");
	A(t_optgroup, "optgroup"); A(t_fieldset, "fieldset"); A(t_html, "html");
	A(t_details, "details"); A(t_dialog, "dialog");
#undef A
	/* elements sharing a style must have the same inline style */
	((ec_atom *) c->a_style)->ref = 1;
	return c;
}

void ec_destroy(ec_ctx *c)
{
	if (c == NULL)
		return;
	ec_arena_free(&c->smem);
	ec_arena_free(&c->rmem);
	ec_arena_free(&c->mem);
	free(c->atoms);
	free(c->shadow);
	free(c);
}

static void parse_all(ec_ctx *c, const ec_vec *v)
{
	uint32_t i;
	for (i = 0; v != NULL && i < v->n; i++) {
		ec_block *b = (ec_block *) v->e[i].sel->block;
		if (!b->parsed)
			ec_parse_block(c, b, &c->rmem);
	}
}

void ec_share(ec_ctx *c)
{
	uint32_t i;
	int k;

	/* every declaration block parsed now, so that a context on it never
	   writes into it */
	parse_all(c, &c->universal);
	parse_all(c, &c->never);
	for (i = 0; i <= c->atom_mask; i++) {
		ec_atom *a;
		for (a = c->atoms[i]; a != NULL; a = a->next) {
			for (k = 0; k < EC_NB; k++)
				parse_all(c, a->rules[k]);
			a->shared = 1;
		}
	}
}

ec_shadow *ec_shadow_of(ec_ctx *c, const ec_atom *a, int make)
{
	uint32_t h = (a->hash ^ (a->hash >> 8)) & (EC_SHADOW_SIZE - 1);
	ec_shadow *s;

	if (c->shadow != NULL)
		for (s = c->shadow[h]; s != NULL; s = s->next)
			if (s->a == a)
				return s;
	if (!make)
		return NULL;
	if (c->shadow == NULL && (c->shadow = calloc(EC_SHADOW_SIZE, sizeof *c->shadow)) == NULL)
		return NULL;
	s = ec_alloc(&c->rmem, sizeof *s);
	if (s == NULL)
		return NULL;
	memset(s, 0, sizeof *s);
	s->a = a;
	s->next = c->shadow[h];
	c->shadow[h] = s;
	return s;
}

void ec_unstyle(ec_ctx *c)
{
	ec_arena_free(&c->smem);
	c->root_style = NULL;
}

void ec_set_media(ec_ctx *c, const ec_media *m)
{
	c->media = *m;
	c->initial.v[EP_FONT_SIZE].i = EC_FX(m->font_size);
}

void ec_media_span(const ec_ctx *c, int32_t *lo, int32_t *hi)
{
	*lo = c->span_lo;
	*hi = c->span_hi;
}

void ec_drop_sheets(ec_ctx *c)
{
	uint32_t i;

	ec_unstyle(c);
	/* the rules on its own atoms, on the base's (its shadow), and on none */
	for (i = 0; i <= c->atom_mask; i++) {
		ec_atom *a;
		for (a = c->atoms[i]; a != NULL; a = a->next) {
			memset(a->rules, 0, sizeof a->rules);
			memset(a->sibrules, 0, sizeof a->sibrules);
		}
	}
	free(c->shadow);
	c->shadow = NULL;
	/* and what they were: their text, rules and declarations (a page's
	   sheets added again as often as its window is sized take no more) */
	ec_arena_free(&c->rmem);
	c->pmem = &c->rmem;
	memset(&c->universal, 0, sizeof c->universal);
	memset(&c->universal_sib, 0, sizeof c->universal_sib);
	memset(&c->never, 0, sizeof c->never);
	c->order = c->base != NULL ? c->base->order : 0;
	c->sibling_rules = c->base != NULL ? c->base->sibling_rules : 0;
	c->layer = 0;
	memset(&c->st, 0, sizeof c->st);
	c->span_lo = 0;
	c->span_hi = INT32_MAX;
}

int ec_sheets_short(const ec_ctx *c)
{
	return c->rmem.nomem;
}

/* an arena's pieces but what is left of the last */
static size_t arena_used(const ec_arena *a)
{
	return a->size - (size_t) (a->end - a->p);
}

size_t ec_mem(const ec_ctx *c)
{
	return arena_used(&c->mem) + arena_used(&c->rmem) + arena_used(&c->smem);
}

const ec_stats *ec_get_stats(const ec_ctx *c)
{
	return &c->st;
}
