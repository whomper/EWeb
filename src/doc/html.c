/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * html.c - EWeb's HTML parser, after the HTML Standard (13.2, as of
 * October 2026): the encoding sniffing algorithm, the tokenizer and tree
 * construction, and the fragment case (13.4). Written for a 68030:
 *
 * - The page is decoded to UTF-8 once, its newlines normalised on the
 *   way, into one buffer ending in a 0 that every scanning loop stops at.
 * - The tokenizer takes runs: text up to the next character that means
 *   something in its state, a tag name or an attribute value, at once.
 *   Text goes to the tree as pointers into the page; names and values
 *   are gathered in one scratch buffer.
 * - The tree builder writes EWeb's nodes (doc.h) out of the document's
 *   memory, with no callbacks and no reference counts; an element's name
 *   is its atom for the CSS engine, looked up once a document for the
 *   names the parser knows.
 *
 * There are no scripts, so nothing writes into the page while it is
 * parsed; a <meta charset> that changes the encoding ends the parse, and
 * html_parse starts again in that encoding.
 */
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/codec.h>
#include <parserutils/charset/mibenum.h>

#include "doc.h"
#include "html.h"
#include "entities.h"

/* ---------------------------------------------------------------- names */

#define TAGS(X) \
	X(A, "a") X(ADDRESS, "address") X(ANNOTATION_XML, "annotation-xml") X(APPLET, "applet") \
	X(AREA, "area") X(ARTICLE, "article") X(ASIDE, "aside") X(AUDIO, "audio") X(B, "b") \
	X(BASE, "base") X(BASEFONT, "basefont") X(BGSOUND, "bgsound") X(BIG, "big") \
	X(BLOCKQUOTE, "blockquote") X(BODY, "body") X(BR, "br") X(BUTTON, "button") \
	X(CANVAS, "canvas") X(CAPTION, "caption") X(CENTER, "center") X(CODE, "code") \
	X(COL, "col") X(COLGROUP, "colgroup") X(DD, "dd") X(DESC, "desc") X(DETAILS, "details") \
	X(DIALOG, "dialog") X(DIR, "dir") X(DIV, "div") X(DL, "dl") X(DT, "dt") X(EM, "em") \
	X(EMBED, "embed") X(FIELDSET, "fieldset") X(FIGCAPTION, "figcaption") X(FIGURE, "figure") \
	X(FONT, "font") X(FOOTER, "footer") X(FOREIGNOBJECT, "foreignobject") X(FORM, "form") \
	X(FRAME, "frame") X(FRAMESET, "frameset") X(H1, "h1") X(H2, "h2") X(H3, "h3") \
	X(H4, "h4") X(H5, "h5") X(H6, "h6") X(HEAD, "head") X(HEADER, "header") \
	X(HGROUP, "hgroup") X(HR, "hr") X(HTML, "html") X(I, "i") X(IFRAME, "iframe") \
	X(IMAGE, "image") X(IMG, "img") X(INPUT, "input") X(KEYGEN, "keygen") X(LABEL, "label") \
	X(LEGEND, "legend") X(LI, "li") X(LINK, "link") X(LISTING, "listing") X(MAIN, "main") \
	X(MALIGNMARK, "malignmark") X(MARQUEE, "marquee") X(MATH, "math") X(MENU, "menu") \
	X(META, "meta") X(MGLYPH, "mglyph") X(MI, "mi") X(MN, "mn") X(MO, "mo") X(MS, "ms") \
	X(MTEXT, "mtext") X(NAV, "nav") X(NOBR, "nobr") X(NOEMBED, "noembed") \
	X(NOFRAMES, "noframes") X(NOSCRIPT, "noscript") X(OBJECT, "object") X(OL, "ol") \
	X(OPTGROUP, "optgroup") X(OPTION, "option") X(OUTPUT, "output") X(P, "p") \
	X(PARAM, "param") X(PLAINTEXT, "plaintext") X(PRE, "pre") X(RB, "rb") X(RP, "rp") \
	X(RT, "rt") X(RTC, "rtc") X(RUBY, "ruby") X(S, "s") X(SCRIPT, "script") \
	X(SEARCH, "search") X(SECTION, "section") X(SELECT, "select") X(SMALL, "small") \
	X(SOURCE, "source") X(SPAN, "span") X(STRIKE, "strike") X(STRONG, "strong") \
	X(STYLE, "style") X(SUB, "sub") X(SUMMARY, "summary") X(SUP, "sup") X(SVG, "svg") \
	X(TABLE, "table") X(TBODY, "tbody") X(TD, "td") X(TEMPLATE, "template") \
	X(TEXTAREA, "textarea") X(TFOOT, "tfoot") X(TH, "th") X(THEAD, "thead") \
	X(TITLE, "title") X(TR, "tr") X(TRACK, "track") X(TT, "tt") X(U, "u") X(UL, "ul") \
	X(VAR, "var") X(VIDEO, "video") X(WBR, "wbr") X(XMP, "xmp")

#define X_ENUM(id, s) H_##id,
enum { H_NONE, TAGS(X_ENUM) H_N };
#define X_NAME(id, s) s,
static const char *const hname[H_N] = { "", TAGS(X_NAME) };

/* what the tree builder asks of an HTML element */
#define F_SPECIAL	0x001	/* the special category */
#define F_FORMAT	0x002	/* formatting elements */
#define F_IMPLIED	0x004	/* generate implied end tags */
#define F_THOROUGH	0x008	/* ... thoroughly */
#define F_SCOPE		0x010	/* bounds "in scope" */
#define F_BREAKOUT	0x020	/* a start tag that leaves foreign content */
#define F_ASSOC		0x040	/* form-associated */
#define F_LISTED	0x080	/* listed */
#define F_HEADING	0x100	/* h1-h6 */
#define F_BLOCK		0x200	/* "address", "article", ...: closes a p */
#define F_BLOCKEND	0x400	/* their end tags, with button, listing, pre, select */

static uint16_t hflags[H_N];
static uint8_t hT[H_N];			/* EWeb's T_* */

/* the names in a hash table: 512 slots, from the name's length and three
   of its bytes (constants chosen so that no name takes over two looks) */
static uint8_t htab[512];
static uint8_t hlen[H_N];
static int ready;

static unsigned hhash(const uint8_t *s, size_t n)
{
	return (unsigned) (n * 26 + s[0] * 3 + s[n - 1] * 11 + s[n >> 1]) & 511;
}

static void names_init(void)
{
	static const uint8_t special[] = { H_ADDRESS, H_APPLET, H_AREA, H_ARTICLE, H_ASIDE, H_BASE,
		H_BASEFONT, H_BGSOUND, H_BLOCKQUOTE, H_BODY, H_BR, H_BUTTON, H_CAPTION, H_CENTER,
		H_COL, H_COLGROUP, H_DD, H_DETAILS, H_DIR, H_DIV, H_DL, H_DT, H_EMBED, H_FIELDSET,
		H_FIGCAPTION, H_FIGURE, H_FOOTER, H_FORM, H_FRAME, H_FRAMESET, H_H1, H_H2, H_H3,
		H_H4, H_H5, H_H6, H_HEAD, H_HEADER, H_HGROUP, H_HR, H_HTML, H_IFRAME, H_IMG,
		H_INPUT, H_KEYGEN, H_LI, H_LINK, H_LISTING, H_MAIN, H_MARQUEE, H_MENU, H_META,
		H_NAV, H_NOEMBED, H_NOFRAMES, H_NOSCRIPT, H_OBJECT, H_OL, H_P, H_PARAM,
		H_PLAINTEXT, H_PRE, H_SCRIPT, H_SEARCH, H_SECTION, H_SELECT, H_SOURCE, H_STYLE,
		H_SUMMARY, H_TABLE, H_TBODY, H_TD, H_TEMPLATE, H_TEXTAREA, H_TFOOT, H_TH, H_THEAD,
		H_TITLE, H_TR, H_TRACK, H_UL, H_WBR, H_XMP, 0 };
	static const uint8_t format[] = { H_A, H_B, H_BIG, H_CODE, H_EM, H_FONT, H_I, H_NOBR, H_S,
		H_SMALL, H_STRIKE, H_STRONG, H_TT, H_U, 0 };
	static const uint8_t implied[] = { H_DD, H_DT, H_LI, H_OPTGROUP, H_OPTION, H_P, H_RB, H_RP,
		H_RT, H_RTC, 0 };
	static const uint8_t thorough[] = { H_CAPTION, H_COLGROUP, H_TBODY, H_TD, H_TFOOT, H_TH,
		H_THEAD, H_TR, 0 };
	static const uint8_t scope[] = { H_APPLET, H_CAPTION, H_HTML, H_TABLE, H_TD, H_TH,
		H_MARQUEE, H_OBJECT, H_SELECT, H_TEMPLATE, 0 };
	static const uint8_t breakout[] = { H_B, H_BIG, H_BLOCKQUOTE, H_BODY, H_BR, H_CENTER,
		H_CODE, H_DD, H_DIV, H_DL, H_DT, H_EM, H_EMBED, H_H1, H_H2, H_H3, H_H4, H_H5, H_H6,
		H_HEAD, H_HR, H_I, H_IMG, H_LI, H_LISTING, H_MENU, H_META, H_NOBR, H_OL, H_P, H_PRE,
		H_RUBY, H_S, H_SMALL, H_SPAN, H_STRONG, H_STRIKE, H_SUB, H_SUP, H_TABLE, H_TT, H_U,
		H_UL, H_VAR, 0 };
	static const uint8_t listed[] = { H_BUTTON, H_FIELDSET, H_INPUT, H_OBJECT, H_OUTPUT,
		H_SELECT, H_TEXTAREA, 0 };
	static const uint8_t block[] = { H_ADDRESS, H_ARTICLE, H_ASIDE, H_BLOCKQUOTE, H_CENTER,
		H_DETAILS, H_DIALOG, H_DIR, H_DIV, H_DL, H_FIELDSET, H_FIGCAPTION, H_FIGURE,
		H_FOOTER, H_HEADER, H_HGROUP, H_MAIN, H_MENU, H_NAV, H_OL, H_P, H_SEARCH,
		H_SECTION, H_SUMMARY, H_UL, 0 };
	static const uint8_t blockend[] = { H_ADDRESS, H_ARTICLE, H_ASIDE, H_BLOCKQUOTE,
		H_BUTTON, H_CENTER, H_DETAILS, H_DIALOG, H_DIR, H_DIV, H_DL, H_FIELDSET,
		H_FIGCAPTION, H_FIGURE, H_FOOTER, H_HEADER, H_HGROUP, H_LISTING, H_MAIN, H_MENU,
		H_NAV, H_OL, H_PRE, H_SEARCH, H_SECTION, H_SELECT, H_SUMMARY, H_UL, 0 };
	static const struct { uint8_t h, t; } tmap[] = {
		{ H_HTML, T_HTML }, { H_HEAD, T_HEAD }, { H_BODY, T_BODY }, { H_TITLE, T_TITLE },
		{ H_BASE, T_BASE }, { H_LINK, T_LINK }, { H_META, T_META }, { H_STYLE, T_STYLE },
		{ H_SCRIPT, T_SCRIPT }, { H_NOSCRIPT, T_NOSCRIPT }, { H_A, T_A }, { H_IMG, T_IMG },
		{ H_BR, T_BR }, { H_HR, T_HR }, { H_LI, T_LI }, { H_UL, T_UL }, { H_OL, T_OL },
		{ H_TABLE, T_TABLE }, { H_TR, T_TR }, { H_TD, T_TD }, { H_TH, T_TH },
		{ H_INPUT, T_INPUT }, { H_BUTTON, T_BUTTON }, { H_SELECT, T_SELECT },
		{ H_OPTION, T_OPTION }, { H_TEXTAREA, T_TEXTAREA }, { H_PRE, T_PRE },
		{ H_IFRAME, T_IFRAME }, { H_SVG, T_SVG }, { H_CANVAS, T_CANVAS },
		{ H_VIDEO, T_VIDEO }, { H_AUDIO, T_AUDIO }, { H_OBJECT, T_OBJECT },
		{ H_EMBED, T_EMBED }, { H_TEMPLATE, T_TEMPLATE }, { H_LABEL, T_LABEL },
		{ H_FORM, T_FORM }, { H_WBR, T_WBR }, { H_FIELDSET, T_FIELDSET },
		{ H_LEGEND, T_LEGEND }, { H_OPTGROUP, T_OPTGROUP } };
	const uint8_t *l;
	unsigned i;

	for (l = special; *l; l++)
		hflags[*l] |= F_SPECIAL;
	for (l = format; *l; l++)
		hflags[*l] |= F_FORMAT;
	for (l = implied; *l; l++)
		hflags[*l] |= F_IMPLIED | F_THOROUGH;
	for (l = thorough; *l; l++)
		hflags[*l] |= F_THOROUGH;
	for (l = scope; *l; l++)
		hflags[*l] |= F_SCOPE;
	for (l = breakout; *l; l++)
		hflags[*l] |= F_BREAKOUT;
	for (l = listed; *l; l++)
		hflags[*l] |= F_LISTED | F_ASSOC;
	hflags[H_IMG] |= F_ASSOC;
	for (i = H_H1; i <= H_H6; i++)
		hflags[i] |= F_HEADING;
	for (l = block; *l; l++)
		hflags[*l] |= F_BLOCK;
	for (l = blockend; *l; l++)
		hflags[*l] |= F_BLOCKEND;
	for (i = 0; i < sizeof tmap / sizeof tmap[0]; i++)
		hT[tmap[i].h] = tmap[i].t;
	for (i = 1; i < H_N; i++) {
		const uint8_t *s = (const uint8_t *) hname[i];
		unsigned k = hhash(s, strlen(hname[i]));
		hlen[i] = (uint8_t) strlen(hname[i]);
		while (htab[k])
			k = (k + 1) & 511;
		htab[k] = (uint8_t) i;
	}
	ready = 1;
}

/* the parser's name for a tag name (lower case), H_NONE if it has none */
static int hlookup(const uint8_t *s, size_t n)
{
	unsigned k;
	if (n == 0 || n > 14)
		return H_NONE;
	for (k = hhash(s, n); htab[k]; k = (k + 1) & 511) {
		const char *h = hname[htab[k]];
		if (hlen[htab[k]] == n && h[0] == (char) s[0]) {
			size_t i = 1;
			while (i < n && h[i] == (char) s[i])
				i++;
			if (i == n)
				return htab[k];
		}
	}
	return H_NONE;
}

/* SVG's element names in camel case (13.2.6.5) */
static const char *const svg_names[] = {
	"altGlyph", "altGlyphDef", "altGlyphItem", "animateColor", "animateMotion",
	"animateTransform", "clipPath", "feBlend", "feColorMatrix", "feComponentTransfer",
	"feComposite", "feConvolveMatrix", "feDiffuseLighting", "feDisplacementMap",
	"feDistantLight", "feDropShadow", "feFlood", "feFuncA", "feFuncB", "feFuncG", "feFuncR",
	"feGaussianBlur", "feImage", "feMerge", "feMergeNode", "feMorphology", "feOffset",
	"fePointLight", "feSpecularLighting", "feSpotLight", "feTile", "feTurbulence",
	"foreignObject", "glyphRef", "linearGradient", "radialGradient", "textPath", NULL };

/* SVG's attribute names in camel case (13.2.6.1) */
static const char *const svg_attrs[] = {
	"attributeName", "attributeType", "baseFrequency", "baseProfile", "calcMode",
	"clipPathUnits", "diffuseConstant", "edgeMode", "filterUnits", "glyphRef",
	"gradientTransform", "gradientUnits", "kernelMatrix", "kernelUnitLength", "keyPoints",
	"keySplines", "keyTimes", "lengthAdjust", "limitingConeAngle", "markerHeight",
	"markerUnits", "markerWidth", "maskContentUnits", "maskUnits", "numOctaves",
	"pathLength", "patternContentUnits", "patternTransform", "patternUnits", "pointsAtX",
	"pointsAtY", "pointsAtZ", "preserveAlpha", "preserveAspectRatio", "primitiveUnits",
	"refX", "refY", "repeatCount", "repeatDur", "requiredExtensions", "requiredFeatures",
	"specularConstant", "specularExponent", "spreadMethod", "startOffset", "stdDeviation",
	"stitchTiles", "surfaceScale", "systemLanguage", "tableValues", "targetX", "targetY",
	"textLength", "viewBox", "viewTarget", "xChannelSelector", "yChannelSelector",
	"zoomAndPan", NULL };

static int ieq(const char *a, const uint8_t *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		uint8_t c = b[i];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (a[i] == 0 || (uint8_t) (a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i]) != c)
			return 0;
	}
	return a[n] == 0;
}

static const char *camel(const char *const *list, const uint8_t *s, size_t n)
{
	for (; *list; list++)
		if (ieq(*list, s, n))
			return *list;
	return NULL;
}

const char *html_foreign_attr(int ns, const char *name, const char **prefix)
{
	static const char *const foreign[][2] = {
		{ "xlink:actuate", "xlink" }, { "xlink:arcrole", "xlink" }, { "xlink:href", "xlink" },
		{ "xlink:role", "xlink" }, { "xlink:show", "xlink" }, { "xlink:title", "xlink" },
		{ "xlink:type", "xlink" }, { "xml:lang", "xml" }, { "xml:space", "xml" },
		{ "xmlns", "xmlns" }, { "xmlns:xlink", "xmlns" } };
	size_t i, n = strlen(name);
	const char *c;

	*prefix = NULL;
	for (i = 0; i < sizeof foreign / sizeof foreign[0]; i++)
		if (strcmp(name, foreign[i][0]) == 0) {
			const char *colon = strchr(name, ':');
			*prefix = foreign[i][1];
			return colon ? colon + 1 : name;
		}
	if (ns == DN_NS_SVG && (c = camel(svg_attrs, (const uint8_t *) name, n)) != NULL)
		return c;
	if (ns == DN_NS_MATHML && strcmp(name, "definitionurl") == 0)
		return "definitionURL";
	return name;
}

/* ---------------------------------------------------------------- the parser's state */

enum {
	/* tokenizer states (13.2.5) */
	S_DATA, S_RCDATA, S_RAWTEXT, S_SCRIPT, S_PLAINTEXT, S_TAG_OPEN, S_END_TAG_OPEN,
	S_TAG_NAME, S_RCDATA_LT, S_RAWTEXT_LT, S_SCRIPT_LT, S_ESC_START, S_ESC_START_DASH,
	S_ESC, S_ESC_DASH, S_ESC_DASH_DASH, S_ESC_LT, S_DESC_START, S_DESC, S_DESC_DASH,
	S_DESC_DASH_DASH, S_DESC_LT, S_DESC_END, S_BEFORE_ATTR_NAME, S_ATTR_NAME,
	S_AFTER_ATTR_NAME, S_BEFORE_ATTR_VALUE, S_AV_DQ, S_AV_SQ, S_AV_UQ, S_AFTER_AV,
	S_SELF_CLOSING, S_BOGUS_COMMENT, S_MARKUP_DECL, S_COMMENT_START, S_COMMENT_START_DASH,
	S_COMMENT, S_COMMENT_LT, S_COMMENT_LT_BANG, S_COMMENT_LT_BANG_DASH,
	S_COMMENT_LT_BANG_DASH_DASH, S_COMMENT_END_DASH, S_COMMENT_END, S_COMMENT_END_BANG,
	S_DOCTYPE, S_BEFORE_DT_NAME, S_DT_NAME, S_AFTER_DT_NAME, S_AFTER_DT_PUBLIC,
	S_BEFORE_DT_PUBLIC, S_DT_PUBLIC_DQ, S_DT_PUBLIC_SQ, S_AFTER_DT_PUBLIC_ID,
	S_BETWEEN_DT_IDS, S_AFTER_DT_SYSTEM, S_BEFORE_DT_SYSTEM, S_DT_SYSTEM_DQ,
	S_DT_SYSTEM_SQ, S_AFTER_DT_SYSTEM_ID, S_BOGUS_DOCTYPE, S_CDATA, S_CDATA_BRACKET,
	S_CDATA_END, S_PI_OPEN, S_PI_TARGET, S_AFTER_PI_TARGET, S_PI_DATA, S_PI_Q
};

enum { TK_DOCTYPE, TK_START, TK_END, TK_COMMENT, TK_CHARS, TK_PI, TK_EOF };

/* insertion modes (13.2.4.1) */
enum {
	M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_IN_HEAD_NOSCRIPT, M_AFTER_HEAD,
	M_IN_BODY, M_TEXT, M_IN_TABLE, M_IN_TABLE_TEXT, M_IN_CAPTION, M_IN_COLUMN_GROUP,
	M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL, M_IN_TEMPLATE, M_AFTER_BODY, M_IN_FRAMESET,
	M_AFTER_FRAMESET, M_AFTER_AFTER_BODY, M_AFTER_AFTER_FRAMESET
};

/* dnode.pf */
#define PF_OPEN		1	/* on the stack of open elements */
#define PF_AFE		2	/* in the list of active formatting elements */
#define PF_HIP		4	/* annotation-xml, an HTML integration point */
#define PF_NO_PUBLIC	8	/* a DOCTYPE's public identifier is missing */
#define PF_NO_SYSTEM	16	/* ... its system identifier */

typedef struct { uint32_t no, nl, vo, vl; uint8_t dup; } RATTR;	/* in sb */
typedef struct { const uint8_t *n, *v; uint32_t nl, vl; } TATTR;

typedef struct {
	int type;
	const uint8_t *s;		/* text, comment, tag name, DOCTYPE name, PI target */
	size_t n;
	int hid;			/* a tag's H_* */
	uint8_t self_closing, nul;	/* chars: a lone U+0000 */
	TATTR *a;
	uint32_t na;
	const uint8_t *pub, *sys, *d;	/* DOCTYPE ids; PI data */
	size_t npub, nsys, nd;
	uint8_t has_name, has_pub, has_sys, quirks;
	const ec_atom *atom;		/* a tag's name, once wanted */
} TOKEN;

typedef struct P {
	doc *d;
	void *jmp[5];			/* __builtin_setjmp's, for running out of memory */

	/* the tokenizer */
	const uint8_t *s, *e;		/* the page (UTF-8, *e == 0) and where it is */
	int state;
	uint8_t *sb;			/* a token's names, values and text */
	size_t sbn, sbcap;
	RATTR *ra;
	uint32_t nra, racap;
	TATTR *ta;
	uint32_t tacap;
	int is_end;
	uint8_t self_closing;
	uint32_t name_l;		/* a tag name: sb[0..name_l) */
	uint32_t dt_l, dp_o, dp_l, ds_o, ds_l;	/* DOCTYPE fields in sb */
	uint8_t dt_name, dt_pub, dt_sys, dt_quirks;
	uint32_t pi_tl;			/* a PI's target: sb[0..pi_tl), its data after */
	char last[16];			/* the last start tag's name ("" none) */
	size_t nlast;
	uint8_t cref[12];		/* a character reference's text */
	TOKEN t;

	/* the tree builder */
	dnode *root;
	dnode **open;
	int nopen, capopen, ntemplates;
	dnode **afe;			/* NULL: a marker */
	int nafe, capafe;
	uint8_t *tmodes;
	int ntm, captm;
	dnode *head, *form, *ctx;
	int mode, orig;
	uint8_t frameset_ok, foster, scripting, skip_lf, quirks, tentative, stop;
	uint8_t *ptc;			/* pending table character tokens */
	size_t nptc, captc;
	int ptc_text;			/* ... some not white space */
	const ec_atom *atoms[H_N];
	const ec_atom *a_class, *a_id;
	uint16_t enc;			/* the encoding (MIB enum) */
	uint16_t changed;		/* <meta charset> said this one: start again */
} P;

static void oom(P *p)
{
	__builtin_longjmp(p->jmp, 1);
}

static void *dalloc(P *p, size_t n)
{
	void *m = doc_alloc(p->d, n);
	if (m == NULL)
		oom(p);
	return m;
}

static void *grow(P *p, void *a, size_t n, size_t *cap, size_t need, size_t size)
{
	void *m;
	size_t c;
	(void) n;
	if (need <= *cap)
		return a;
	c = *cap ? *cap : 16;
	while (c < need)
		c *= 2;
	m = realloc(a, c * size);
	if (m == NULL)
		oom(p);
	*cap = c;
	return m;
}

#define GROW(p, arr, n, cap, need) do { size_t c_ = (size_t) (cap); \
	(arr) = grow((p), (arr), (n), &c_, (need), sizeof *(arr)); (cap) = c_; } while (0)

/* ---------------------------------------------------------------- scratch text */

static void sb_put(P *p, const uint8_t *s, size_t n)
{
	if (p->sbn + n > p->sbcap)
		GROW(p, p->sb, p->sbn, p->sbcap, p->sbn + n + 64);
	memcpy(p->sb + p->sbn, s, n);
	p->sbn += n;
}

static void sb_c(P *p, uint8_t c)
{
	if (p->sbn + 1 > p->sbcap)
		GROW(p, p->sb, p->sbn, p->sbcap, p->sbn + 64);
	p->sb[p->sbn++] = c;
}

static size_t utf8_put(uint8_t *o, uint32_t c)
{
	if (c < 0x80) {
		o[0] = (uint8_t) c;
		return 1;
	}
	if (c < 0x800) {
		o[0] = (uint8_t) (0xc0 | c >> 6);
		o[1] = (uint8_t) (0x80 | (c & 0x3f));
		return 2;
	}
	if (c < 0x10000) {
		o[0] = (uint8_t) (0xe0 | c >> 12);
		o[1] = (uint8_t) (0x80 | (c >> 6 & 0x3f));
		o[2] = (uint8_t) (0x80 | (c & 0x3f));
		return 3;
	}
	o[0] = (uint8_t) (0xf0 | c >> 18);
	o[1] = (uint8_t) (0x80 | (c >> 12 & 0x3f));
	o[2] = (uint8_t) (0x80 | (c >> 6 & 0x3f));
	o[3] = (uint8_t) (0x80 | (c & 0x3f));
	return 4;
}

static const uint8_t FFFD[3] = { 0xef, 0xbf, 0xbd };

static void sb_fffd(P *p)
{
	sb_put(p, FFFD, 3);
}

/* ---------------------------------------------------------------- the tokenizer */

#define WS(c) ((c) == ' ' || (c) == '\n' || (c) == '\t' || (c) == '\f')
#define ALPHA(c) ((uint8_t) (((c) | 0x20) - 'a') < 26)
#define DIGIT(c) ((uint8_t) ((c) - '0') < 10)
#define ALNUM(c) (ALPHA(c) || DIGIT(c))

/* the bytes each run stops at, a table each so that a loop is a load and
   a test (the page ends in a 0, which stops them all) */
enum { R_DATA, R_RCDATA, R_RAW, R_TAGNAME, R_ATTRNAME, R_DQ, R_SQ, R_UQ, R_COMMENT, R_ESC,
       R_BOGUS, R_PI, R_N };
static uint8_t rstop[R_N][256];

static void runs_init(void)
{
	static const char *const stops[R_N] = { "<&", "<&", "<", "\t\n\f />ABCDEFGHIJKLMNOPQRSTUVWXYZ",
		"\t\n\f />=ABCDEFGHIJKLMNOPQRSTUVWXYZ", "\"&", "'&", "\t\n\f >&", "<-", "<-", ">", ">?" };
	int r;
	for (r = 0; r < R_N; r++) {
		const char *c;
		rstop[r][0] = 1;
		for (c = stops[r]; *c; c++)
			rstop[r][(uint8_t) *c] = 1;
	}
}

static int adjusted_foreign(P *p);

static int tok_chars(P *p, const uint8_t *s, size_t n, int nul)
{
	p->t.type = TK_CHARS;
	p->t.s = s;
	p->t.n = n;
	p->t.nul = (uint8_t) nul;
	return TK_CHARS;
}

static void begin_tag(P *p, int end)
{
	p->sbn = 0;
	p->nra = 0;
	p->is_end = end;
	p->self_closing = 0;
}

static void new_attr(P *p)
{
	RATTR *a;
	if (p->nra >= p->racap)
		GROW(p, p->ra, p->nra, p->racap, p->nra + 1);
	a = &p->ra[p->nra++];
	a->no = (uint32_t) p->sbn;
	a->nl = a->vo = a->vl = 0;
	a->dup = 0;
}

static void end_attr_name(P *p)
{
	RATTR *a = &p->ra[p->nra - 1];
	uint32_t i;
	a->nl = (uint32_t) p->sbn - a->no;
	a->vo = (uint32_t) p->sbn;
	for (i = 0; i + 1 < p->nra; i++)
		if (!p->ra[i].dup && p->ra[i].nl == a->nl &&
		    memcmp(p->sb + p->ra[i].no, p->sb + a->no, a->nl) == 0) {
			a->dup = 1;
			break;
		}
}

static void end_attr_value(P *p)
{
	RATTR *a = &p->ra[p->nra - 1];
	a->vl = (uint32_t) p->sbn - a->vo;
}

static int emit_tag(P *p)
{
	TOKEN *t = &p->t;
	uint32_t i, k = 0;

	t->type = p->is_end ? TK_END : TK_START;
	t->s = p->sb;
	t->n = p->name_l;
	t->hid = hlookup(p->sb, p->name_l);
	t->self_closing = p->self_closing;
	t->atom = NULL;
	t->na = 0;
	if (!p->is_end) {
		if (p->nra > p->tacap)
			GROW(p, p->ta, 0, p->tacap, p->nra);
		for (i = 0; i < p->nra; i++)
			if (!p->ra[i].dup) {
				p->ta[k].n = p->sb + p->ra[i].no;
				p->ta[k].nl = p->ra[i].nl;
				p->ta[k].v = p->sb + p->ra[i].vo;
				p->ta[k].vl = p->ra[i].vl;
				k++;
			}
		t->na = k;
		if (p->name_l < sizeof p->last) {
			memcpy(p->last, p->sb, p->name_l);
			p->nlast = p->name_l;
		} else {
			p->nlast = 0;
		}
	}
	t->a = p->ta;
	return t->type;
}

static int emit_comment(P *p)
{
	p->t.type = TK_COMMENT;
	p->t.s = p->sb;
	p->t.n = p->sbn;
	return TK_COMMENT;
}

static void begin_doctype(P *p)
{
	p->sbn = 0;
	p->dt_l = p->dp_o = p->dp_l = p->ds_o = p->ds_l = 0;
	p->dt_name = p->dt_pub = p->dt_sys = p->dt_quirks = 0;
}

static int emit_doctype(P *p)
{
	TOKEN *t = &p->t;
	t->type = TK_DOCTYPE;
	t->s = p->sb;
	t->n = p->dt_l;
	t->pub = p->sb + p->dp_o;
	t->npub = p->dp_l;
	t->sys = p->sb + p->ds_o;
	t->nsys = p->ds_l;
	t->has_name = p->dt_name;
	t->has_pub = p->dt_pub;
	t->has_sys = p->dt_sys;
	t->quirks = p->dt_quirks;
	return TK_DOCTYPE;
}

static int emit_eof(P *p)
{
	p->t.type = TK_EOF;
	return TK_EOF;
}

/* "<?" turned out not to start a processing instruction: a comment of
   "?" and what was taken for its target */
static void pi_to_comment(P *p)
{
	sb_c(p, 0);
	memmove(p->sb + 1, p->sb, p->sbn - 1);
	p->sb[0] = '?';
}

/* A character reference (13.2.5.72-80) after the '&' at p->s: 1 if it was
   one, its text appended to the attribute value in sb (attr) or set as
   the token (p->cref); 0 if not, and the '&' is only an '&'. */
static int charref(P *p, int attr)
{
	const uint8_t *s = p->s, *e = p->e;
	uint32_t cp[2];
	int ncp;

	if (s < e && *s == '#') {
		const uint8_t *q = s + 1, *d0;
		uint32_t code = 0;
		int hex = 0;
		if (q < e && (*q | 0x20) == 'x') {
			hex = 1;
			q++;
		}
		d0 = q;
		for (;; q++) {
			uint32_t v;
			uint8_t c = *q;
			if (DIGIT(c))
				v = c - '0';
			else if (hex && (uint8_t) ((c | 0x20) - 'a') < 6)
				v = (uint32_t) ((c | 0x20) - 'a' + 10);
			else
				break;
			code = code * (hex ? 16 : 10) + v;
			if (code > 0x10ffff)
				code = 0x110000;
		}
		if (q == d0)
			return 0;
		if (*q == ';')
			q++;
		if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
			code = 0xfffd;
		} else if (code >= 0x80 && code <= 0x9f) {
			static const uint16_t c1[32] = {
				0x20ac, 0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
				0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d, 0x017d, 0x8f,
				0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
				0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d, 0x017e, 0x0178 };
			code = c1[code - 0x80];
		}
		cp[0] = code;
		ncp = 1;
		p->s = q;
	} else {
		int lo = 0, hi = ENT_N, best = -1, k;
		size_t bestk = 0;
		for (k = 0; k < ENT_LONGEST && s + k < e; k++) {
			uint8_t c = s[k];
			int a, b;
			if (!ALNUM(c) && c != ';')
				break;
			/* names of length k are first in [lo, hi) and have no byte k */
			a = lo;
			b = hi;
			while (a < b) {
				int m = (a + b) / 2;
				if (ent[m].len > k && (uint8_t) ent_names[ent[m].off + k] >= c)
					b = m;
				else
					a = m + 1;
			}
			lo = a;
			a = lo;
			b = hi;
			while (a < b) {
				int m = (a + b) / 2;
				if (ent[m].len > k && (uint8_t) ent_names[ent[m].off + k] > c)
					b = m;
				else
					a = m + 1;
			}
			hi = a;
			if (lo >= hi)
				break;
			if (ent[lo].len == k + 1) {
				best = lo;
				bestk = (size_t) k + 1;
			}
			if (c == ';')
				break;
		}
		if (best < 0)
			return 0;
		if (attr && s[bestk - 1] != ';' && (s[bestk] == '=' || ALNUM(s[bestk])))
			return 0;
		cp[0] = ent[best].cp;
		cp[1] = ent[best].cp2;
		ncp = cp[1] ? 2 : 1;
		p->s = s + bestk;
	}
	if (attr) {
		uint8_t u[8];
		size_t n = utf8_put(u, cp[0]);
		if (ncp > 1)
			n += utf8_put(u + n, cp[1]);
		sb_put(p, u, n);
	} else {
		size_t n = utf8_put(p->cref, cp[0]);
		if (ncp > 1)
			n += utf8_put(p->cref + n, cp[1]);
		tok_chars(p, p->cref, n, 0);
	}
	return 1;
}

/* After "</" in RCDATA, RAWTEXT or script data (escaped or not): an
   appropriate end tag starts here, or "</" and its letters are text. *sp
   is after the '/'. Returns -1 when the tag has begun (the state is set),
   else the length of the text from the '<' (the state is back). */
static long end_tag(P *p, const uint8_t *s0, const uint8_t **sp, int text_state)
{
	const uint8_t *s = s0, *x = s;
	while (ALPHA(*x))
		x++;
	if ((size_t) (x - s) == p->nlast && p->nlast > 0 && (WS(*x) || *x == '/' || *x == '>')) {
		size_t i;
		for (i = 0; i < p->nlast; i++)
			if ((s[i] | 0x20) != (uint8_t) p->last[i])
				break;
		if (i == p->nlast) {
			begin_tag(p, 1);
			for (i = 0; i < p->nlast; i++)
				sb_c(p, (uint8_t) (s[i] | 0x20));
			p->name_l = (uint32_t) p->nlast;
			p->state = WS(*x) ? S_BEFORE_ATTR_NAME : *x == '/' ? S_SELF_CLOSING : S_DATA;
			*sp = x + 1;
			return -1;
		}
	}
	p->state = text_state;
	*sp = x;
	return (long) (x - s) + 2;
}

/* the next token, in p->t (a function of its own, so that its loops
   keep their pointers in registers) */
static __attribute__((noinline)) int tok(P *p)
{
	const uint8_t *s = p->s, *e = p->e, *r;
	uint8_t c;

#define RET(x) do { p->s = s; return (x); } while (0)
#define TEXT(ptr, n) do { p->s = s; return tok_chars(p, (ptr), (n), 0); } while (0)

	for (;;) switch (p->state) {
	case S_DATA:
		r = s;
		while (!rstop[R_DATA][*s])
			s++;
		if (s > r)
			TEXT(r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == '<') {
			p->state = S_TAG_OPEN;
		} else if (c == '&') {
			p->s = s;
			if (charref(p, 0))
				return TK_CHARS;
			TEXT(s - 1, 1);
		} else {
			p->s = s;
			return tok_chars(p, s - 1, 1, 1);
		}
		break;
	case S_RCDATA:
	case S_RAWTEXT:
	case S_SCRIPT:
	case S_PLAINTEXT: {
		int st = p->state;
		const uint8_t *stop = rstop[st == S_RCDATA ? R_RCDATA : R_RAW];
		r = s;
		if (st != S_PLAINTEXT)
			while (!stop[*s])
				s++;
		else
			while (*s)
				s++;
		if (s > r)
			TEXT(r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == 0)
			TEXT(FFFD, 3);
		if (c == '&') {
			p->s = s;
			if (charref(p, 0))
				return TK_CHARS;
			TEXT(s - 1, 1);
		}
		p->state = st == S_RCDATA ? S_RCDATA_LT : st == S_RAWTEXT ? S_RAWTEXT_LT : S_SCRIPT_LT;
		break;
	}
	case S_TAG_OPEN:
		c = *s;
		if (c == '!') {
			s++;
			p->state = S_MARKUP_DECL;
		} else if (c == '/') {
			s++;
			p->state = S_END_TAG_OPEN;
		} else if (ALPHA(c)) {
			begin_tag(p, 0);
			p->state = S_TAG_NAME;
		} else if (c == '?' && s < e) {
			s++;
			p->sbn = 0;
			p->state = S_PI_OPEN;
		} else {
			p->state = S_DATA;
			TEXT(s - 1, 1);
		}
		break;
	case S_END_TAG_OPEN:
		c = *s;
		if (ALPHA(c)) {
			begin_tag(p, 1);
			p->state = S_TAG_NAME;
		} else if (c == '>') {
			s++;
			p->state = S_DATA;
		} else if (s >= e) {
			p->state = S_DATA;
			TEXT(s - 2, 2);
		} else {
			p->sbn = 0;
			p->state = S_BOGUS_COMMENT;
		}
		break;
	case S_TAG_NAME:
		r = s;
		while (!rstop[R_TAGNAME][*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		c = *s;
		if (s >= e)
			RET(emit_eof(p));
		s++;
		if (c >= 'A' && c <= 'Z') {
			sb_c(p, (uint8_t) (c + 32));
			break;
		}
		if (c == 0) {
			sb_fffd(p);
			break;
		}
		p->name_l = (uint32_t) p->sbn;
		if (c == '>') {
			p->state = S_DATA;
			RET(emit_tag(p));
		}
		p->state = c == '/' ? S_SELF_CLOSING : S_BEFORE_ATTR_NAME;
		break;
	case S_RCDATA_LT:
	case S_RAWTEXT_LT: {
		int back = p->state == S_RCDATA_LT ? S_RCDATA : S_RAWTEXT;
		if (*s == '/') {
			long n;
			const uint8_t *x;
			n = end_tag(p, s + 1, &x, back);
			s = x;
			if (n < 0) {
				if (p->state == S_DATA)
					RET(emit_tag(p));
				break;
			}
			TEXT(s - n, (size_t) n);
		}
		p->state = back;
		TEXT(s - 1, 1);
	}
	case S_SCRIPT_LT:
		if (*s == '/') {
			long n;
			const uint8_t *x;
			n = end_tag(p, s + 1, &x, S_SCRIPT);
			s = x;
			if (n < 0) {
				if (p->state == S_DATA)
					RET(emit_tag(p));
				break;
			}
			TEXT(s - n, (size_t) n);
		}
		if (*s == '!') {
			s++;
			p->state = S_ESC_START;
			TEXT(s - 2, 2);
		}
		p->state = S_SCRIPT;
		TEXT(s - 1, 1);
	case S_ESC_START:
	case S_ESC_START_DASH:
		if (*s == '-') {
			p->state = p->state == S_ESC_START ? S_ESC_START_DASH : S_ESC_DASH_DASH;
			s++;
			TEXT(s - 1, 1);
		}
		p->state = S_SCRIPT;
		break;
	case S_ESC:
	case S_DESC:
		r = s;
		while (!rstop[R_ESC][*s])
			s++;
		if (s > r)
			TEXT(r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == 0)
			TEXT(FFFD, 3);
		if (c == '-') {
			p->state = p->state == S_ESC ? S_ESC_DASH : S_DESC_DASH;
			TEXT(s - 1, 1);
		}
		if (p->state == S_ESC) {
			p->state = S_ESC_LT;
			break;
		}
		p->state = S_DESC_LT;
		TEXT(s - 1, 1);
	case S_ESC_DASH:
	case S_ESC_DASH_DASH:
	case S_DESC_DASH:
	case S_DESC_DASH_DASH: {
		int esc = p->state == S_ESC_DASH || p->state == S_ESC_DASH_DASH;
		int dd = p->state == S_ESC_DASH_DASH || p->state == S_DESC_DASH_DASH;
		if (s >= e)
			RET(emit_eof(p));
		c = *s;
		if (c == '-') {
			s++;
			p->state = esc ? S_ESC_DASH_DASH : S_DESC_DASH_DASH;
			TEXT(s - 1, 1);
		}
		if (c == '<') {
			s++;
			if (esc) {
				p->state = S_ESC_LT;
				break;
			}
			p->state = S_DESC_LT;
			TEXT(s - 1, 1);
		}
		if (c == '>' && dd) {
			s++;
			p->state = S_SCRIPT;
			TEXT(s - 1, 1);
		}
		if (c == 0) {
			s++;
			p->state = esc ? S_ESC : S_DESC;
			TEXT(FFFD, 3);
		}
		p->state = esc ? S_ESC : S_DESC;
		break;
	}
	case S_ESC_LT:
		if (*s == '/') {
			long n;
			const uint8_t *x;
			n = end_tag(p, s + 1, &x, S_ESC);
			s = x;
			if (n < 0) {
				if (p->state == S_DATA)
					RET(emit_tag(p));
				break;
			}
			TEXT(s - n, (size_t) n);
		}
		p->state = ALPHA(*s) ? S_DESC_START : S_ESC;
		TEXT(s - 1, 1);
	case S_DESC_START:
	case S_DESC_END: {
		/* letters, then "script" and a space, '/' or '>' toggles the
		   double escape */
		const uint8_t *x = s;
		int script;
		while (ALPHA(*x))
			x++;
		script = x - s == 6 && ieq("script", s, 6);
		if (WS(*x) || *x == '/' || *x == '>')
			p->state = (p->state == S_DESC_START) == script ? S_DESC : S_ESC;
		else
			p->state = p->state == S_DESC_START ? S_ESC : S_DESC;
		r = s;
		s = x;
		if (s > r)
			TEXT(r, (size_t) (s - r));
		break;
	}
	case S_DESC_LT:
		if (*s == '/') {
			s++;
			p->state = S_DESC_END;
			TEXT(s - 1, 1);
		}
		p->state = S_DESC;
		break;
	case S_BEFORE_ATTR_NAME:
		while (WS(*s))
			s++;
		c = *s;
		if (c == '/' || c == '>' || s >= e) {
			p->state = S_AFTER_ATTR_NAME;
			break;
		}
		new_attr(p);
		if (c == '=') {
			sb_c(p, '=');
			s++;
		}
		p->state = S_ATTR_NAME;
		break;
	case S_ATTR_NAME:
		r = s;
		while (!rstop[R_ATTRNAME][*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		c = *s;
		if (c >= 'A' && c <= 'Z') {
			sb_c(p, (uint8_t) (c + 32));
			s++;
			break;
		}
		if (c == 0 && s < e) {
			sb_fffd(p);
			s++;
			break;
		}
		end_attr_name(p);
		if (c == '=') {
			s++;
			p->state = S_BEFORE_ATTR_VALUE;
		} else {
			p->state = S_AFTER_ATTR_NAME;
		}
		break;
	case S_AFTER_ATTR_NAME:
		while (WS(*s))
			s++;
		if (s >= e)
			RET(emit_eof(p));
		c = *s;
		if (c == '/') {
			s++;
			p->state = S_SELF_CLOSING;
		} else if (c == '=') {
			s++;
			p->state = S_BEFORE_ATTR_VALUE;
		} else if (c == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_tag(p));
		} else {
			new_attr(p);
			p->state = S_ATTR_NAME;
		}
		break;
	case S_BEFORE_ATTR_VALUE:
		while (WS(*s))
			s++;
		c = *s;
		p->ra[p->nra - 1].vo = (uint32_t) p->sbn;
		if (c == '"') {
			s++;
			p->state = S_AV_DQ;
		} else if (c == '\'') {
			s++;
			p->state = S_AV_SQ;
		} else if (c == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_tag(p));
		} else {
			p->state = S_AV_UQ;
		}
		break;
	case S_AV_DQ:
	case S_AV_SQ:
	case S_AV_UQ: {
		int st = p->state;
		const uint8_t *stop = rstop[st == S_AV_DQ ? R_DQ : st == S_AV_SQ ? R_SQ : R_UQ];
		r = s;
		while (!stop[*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == '&') {
			p->s = s;
			if (charref(p, 1))
				s = p->s;
			else
				sb_c(p, '&');
		} else if (c == 0) {
			sb_fffd(p);
		} else if (c == '>') {
			end_attr_value(p);
			p->state = S_DATA;
			RET(emit_tag(p));
		} else if (st == S_AV_UQ) {
			end_attr_value(p);
			p->state = S_BEFORE_ATTR_NAME;
		} else {
			end_attr_value(p);
			p->state = S_AFTER_AV;
		}
		break;
	}
	case S_AFTER_AV:
		if (s >= e)
			RET(emit_eof(p));
		c = *s;
		if (WS(c)) {
			s++;
			p->state = S_BEFORE_ATTR_NAME;
		} else if (c == '/') {
			s++;
			p->state = S_SELF_CLOSING;
		} else if (c == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_tag(p));
		} else {
			p->state = S_BEFORE_ATTR_NAME;
		}
		break;
	case S_SELF_CLOSING:
		if (s >= e)
			RET(emit_eof(p));
		if (*s == '>') {
			s++;
			p->self_closing = 1;
			p->state = S_DATA;
			RET(emit_tag(p));
		}
		p->state = S_BEFORE_ATTR_NAME;
		break;
	case S_BOGUS_COMMENT:
		r = s;
		while (!rstop[R_BOGUS][*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		if (*s++ == '>') {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		sb_fffd(p);
		break;
	case S_MARKUP_DECL:
		if (s[0] == '-' && s[1] == '-') {
			s += 2;
			p->sbn = 0;
			p->state = S_COMMENT_START;
		} else if (e - s >= 7 && ieq("doctype", s, 7)) {
			s += 7;
			p->state = S_DOCTYPE;
		} else if (e - s >= 7 && memcmp(s, "[CDATA[", 7) == 0) {
			s += 7;
			if (adjusted_foreign(p)) {
				p->state = S_CDATA;
			} else {
				p->sbn = 0;
				sb_put(p, (const uint8_t *) "[CDATA[", 7);
				p->state = S_BOGUS_COMMENT;
			}
		} else {
			p->sbn = 0;
			p->state = S_BOGUS_COMMENT;
		}
		break;
	case S_COMMENT_START:
		if (*s == '-') {
			s++;
			p->state = S_COMMENT_START_DASH;
		} else if (*s == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_comment(p));
		} else {
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT_START_DASH:
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		if (*s == '-') {
			s++;
			p->state = S_COMMENT_END;
		} else if (*s == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_comment(p));
		} else {
			sb_c(p, '-');
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT:
		r = s;
		while (!rstop[R_COMMENT][*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		c = *s++;
		if (c == '<') {
			sb_c(p, '<');
			p->state = S_COMMENT_LT;
		} else if (c == '-') {
			p->state = S_COMMENT_END_DASH;
		} else {
			sb_fffd(p);
		}
		break;
	case S_COMMENT_LT:
		if (*s == '!') {
			s++;
			sb_c(p, '!');
			p->state = S_COMMENT_LT_BANG;
		} else if (*s == '<') {
			s++;
			sb_c(p, '<');
		} else {
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT_LT_BANG:
		if (*s == '-') {
			s++;
			p->state = S_COMMENT_LT_BANG_DASH;
		} else {
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT_LT_BANG_DASH:
		if (*s == '-') {
			s++;
			p->state = S_COMMENT_LT_BANG_DASH_DASH;
		} else {
			p->state = S_COMMENT_END_DASH;
		}
		break;
	case S_COMMENT_LT_BANG_DASH_DASH:
		p->state = S_COMMENT_END;
		break;
	case S_COMMENT_END_DASH:
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		if (*s == '-') {
			s++;
			p->state = S_COMMENT_END;
		} else {
			sb_c(p, '-');
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT_END:
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		c = *s;
		if (c == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_comment(p));
		} else if (c == '!') {
			s++;
			p->state = S_COMMENT_END_BANG;
		} else if (c == '-') {
			s++;
			sb_c(p, '-');
		} else {
			sb_put(p, (const uint8_t *) "--", 2);
			p->state = S_COMMENT;
		}
		break;
	case S_COMMENT_END_BANG:
		if (s >= e) {
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		c = *s;
		if (c == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_comment(p));
		}
		sb_put(p, (const uint8_t *) "--!", 3);
		if (c == '-') {
			s++;
			p->state = S_COMMENT_END_DASH;
		} else {
			p->state = S_COMMENT;
		}
		break;
	case S_DOCTYPE:
		begin_doctype(p);
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		if (WS(*s))
			s++;
		p->state = S_BEFORE_DT_NAME;
		break;
	case S_BEFORE_DT_NAME:
		while (WS(*s))
			s++;
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		c = *s++;
		if (c == '>') {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		p->dt_name = 1;
		if (c >= 'A' && c <= 'Z')
			sb_c(p, (uint8_t) (c + 32));
		else if (c == 0)
			sb_fffd(p);
		else
			sb_c(p, c);
		p->state = S_DT_NAME;
		break;
	case S_DT_NAME:
		if (s >= e) {
			p->dt_l = (uint32_t) p->sbn;
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		c = *s++;
		if (WS(c)) {
			p->dt_l = (uint32_t) p->sbn;
			p->state = S_AFTER_DT_NAME;
		} else if (c == '>') {
			p->dt_l = (uint32_t) p->sbn;
			p->state = S_DATA;
			RET(emit_doctype(p));
		} else if (c >= 'A' && c <= 'Z') {
			sb_c(p, (uint8_t) (c + 32));
		} else if (c == 0) {
			sb_fffd(p);
		} else {
			sb_c(p, c);
		}
		break;
	case S_AFTER_DT_NAME:
		while (WS(*s))
			s++;
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		if (*s == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		if (e - s >= 6 && ieq("public", s, 6)) {
			s += 6;
			p->state = S_AFTER_DT_PUBLIC;
		} else if (e - s >= 6 && ieq("system", s, 6)) {
			s += 6;
			p->state = S_AFTER_DT_SYSTEM;
		} else {
			p->dt_quirks = 1;
			p->state = S_BOGUS_DOCTYPE;
		}
		break;
	case S_AFTER_DT_PUBLIC:
	case S_BEFORE_DT_PUBLIC:
	case S_AFTER_DT_SYSTEM:
	case S_BEFORE_DT_SYSTEM: {
		int sys = p->state == S_AFTER_DT_SYSTEM || p->state == S_BEFORE_DT_SYSTEM;
		if (WS(*s)) {
			while (WS(*s))
				s++;
			p->state = sys ? S_BEFORE_DT_SYSTEM : S_BEFORE_DT_PUBLIC;
			break;
		}
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		c = *s++;
		if (c == '"' || c == '\'') {
			if (sys) {
				p->dt_sys = 1;
				p->ds_o = (uint32_t) p->sbn;
				p->state = c == '"' ? S_DT_SYSTEM_DQ : S_DT_SYSTEM_SQ;
			} else {
				p->dt_pub = 1;
				p->dp_o = (uint32_t) p->sbn;
				p->state = c == '"' ? S_DT_PUBLIC_DQ : S_DT_PUBLIC_SQ;
			}
		} else if (c == '>') {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		} else {
			s--;
			p->dt_quirks = 1;
			p->state = S_BOGUS_DOCTYPE;
		}
		break;
	}
	case S_DT_PUBLIC_DQ:
	case S_DT_PUBLIC_SQ:
	case S_DT_SYSTEM_DQ:
	case S_DT_SYSTEM_SQ: {
		int sys = p->state == S_DT_SYSTEM_DQ || p->state == S_DT_SYSTEM_SQ;
		uint8_t q = p->state == S_DT_PUBLIC_DQ || p->state == S_DT_SYSTEM_DQ ? '"' : '\'';
		uint32_t o = sys ? p->ds_o : p->dp_o, *l = sys ? &p->ds_l : &p->dp_l;
		if (s >= e) {
			*l = (uint32_t) p->sbn - o;
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		c = *s++;
		if (c == q) {
			*l = (uint32_t) p->sbn - o;
			p->state = sys ? S_AFTER_DT_SYSTEM_ID : S_AFTER_DT_PUBLIC_ID;
		} else if (c == '>') {
			*l = (uint32_t) p->sbn - o;
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		} else if (c == 0) {
			sb_fffd(p);
		} else {
			sb_c(p, c);
		}
		break;
	}
	case S_AFTER_DT_PUBLIC_ID:
	case S_BETWEEN_DT_IDS:
		if (WS(*s)) {
			while (WS(*s))
				s++;
			p->state = S_BETWEEN_DT_IDS;
			break;
		}
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		c = *s++;
		if (c == '>') {
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		if (c == '"' || c == '\'') {
			p->dt_sys = 1;
			p->ds_o = (uint32_t) p->sbn;
			p->state = c == '"' ? S_DT_SYSTEM_DQ : S_DT_SYSTEM_SQ;
		} else {
			s--;
			p->dt_quirks = 1;
			p->state = S_BOGUS_DOCTYPE;
		}
		break;
	case S_AFTER_DT_SYSTEM_ID:
		while (WS(*s))
			s++;
		if (s >= e) {
			p->dt_quirks = 1;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		if (*s == '>') {
			s++;
			p->state = S_DATA;
			RET(emit_doctype(p));
		}
		p->state = S_BOGUS_DOCTYPE;
		break;
	case S_BOGUS_DOCTYPE:
		while (s < e && *s != '>')
			s++;
		if (s < e)
			s++;
		p->state = S_DATA;
		RET(emit_doctype(p));
	case S_CDATA:
		r = s;
		while (*s && *s != ']')
			s++;
		if (s > r)
			TEXT(r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == 0) {
			p->s = s;
			return tok_chars(p, s - 1, 1, 1);
		}
		p->state = S_CDATA_BRACKET;
		break;
	case S_CDATA_BRACKET:
		if (*s == ']') {
			s++;
			p->state = S_CDATA_END;
			break;
		}
		p->state = S_CDATA;
		TEXT(s - 1, 1);
	case S_CDATA_END:
		if (*s == ']') {
			s++;
			TEXT(s - 3, 1);
		}
		if (*s == '>') {
			s++;
			p->state = S_DATA;
			break;
		}
		p->state = S_CDATA;
		TEXT(s - 2, 2);
	case S_PI_OPEN:
		if (s >= e)
			RET(emit_eof(p));
		if (ALPHA(*s) || *s == '_') {
			p->state = S_PI_TARGET;
		} else {
			pi_to_comment(p);
			p->state = S_BOGUS_COMMENT;
		}
		break;
	case S_PI_TARGET:
		while (ALNUM(*s) || *s == '-' || *s == '_')
			sb_c(p, *s++);
		if (s >= e)
			RET(emit_eof(p));
		c = *s;
		if (WS(c) || c == '?' || c == '>') {
			if ((p->sbn == 3 && ieq("xml", p->sb, 3)) ||
			    (p->sbn == 14 && ieq("xml-stylesheet", p->sb, 14))) {
				pi_to_comment(p);
				p->state = S_BOGUS_COMMENT;
			} else {
				p->pi_tl = (uint32_t) p->sbn;
				p->state = S_AFTER_PI_TARGET;
			}
		} else {
			pi_to_comment(p);
			p->state = S_BOGUS_COMMENT;
		}
		break;
	case S_AFTER_PI_TARGET:
		while (WS(*s))
			s++;
		p->state = S_PI_DATA;
		break;
	case S_PI_DATA:
		r = s;
		while (!rstop[R_PI][*s])
			s++;
		if (s > r)
			sb_put(p, r, (size_t) (s - r));
		if (s >= e)
			RET(emit_eof(p));
		c = *s++;
		if (c == '?') {
			p->state = S_PI_Q;
		} else if (c == '>') {
			p->state = S_DATA;
			p->t.type = TK_PI;
			p->t.s = p->sb;
			p->t.n = p->pi_tl;
			p->t.d = p->sb + p->pi_tl;
			p->t.nd = p->sbn - p->pi_tl;
			RET(TK_PI);
		} else {
			sb_c(p, 0);	/* a 0 in the data stays (HTML says nothing else) */
		}
		break;
	case S_PI_Q:
		if (s >= e)
			RET(emit_eof(p));
		if (*s == '>') {
			s++;
			p->state = S_DATA;
			p->t.type = TK_PI;
			p->t.s = p->sb;
			p->t.n = p->pi_tl;
			p->t.d = p->sb + p->pi_tl;
			p->t.nd = p->sbn - p->pi_tl;
			RET(TK_PI);
		}
		sb_c(p, '?');
		p->state = S_PI_DATA;
		break;
	}
#undef RET
#undef TEXT
}

/* ---------------------------------------------------------------- nodes */

static dnode *new_node(P *p, int type)
{
	dnode *n = dalloc(p, sizeof *n);
	memset(n, 0, sizeof *n);
	n->type = (uint8_t) type;
	n->y = -1;
	return n;
}

static void set_text(P *p, dnode *n, const uint8_t *s, size_t len)
{
	n->text = dalloc(p, len + 1);
	if (len)
		memcpy(n->text, s, len);
	n->text[len] = 0;
	n->len = n->cap = (uint32_t) len;
}

/* appends to a text node: its room doubles, in place when it was the last
   memory taken */
static void text_add(P *p, dnode *n, const uint8_t *s, size_t len)
{
	if (n->len + len > n->cap) {
		size_t cap = n->cap * 2;
		if (cap < n->len + len)
			cap = n->len + len;
		if (cap < 32)
			cap = 32;
		n->text = doc_grow(p->d, n->text, n->cap + 1, cap + 1);
		if (n->text == NULL)
			oom(p);
		n->cap = (uint32_t) cap;
	}
	memcpy(n->text + n->len, s, len);
	n->len += (uint32_t) len;
	n->text[n->len] = 0;
}

static const ec_atom *atom(P *p, const uint8_t *s, size_t n)
{
	const ec_atom *a = ec_intern(p->d->css, (const char *) s, n);
	if (a == NULL)
		oom(p);
	return a;
}

static const ec_atom *hatom(P *p, int hid)
{
	if (p->atoms[hid] == NULL)
		p->atoms[hid] = atom(p, (const uint8_t *) hname[hid], hlen[hid]);
	return p->atoms[hid];
}

static const ec_atom *tok_atom(P *p)
{
	if (p->t.atom == NULL)
		p->t.atom = p->t.hid ? hatom(p, p->t.hid) : atom(p, p->t.s, p->t.n);
	return p->t.atom;
}

/* the class attribute, split at white space */
static void set_classes(P *p, dnode *n, const char *s, size_t len)
{
	const char *e = s + len, *w;
	size_t k = 0, i = 0;
	while (s + i < e) {
		while (s + i < e && WS((uint8_t) s[i]))
			i++;
		if (s + i < e)
			k++;
		while (s + i < e && !WS((uint8_t) s[i]))
			i++;
	}
	n->ncls = 0;
	n->cls = NULL;
	if (k == 0)
		return;
	n->cls = dalloc(p, k * sizeof *n->cls);
	while (s < e) {
		while (s < e && WS((uint8_t) *s))
			s++;
		w = s;
		while (s < e && !WS((uint8_t) *s))
			s++;
		if (s > w)
			n->cls[n->ncls++] = atom(p, (const uint8_t *) w, (size_t) (s - w));
	}
}

static void put_attrs(P *p, dnode *n, const TATTR *a, uint32_t na)
{
	uint32_t i;
	if (na == 0)
		return;
	n->attrs = dalloc(p, na * sizeof *n->attrs);
	for (i = 0; i < na; i++) {
		char *v = dalloc(p, a[i].vl + 1);
		ec_attr *at = &n->attrs[i];
		memcpy(v, a[i].v, a[i].vl);
		v[a[i].vl] = 0;
		at->name = atom(p, a[i].n, a[i].nl);
		at->val = v;
		at->len = a[i].vl;
		if (at->name == p->a_class)
			set_classes(p, n, v, a[i].vl);
		else if (at->name == p->a_id && n->e.id == NULL && a[i].vl > 0)
			n->e.id = atom(p, a[i].v, a[i].vl);
	}
	n->nattr = (uint16_t) na;
}

static int has_attr(const dnode *n, const ec_atom *name)
{
	uint16_t i;
	for (i = 0; i < n->nattr; i++)
		if (n->attrs[i].name == name)
			return 1;
	return 0;
}

static const TATTR *tok_attr(const TOKEN *t, const char *name)
{
	size_t n = strlen(name);
	uint32_t i;
	for (i = 0; i < t->na; i++)
		if (t->a[i].nl == n && memcmp(t->a[i].n, name, n) == 0)
			return &t->a[i];
	return NULL;
}

static int tval_is(const TATTR *a, const char *v)
{
	return a != NULL && ieq(v, a->v, a->vl);
}

/* parsing template contents */
static int template_open(P *p)
{
	return p->ntemplates > 0 || (p->ctx != NULL && p->ctx->ns == DN_NS_HTML && p->ctx->hid == H_TEMPLATE);
}

/* an element for a tag (13.2.6.1, create an element for the token) */
static dnode *element(P *p, int ns, int hid, const uint8_t *name, size_t nl, const TATTR *a, uint32_t na)
{
	dnode *n = new_node(p, DN_ELEMENT);
	n->ns = (uint8_t) ns;
	n->hid = (uint16_t) hid;
	if (ns == DN_NS_SVG) {
		const char *c = camel(svg_names, name, nl);
		n->e.name = c ? atom(p, (const uint8_t *) c, strlen(c)) : hid ? hatom(p, hid) : atom(p, name, nl);
		n->tag = hid == H_SVG ? T_SVG : T_OTHER;
	} else {
		n->e.name = hid ? hatom(p, hid) : atom(p, name, nl);
		n->tag = ns == DN_NS_HTML ? hT[hid] : T_OTHER;
	}
	put_attrs(p, n, a, na);
	if (ns == DN_NS_MATHML && hid == H_ANNOTATION_XML) {
		uint32_t i;
		for (i = 0; i < na; i++)
			if (a[i].nl == 8 && memcmp(a[i].n, "encoding", 8) == 0 &&
			    (ieq("text/html", a[i].v, a[i].vl) || ieq("application/xhtml+xml", a[i].v, a[i].vl)))
				n->pf |= PF_HIP;
	}
	if (ns == DN_NS_HTML && (hflags[hid] & F_ASSOC) && p->form != NULL && !template_open(p) && p->ctx == NULL) {
		/* a listed element with a form attribute is the attribute's */
		uint32_t i;
		for (i = 0; i < na && (hflags[hid] & F_LISTED); i++)
			if (a[i].nl == 4 && memcmp(a[i].n, "form", 4) == 0)
				break;
		if (i == na || !(hflags[hid] & F_LISTED))
			n->owner = p->form;
	}
	p->d->elements++;
	return n;
}

static dnode *tok_element(P *p, int ns)
{
	return element(p, ns, p->t.hid, p->t.s, p->t.n, p->t.a, p->t.na);
}

/* a new element like old, for the token it was made for */
static dnode *clone(P *p, const dnode *old)
{
	dnode *n = new_node(p, DN_ELEMENT);
	n->ns = old->ns;
	n->hid = old->hid;
	n->tag = old->tag;
	n->e.name = old->e.name;
	n->e.id = old->e.id;
	n->attrs = old->attrs;
	n->nattr = old->nattr;
	n->cls = old->cls;
	n->ncls = old->ncls;
	p->d->elements++;
	return n;
}

static void link_before(dnode *parent, dnode *child, dnode *ref)
{
	child->parent = parent;
	if (ref == NULL) {
		child->prev = parent->last;
		child->next = NULL;
		if (parent->last)
			parent->last->next = child;
		else
			parent->first = child;
		parent->last = child;
	} else {
		child->prev = ref->prev;
		child->next = ref;
		if (ref->prev)
			ref->prev->next = child;
		else
			parent->first = child;
		ref->prev = child;
	}
}

static void unlink_node(dnode *c)
{
	dnode *parent = c->parent;
	if (parent == NULL)
		return;
	if (c->prev)
		c->prev->next = c->next;
	else
		parent->first = c->next;
	if (c->next)
		c->next->prev = c->prev;
	else
		parent->last = c->prev;
	c->parent = c->prev = c->next = NULL;
}

/* ---------------------------------------------------------------- the stack and the list */

#define CUR(p) ((p)->open[(p)->nopen - 1])

static int is(const dnode *n, int hid)
{
	return n->ns == DN_NS_HTML && n->hid == hid;
}

static int special(const dnode *n)
{
	if (n->ns == DN_NS_HTML)
		return (hflags[n->hid] & F_SPECIAL) != 0;
	if (n->ns == DN_NS_MATHML)
		return n->hid == H_MI || n->hid == H_MO || n->hid == H_MN || n->hid == H_MS ||
		       n->hid == H_MTEXT || n->hid == H_ANNOTATION_XML;
	return n->hid == H_FOREIGNOBJECT || n->hid == H_DESC || n->hid == H_TITLE;
}

static int mathml_tip(const dnode *n)
{
	return n->ns == DN_NS_MATHML && (n->hid == H_MI || n->hid == H_MO || n->hid == H_MN ||
					 n->hid == H_MS || n->hid == H_MTEXT);
}

static int html_ip(const dnode *n)
{
	if (n->ns == DN_NS_MATHML)
		return n->hid == H_ANNOTATION_XML && (n->pf & PF_HIP);
	return n->ns == DN_NS_SVG && (n->hid == H_FOREIGNOBJECT || n->hid == H_DESC || n->hid == H_TITLE);
}

static void push(P *p, dnode *n)
{
	if (p->nopen >= p->capopen)
		GROW(p, p->open, p->nopen, p->capopen, p->nopen + 1);
	p->open[p->nopen++] = n;
	n->pf |= PF_OPEN;
	if (is(n, H_TEMPLATE))
		p->ntemplates++;
}

static void pop(P *p)
{
	dnode *n = p->open[--p->nopen];
	n->pf &= ~PF_OPEN;
	if (is(n, H_TEMPLATE))
		p->ntemplates--;
}

static int stack_index(P *p, const dnode *n)
{
	int i;
	if (!(n->pf & PF_OPEN))
		return -1;
	for (i = p->nopen - 1; i >= 0; i--)
		if (p->open[i] == n)
			return i;
	return -1;
}

static void stack_remove_at(P *p, int i)
{
	dnode *n = p->open[i];
	n->pf &= ~PF_OPEN;
	if (is(n, H_TEMPLATE))
		p->ntemplates--;
	memmove(p->open + i, p->open + i + 1, (size_t) (p->nopen - i - 1) * sizeof *p->open);
	p->nopen--;
}

static void stack_insert_at(P *p, int i, dnode *n)
{
	if (p->nopen >= p->capopen)
		GROW(p, p->open, p->nopen, p->capopen, p->nopen + 1);
	memmove(p->open + i + 1, p->open + i, (size_t) (p->nopen - i) * sizeof *p->open);
	p->open[i] = n;
	p->nopen++;
	n->pf |= PF_OPEN;
	if (is(n, H_TEMPLATE))
		p->ntemplates++;
}

static void pop_until(P *p, int hid)
{
	while (p->nopen > 0) {
		dnode *n = CUR(p);
		pop(p);
		if (is(n, hid))
			break;
	}
}

static void pop_until_node(P *p, const dnode *n)
{
	while (p->nopen > 0) {
		dnode *c = CUR(p);
		pop(p);
		if (c == n)
			break;
	}
}

enum { SC_DEFAULT, SC_LIST, SC_BUTTON, SC_TABLE };

static int bounds(const dnode *n, int kind)
{
	if (kind == SC_TABLE)
		return n->ns == DN_NS_HTML && (n->hid == H_HTML || n->hid == H_TABLE || n->hid == H_TEMPLATE);
	if (n->ns == DN_NS_HTML)
		return (hflags[n->hid] & F_SCOPE) || (kind == SC_LIST && (n->hid == H_OL || n->hid == H_UL)) ||
		       (kind == SC_BUTTON && n->hid == H_BUTTON);
	if (n->ns == DN_NS_MATHML)
		return mathml_tip(n) || n->hid == H_ANNOTATION_XML;
	return n->hid == H_FOREIGNOBJECT || n->hid == H_DESC || n->hid == H_TITLE;
}

static int in_scope(P *p, int hid, int kind)
{
	int i;
	for (i = p->nopen - 1; i >= 0; i--) {
		const dnode *n = p->open[i];
		if (is(n, hid))
			return 1;
		if (bounds(n, kind))
			return 0;
	}
	return 0;
}

static int in_scope_node(P *p, const dnode *target, int kind)
{
	int i;
	for (i = p->nopen - 1; i >= 0; i--) {
		const dnode *n = p->open[i];
		if (n == target)
			return 1;
		if (bounds(n, kind))
			return 0;
	}
	return 0;
}

/* an HTML element whose name is the token's */
static int same_name(P *p, const dnode *n)
{
	if (n->ns != DN_NS_HTML)
		return 0;
	if (p->t.hid)
		return n->hid == p->t.hid;
	return n->hid == H_NONE && n->e.name == tok_atom(p);
}

static void implied_end(P *p, int except)
{
	while (p->nopen > 0) {
		const dnode *n = CUR(p);
		if (n->ns != DN_NS_HTML || !(hflags[n->hid] & F_IMPLIED) || n->hid == except)
			break;
		pop(p);
	}
}

static void implied_end_thorough(P *p)
{
	while (p->nopen > 0) {
		const dnode *n = CUR(p);
		if (n->ns != DN_NS_HTML || !(hflags[n->hid] & F_THOROUGH))
			break;
		pop(p);
	}
}

static void close_p(P *p)
{
	implied_end(p, H_P);
	pop_until(p, H_P);
}

static void afe_put(P *p, int i, dnode *n)
{
	if (p->nafe >= p->capafe)
		GROW(p, p->afe, p->nafe, p->capafe, p->nafe + 1);
	memmove(p->afe + i + 1, p->afe + i, (size_t) (p->nafe - i) * sizeof *p->afe);
	p->afe[i] = n;
	p->nafe++;
	if (n)
		n->pf |= PF_AFE;
}

static void afe_remove_at(P *p, int i)
{
	if (p->afe[i])
		p->afe[i]->pf &= ~PF_AFE;
	memmove(p->afe + i, p->afe + i + 1, (size_t) (p->nafe - i - 1) * sizeof *p->afe);
	p->nafe--;
}

static int afe_index(P *p, const dnode *n)
{
	int i;
	if (!(n->pf & PF_AFE))
		return -1;
	for (i = p->nafe - 1; i >= 0; i--)
		if (p->afe[i] == n)
			return i;
	return -1;
}

static void marker(P *p)
{
	afe_put(p, p->nafe, NULL);
}

static void clear_to_marker(P *p)
{
	while (p->nafe > 0) {
		dnode *n = p->afe[p->nafe - 1];
		afe_remove_at(p, p->nafe - 1);
		if (n == NULL)
			break;
	}
}

static int same_attrs(const dnode *a, const dnode *b)
{
	uint16_t i, j;
	if (a->nattr != b->nattr)
		return 0;
	if (a->attrs == b->attrs)
		return 1;
	for (i = 0; i < a->nattr; i++) {
		for (j = 0; j < b->nattr; j++)
			if (a->attrs[i].name == b->attrs[j].name && a->attrs[i].len == b->attrs[j].len &&
			    memcmp(a->attrs[i].val, b->attrs[j].val, a->attrs[i].len) == 0)
				break;
		if (j == b->nattr)
			return 0;
	}
	return 1;
}

/* push onto the list of active formatting elements: Noah's Ark */
static void afe_push(P *p, dnode *n)
{
	int i, same = 0, first = -1;
	for (i = p->nafe - 1; i >= 0 && p->afe[i]; i--) {
		const dnode *o = p->afe[i];
		if (o->ns == n->ns && o->e.name == n->e.name && same_attrs(o, n)) {
			same++;
			first = i;
		}
	}
	if (same >= 3)
		afe_remove_at(p, first);
	afe_put(p, p->nafe, n);
}

/* ---------------------------------------------------------------- inserting */

/* the appropriate place for inserting a node (13.2.6.1) */
static void place(P *p, dnode *over, dnode **parent, dnode **before)
{
	dnode *t = over ? over : CUR(p);
	*before = NULL;
	if (p->foster && t->ns == DN_NS_HTML && (t->hid == H_TABLE || t->hid == H_TBODY ||
	    t->hid == H_TFOOT || t->hid == H_THEAD || t->hid == H_TR)) {
		int i;
		for (i = p->nopen - 1; i >= 0; i--)
			if (is(p->open[i], H_TEMPLATE) || is(p->open[i], H_TABLE))
				break;
		if (i < 0) {
			t = p->open[0];
		} else if (is(p->open[i], H_TEMPLATE)) {
			t = p->open[i];
		} else if (p->open[i]->parent != NULL) {
			t = p->open[i]->parent;
			*before = p->open[i];
		} else {
			t = p->open[i - 1];
		}
	}
	*parent = t;
}

static void insert_el(P *p, dnode *n)
{
	dnode *parent, *before, *c;
	place(p, NULL, &parent, &before);
	if (parent->type == DN_DOCUMENT)
		for (c = parent->first; c != NULL; c = c->next)
			if (c->type == DN_ELEMENT)
				break;
	if (parent->type != DN_DOCUMENT || c == NULL)
		link_before(parent, n, before);
	push(p, n);
}

static dnode *insert_tok(P *p, int ns)
{
	dnode *n = tok_element(p, ns);
	insert_el(p, n);
	return n;
}

/* an HTML element for a tag with no attributes */
static dnode *insert_html(P *p, int hid)
{
	dnode *n = element(p, DN_NS_HTML, hid, (const uint8_t *) hname[hid], strlen(hname[hid]), NULL, 0);
	insert_el(p, n);
	return n;
}

static void insert_chars(P *p, const uint8_t *s, size_t n)
{
	dnode *parent, *before, *prev, *t;
	if (n == 0)
		return;
	place(p, NULL, &parent, &before);
	if (parent->type == DN_DOCUMENT)
		return;
	prev = before ? before->prev : parent->last;
	if (prev != NULL && prev->type == DN_TEXT) {
		text_add(p, prev, s, n);
		return;
	}
	t = new_node(p, DN_TEXT);
	set_text(p, t, s, n);
	t->cap = t->len;
	link_before(parent, t, before);
}

static void insert_comment(P *p, dnode *parent)
{
	dnode *before = NULL, *c = new_node(p, p->t.type == TK_PI ? DN_PI : DN_OTHER);
	if (p->t.type == TK_PI) {
		c->text = dalloc(p, p->t.n + p->t.nd + 2);
		memcpy(c->text, p->t.s, p->t.n);
		c->text[p->t.n] = 0;
		memcpy(c->text + p->t.n + 1, p->t.d, p->t.nd);
		c->text[p->t.n + 1 + p->t.nd] = 0;
		c->len = (uint32_t) (p->t.n + 1 + p->t.nd);
	} else {
		set_text(p, c, p->t.s, p->t.n);
	}
	if (parent == NULL)
		place(p, NULL, &parent, &before);
	link_before(parent, c, before);
}

/* reconstruct the active formatting elements */
static void reconstruct(P *p)
{
	int i;
	if (p->nafe == 0)
		return;
	i = p->nafe - 1;
	if (p->afe[i] == NULL || (p->afe[i]->pf & PF_OPEN))
		return;
	while (i > 0) {
		i--;
		if (p->afe[i] == NULL || (p->afe[i]->pf & PF_OPEN)) {
			i++;
			break;
		}
	}
	for (; i < p->nafe; i++) {
		dnode *n = clone(p, p->afe[i]);
		insert_el(p, n);
		p->afe[i]->pf &= ~PF_AFE;
		p->afe[i] = n;
		n->pf |= PF_AFE;
	}
}

/* ---------------------------------------------------------------- tree construction */

enum { DONE, AGAIN };

static void process_in(P *p, int m);
static void any_other_end(P *p);

static int is_ws_run(const uint8_t *s, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (!WS(s[i]))
			return 0;
	return 1;
}

/* the leading white space of a character token, taken off it */
static size_t take_ws(P *p)
{
	size_t i = 0;
	if (p->t.nul)
		return 0;
	while (i < p->t.n && WS(p->t.s[i]))
		i++;
	return i;
}

/* one character off the front of a character token */
static void skip_char(P *p)
{
	size_t k = 1;
	uint8_t c = p->t.s[0];
	if (c >= 0xc0)
		k = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
	if (k > p->t.n)
		k = p->t.n;
	p->t.s += k;
	p->t.n -= k;
}

static void reset_mode(P *p)
{
	int i;
	for (i = p->nopen - 1; i >= 0; i--) {
		const dnode *n = p->open[i];
		int last = i == 0;
		if (last && p->ctx)
			n = p->ctx;
		if (n->ns == DN_NS_HTML) {
			switch (n->hid) {
			case H_TD:
			case H_TH:
				if (!last) {
					p->mode = M_IN_CELL;
					return;
				}
				break;
			case H_TR:
				p->mode = M_IN_ROW;
				return;
			case H_TBODY:
			case H_THEAD:
			case H_TFOOT:
				p->mode = M_IN_TABLE_BODY;
				return;
			case H_CAPTION:
				p->mode = M_IN_CAPTION;
				return;
			case H_COLGROUP:
				p->mode = M_IN_COLUMN_GROUP;
				return;
			case H_TABLE:
				p->mode = M_IN_TABLE;
				return;
			case H_TEMPLATE:
				p->mode = p->tmodes[p->ntm - 1];
				return;
			case H_HEAD:
				if (!last) {
					p->mode = M_IN_HEAD;
					return;
				}
				break;
			case H_BODY:
				p->mode = M_IN_BODY;
				return;
			case H_FRAMESET:
				p->mode = M_IN_FRAMESET;
				return;
			case H_HTML:
				p->mode = p->head == NULL ? M_BEFORE_HEAD : M_AFTER_HEAD;
				return;
			}
		}
		if (last) {
			p->mode = M_IN_BODY;
			return;
		}
	}
	p->mode = M_IN_BODY;
}

static void tmode_push(P *p, int m)
{
	if (p->ntm >= p->captm)
		GROW(p, p->tmodes, p->ntm, p->captm, p->ntm + 1);
	p->tmodes[p->ntm++] = (uint8_t) m;
}

static void raw_text(P *p, int state)
{
	insert_tok(p, DN_NS_HTML);
	p->state = state;
	p->orig = p->mode;
	p->mode = M_TEXT;
}

static int is_end(P *p, int hid)
{
	return p->t.type == TK_END && p->t.hid == hid;
}

/* the adoption agency algorithm; 1 if the token is to be handled as "any
   other end tag" */
static int adoption(P *p)
{
	dnode *cur = CUR(p);
	int outer;

	if (same_name(p, cur) && !(cur->pf & PF_AFE)) {
		pop(p);
		return 0;
	}
	for (outer = 0; outer < 8; outer++) {
		dnode *fe = NULL, *fb, *ca, *node, *last, *bm_after = NULL, *ne, *parent, *before;
		int i, fi = -1, si, fbi, ni, inner, moved = 0;

		for (i = p->nafe - 1; i >= 0 && p->afe[i]; i--)
			if (same_name(p, p->afe[i])) {
				fi = i;
				fe = p->afe[i];
				break;
			}
		if (fe == NULL)
			return 1;
		si = stack_index(p, fe);
		if (si < 0) {
			afe_remove_at(p, fi);
			return 0;
		}
		if (!in_scope_node(p, fe, SC_DEFAULT))
			return 0;
		fbi = -1;
		for (i = si + 1; i < p->nopen; i++)
			if (special(p->open[i])) {
				fbi = i;
				break;
			}
		if (fbi < 0) {
			while (p->nopen > si)
				pop(p);
			afe_remove_at(p, afe_index(p, fe));
			return 0;
		}
		fb = p->open[fbi];
		ca = p->open[si - 1];
		node = last = fb;
		ni = fbi;
		for (inner = 1;; inner++) {
			int ai;
			ni--;
			node = p->open[ni];
			if (node == fe)
				break;
			ai = afe_index(p, node);
			if (inner > 3 && ai >= 0) {
				afe_remove_at(p, ai);
				ai = -1;
			}
			if (ai < 0) {
				stack_remove_at(p, ni);
				continue;
			}
			ne = clone(p, node);
			node->pf &= ~(PF_AFE | PF_OPEN);
			p->afe[ai] = ne;
			p->open[ni] = ne;
			ne->pf |= PF_AFE | PF_OPEN;
			node = ne;
			if (!moved) {
				bm_after = ne;
				moved = 1;
			}
			unlink_node(last);
			link_before(node, last, NULL);
			last = node;
		}
		unlink_node(last);
		place(p, ca, &parent, &before);
		link_before(parent, last, before);
		ne = clone(p, fe);
		while (fb->first) {
			dnode *c = fb->first;
			unlink_node(c);
			link_before(ne, c, NULL);
		}
		link_before(fb, ne, NULL);
		/* the list: ne where the bookmark is */
		fi = afe_index(p, fe);
		if (bm_after == NULL) {
			p->afe[fi] = ne;
			fe->pf &= ~PF_AFE;
			ne->pf |= PF_AFE;
		} else {
			afe_remove_at(p, fi);
			afe_put(p, afe_index(p, bm_after) + 1, ne);
		}
		/* the stack: ne just below fb */
		stack_remove_at(p, stack_index(p, fe));
		stack_insert_at(p, stack_index(p, fb) + 1, ne);
	}
	return 0;
}

static void any_other_end(P *p)
{
	int i;
	for (i = p->nopen - 1; i >= 0; i--) {
		dnode *n = p->open[i];
		if (same_name(p, n)) {
			implied_end(p, n->hid ? n->hid : -1);
			pop_until_node(p, n);
			return;
		}
		if (special(n))
			return;
	}
}

/* white space and other characters for the modes that tell them apart:
   the white space at the front goes to ws (insert it if 1), the rest is
   handled as "anything else" by the caller (returns 1 if there is any) */
static int front_ws(P *p, int insert)
{
	size_t k = take_ws(p);
	if (k > 0) {
		if (insert)
			insert_chars(p, p->t.s, k);
		p->t.s += k;
		p->t.n -= k;
	}
	return p->t.n > 0;
}

static uint16_t enc_of(const uint8_t *s, size_t n, int meta);

static int m_initial(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (!front_ws(p, 0))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, p->root);
		return DONE;
	case TK_DOCTYPE: {
		static const char *const pub_starts[] = { "+//silmaril//dtd html pro v0r11 19970101//",
			"-//as//dtd html 3.0 aswedit + extensions//", "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
			"-//ietf//dtd html 2.0 level 1//", "-//ietf//dtd html 2.0 level 2//",
			"-//ietf//dtd html 2.0 strict level 1//", "-//ietf//dtd html 2.0 strict level 2//",
			"-//ietf//dtd html 2.0 strict//", "-//ietf//dtd html 2.0//", "-//ietf//dtd html 2.1e//",
			"-//ietf//dtd html 3.0//", "-//ietf//dtd html 3.2 final//", "-//ietf//dtd html 3.2//",
			"-//ietf//dtd html 3//", "-//ietf//dtd html level 0//", "-//ietf//dtd html level 1//",
			"-//ietf//dtd html level 2//", "-//ietf//dtd html level 3//",
			"-//ietf//dtd html strict level 0//", "-//ietf//dtd html strict level 1//",
			"-//ietf//dtd html strict level 2//", "-//ietf//dtd html strict level 3//",
			"-//ietf//dtd html strict//", "-//ietf//dtd html//", "-//metrius//dtd metrius presentational//",
			"-//microsoft//dtd internet explorer 2.0 html strict//",
			"-//microsoft//dtd internet explorer 2.0 html//",
			"-//microsoft//dtd internet explorer 2.0 tables//",
			"-//microsoft//dtd internet explorer 3.0 html strict//",
			"-//microsoft//dtd internet explorer 3.0 html//",
			"-//microsoft//dtd internet explorer 3.0 tables//", "-//netscape comm. corp.//dtd html//",
			"-//netscape comm. corp.//dtd strict html//", "-//o'reilly and associates//dtd html 2.0//",
			"-//o'reilly and associates//dtd html extended 1.0//",
			"-//o'reilly and associates//dtd html extended relaxed 1.0//",
			"-//sq//dtd html 2.0 hotmetal + extensions//",
			"-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
			"-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
			"-//spyglass//dtd html 2.0 extended//", "-//sun microsystems corp.//dtd hotjava html//",
			"-//sun microsystems corp.//dtd hotjava strict html//", "-//w3c//dtd html 3 1995-03-24//",
			"-//w3c//dtd html 3.2 draft//", "-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//",
			"-//w3c//dtd html 3.2s draft//", "-//w3c//dtd html 4.0 frameset//",
			"-//w3c//dtd html 4.0 transitional//", "-//w3c//dtd html experimental 19960712//",
			"-//w3c//dtd html experimental 970421//", "-//w3c//dtd w3 html//",
			"-//w3o//dtd w3 html 3.0//", "-//webtechs//dtd mozilla html 2.0//",
			"-//webtechs//dtd mozilla html//", NULL };
		dnode *dt = new_node(p, DN_DOCTYPE);
		size_t i, np = t->npub, ns = t->nsys;
		int q = 0;
		char *x = dalloc(p, t->n + np + ns + 3);
		if (t->n)
			memcpy(x, t->s, t->n);
		x[t->n] = 0;
		if (np)
			memcpy(x + t->n + 1, t->pub, np);
		x[t->n + 1 + np] = 0;
		if (ns)
			memcpy(x + t->n + 2 + np, t->sys, ns);
		x[t->n + 2 + np + ns] = 0;
		dt->text = x;
		dt->len = (uint32_t) t->n;
		if (!t->has_pub)
			dt->pf |= PF_NO_PUBLIC;
		if (!t->has_sys)
			dt->pf |= PF_NO_SYSTEM;
		link_before(p->root, dt, NULL);
#define PUB_IS(s) (t->has_pub && ieq(s, t->pub, np))
#define PUB_STARTS(s) (t->has_pub && np >= strlen(s) && ieq(s, t->pub, strlen(s)))
		if (t->quirks || !(t->n == 4 && memcmp(t->s, "html", 4) == 0) ||
		    PUB_IS("-//w3o//dtd w3 html strict 3.0//en//") || PUB_IS("-/w3c/dtd html 4.0 transitional/en") ||
		    PUB_IS("html") || (t->has_sys && ieq("http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd", t->sys, ns)) ||
		    (!t->has_sys && (PUB_STARTS("-//w3c//dtd html 4.01 frameset//") ||
				     PUB_STARTS("-//w3c//dtd html 4.01 transitional//"))))
			q = 1;
		for (i = 0; !q && pub_starts[i]; i++)
			if (PUB_STARTS(pub_starts[i]))
				q = 1;
		if (!q && (PUB_STARTS("-//w3c//dtd xhtml 1.0 frameset//") ||
			   PUB_STARTS("-//w3c//dtd xhtml 1.0 transitional//") ||
			   (t->has_sys && (PUB_STARTS("-//w3c//dtd html 4.01 frameset//") ||
					   PUB_STARTS("-//w3c//dtd html 4.01 transitional//")))))
			q = 2;
#undef PUB_IS
#undef PUB_STARTS
		p->quirks = (uint8_t) q;
		p->mode = M_BEFORE_HTML;
		return DONE;
	}
	}
	p->quirks = 1;
	p->mode = M_BEFORE_HTML;
	return AGAIN;
}

static int m_before_html(P *p)
{
	TOKEN *t = &p->t;
	dnode *n;
	switch (t->type) {
	case TK_DOCTYPE:
		return DONE;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, p->root);
		return DONE;
	case TK_CHARS:
		if (!front_ws(p, 0))
			return DONE;
		break;
	case TK_START:
		if (t->hid == H_HTML) {
			n = tok_element(p, DN_NS_HTML);
			link_before(p->root, n, NULL);
			push(p, n);
			p->mode = M_BEFORE_HEAD;
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid != H_HEAD && t->hid != H_BODY && t->hid != H_HTML && t->hid != H_BR)
			return DONE;
		break;
	}
	n = element(p, DN_NS_HTML, H_HTML, (const uint8_t *) "html", 4, NULL, 0);
	link_before(p->root, n, NULL);
	push(p, n);
	p->mode = M_BEFORE_HEAD;
	return AGAIN;
}

static int m_before_head(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (!front_ws(p, 0))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		if (t->hid == H_HTML) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		if (t->hid == H_HEAD) {
			p->head = insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_HEAD;
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid != H_HEAD && t->hid != H_BODY && t->hid != H_HTML && t->hid != H_BR)
			return DONE;
		break;
	}
	p->head = insert_html(p, H_HEAD);
	p->mode = M_IN_HEAD;
	return AGAIN;
}

/* a <meta charset> or <meta http-equiv=Content-Type content> while the
   encoding is tentative (13.2.6.4.4) */
static void meta_charset(P *p)
{
	const TATTR *cs = tok_attr(&p->t, "charset"), *he, *ct;
	uint16_t enc = 0;
	if (!p->tentative)
		return;
	if (cs != NULL) {
		enc = enc_of(cs->v, cs->vl, 1);
	} else if ((he = tok_attr(&p->t, "http-equiv")) != NULL && tval_is(he, "content-type") &&
		   (ct = tok_attr(&p->t, "content")) != NULL) {
		/* the algorithm for extracting a character encoding from a meta
		   element: after "charset", '=', a value quoted or not */
		const uint8_t *s = ct->v, *e = ct->v + ct->vl;
		while (s + 7 <= e) {
			const uint8_t *q;
			if (!ieq("charset", s, 7)) {
				s++;
				continue;
			}
			s += 7;
			while (s < e && (WS(*s) || *s == '\r'))
				s++;
			if (s >= e || *s != '=')
				continue;
			s++;
			while (s < e && (WS(*s) || *s == '\r'))
				s++;
			if (s < e && (*s == '"' || *s == '\'')) {
				uint8_t qc = *s++;
				for (q = s; q < e && *q != qc; q++)
					;
				if (q < e)
					enc = enc_of(s, (size_t) (q - s), 1);
			} else {
				for (q = s; q < e && !WS(*q) && *q != ';'; q++)
					;
				if (q > s)
					enc = enc_of(s, (size_t) (q - s), 1);
			}
			break;
		}
	}
	if (enc == 0)
		return;
	if (enc == p->enc) {
		p->tentative = 0;
		return;
	}
	p->changed = enc;
	p->stop = 1;
}

static int m_in_head(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (!front_ws(p, 1))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		switch (t->hid) {
		case H_HTML:
			process_in(p, M_IN_BODY);
			return DONE;
		case H_BASE:
		case H_BASEFONT:
		case H_BGSOUND:
		case H_LINK:
			insert_tok(p, DN_NS_HTML);
			pop(p);
			return DONE;
		case H_META:
			insert_tok(p, DN_NS_HTML);
			pop(p);
			meta_charset(p);
			return DONE;
		case H_TITLE:
			raw_text(p, S_RCDATA);
			return DONE;
		case H_NOSCRIPT:
			if (!p->scripting) {
				insert_tok(p, DN_NS_HTML);
				p->mode = M_IN_HEAD_NOSCRIPT;
				return DONE;
			}
			raw_text(p, S_RAWTEXT);
			return DONE;
		case H_NOFRAMES:
		case H_STYLE:
			raw_text(p, S_RAWTEXT);
			return DONE;
		case H_SCRIPT:
			raw_text(p, S_SCRIPT);
			return DONE;
		case H_TEMPLATE:
			marker(p);
			p->frameset_ok = 0;
			p->mode = M_IN_TEMPLATE;
			tmode_push(p, M_IN_TEMPLATE);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_HEAD:
			return DONE;
		}
		break;
	case TK_END:
		switch (t->hid) {
		case H_HEAD:
			pop(p);
			p->mode = M_AFTER_HEAD;
			return DONE;
		case H_BODY:
		case H_HTML:
		case H_BR:
			break;
		case H_TEMPLATE:
			if (p->ntemplates == 0)
				return DONE;
			implied_end_thorough(p);
			pop_until(p, H_TEMPLATE);
			clear_to_marker(p);
			p->ntm--;
			reset_mode(p);
			return DONE;
		default:
			return DONE;
		}
		break;
	}
	pop(p);
	p->mode = M_AFTER_HEAD;
	return AGAIN;
}

static int m_in_head_noscript(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		switch (t->hid) {
		case H_HTML:
			process_in(p, M_IN_BODY);
			return DONE;
		case H_BASEFONT:
		case H_BGSOUND:
		case H_LINK:
		case H_META:
		case H_NOFRAMES:
		case H_STYLE:
			process_in(p, M_IN_HEAD);
			return DONE;
		case H_HEAD:
		case H_NOSCRIPT:
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid == H_NOSCRIPT) {
			pop(p);
			p->mode = M_IN_HEAD;
			return DONE;
		}
		if (t->hid != H_BR)
			return DONE;
		break;
	case TK_CHARS:
		if (!front_ws(p, 1))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		process_in(p, M_IN_HEAD);
		return DONE;
	}
	pop(p);
	p->mode = M_IN_HEAD;
	return AGAIN;
}

static int m_after_head(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (!front_ws(p, 1))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		switch (t->hid) {
		case H_HTML:
			process_in(p, M_IN_BODY);
			return DONE;
		case H_BODY:
			insert_tok(p, DN_NS_HTML);
			p->frameset_ok = 0;
			p->mode = M_IN_BODY;
			return DONE;
		case H_FRAMESET:
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_FRAMESET;
			return DONE;
		case H_BASE:
		case H_BASEFONT:
		case H_BGSOUND:
		case H_LINK:
		case H_META:
		case H_NOFRAMES:
		case H_SCRIPT:
		case H_STYLE:
		case H_TEMPLATE:
		case H_TITLE: {
			int i;
			push(p, p->head);
			process_in(p, M_IN_HEAD);
			for (i = p->nopen - 1; i >= 0; i--)
				if (p->open[i] == p->head) {
					stack_remove_at(p, i);
					break;
				}
			return DONE;
		}
		case H_HEAD:
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid == H_TEMPLATE) {
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		if (t->hid != H_BODY && t->hid != H_HTML && t->hid != H_BR)
			return DONE;
		break;
	}
	insert_html(p, H_BODY);
	p->frameset_ok = 1;
	p->mode = M_IN_BODY;
	return AGAIN;
}

static void add_missing_attrs(P *p, dnode *n)
{
	uint32_t i, k = 0;
	ec_attr *a;
	for (i = 0; i < p->t.na; i++)
		if (!has_attr(n, atom(p, p->t.a[i].n, p->t.a[i].nl)))
			k++;
	if (k == 0)
		return;
	a = dalloc(p, (n->nattr + k) * sizeof *a);
	if (n->nattr)
		memcpy(a, n->attrs, n->nattr * sizeof *a);
	for (i = 0; i < p->t.na; i++) {
		const ec_atom *name = atom(p, p->t.a[i].n, p->t.a[i].nl);
		dnode tmp;
		if (has_attr(n, name))
			continue;
		memset(&tmp, 0, sizeof tmp);
		put_attrs(p, &tmp, &p->t.a[i], 1);
		a[n->nattr++] = tmp.attrs[0];
		n->attrs = a;
		if (tmp.cls != NULL) {
			n->cls = tmp.cls;
			n->ncls = tmp.ncls;
		}
		if (tmp.e.id != NULL && n->e.id == NULL)
			n->e.id = tmp.e.id;
	}
	n->attrs = a;
}

static void foreign_start(P *p, int ns)
{
	reconstruct(p);
	insert_tok(p, ns);
	if (p->t.self_closing)
		pop(p);
}

static int m_in_body(P *p)
{
	TOKEN *t = &p->t;
	int i;

	switch (t->type) {
	case TK_CHARS:
		if (t->nul)
			return DONE;
		reconstruct(p);
		insert_chars(p, t->s, t->n);
		if (p->frameset_ok && !is_ws_run(t->s, t->n))
			p->frameset_ok = 0;
		return DONE;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_EOF:
		if (p->ntm > 0) {
			process_in(p, M_IN_TEMPLATE);
			return DONE;
		}
		p->stop = 1;
		return DONE;
	case TK_START:
		if (t->hid == H_NONE)
			goto any_start;
		if (hflags[t->hid] & F_BLOCK) {
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		}
		if (hflags[t->hid] & F_HEADING) {
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			if (CUR(p)->ns == DN_NS_HTML && (hflags[CUR(p)->hid] & F_HEADING))
				pop(p);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		}
		switch (t->hid) {
		case H_HTML:
			if (!template_open(p))
				add_missing_attrs(p, p->open[0]);
			return DONE;
		case H_BASE:
		case H_BASEFONT:
		case H_BGSOUND:
		case H_LINK:
		case H_META:
		case H_NOFRAMES:
		case H_SCRIPT:
		case H_STYLE:
		case H_TEMPLATE:
		case H_TITLE:
			process_in(p, M_IN_HEAD);
			return DONE;
		case H_BODY:
			if (p->nopen == 1 || !is(p->open[1], H_BODY) || template_open(p))
				return DONE;
			p->frameset_ok = 0;
			add_missing_attrs(p, p->open[1]);
			return DONE;
		case H_FRAMESET:
			if (p->nopen == 1 || !is(p->open[1], H_BODY) || !p->frameset_ok)
				return DONE;
			unlink_node(p->open[1]);
			while (p->nopen > 1)
				pop(p);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_FRAMESET;
			return DONE;
		case H_PRE:
		case H_LISTING:
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			insert_tok(p, DN_NS_HTML);
			p->skip_lf = 2;
			p->frameset_ok = 0;
			return DONE;
		case H_FORM:
			if (p->form != NULL && !template_open(p))
				return DONE;
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			{
				dnode *f = insert_tok(p, DN_NS_HTML);
				if (!template_open(p))
					p->form = f;
			}
			return DONE;
		case H_LI:
		case H_DD:
		case H_DT:
			p->frameset_ok = 0;
			for (i = p->nopen - 1; i >= 0; i--) {
				dnode *n = p->open[i];
				int h = n->ns == DN_NS_HTML ? n->hid : -1;
				if ((t->hid == H_LI && h == H_LI) || (t->hid != H_LI && (h == H_DD || h == H_DT))) {
					implied_end(p, h);
					pop_until(p, h);
					break;
				}
				if (special(n) && !(h == H_ADDRESS || h == H_DIV || h == H_P))
					break;
			}
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_PLAINTEXT:
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			insert_tok(p, DN_NS_HTML);
			p->state = S_PLAINTEXT;
			return DONE;
		case H_BUTTON:
			if (in_scope(p, H_BUTTON, SC_DEFAULT)) {
				implied_end(p, -1);
				pop_until(p, H_BUTTON);
			}
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			p->frameset_ok = 0;
			return DONE;
		case H_A:
			for (i = p->nafe - 1; i >= 0 && p->afe[i]; i--)
				if (is(p->afe[i], H_A)) {
					dnode *a = p->afe[i];
					int k;
					if (adoption(p))
						any_other_end(p);
					if ((k = afe_index(p, a)) >= 0)
						afe_remove_at(p, k);
					if ((k = stack_index(p, a)) >= 0)
						stack_remove_at(p, k);
					break;
				}
			reconstruct(p);
			afe_push(p, insert_tok(p, DN_NS_HTML));
			return DONE;
		case H_B:
		case H_BIG:
		case H_CODE:
		case H_EM:
		case H_FONT:
		case H_I:
		case H_S:
		case H_SMALL:
		case H_STRIKE:
		case H_STRONG:
		case H_TT:
		case H_U:
			reconstruct(p);
			afe_push(p, insert_tok(p, DN_NS_HTML));
			return DONE;
		case H_NOBR:
			reconstruct(p);
			if (in_scope(p, H_NOBR, SC_DEFAULT)) {
				if (adoption(p))
					any_other_end(p);
				reconstruct(p);
			}
			afe_push(p, insert_tok(p, DN_NS_HTML));
			return DONE;
		case H_APPLET:
		case H_MARQUEE:
		case H_OBJECT:
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			marker(p);
			p->frameset_ok = 0;
			return DONE;
		case H_TABLE:
			if (p->quirks != 1 && in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			insert_tok(p, DN_NS_HTML);
			p->frameset_ok = 0;
			p->mode = M_IN_TABLE;
			return DONE;
		case H_AREA:
		case H_BR:
		case H_EMBED:
		case H_IMG:
		case H_KEYGEN:
		case H_WBR:
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			pop(p);
			p->frameset_ok = 0;
			return DONE;
		case H_INPUT:
			if (p->ctx && is(p->ctx, H_SELECT))
				return DONE;
			if (in_scope(p, H_SELECT, SC_DEFAULT))
				pop_until(p, H_SELECT);
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			pop(p);
			if (!tval_is(tok_attr(t, "type"), "hidden"))
				p->frameset_ok = 0;
			return DONE;
		case H_PARAM:
		case H_SOURCE:
		case H_TRACK:
			insert_tok(p, DN_NS_HTML);
			pop(p);
			return DONE;
		case H_HR:
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			if (in_scope(p, H_SELECT, SC_DEFAULT))
				implied_end(p, -1);
			insert_tok(p, DN_NS_HTML);
			pop(p);
			p->frameset_ok = 0;
			return DONE;
		case H_IMAGE:
			t->hid = H_IMG;
			t->s = (const uint8_t *) "img";
			t->n = 3;
			t->atom = NULL;
			return m_in_body(p);
		case H_TEXTAREA:
			insert_tok(p, DN_NS_HTML);
			p->skip_lf = 2;
			p->state = S_RCDATA;
			p->orig = p->mode;
			p->frameset_ok = 0;
			p->mode = M_TEXT;
			return DONE;
		case H_XMP:
			if (in_scope(p, H_P, SC_BUTTON))
				close_p(p);
			reconstruct(p);
			p->frameset_ok = 0;
			raw_text(p, S_RAWTEXT);
			return DONE;
		case H_IFRAME:
			p->frameset_ok = 0;
			raw_text(p, S_RAWTEXT);
			return DONE;
		case H_NOEMBED:
			raw_text(p, S_RAWTEXT);
			return DONE;
		case H_NOSCRIPT:
			if (p->scripting) {
				raw_text(p, S_RAWTEXT);
				return DONE;
			}
			goto any_start;
		case H_SELECT:
			if (p->ctx && is(p->ctx, H_SELECT))
				return DONE;
			if (in_scope(p, H_SELECT, SC_DEFAULT)) {
				pop_until(p, H_SELECT);
				return DONE;
			}
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			p->frameset_ok = 0;
			return DONE;
		case H_OPTION:
			if (in_scope(p, H_SELECT, SC_DEFAULT))
				implied_end(p, H_OPTGROUP);
			else if (is(CUR(p), H_OPTION))
				pop(p);
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_OPTGROUP:
			if (in_scope(p, H_SELECT, SC_DEFAULT))
				implied_end(p, -1);
			else if (is(CUR(p), H_OPTION))
				pop(p);
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_RB:
		case H_RTC:
			if (in_scope(p, H_RUBY, SC_DEFAULT))
				implied_end(p, -1);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_RP:
		case H_RT:
			if (in_scope(p, H_RUBY, SC_DEFAULT))
				implied_end(p, H_RTC);
			insert_tok(p, DN_NS_HTML);
			return DONE;
		case H_MATH:
			foreign_start(p, DN_NS_MATHML);
			return DONE;
		case H_SVG:
			foreign_start(p, DN_NS_SVG);
			return DONE;
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_FRAME:
		case H_HEAD:
		case H_TBODY:
		case H_TD:
		case H_TFOOT:
		case H_TH:
		case H_THEAD:
		case H_TR:
			return DONE;
		}
	any_start:
		reconstruct(p);
		insert_tok(p, DN_NS_HTML);
		return DONE;
	case TK_END:
		if (t->hid == H_NONE) {
			any_other_end(p);
			return DONE;
		}
		if (hflags[t->hid] & F_BLOCKEND) {
			if (!in_scope(p, t->hid, SC_DEFAULT))
				return DONE;
			implied_end(p, -1);
			pop_until(p, t->hid);
			return DONE;
		}
		if (hflags[t->hid] & F_HEADING) {
			for (i = p->nopen - 1; i >= 0; i--) {
				dnode *n = p->open[i];
				if (n->ns == DN_NS_HTML && (hflags[n->hid] & F_HEADING))
					break;
				if (bounds(n, SC_DEFAULT))
					return DONE;
			}
			if (i < 0)
				return DONE;
			implied_end(p, -1);
			while (p->nopen > 0) {
				dnode *n = CUR(p);
				pop(p);
				if (n->ns == DN_NS_HTML && (hflags[n->hid] & F_HEADING))
					break;
			}
			return DONE;
		}
		switch (t->hid) {
		case H_TEMPLATE:
			process_in(p, M_IN_HEAD);
			return DONE;
		case H_BODY:
			if (in_scope(p, H_BODY, SC_DEFAULT))
				p->mode = M_AFTER_BODY;
			return DONE;
		case H_HTML:
			if (!in_scope(p, H_BODY, SC_DEFAULT))
				return DONE;
			p->mode = M_AFTER_BODY;
			return AGAIN;
		case H_FORM:
			if (!template_open(p)) {
				dnode *n = p->form;
				p->form = NULL;
				if (n == NULL || !in_scope_node(p, n, SC_DEFAULT))
					return DONE;
				implied_end(p, -1);
				if ((i = stack_index(p, n)) >= 0)
					stack_remove_at(p, i);
			} else {
				if (!in_scope(p, H_FORM, SC_DEFAULT))
					return DONE;
				implied_end(p, -1);
				pop_until(p, H_FORM);
			}
			return DONE;
		case H_P:
			if (!in_scope(p, H_P, SC_BUTTON))
				insert_html(p, H_P);
			close_p(p);
			return DONE;
		case H_LI:
			if (!in_scope(p, H_LI, SC_LIST))
				return DONE;
			implied_end(p, H_LI);
			pop_until(p, H_LI);
			return DONE;
		case H_DD:
		case H_DT:
			if (!in_scope(p, t->hid, SC_DEFAULT))
				return DONE;
			implied_end(p, t->hid);
			pop_until(p, t->hid);
			return DONE;
		case H_A:
		case H_B:
		case H_BIG:
		case H_CODE:
		case H_EM:
		case H_FONT:
		case H_I:
		case H_NOBR:
		case H_S:
		case H_SMALL:
		case H_STRIKE:
		case H_STRONG:
		case H_TT:
		case H_U:
			if (adoption(p))
				any_other_end(p);
			return DONE;
		case H_APPLET:
		case H_MARQUEE:
		case H_OBJECT:
			if (!in_scope(p, t->hid, SC_DEFAULT))
				return DONE;
			implied_end(p, -1);
			pop_until(p, t->hid);
			clear_to_marker(p);
			return DONE;
		case H_BR:
			t->type = TK_START;
			t->na = 0;
			reconstruct(p);
			insert_tok(p, DN_NS_HTML);
			pop(p);
			p->frameset_ok = 0;
			return DONE;
		}
		any_other_end(p);
		return DONE;
	}
	return DONE;
}

static int m_text(P *p)
{
	TOKEN *t = &p->t;
	if (t->type == TK_CHARS) {
		insert_chars(p, t->s, t->n);
		return DONE;
	}
	if (t->type == TK_EOF) {
		pop(p);
		p->mode = p->orig;
		return AGAIN;
	}
	if (t->type == TK_END) {
		pop(p);
		p->mode = p->orig;
	}
	return DONE;
}

static void clear_to_table(P *p, int body, int row)
{
	while (p->nopen > 0) {
		const dnode *n = CUR(p);
		if (n->ns == DN_NS_HTML && (n->hid == H_HTML || n->hid == H_TEMPLATE ||
		    (!body && !row && n->hid == H_TABLE) ||
		    (body && (n->hid == H_TBODY || n->hid == H_TFOOT || n->hid == H_THEAD)) ||
		    (row && n->hid == H_TR)))
			break;
		pop(p);
	}
}

static int foster_in_body(P *p)
{
	uint8_t f = p->foster;
	p->foster = 1;
	process_in(p, M_IN_BODY);
	p->foster = f;
	return DONE;
}

static int m_in_table(P *p)
{
	TOKEN *t = &p->t;
	const dnode *cur = CUR(p);
	switch (t->type) {
	case TK_CHARS:
		if (cur->ns == DN_NS_HTML && (cur->hid == H_TABLE || cur->hid == H_TBODY ||
		    cur->hid == H_TEMPLATE || cur->hid == H_TFOOT || cur->hid == H_THEAD || cur->hid == H_TR)) {
			p->nptc = 0;
			p->ptc_text = 0;
			p->orig = p->mode;
			p->mode = M_IN_TABLE_TEXT;
			return AGAIN;
		}
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		switch (t->hid) {
		case H_CAPTION:
			clear_to_table(p, 0, 0);
			marker(p);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_CAPTION;
			return DONE;
		case H_COLGROUP:
			clear_to_table(p, 0, 0);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_COLUMN_GROUP;
			return DONE;
		case H_COL:
			clear_to_table(p, 0, 0);
			insert_html(p, H_COLGROUP);
			p->mode = M_IN_COLUMN_GROUP;
			return AGAIN;
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
			clear_to_table(p, 0, 0);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_TABLE_BODY;
			return DONE;
		case H_TD:
		case H_TH:
		case H_TR:
			clear_to_table(p, 0, 0);
			insert_html(p, H_TBODY);
			p->mode = M_IN_TABLE_BODY;
			return AGAIN;
		case H_TABLE:
			if (!in_scope(p, H_TABLE, SC_TABLE))
				return DONE;
			pop_until(p, H_TABLE);
			reset_mode(p);
			return AGAIN;
		case H_STYLE:
		case H_SCRIPT:
		case H_TEMPLATE:
			process_in(p, M_IN_HEAD);
			return DONE;
		case H_INPUT:
			if (!tval_is(tok_attr(t, "type"), "hidden"))
				break;
			insert_tok(p, DN_NS_HTML);
			pop(p);
			return DONE;
		case H_FORM: {
			dnode *f;
			if (p->form != NULL && !template_open(p))
				return DONE;
			f = insert_tok(p, DN_NS_HTML);
			if (!template_open(p))
				p->form = f;
			pop(p);
			return DONE;
		}
		}
		break;
	case TK_END:
		switch (t->hid) {
		case H_TABLE:
			if (!in_scope(p, H_TABLE, SC_TABLE))
				return DONE;
			pop_until(p, H_TABLE);
			reset_mode(p);
			return DONE;
		case H_BODY:
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_HTML:
		case H_TBODY:
		case H_TD:
		case H_TFOOT:
		case H_TH:
		case H_THEAD:
		case H_TR:
			return DONE;
		case H_TEMPLATE:
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		break;
	case TK_EOF:
		process_in(p, M_IN_BODY);
		return DONE;
	}
	return foster_in_body(p);
}

static int m_in_table_text(P *p)
{
	TOKEN *t = &p->t;
	if (t->type == TK_CHARS) {
		if (t->nul)
			return DONE;
		if (p->nptc + t->n > p->captc)
			GROW(p, p->ptc, p->nptc, p->captc, p->nptc + t->n);
		memcpy(p->ptc + p->nptc, t->s, t->n);
		p->nptc += t->n;
		if (!p->ptc_text && !is_ws_run(t->s, t->n))
			p->ptc_text = 1;
		return DONE;
	}
	if (p->ptc_text) {
		uint8_t f = p->foster;
		p->foster = 1;
		reconstruct(p);
		insert_chars(p, p->ptc, p->nptc);
		p->frameset_ok = 0;
		p->foster = f;
	} else {
		insert_chars(p, p->ptc, p->nptc);
	}
	p->nptc = 0;
	p->mode = p->orig;
	return AGAIN;
}

static int caption_close(P *p)
{
	if (!in_scope(p, H_CAPTION, SC_TABLE))
		return 0;
	implied_end(p, -1);
	pop_until(p, H_CAPTION);
	clear_to_marker(p);
	p->mode = M_IN_TABLE;
	return 1;
}

static int m_in_caption(P *p)
{
	TOKEN *t = &p->t;
	if (is_end(p, H_CAPTION)) {
		caption_close(p);
		return DONE;
	}
	if ((t->type == TK_START && (t->hid == H_CAPTION || t->hid == H_COL || t->hid == H_COLGROUP ||
	     t->hid == H_TBODY || t->hid == H_TD || t->hid == H_TFOOT || t->hid == H_TH ||
	     t->hid == H_THEAD || t->hid == H_TR)) || is_end(p, H_TABLE))
		return caption_close(p) ? AGAIN : DONE;
	if (t->type == TK_END && (t->hid == H_BODY || t->hid == H_COL || t->hid == H_COLGROUP ||
	    t->hid == H_HTML || t->hid == H_TBODY || t->hid == H_TD || t->hid == H_TFOOT ||
	    t->hid == H_TH || t->hid == H_THEAD || t->hid == H_TR))
		return DONE;
	process_in(p, M_IN_BODY);
	return DONE;
}

static int m_in_column_group(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (!front_ws(p, 1))
			return DONE;
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		if (t->hid == H_HTML) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		if (t->hid == H_COL) {
			insert_tok(p, DN_NS_HTML);
			pop(p);
			return DONE;
		}
		if (t->hid == H_TEMPLATE) {
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid == H_COLGROUP) {
			if (is(CUR(p), H_COLGROUP)) {
				pop(p);
				p->mode = M_IN_TABLE;
			}
			return DONE;
		}
		if (t->hid == H_COL)
			return DONE;
		if (t->hid == H_TEMPLATE) {
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		break;
	case TK_EOF:
		process_in(p, M_IN_BODY);
		return DONE;
	}
	if (!is(CUR(p), H_COLGROUP)) {
		if (t->type == TK_CHARS) {
			skip_char(p);
			return t->n ? AGAIN : DONE;
		}
		return DONE;
	}
	pop(p);
	p->mode = M_IN_TABLE;
	return AGAIN;
}

static int m_in_table_body(P *p)
{
	TOKEN *t = &p->t;
	if (t->type == TK_START) {
		switch (t->hid) {
		case H_TR:
			clear_to_table(p, 1, 0);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_ROW;
			return DONE;
		case H_TH:
		case H_TD:
			clear_to_table(p, 1, 0);
			insert_html(p, H_TR);
			p->mode = M_IN_ROW;
			return AGAIN;
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
			goto end_table;
		}
	} else if (t->type == TK_END) {
		switch (t->hid) {
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
			if (!in_scope(p, t->hid, SC_TABLE))
				return DONE;
			clear_to_table(p, 1, 0);
			pop(p);
			p->mode = M_IN_TABLE;
			return DONE;
		case H_TABLE:
			goto end_table;
		case H_BODY:
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_HTML:
		case H_TD:
		case H_TH:
		case H_TR:
			return DONE;
		}
	}
	return m_in_table(p);
end_table:
	if (!in_scope(p, H_TBODY, SC_TABLE) && !in_scope(p, H_THEAD, SC_TABLE) && !in_scope(p, H_TFOOT, SC_TABLE))
		return DONE;
	clear_to_table(p, 1, 0);
	pop(p);
	p->mode = M_IN_TABLE;
	return AGAIN;
}

static int m_in_row(P *p)
{
	TOKEN *t = &p->t;
	if (t->type == TK_START) {
		switch (t->hid) {
		case H_TH:
		case H_TD:
			clear_to_table(p, 0, 1);
			insert_tok(p, DN_NS_HTML);
			p->mode = M_IN_CELL;
			marker(p);
			return DONE;
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
		case H_TR:
			goto end_row;
		}
	} else if (t->type == TK_END) {
		switch (t->hid) {
		case H_TR:
			if (!in_scope(p, H_TR, SC_TABLE))
				return DONE;
			clear_to_table(p, 0, 1);
			pop(p);
			p->mode = M_IN_TABLE_BODY;
			return DONE;
		case H_TABLE:
			goto end_row;
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
			if (!in_scope(p, t->hid, SC_TABLE))
				return DONE;
			goto end_row;
		case H_BODY:
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_HTML:
		case H_TD:
		case H_TH:
			return DONE;
		}
	}
	return m_in_table(p);
end_row:
	if (!in_scope(p, H_TR, SC_TABLE))
		return DONE;
	clear_to_table(p, 0, 1);
	pop(p);
	p->mode = M_IN_TABLE_BODY;
	return AGAIN;
}

static void close_cell(P *p)
{
	implied_end(p, -1);
	while (p->nopen > 0) {
		dnode *n = CUR(p);
		pop(p);
		if (is(n, H_TD) || is(n, H_TH))
			break;
	}
	clear_to_marker(p);
	p->mode = M_IN_ROW;
}

static int m_in_cell(P *p)
{
	TOKEN *t = &p->t;
	if (t->type == TK_END) {
		switch (t->hid) {
		case H_TD:
		case H_TH:
			if (!in_scope(p, t->hid, SC_TABLE))
				return DONE;
			implied_end(p, -1);
			pop_until(p, t->hid);
			clear_to_marker(p);
			p->mode = M_IN_ROW;
			return DONE;
		case H_BODY:
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_HTML:
			return DONE;
		case H_TABLE:
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
		case H_TR:
			if (!in_scope(p, t->hid, SC_TABLE))
				return DONE;
			close_cell(p);
			return AGAIN;
		}
	} else if (t->type == TK_START) {
		switch (t->hid) {
		case H_CAPTION:
		case H_COL:
		case H_COLGROUP:
		case H_TBODY:
		case H_TD:
		case H_TFOOT:
		case H_TH:
		case H_THEAD:
		case H_TR:
			if (!in_scope(p, H_TD, SC_TABLE) && !in_scope(p, H_TH, SC_TABLE))
				return DONE;
			close_cell(p);
			return AGAIN;
		}
	}
	process_in(p, M_IN_BODY);
	return DONE;
}

static int m_in_template(P *p)
{
	TOKEN *t = &p->t;
	int m;
	switch (t->type) {
	case TK_CHARS:
	case TK_COMMENT:
	case TK_PI:
	case TK_DOCTYPE:
		process_in(p, M_IN_BODY);
		return DONE;
	case TK_START:
		switch (t->hid) {
		case H_BASE:
		case H_BASEFONT:
		case H_BGSOUND:
		case H_LINK:
		case H_META:
		case H_NOFRAMES:
		case H_SCRIPT:
		case H_STYLE:
		case H_TEMPLATE:
		case H_TITLE:
			process_in(p, M_IN_HEAD);
			return DONE;
		case H_CAPTION:
		case H_COLGROUP:
		case H_TBODY:
		case H_TFOOT:
		case H_THEAD:
			m = M_IN_TABLE;
			break;
		case H_COL:
			m = M_IN_COLUMN_GROUP;
			break;
		case H_TR:
			m = M_IN_TABLE_BODY;
			break;
		case H_TD:
		case H_TH:
			m = M_IN_ROW;
			break;
		default:
			m = M_IN_BODY;
			break;
		}
		p->tmodes[p->ntm - 1] = (uint8_t) m;
		p->mode = m;
		return AGAIN;
	case TK_END:
		if (t->hid == H_TEMPLATE)
			process_in(p, M_IN_HEAD);
		return DONE;
	case TK_EOF:
		if (p->ntemplates == 0) {
			p->stop = 1;
			return DONE;
		}
		pop_until(p, H_TEMPLATE);
		clear_to_marker(p);
		p->ntm--;
		reset_mode(p);
		return AGAIN;
	}
	return DONE;
}

static int m_after_body(P *p)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		if (take_ws(p) == t->n) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		if (take_ws(p) > 0) {
			size_t k = take_ws(p), n = t->n;
			t->n = k;
			process_in(p, M_IN_BODY);
			t->s += k;
			t->n = n - k;
		}
		break;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, p->open[0]);
		return DONE;
	case TK_DOCTYPE:
		return DONE;
	case TK_START:
		if (t->hid == H_HTML) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		break;
	case TK_END:
		if (t->hid == H_HTML) {
			if (p->ctx == NULL)
				p->mode = M_AFTER_AFTER_BODY;
			return DONE;
		}
		break;
	case TK_EOF:
		p->stop = 1;
		return DONE;
	}
	p->mode = M_IN_BODY;
	return AGAIN;
}

static int m_frameset(P *p, int after)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_CHARS:
		while (t->n > 0) {
			size_t k = take_ws(p);
			if (k > 0) {
				insert_chars(p, t->s, k);
				t->s += k;
				t->n -= k;
			} else {
				skip_char(p);
			}
		}
		return DONE;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return DONE;
	case TK_START:
		if (t->hid == H_HTML) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		if (t->hid == H_NOFRAMES) {
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		if (!after && t->hid == H_FRAMESET) {
			insert_tok(p, DN_NS_HTML);
			return DONE;
		}
		if (!after && t->hid == H_FRAME) {
			insert_tok(p, DN_NS_HTML);
			pop(p);
			return DONE;
		}
		return DONE;
	case TK_END:
		if (!after && t->hid == H_FRAMESET) {
			if (p->nopen == 1)
				return DONE;
			pop(p);
			if (p->ctx == NULL && !is(CUR(p), H_FRAMESET))
				p->mode = M_AFTER_FRAMESET;
			return DONE;
		}
		if (after && t->hid == H_HTML)
			p->mode = M_AFTER_AFTER_FRAMESET;
		return DONE;
	case TK_EOF:
		p->stop = 1;
		return DONE;
	}
	return DONE;
}

static int m_after_after(P *p, int frameset)
{
	TOKEN *t = &p->t;
	switch (t->type) {
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, p->root);
		return DONE;
	case TK_DOCTYPE:
		process_in(p, M_IN_BODY);
		return DONE;
	case TK_CHARS:
		if (take_ws(p) > 0) {
			size_t k = take_ws(p), n = t->n;
			t->n = k;
			process_in(p, M_IN_BODY);
			t->s += k;
			t->n = n - k;
		}
		if (t->n == 0)
			return DONE;
		if (frameset) {
			skip_char(p);
			return t->n ? AGAIN : DONE;
		}
		break;
	case TK_START:
		if (t->hid == H_HTML) {
			process_in(p, M_IN_BODY);
			return DONE;
		}
		if (frameset && t->hid == H_NOFRAMES) {
			process_in(p, M_IN_HEAD);
			return DONE;
		}
		break;
	case TK_EOF:
		p->stop = 1;
		return DONE;
	}
	if (frameset)
		return DONE;
	p->mode = M_IN_BODY;
	return AGAIN;
}

static int run_mode(P *p, int m)
{
	switch (m) {
	case M_INITIAL: return m_initial(p);
	case M_BEFORE_HTML: return m_before_html(p);
	case M_BEFORE_HEAD: return m_before_head(p);
	case M_IN_HEAD: return m_in_head(p);
	case M_IN_HEAD_NOSCRIPT: return m_in_head_noscript(p);
	case M_AFTER_HEAD: return m_after_head(p);
	case M_IN_BODY: return m_in_body(p);
	case M_TEXT: return m_text(p);
	case M_IN_TABLE: return m_in_table(p);
	case M_IN_TABLE_TEXT: return m_in_table_text(p);
	case M_IN_CAPTION: return m_in_caption(p);
	case M_IN_COLUMN_GROUP: return m_in_column_group(p);
	case M_IN_TABLE_BODY: return m_in_table_body(p);
	case M_IN_ROW: return m_in_row(p);
	case M_IN_CELL: return m_in_cell(p);
	case M_IN_TEMPLATE: return m_in_template(p);
	case M_AFTER_BODY: return m_after_body(p);
	case M_IN_FRAMESET: return m_frameset(p, 0);
	case M_AFTER_FRAMESET: return m_frameset(p, 1);
	case M_AFTER_AFTER_BODY: return m_after_after(p, 0);
	case M_AFTER_AFTER_FRAMESET: return m_after_after(p, 1);
	}
	return DONE;
}

/* the rules of mode m, and of the modes it hands the token on to */
static void process_in(P *p, int m)
{
	while (run_mode(p, m) == AGAIN) {
		if (p->t.type == TK_CHARS && p->t.n == 0)
			return;
		m = p->mode;
	}
}

/* ---------------------------------------------------------------- foreign content */

static dnode *adjusted_current(P *p)
{
	if (p->nopen == 0)
		return NULL;
	if (p->ctx && p->nopen == 1)
		return p->ctx;
	return CUR(p);
}

static int adjusted_foreign(P *p)
{
	const dnode *n = adjusted_current(p);
	return n != NULL && n->ns != DN_NS_HTML;
}

/* a lower case name compared with an element's name */
static int name_is_lower(const dnode *n, const uint8_t *s, size_t len)
{
	size_t nl;
	const char *t = ec_atom_text(n->e.name, &nl);
	return nl == len && ieq(t, s, len);
}

static void foreign(P *p)
{
	TOKEN *t = &p->t;
	int i;
	switch (t->type) {
	case TK_CHARS:
		if (t->nul) {
			insert_chars(p, FFFD, 3);
			return;
		}
		insert_chars(p, t->s, t->n);
		if (p->frameset_ok && !is_ws_run(t->s, t->n))
			p->frameset_ok = 0;
		return;
	case TK_COMMENT:
	case TK_PI:
		insert_comment(p, NULL);
		return;
	case TK_DOCTYPE:
		return;
	case TK_START: {
		dnode *acn;
		if ((t->hid && (hflags[t->hid] & F_BREAKOUT)) ||
		    (t->hid == H_FONT && (tok_attr(t, "color") || tok_attr(t, "face") || tok_attr(t, "size"))))
			goto breakout;
		acn = adjusted_current(p);
		insert_tok(p, acn->ns);
		if (t->self_closing)
			pop(p);
		return;
	}
	case TK_END:
		if (t->hid == H_BR || t->hid == H_P)
			goto breakout;
		if (t->hid == H_SCRIPT && CUR(p)->ns == DN_NS_SVG && CUR(p)->hid == H_SCRIPT) {
			pop(p);
			return;
		}
		for (i = p->nopen - 1; i >= 0; i--) {
			dnode *n = p->open[i];
			if (i == 0)
				return;
			if (name_is_lower(n, t->s, t->n)) {
				pop_until_node(p, n);
				return;
			}
			if (p->open[i - 1]->ns == DN_NS_HTML) {
				process_in(p, p->mode);
				return;
			}
		}
		return;
	}
	return;
breakout:
	while (p->nopen > 0) {
		dnode *n = CUR(p);
		if (mathml_tip(n) || html_ip(n) || n->ns == DN_NS_HTML)
			break;
		pop(p);
	}
	process_in(p, p->mode);
}

/* the tree construction dispatcher */
static void dispatch(P *p)
{
	TOKEN *t = &p->t;
	dnode *n;

	if (p->skip_lf) {
		/* the newline right after <pre>, <listing> or <textarea> */
		if (p->skip_lf == 2 && t->type == TK_CHARS && !t->nul && t->n > 0 && t->s[0] == '\n') {
			t->s++;
			t->n--;
		}
		p->skip_lf = 0;
		if (t->type == TK_CHARS && t->n == 0)
			return;
	}
	n = adjusted_current(p);
	if (n == NULL || n->ns == DN_NS_HTML || t->type == TK_EOF ||
	    (mathml_tip(n) && ((t->type == TK_START && t->hid != H_MGLYPH && t->hid != H_MALIGNMARK) ||
			       t->type == TK_CHARS)) ||
	    (n->ns == DN_NS_MATHML && n->hid == H_ANNOTATION_XML && t->type == TK_START && t->hid == H_SVG) ||
	    (html_ip(n) && (t->type == TK_START || t->type == TK_CHARS)))
		process_in(p, p->mode);
	else
		foreign(p);
}

/* ---------------------------------------------------------------- encodings */

/* the encodings the parser itself tells apart, looked up once (each
   lookup is a search of libparserutils' list of names) */
static uint16_t mib_utf8, mib_utf16, mib_utf16le, mib_utf16be, mib_1252;

static uint16_t mib_of(const char *name)
{
	return parserutils_charset_mibenum_from_name(name, strlen(name));
}

static void mibs_init(void)
{
	mib_utf8 = mib_of("UTF-8");
	mib_utf16 = mib_of("UTF-16");
	mib_utf16le = mib_of("UTF-16LE");
	mib_utf16be = mib_of("UTF-16BE");
	mib_1252 = mib_of("windows-1252");
}

/* WHATWG's "get an encoding" for what libparserutils decodes, and what
   HTML does with it: the MIB enum, or 0. meta: from the page, where
   UTF-16 means UTF-8 and x-user-defined windows-1252 */
static uint16_t enc_of(const uint8_t *s, size_t n, int meta)
{
	static const char *const latin[] = { "iso-8859-1", "us-ascii", "ascii", "latin1", "l1",
		"iso_8859-1", "iso8859-1", "iso88591", "cp819", "ibm819", "csisolatin1",
		"iso-ir-100", "iso_8859-1:1987", "ansi_x3.4-1968", "x-cp1252", "cp1252", NULL };
	static const char *const turkish[] = { "iso-8859-9", "iso8859-9", "iso88599", "latin5",
		"l5", "iso_8859-9", "iso-ir-148", "csisolatin5", "iso_8859-9:1989", NULL };
	char name[48];
	size_t i;
	uint16_t mib;
	parserutils_charset_codec *c;

	while (n > 0 && (WS(*s) || *s == '\r'))
		s++, n--;
	while (n > 0 && (WS(s[n - 1]) || s[n - 1] == '\r'))
		n--;
	if (n == 0 || n >= sizeof name)
		return 0;
	for (i = 0; i < n; i++)
		name[i] = (char) (s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
	name[n] = 0;
	for (i = 0; latin[i]; i++)
		if (strcmp(name, latin[i]) == 0)
			strcpy(name, "windows-1252");
	for (i = 0; turkish[i]; i++)
		if (strcmp(name, turkish[i]) == 0)
			strcpy(name, "windows-1254");
	if (strcmp(name, "x-user-defined") == 0)
		strcpy(name, meta ? "windows-1252" : "x-user-defined");
	if (meta && (strcmp(name, "utf-16") == 0 || strcmp(name, "utf-16le") == 0 ||
		     strcmp(name, "utf-16be") == 0 || strcmp(name, "unicode") == 0 ||
		     strcmp(name, "ucs-2") == 0 || strcmp(name, "unicodefeff") == 0))
		strcpy(name, "utf-8");
	if (strcmp(name, "utf-8") == 0 || strcmp(name, "utf8") == 0)
		return mib_utf8;
	if (strcmp(name, "windows-1252") == 0)
		return mib_1252;
	mib = mib_of(name);
	if (mib == 0)
		return 0;
	if (mib != mib_utf8 && mib != mib_utf16 && mib != mib_utf16le && mib != mib_utf16be) {
		/* one libparserutils decodes? (the last asked about is kept) */
		static uint16_t asked, yes;
		if (mib != asked) {
			asked = mib;
			yes = parserutils_charset_codec_create(parserutils_charset_mibenum_to_name(mib), &c) ==
			      PARSERUTILS_OK;
			if (yes)
				parserutils_charset_codec_destroy(c);
		}
		if (!yes)
			return 0;
	}
	return mib;
}



/* the prescan for a <meta> naming the encoding (13.2.3.2), over the first
   1024 bytes; 0 if none */
static uint16_t prescan(const uint8_t *s, size_t n)
{
	const uint8_t *e = s + (n < 1024 ? n : 1024), *b = s;
#define SPACE(c) ((c) == 9 || (c) == 10 || (c) == 12 || (c) == 13 || (c) == 32)

	while (s < e) {
		if (e - s >= 4 && memcmp(s, "<!--", 4) == 0) {
			s += 2;
			while (s + 2 < e && !(s[0] == '-' && s[1] == '-' && s[2] == '>'))
				s++;
			if (s + 2 >= e)
				break;
			s += 3;
			continue;
		}
		if (e - s >= 6 && s[0] == '<' && ieq("meta", s + 1, 4) && (SPACE(s[5]) || s[5] == '/')) {
			uint8_t names[64][16];
			int nn = 0, got_pragma = 0, need = -1, k;
			uint16_t charset = 0;
			int charset_set = 0;
			s += 5;
			for (;;) {
				/* get an attribute */
				uint8_t an[16], av[64];
				size_t anl = 0, avl = 0, al = 0;
				while (s < e && (SPACE(*s) || *s == '/'))
					s++;
				if (s >= e)
					goto xml;
				if (*s == '>')
					break;
				for (;;) {
					uint8_t c;
					if (s >= e)
						goto xml;
					c = *s;
					if (c == '=' && al > 0) {
						s++;
						goto value;
					}
					if (SPACE(c)) {
						while (s < e && SPACE(*s))
							s++;
						if (s >= e)
							goto xml;
						if (*s != '=')
							goto got;
						s++;
						goto value;
					}
					if (c == '/' || c == '>')
						goto got;
					if (anl < sizeof an)
						an[anl++] = (uint8_t) (c >= 'A' && c <= 'Z' ? c + 32 : c);
					al++;
					s++;
				}
			value:
				while (s < e && SPACE(*s))
					s++;
				if (s >= e)
					goto xml;
				if (*s == '"' || *s == '\'') {
					uint8_t q = *s++;
					while (s < e && *s != q) {
						if (avl < sizeof av)
							av[avl++] = (uint8_t) (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
						s++;
					}
					if (s >= e)
						goto xml;
					s++;
				} else if (*s != '>') {
					while (s < e && !SPACE(*s) && *s != '>') {
						if (avl < sizeof av)
							av[avl++] = (uint8_t) (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
						s++;
					}
					if (s >= e)
						goto xml;
				}
			got:
				if (al > sizeof an)
					anl = 0;	/* too long to be one we want */
				for (k = 0; k < nn; k++)
					if (names[k][0] == anl && memcmp(names[k] + 1, an, anl) == 0)
						break;
				if (k < nn || anl == 0)
					continue;
				if (nn < 64 && anl < 15) {
					names[nn][0] = (uint8_t) anl;
					memcpy(names[nn] + 1, an, anl);
					nn++;
				}
				if (anl == 10 && memcmp(an, "http-equiv", 10) == 0) {
					if (avl == 12 && memcmp(av, "content-type", 12) == 0)
						got_pragma = 1;
				} else if (anl == 7 && memcmp(an, "content", 7) == 0) {
					/* extracting a character encoding from a meta element */
					size_t i = 0;
					while (i + 7 <= avl && !charset_set) {
						size_t j;
						if (memcmp(av + i, "charset", 7) != 0) {
							i++;
							continue;
						}
						j = i + 7;
						while (j < avl && SPACE(av[j]))
							j++;
						if (j >= avl || av[j] != '=') {
							i = j;
							continue;
						}
						j++;
						while (j < avl && SPACE(av[j]))
							j++;
						if (j < avl && (av[j] == '"' || av[j] == '\'')) {
							uint8_t q = av[j++];
							size_t x = j;
							while (x < avl && av[x] != q)
								x++;
							if (x < avl) {
								charset = enc_of(av + j, x - j, 1);
								charset_set = 1;
								need = 1;
							}
						} else {
							size_t x = j;
							while (x < avl && !SPACE(av[x]) && av[x] != ';')
								x++;
							if (x > j) {
								charset = enc_of(av + j, x - j, 1);
								charset_set = 1;
								need = 1;
							}
						}
						break;
					}
				} else if (anl == 7 && memcmp(an, "charset", 7) == 0) {
					charset = enc_of(av, avl, 1);
					charset_set = 1;
					need = 0;
				}
			}
			if (need >= 0 && !(need == 1 && !got_pragma) && charset != 0)
				return charset;
			s++;
			continue;
		}
		if (e - s >= 2 && s[0] == '<' && (ALPHA(s[1]) || (s[1] == '/' && e - s >= 3 && ALPHA(s[2])))) {
			s += s[1] == '/' ? 2 : 1;
			while (s < e && !SPACE(*s) && *s != '>')
				s++;
			/* the attributes, read and dropped */
			for (;;) {
				while (s < e && (SPACE(*s) || *s == '/'))
					s++;
				if (s >= e)
					goto xml;
				if (*s == '>')
					break;
				while (s < e && *s != '=' && !SPACE(*s) && *s != '/' && *s != '>')
					s++;
				while (s < e && SPACE(*s))
					s++;
				if (s < e && *s == '=') {
					s++;
					while (s < e && SPACE(*s))
						s++;
					if (s < e && (*s == '"' || *s == '\'')) {
						uint8_t q = *s++;
						while (s < e && *s != q)
							s++;
						if (s < e)
							s++;
					} else {
						while (s < e && !SPACE(*s) && *s != '>')
							s++;
					}
				}
				if (s >= e)
					goto xml;
			}
			s++;
			continue;
		}
		if (e - s >= 2 && s[0] == '<' && (s[1] == '!' || s[1] == '/' || s[1] == '?')) {
			while (s < e && *s != '>')
				s++;
			if (s >= e)
				break;
		}
		s++;
	}
xml:
	/* get an XML encoding */
	s = b;
	if (e - s >= 5 && memcmp(s, "<?xml", 5) == 0) {
		const uint8_t *end = memchr(s, '>', (size_t) (e - s)), *q;
		if (end == NULL)
			return 0;
		for (q = s; q + 8 <= end; q++)
			if (memcmp(q, "encoding", 8) == 0)
				break;
		if (q + 8 > end)
			return 0;
		q += 8;
		while (q < end && *q <= 0x20)
			q++;
		if (q >= end || *q != '=')
			return 0;
		q++;
		while (q < end && *q <= 0x20)
			q++;
		if (q < end && (*q == '"' || *q == '\'')) {
			const uint8_t *v = q + 1, *x = v;
			while (x < end && *x != *q)
				x++;
			if (x < end) {
				const uint8_t *y;
				for (y = v; y < x; y++)
					if (*y <= 0x20)
						return 0;
				return enc_of(v, (size_t) (x - v), 1);
			}
		}
	}
	return 0;
#undef SPACE
}

/* copies the bytes from s that are ASCII other than CR to o, four at a
   time where longs may be at any address (a 68020 or later); how many */
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__) || \
    defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#define ANY_ADDRESS 1
#endif
static size_t ascii_copy(uint8_t *o, const uint8_t *s, const uint8_t *e)
{
	const uint8_t *b = s;
#ifdef ANY_ADDRESS
	while (e - s >= 4) {
		uint32_t w, x;
		__builtin_memcpy(&w, s, 4);	/* a move, even freestanding */
		x = w ^ 0x0d0d0d0dUL;
		if ((w & 0x80808080UL) || ((x - 0x01010101UL) & ~x & 0x80808080UL))
			break;
		__builtin_memcpy(o, &w, 4);
		s += 4;
		o += 4;
	}
#endif
	while (s < e && *s < 0x80 && *s != '\r')
		*o++ = *s++;
	return (size_t) (s - b);
}

/* The page decoded to UTF-8, its newlines normalised (CR LF and CR are
   LF), with a 0 after it; *n its length. NULL if out of memory. */
static uint8_t *decode(const uint8_t *s, size_t len, uint16_t enc, size_t *n)
{
	const uint8_t *e = s + len;
	uint8_t *out, *o;
	size_t cap;

	if (enc == mib_utf16le || enc == mib_utf16be || enc == mib_utf16) {
		int be = enc != mib_utf16le;
		uint32_t lead = 0;
		int cr = 0;
		cap = len / 2 * 3 + 8;
		if ((out = malloc(cap)) == NULL)
			return NULL;
		o = out;
		while (s + 1 < e) {
			uint32_t u = be ? (uint32_t) (s[0] << 8 | s[1]) : (uint32_t) (s[1] << 8 | s[0]);
			s += 2;
			if (lead) {
				if (u >= 0xdc00 && u <= 0xdfff) {
					o += utf8_put(o, 0x10000 + ((lead - 0xd800) << 10) + (u - 0xdc00));
					lead = 0;
					continue;
				}
				o += utf8_put(o, 0xfffd);
				lead = 0;
			}
			if (u >= 0xd800 && u <= 0xdbff) {
				lead = u;
				continue;
			}
			if (u >= 0xdc00 && u <= 0xdfff)
				u = 0xfffd;
			if (u == '\n' && cr) {
				cr = 0;
				continue;
			}
			cr = u == '\r';
			o += utf8_put(o, cr ? '\n' : u);
		}
		if (lead || s < e)
			o += utf8_put(o, 0xfffd);
	} else if (enc == mib_utf8) {
		/* the WHATWG decoder: each byte that can't start a sequence,
		   and each sequence cut short, is one U+FFFD. Only those make
		   the text longer, so only they ask for room. */
		cap = len + 16;
		if ((out = malloc(cap)) == NULL)
			return NULL;
		o = out;
		while (s < e) {
			uint8_t c;
			uint8_t lo = 0x80, hi = 0xbf;
			size_t k, i, k_ = ascii_copy(o, s, e);
			o += k_;
			s += k_;
			if (s >= e)
				break;
			c = *s;
			if (c < 0x80) {
				if (c == '\r') {
					*o++ = '\n';
					s++;
					if (s < e && *s == '\n')
						s++;
					continue;
				}
				*o++ = c;
				s++;
				continue;
			}
			if (c >= 0xc2 && c <= 0xdf) {
				k = 2;
			} else if (c >= 0xe0 && c <= 0xef) {
				k = 3;
				if (c == 0xe0)
					lo = 0xa0;
				else if (c == 0xed)
					hi = 0x9f;
			} else if (c >= 0xf0 && c <= 0xf4) {
				k = 4;
				if (c == 0xf0)
					lo = 0x90;
				else if (c == 0xf4)
					hi = 0x8f;
			} else {
				k = 0;
			}
			for (i = 1; i < k; i++) {
				if (s + i >= e || s[i] < lo || s[i] > hi)
					break;
				lo = 0x80;
				hi = 0xbf;
			}
			if (k == 0 || i < k) {
				if (cap - (size_t) (o - out) < (size_t) (e - s) + 8) {
					size_t at = (size_t) (o - out);
					uint8_t *m;
					cap = cap * 2 + 16;
					if ((m = realloc(out, cap)) == NULL) {
						free(out);
						return NULL;
					}
					out = m;
					o = out + at;
				}
				o += utf8_put(o, 0xfffd);
				s += k == 0 ? 1 : i;
				continue;
			}
			o[0] = s[0];
			o[1] = s[1];
			if (k > 2) {
				o[2] = s[2];
				if (k > 3)
					o[3] = s[3];
			}
			o += k;
			s += k;
		}
	} else {
		/* a byte a character: a table of 0x80-0xff from the codec, kept
		   for the next page in the same charset */
		static uint8_t tab[128][4], tl[128];
		static uint16_t tab_enc;
		parserutils_charset_codec *c;
		unsigned b;
		if (tab_enc != enc) {
			if (parserutils_charset_codec_create(parserutils_charset_mibenum_to_name(enc), &c) != PARSERUTILS_OK)
				return NULL;
			for (b = 0; b < 128; b++) {
				uint8_t byte = (uint8_t) (0x80 + b), ucs[4], *d = ucs;
				const uint8_t *src = &byte;
				size_t sl = 1, dl = 4;
				uint32_t u = 0xfffd;
				parserutils_charset_codec_reset(c);
				if (parserutils_charset_codec_decode(c, &src, &sl, &d, &dl) == PARSERUTILS_OK && dl == 0)
					u = (uint32_t) ucs[0] << 24 | (uint32_t) ucs[1] << 16 | (uint32_t) ucs[2] << 8 | ucs[3];
				tl[b] = (uint8_t) utf8_put(tab[b], u);
			}
			parserutils_charset_codec_destroy(c);
			tab_enc = enc;
		}
		cap = 8;
		for (; s < e; s++)
			cap += *s < 0x80 ? 1 : tl[*s - 0x80];
		s = e - len;
		if ((out = malloc(cap)) == NULL)
			return NULL;
		o = out;
		while (s < e) {
			uint8_t ch;
			size_t k_ = ascii_copy(o, s, e);
			o += k_;
			s += k_;
			if (s >= e)
				break;
			ch = *s++;
			if (ch < 0x80) {
				if (ch == '\r') {
					ch = '\n';
					if (s < e && *s == '\n')
						s++;
				}
				*o++ = ch;
			} else {
				__builtin_memcpy(o, tab[ch - 0x80], 4);
				o += tl[ch - 0x80];
			}
		}
	}
	*o = 0;
	*n = (size_t) (o - out);
	return out;
}

/* ---------------------------------------------------------------- selectedcontent */

/* a node and what is under it, copied */
static dnode *deep_clone(P *p, const dnode *old)
{
	dnode *n, *c;
	if (old->type == DN_ELEMENT) {
		n = clone(p, old);
	} else {
		n = new_node(p, old->type);
		set_text(p, n, (const uint8_t *) old->text, old->len);
	}
	for (c = old->first; c != NULL; c = c->next)
		link_before(n, deep_clone(p, c), NULL);
	return n;
}

/* the next node in tree order under top, not going into n's children
   when skip */
static dnode *next_in(dnode *n, const dnode *top, int skip)
{
	if (!skip && n->first)
		return n->first;
	while (n != top) {
		if (n->next)
			return n->next;
		n = n->parent;
	}
	return NULL;
}

/* A <select> showing its option in a <selectedcontent> (customizable
   select): what the DOM's insertion steps leave in it once the page is
   parsed, a copy of the selected option's children (4.10.7). */
static void selectedcontent(P *p, dnode *top)
{
	dnode *n;
	const ec_atom *sc = NULL, *sel = NULL, *dis = NULL, *mult = NULL;
	for (n = top; n != NULL; n = next_in(n, top, 0)) {
		dnode *o, *chosen = NULL, *first = NULL;
		if (!is(n, H_SELECT))
			continue;
		if (sc == NULL) {
			sc = atom(p, (const uint8_t *) "selectedcontent", 15);
			sel = atom(p, (const uint8_t *) "selected", 8);
			dis = atom(p, (const uint8_t *) "disabled", 8);
			mult = atom(p, (const uint8_t *) "multiple", 8);
		}
		if (has_attr(n, mult))
			continue;
		for (o = n->first ? n->first : NULL; o != NULL; o = next_in(o, n, is(o, H_SELECT))) {
			if (!is(o, H_OPTION))
				continue;
			if (has_attr(o, sel))
				chosen = o;
			if (first == NULL && !has_attr(o, dis))
				first = o;
		}
		if (chosen == NULL)
			chosen = first;
		for (o = n->first; o != NULL; o = next_in(o, n, 0)) {
			dnode *c;
			if (o->type != DN_ELEMENT || o->ns != DN_NS_HTML || o->e.name != sc)
				continue;
			o->first = o->last = NULL;
			if (chosen != NULL)
				for (c = chosen->first; c != NULL; c = c->next)
					link_before(o, deep_clone(p, c), NULL);
			break;
		}
	}
}

/* ---------------------------------------------------------------- the parse */

static void p_free(P *p)
{
	free(p->sb);
	free(p->ra);
	free(p->ta);
	free(p->open);
	free(p->afe);
	free(p->tmodes);
	free(p->ptc);
}

/* one parse of the decoded page (kept out of run(), whose setjmp would
   keep the tokenizer's variables in memory) */
static __attribute__((noinline)) void parse(P *p, const uint8_t *in, size_t n, const html_opts *o)
{
	GROW(p, p->sb, 0, p->sbcap, 256);	/* never NULL, even for an empty token */
	p->s = in;
	p->e = in + n;
	p->state = S_DATA;
	p->mode = M_INITIAL;
	p->frameset_ok = 1;
	p->root = new_node(p, DN_DOCUMENT);
	p->a_class = atom(p, (const uint8_t *) "class", 5);
	p->a_id = atom(p, (const uint8_t *) "id", 2);
	if (o->context != NULL) {
		/* the fragment case: the context element, and an html element
		   whose children are the fragment */
		size_t cl = strlen(o->context), i;
		uint8_t lower[16];
		int hid = H_NONE;
		dnode *h;
		if (cl < sizeof lower) {
			for (i = 0; i < cl; i++)
				lower[i] = (uint8_t) (o->context[i] >= 'A' && o->context[i] <= 'Z' ?
						      o->context[i] + 32 : o->context[i]);
			hid = hlookup(lower, cl);
		}
		p->ctx = element(p, o->context_ns, hid, (const uint8_t *) o->context, cl, NULL, 0);
		p->d->elements--;
		if (o->context_ns == DN_NS_HTML)
			switch (hid) {
			case H_TITLE:
			case H_TEXTAREA:
				p->state = S_RCDATA;
				break;
			case H_STYLE:
			case H_XMP:
			case H_IFRAME:
			case H_NOEMBED:
			case H_NOFRAMES:
				p->state = S_RAWTEXT;
				break;
			case H_SCRIPT:
				p->state = S_SCRIPT;
				break;
			case H_NOSCRIPT:
				if (p->scripting)
					p->state = S_RAWTEXT;
				break;
			case H_PLAINTEXT:
				p->state = S_PLAINTEXT;
				break;
			}
		h = element(p, DN_NS_HTML, H_HTML, (const uint8_t *) "html", 4, NULL, 0);
		link_before(p->root, h, NULL);
		push(p, h);
		if (is(p->ctx, H_TEMPLATE))
			tmode_push(p, M_IN_TEMPLATE);
		reset_mode(p);
	}
	for (;;) {
		int t = tok(p);
		dispatch(p);
		if (p->stop || t == TK_EOF)
			break;
	}
	if (!p->changed)
		selectedcontent(p, p->root);
	if (o->context != NULL && p->root->first != NULL && !p->changed) {
		/* the fragment's nodes are the html element's children */
		dnode *h = p->root->first, *c;
		unlink_node(h);
		while ((c = h->first) != NULL) {
			unlink_node(c);
			link_before(p->root, c, NULL);
		}
	}
}

/* one parse of the decoded page; 0 if out of memory */
static int run(P *p, const uint8_t *in, size_t n, const html_opts *o)
{
	if (__builtin_setjmp(p->jmp))
		return 0;
	parse(p, in, n, o);
	return 1;
}

int html_parse(doc *d, const char *data, size_t len, const html_opts *o)
{
	const uint8_t *s = (const uint8_t *) data;
	uint16_t enc = 0;
	int tentative = 1, tries;

	if (!ready) {
		names_init();
		runs_init();
		mibs_init();
	}
	/* the encoding sniffing algorithm (13.2.3.2) */
	if (len >= 3 && s[0] == 0xef && s[1] == 0xbb && s[2] == 0xbf) {
		enc = mib_utf8;
		s += 3;
		len -= 3;
		tentative = 0;
	} else if (len >= 2 && s[0] == 0xfe && s[1] == 0xff) {
		enc = mib_utf16be;
		s += 2;
		len -= 2;
		tentative = 0;
	} else if (len >= 2 && s[0] == 0xff && s[1] == 0xfe) {
		enc = mib_utf16le;
		s += 2;
		len -= 2;
		tentative = 0;
	}
	if (enc == 0 && o->charset != NULL && o->charset[0] &&
	    (enc = enc_of((const uint8_t *) o->charset, strlen(o->charset), 0)) != 0)
		tentative = 0;
	if (o->context != NULL)
		tentative = 0;
	else if (enc == 0)
		enc = prescan(s, len);
	if (enc == 0)
		enc = mib_1252;
	if (enc == 0)
		return 0;

	for (tries = 0; tries < 2; tries++) {
		P p;
		size_t n;
		uint8_t *in = decode(s, len, enc, &n);
		int ok;
		if (in == NULL)
			return 0;
		memset(&p, 0, sizeof p);
		p.d = d;
		p.scripting = (uint8_t) (o->scripting != 0);
		p.enc = enc;
		p.tentative = (uint8_t) tentative;
		d->elements = 0;
		ok = run(&p, in, n, o);
		free(in);
		p_free(&p);
		if (!ok)
			return 0;
		d->root = p.root;
		d->quirks = p.quirks;
		if (p.changed && tries == 0) {
			/* a <meta> changed the encoding: start again, certain */
			enc = p.changed;
			tentative = 0;
			continue;
		}
		break;
	}
	{
		const char *name = parserutils_charset_mibenum_to_name(enc);
		strncpy(d->charset, name ? name : "", sizeof d->charset - 1);
		d->charset[sizeof d->charset - 1] = 0;
	}
	return 1;
}

#ifndef EW_ST	/* the ST edition runs no scripts */
dnode *html_fragment(doc *d, const char *context, int context_ns, const char *data, size_t len, int scripting)
{
	html_opts o;
	dnode *root = d->root;
	uint32_t elements = d->elements;
	uint8_t quirks = d->quirks;
	char charset[sizeof d->charset];
	dnode *f = NULL;
	memset(&o, 0, sizeof o);
	o.charset = "utf-8";
	o.scripting = scripting;
	o.context = context;
	o.context_ns = context_ns;
	memcpy(charset, d->charset, sizeof charset);
	if (html_parse(d, data, len, &o))
		f = d->root;
	d->root = root;
	d->quirks = quirks;
	d->elements = elements + (f != NULL ? d->elements : 0);
	memcpy(d->charset, charset, sizeof charset);
	return f;
}

dnode *html_element(doc *d, const char *name, size_t len)
{
	P p;
	dnode *n;
	uint8_t lower[16];
	size_t i;
	int hid = H_NONE;
	if (!ready) {
		names_init();
		runs_init();
		mibs_init();
	}
	if (len < sizeof lower) {
		for (i = 0; i < len; i++)
			lower[i] = (uint8_t) (name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
		hid = hlookup(lower, len);
	}
	memset(&p, 0, sizeof p);
	p.d = d;
	if (__builtin_setjmp(p.jmp))
		return NULL;
	n = element(&p, DN_NS_HTML, hid, hid ? lower : (const uint8_t *) name, len, NULL, 0);
	p_free(&p);
	return n;
}
#endif
