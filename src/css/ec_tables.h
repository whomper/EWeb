/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The engine's tables, as X-macros: the properties it computes and the
 * keywords their values use. Included by ecss.h (the enums) and by the
 * engine's sources (the names, the parsers, the initial values). */

/* The properties, inherited ones first (EC_NINHERITED of them), so that a
 * style inherits with one copy of the front of its arrays.
 *
 * P(id, name, parser, flags, initial kind, initial value)
 *   parser  how the value is read (VP_* in ec_int.h)
 *   flags   for VP_LEN: what else it accepts (LF_*); for VP_KW: the
 *           keyword list's number (KL_*) */
#define EC_PROPS_INHERITED(P) \
	P(COLOR,               "color",               VP_COLOR,  0,                   EK_COLOR, 0xff000000) \
	P(FONT_FAMILY,         "font-family",         VP_FAMILY, 0,                   EK_PTR,   0) \
	P(FONT_SIZE,           "font-size",           VP_FSIZE,  0,                   EK_PX,    16 << 10) \
	P(FONT_STYLE,          "font-style",          VP_KW,     KL_FONT_STYLE,       EK_KW,    KW_NORMAL) \
	P(FONT_WEIGHT,         "font-weight",         VP_WEIGHT, 0,                   EK_INT,   400) \
	P(FONT_VARIANT,        "font-variant",        VP_KW,     KL_FONT_VARIANT,     EK_KW,    KW_NORMAL) \
	P(LINE_HEIGHT,         "line-height",         VP_LHEIGHT,0,                   EK_KW,    KW_NORMAL) \
	P(TEXT_ALIGN,          "text-align",          VP_KW,     KL_TEXT_ALIGN,       EK_KW,    KW_START) \
	P(TEXT_INDENT,         "text-indent",         VP_LEN,    0,                   EK_PX,    0) \
	P(TEXT_TRANSFORM,      "text-transform",      VP_KW,     KL_TEXT_TRANSFORM,   EK_KW,    KW_NONE) \
	P(LETTER_SPACING,      "letter-spacing",      VP_LEN,    LF_NORMAL | LF_NOPCT, EK_KW,   KW_NORMAL) \
	P(WORD_SPACING,        "word-spacing",        VP_LEN,    LF_NORMAL,           EK_KW,    KW_NORMAL) \
	P(WHITE_SPACE,         "white-space",         VP_KW,     KL_WHITE_SPACE,      EK_KW,    KW_NORMAL) \
	P(LIST_STYLE_TYPE,     "list-style-type",     VP_LISTTYPE,0,                  EK_KW,    KW_DISC) \
	P(LIST_STYLE_POSITION, "list-style-position", VP_KW,     KL_LIST_POS,         EK_KW,    KW_OUTSIDE) \
	P(LIST_STYLE_IMAGE,    "list-style-image",    VP_IMAGE,  0,                   EK_KW,    KW_NONE) \
	P(VISIBILITY,          "visibility",          VP_KW,     KL_VISIBILITY,       EK_KW,    KW_VISIBLE) \
	P(DIRECTION,           "direction",           VP_KW,     KL_DIRECTION,        EK_KW,    KW_LTR) \
	P(BORDER_COLLAPSE,     "border-collapse",     VP_KW,     KL_BORDER_COLLAPSE,  EK_KW,    KW_SEPARATE) \
	P(BORDER_SPACING_H,    "-ec-border-spacing-h",VP_LEN,    LF_NN | LF_NOPCT,    EK_PX,    0) \
	P(BORDER_SPACING_V,    "-ec-border-spacing-v",VP_LEN,    LF_NN | LF_NOPCT,    EK_PX,    0) \
	P(OVERFLOW_WRAP,       "overflow-wrap",       VP_KW,     KL_OVERFLOW_WRAP,    EK_KW,    KW_NORMAL) \
	P(POINTER_EVENTS,      "pointer-events",      VP_KW,     KL_POINTER_EVENTS,   EK_KW,    KW_AUTO) \
	P(TEXT_SHADOW,         "text-shadow",         VP_SHADOW, 0,                   EK_KW,    KW_NONE) \
	P(TEXT_STROKE_WIDTH,   "-webkit-text-stroke-width", VP_BWIDTH, 0,             EK_PX,    0) \
	P(TEXT_STROKE_COLOR,   "-webkit-text-stroke-color", VP_COLOR,  0,             EK_CURCOLOR, 0)

#define EC_PROPS_OTHER(P) \
	P(DISPLAY,             "display",             VP_KW,     KL_DISPLAY,          EK_KW,    KW_INLINE) \
	P(POSITION,            "position",            VP_KW,     KL_POSITION,         EK_KW,    KW_STATIC) \
	P(FLOAT,               "float",               VP_KW,     KL_FLOAT,            EK_KW,    KW_NONE) \
	P(CLEAR,               "clear",               VP_KW,     KL_CLEAR,            EK_KW,    KW_NONE) \
	P(TOP,                 "top",                 VP_LEN,    LF_AUTO,             EK_KW,    KW_AUTO) \
	P(RIGHT,               "right",               VP_LEN,    LF_AUTO,             EK_KW,    KW_AUTO) \
	P(BOTTOM,              "bottom",              VP_LEN,    LF_AUTO,             EK_KW,    KW_AUTO) \
	P(LEFT,                "left",                VP_LEN,    LF_AUTO,             EK_KW,    KW_AUTO) \
	P(WIDTH,               "width",               VP_LEN,    LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(HEIGHT,              "height",              VP_LEN,    LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(MIN_WIDTH,           "min-width",           VP_LEN,    LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(MIN_HEIGHT,          "min-height",          VP_LEN,    LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(MAX_WIDTH,           "max-width",           VP_LEN,    LF_NONE | LF_NN,     EK_KW,    KW_NONE) \
	P(MAX_HEIGHT,          "max-height",          VP_LEN,    LF_NONE | LF_NN,     EK_KW,    KW_NONE) \
	P(MARGIN_TOP,          "margin-top",          VP_LEN,    LF_AUTO,             EK_PX,    0) \
	P(MARGIN_RIGHT,        "margin-right",        VP_LEN,    LF_AUTO,             EK_PX,    0) \
	P(MARGIN_BOTTOM,       "margin-bottom",       VP_LEN,    LF_AUTO,             EK_PX,    0) \
	P(MARGIN_LEFT,         "margin-left",         VP_LEN,    LF_AUTO,             EK_PX,    0) \
	P(PADDING_TOP,         "padding-top",         VP_LEN,    LF_NN,               EK_PX,    0) \
	P(PADDING_RIGHT,       "padding-right",       VP_LEN,    LF_NN,               EK_PX,    0) \
	P(PADDING_BOTTOM,      "padding-bottom",      VP_LEN,    LF_NN,               EK_PX,    0) \
	P(PADDING_LEFT,        "padding-left",        VP_LEN,    LF_NN,               EK_PX,    0) \
	P(BORDER_TOP_WIDTH,    "border-top-width",    VP_BWIDTH, 0,                   EK_PX,    3 << 10) \
	P(BORDER_RIGHT_WIDTH,  "border-right-width",  VP_BWIDTH, 0,                   EK_PX,    3 << 10) \
	P(BORDER_BOTTOM_WIDTH, "border-bottom-width", VP_BWIDTH, 0,                   EK_PX,    3 << 10) \
	P(BORDER_LEFT_WIDTH,   "border-left-width",   VP_BWIDTH, 0,                   EK_PX,    3 << 10) \
	P(BORDER_TOP_STYLE,    "border-top-style",    VP_KW,     KL_BORDER_STYLE,     EK_KW,    KW_NONE) \
	P(BORDER_RIGHT_STYLE,  "border-right-style",  VP_KW,     KL_BORDER_STYLE,     EK_KW,    KW_NONE) \
	P(BORDER_BOTTOM_STYLE, "border-bottom-style", VP_KW,     KL_BORDER_STYLE,     EK_KW,    KW_NONE) \
	P(BORDER_LEFT_STYLE,   "border-left-style",   VP_KW,     KL_BORDER_STYLE,     EK_KW,    KW_NONE) \
	P(BORDER_TOP_COLOR,    "border-top-color",    VP_COLOR,  0,                   EK_CURCOLOR, 0) \
	P(BORDER_RIGHT_COLOR,  "border-right-color",  VP_COLOR,  0,                   EK_CURCOLOR, 0) \
	P(BORDER_BOTTOM_COLOR, "border-bottom-color", VP_COLOR,  0,                   EK_CURCOLOR, 0) \
	P(BORDER_LEFT_COLOR,   "border-left-color",   VP_COLOR,  0,                   EK_CURCOLOR, 0) \
	P(BACKGROUND_COLOR,    "background-color",    VP_COLOR,  0,                   EK_COLOR, 0x00000000) \
	P(BACKGROUND_IMAGE,    "background-image",    VP_IMAGE,  0,                   EK_KW,    KW_NONE) \
	P(BACKGROUND_REPEAT,   "background-repeat",   VP_KW,     KL_BG_REPEAT,        EK_KW,    KW_REPEAT) \
	P(BACKGROUND_POSITION_X,"background-position-x",VP_BGPOS,0,                   EK_PCT,   0) \
	P(BACKGROUND_POSITION_Y,"background-position-y",VP_BGPOS,1,                   EK_PCT,   0) \
	P(BACKGROUND_SIZE_W,   "-ec-background-size-w",VP_LEN,   LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(BACKGROUND_SIZE_H,   "-ec-background-size-h",VP_LEN,   LF_AUTO | LF_NN,     EK_KW,    KW_AUTO) \
	P(BACKGROUND_CLIP,     "background-clip",     VP_KW,     KL_BG_CLIP,          EK_KW,    KW_BORDER_BOX) \
	P(OVERFLOW_X,          "overflow-x",          VP_KW,     KL_OVERFLOW,         EK_KW,    KW_VISIBLE) \
	P(OVERFLOW_Y,          "overflow-y",          VP_KW,     KL_OVERFLOW,         EK_KW,    KW_VISIBLE) \
	P(Z_INDEX,             "z-index",             VP_INT,    LF_AUTO,             EK_KW,    KW_AUTO) \
	P(VERTICAL_ALIGN,      "vertical-align",      VP_VALIGN, 0,                   EK_KW,    KW_BASELINE) \
	P(TEXT_DECORATION_LINE,"text-decoration-line",VP_DECO,   0,                   EK_INT,   0) \
	P(UNICODE_BIDI,        "unicode-bidi",        VP_KW,     KL_UNICODE_BIDI,     EK_KW,    KW_NORMAL) \
	P(BOX_SIZING,          "box-sizing",          VP_KW,     KL_BOX_SIZING,       EK_KW,    KW_CONTENT_BOX) \
	P(OPACITY,             "opacity",             VP_ALPHA,  0,                   EK_NUM,   1 << 10) \
	P(ANIMATION_NAME,      "animation-name",      VP_ANIM,   0,                   EK_KW,    KW_NONE) \
	P(CONTENT,             "content",             VP_CONTENT,0,                   EK_KW,    KW_NORMAL) \
	P(FLEX_DIRECTION,      "flex-direction",      VP_KW,     KL_FLEX_DIRECTION,   EK_KW,    KW_ROW) \
	P(FLEX_WRAP,           "flex-wrap",           VP_KW,     KL_FLEX_WRAP,        EK_KW,    KW_NOWRAP) \
	P(FLEX_GROW,           "flex-grow",           VP_NUM,    LF_NN,               EK_NUM,   0) \
	P(FLEX_SHRINK,         "flex-shrink",         VP_NUM,    LF_NN,               EK_NUM,   1 << 10) \
	P(FLEX_BASIS,          "flex-basis",          VP_LEN,    LF_AUTO | LF_CONTENT | LF_NN, EK_KW, KW_AUTO) \
	P(ORDER,               "order",               VP_INT,    0,                   EK_INT,   0) \
	P(JUSTIFY_CONTENT,     "justify-content",     VP_KW,     KL_JUSTIFY,          EK_KW,    KW_NORMAL) \
	P(ALIGN_ITEMS,         "align-items",         VP_KW,     KL_ALIGN_ITEMS,      EK_KW,    KW_NORMAL) \
	P(ALIGN_SELF,          "align-self",          VP_KW,     KL_ALIGN_SELF,       EK_KW,    KW_AUTO) \
	P(ALIGN_CONTENT,       "align-content",       VP_KW,     KL_ALIGN_CONTENT,    EK_KW,    KW_NORMAL) \
	P(ROW_GAP,             "row-gap",             VP_LEN,    LF_NORMAL | LF_NN,   EK_KW,    KW_NORMAL) \
	P(COLUMN_GAP,          "column-gap",          VP_LEN,    LF_NORMAL | LF_NN,   EK_KW,    KW_NORMAL) \
	P(GRID_TEMPLATE_COLUMNS, "grid-template-columns", VP_TRACKS, 0,               EK_KW,    KW_NONE) \
	P(GRID_TEMPLATE_ROWS,  "grid-template-rows",  VP_TRACKS, 0,                   EK_KW,    KW_NONE) \
	P(GRID_TEMPLATE_AREAS, "grid-template-areas", VP_AREAS,  0,                   EK_KW,    KW_NONE) \
	P(GRID_AUTO_COLUMNS,   "grid-auto-columns",   VP_TRACKS, 1,                   EK_KW,    KW_AUTO) \
	P(GRID_AUTO_ROWS,      "grid-auto-rows",      VP_TRACKS, 1,                   EK_KW,    KW_AUTO) \
	P(GRID_AUTO_FLOW,      "grid-auto-flow",      VP_AUTOFLOW, 0,                 EK_INT,   0) \
	P(GRID_ROW_START,      "grid-row-start",      VP_GRIDLINE, 0,                 EK_KW,    KW_AUTO) \
	P(GRID_ROW_END,        "grid-row-end",        VP_GRIDLINE, 0,                 EK_KW,    KW_AUTO) \
	P(GRID_COLUMN_START,   "grid-column-start",   VP_GRIDLINE, 0,                 EK_KW,    KW_AUTO) \
	P(GRID_COLUMN_END,     "grid-column-end",     VP_GRIDLINE, 0,                 EK_KW,    KW_AUTO) \
	P(JUSTIFY_ITEMS,       "justify-items",       VP_KW,     KL_JUSTIFY_ITEMS,    EK_KW,    KW_NORMAL) \
	P(JUSTIFY_SELF,        "justify-self",        VP_KW,     KL_JUSTIFY_SELF,     EK_KW,    KW_AUTO) \
	P(TABLE_LAYOUT,        "table-layout",        VP_KW,     KL_TABLE_LAYOUT,     EK_KW,    KW_AUTO)

/* The shorthands: S(id, name). Each expands into longhands when parsed. */
#define EC_SHORTHANDS(S) \
	S(MARGIN, "margin") S(PADDING, "padding") S(BORDER, "border") \
	S(BORDER_TOP, "border-top") S(BORDER_RIGHT, "border-right") \
	S(BORDER_BOTTOM, "border-bottom") S(BORDER_LEFT, "border-left") \
	S(BORDER_WIDTH, "border-width") S(BORDER_STYLE, "border-style") \
	S(BORDER_COLOR, "border-color") S(BACKGROUND, "background") \
	S(BACKGROUND_POSITION, "background-position") S(BACKGROUND_SIZE, "background-size") \
	S(FONT, "font") \
	S(LIST_STYLE, "list-style") S(FLEX, "flex") S(FLEX_FLOW, "flex-flow") \
	S(GAP, "gap") S(GRID_GAP, "grid-gap") S(OVERFLOW, "overflow") \
	S(TEXT_DECORATION, "text-decoration") S(INSET, "inset") \
	S(BORDER_SPACING, "border-spacing") S(WORD_WRAP, "word-wrap") \
	S(GRID_ROW, "grid-row") S(GRID_COLUMN, "grid-column") S(GRID_AREA, "grid-area") \
	S(GRID_TEMPLATE, "grid-template") \
	S(WEBKIT_BACKGROUND_CLIP, "-webkit-background-clip") S(WEBKIT_TEXT_STROKE, "-webkit-text-stroke") \
	S(ANIMATION, "animation")

/* The keywords: K(id, text). KW_NONE_ (0) is "no keyword". The last few
 * are HTML's: -webkit-left, -right and -center the text-align <center> and
 * align give, which also place the blocks inside (the HTML Standard,
 * 15.3.3; -moz- spells them too), -ec-center-or-inherit th's (15.3.8),
 * and -webkit-baseline-middle <img align=middle>'s vertical-align (15.4.3). */
#define EC_KEYWORDS(K) \
	K(AUTO, "auto") K(NONE, "none") K(NORMAL, "normal") K(INHERIT, "inherit") \
	K(COVER, "cover") K(CONTAIN, "contain") \
	K(INITIAL, "initial") K(UNSET, "unset") K(REVERT, "revert") K(REVERT_LAYER, "revert-layer") \
	K(INLINE, "inline") K(BLOCK, "block") K(LIST_ITEM, "list-item") K(RUN_IN, "run-in") \
	K(INLINE_BLOCK, "inline-block") K(TABLE, "table") K(INLINE_TABLE, "inline-table") \
	K(TABLE_ROW_GROUP, "table-row-group") K(TABLE_HEADER_GROUP, "table-header-group") \
	K(TABLE_FOOTER_GROUP, "table-footer-group") K(TABLE_ROW, "table-row") \
	K(TABLE_COLUMN_GROUP, "table-column-group") K(TABLE_COLUMN, "table-column") \
	K(TABLE_CELL, "table-cell") K(TABLE_CAPTION, "table-caption") K(FLEX, "flex") \
	K(INLINE_FLEX, "inline-flex") K(GRID, "grid") K(INLINE_GRID, "inline-grid") \
	K(FLOW_ROOT, "flow-root") K(CONTENTS, "contents") K(CONTENT, "content") \
	K(STATIC, "static") K(RELATIVE, "relative") K(ABSOLUTE, "absolute") K(FIXED, "fixed") \
	K(STICKY, "sticky") K(LEFT, "left") K(RIGHT, "right") K(BOTH, "both") \
	K(INLINE_START, "inline-start") K(INLINE_END, "inline-end") \
	K(VISIBLE, "visible") K(HIDDEN, "hidden") K(SCROLL, "scroll") K(CLIP, "clip") \
	K(COLLAPSE, "collapse") K(SEPARATE, "separate") \
	K(PRE, "pre") K(NOWRAP, "nowrap") K(PRE_WRAP, "pre-wrap") K(PRE_LINE, "pre-line") \
	K(BREAK_SPACES, "break-spaces") \
	K(CENTER, "center") K(JUSTIFY, "justify") K(START, "start") K(END, "end") \
	K(MATCH_PARENT, "match-parent") \
	K(CAPITALIZE, "capitalize") K(UPPERCASE, "uppercase") K(LOWERCASE, "lowercase") \
	K(ITALIC, "italic") K(OBLIQUE, "oblique") K(SMALL_CAPS, "small-caps") \
	K(BOLD, "bold") K(BOLDER, "bolder") K(LIGHTER, "lighter") \
	K(LTR, "ltr") K(RTL, "rtl") K(EMBED, "embed") K(ISOLATE, "isolate") \
	K(BIDI_OVERRIDE, "bidi-override") K(ISOLATE_OVERRIDE, "isolate-override") \
	K(PLAINTEXT, "plaintext") \
	K(DISC, "disc") K(CIRCLE, "circle") K(SQUARE, "square") K(DECIMAL, "decimal") \
	K(DECIMAL_LEADING_ZERO, "decimal-leading-zero") K(LOWER_ROMAN, "lower-roman") \
	K(UPPER_ROMAN, "upper-roman") K(LOWER_GREEK, "lower-greek") K(LOWER_LATIN, "lower-latin") \
	K(UPPER_LATIN, "upper-latin") K(LOWER_ALPHA, "lower-alpha") K(UPPER_ALPHA, "upper-alpha") \
	K(ARMENIAN, "armenian") K(GEORGIAN, "georgian") K(HEBREW, "hebrew") \
	K(DISCLOSURE_OPEN, "disclosure-open") K(DISCLOSURE_CLOSED, "disclosure-closed") \
	K(INSIDE, "inside") K(OUTSIDE, "outside") \
	K(DOTTED, "dotted") K(DASHED, "dashed") K(SOLID, "solid") K(DOUBLE, "double") \
	K(GROOVE, "groove") K(RIDGE, "ridge") K(INSET, "inset") K(OUTSET, "outset") \
	K(THIN, "thin") K(MEDIUM, "medium") K(THICK, "thick") \
	K(REPEAT, "repeat") K(REPEAT_X, "repeat-x") K(REPEAT_Y, "repeat-y") \
	K(NO_REPEAT, "no-repeat") K(SPACE, "space") K(ROUND, "round") \
	K(TOP, "top") K(BOTTOM, "bottom") \
	K(BASELINE, "baseline") K(SUB, "sub") K(SUPER, "super") K(TEXT_TOP, "text-top") \
	K(MIDDLE, "middle") K(TEXT_BOTTOM, "text-bottom") \
	K(UNDERLINE, "underline") K(OVERLINE, "overline") K(LINE_THROUGH, "line-through") \
	K(BLINK, "blink") \
	K(CONTENT_BOX, "content-box") K(BORDER_BOX, "border-box") K(PADDING_BOX, "padding-box") \
	K(ROW, "row") K(ROW_REVERSE, "row-reverse") K(COLUMN, "column") \
	K(COLUMN_REVERSE, "column-reverse") K(WRAP, "wrap") K(WRAP_REVERSE, "wrap-reverse") \
	K(FLEX_START, "flex-start") K(FLEX_END, "flex-end") K(SPACE_BETWEEN, "space-between") \
	K(SPACE_AROUND, "space-around") K(SPACE_EVENLY, "space-evenly") K(STRETCH, "stretch") \
	K(SELF_START, "self-start") K(SELF_END, "self-end") \
	K(XX_SMALL, "xx-small") K(X_SMALL, "x-small") K(SMALL, "small") K(LARGE, "large") \
	K(X_LARGE, "x-large") K(XX_LARGE, "xx-large") K(XXX_LARGE, "xxx-large") \
	K(LARGER, "larger") K(SMALLER, "smaller") \
	K(BREAK_WORD, "break-word") K(ANYWHERE, "anywhere") \
	K(SERIF, "serif") K(SANS_SERIF, "sans-serif") K(MONOSPACE, "monospace") \
	K(CURSIVE, "cursive") K(FANTASY, "fantasy") K(SYSTEM_UI, "system-ui") \
	K(UI_SERIF, "ui-serif") K(UI_SANS_SERIF, "ui-sans-serif") K(UI_MONOSPACE, "ui-monospace") \
	K(UI_ROUNDED, "ui-rounded") K(MATH, "math") K(EMOJI, "emoji") \
	K(CAPTION, "caption") K(ICON, "icon") K(MENU, "menu") K(MESSAGE_BOX, "message-box") \
	K(SMALL_CAPTION, "small-caption") K(STATUS_BAR, "status-bar") \
	K(CURRENTCOLOR, "currentcolor") K(TRANSPARENT, "transparent") \
	K(LOCAL, "local") \
	K(OPEN_QUOTE, "open-quote") K(CLOSE_QUOTE, "close-quote") \
	K(NO_OPEN_QUOTE, "no-open-quote") K(NO_CLOSE_QUOTE, "no-close-quote") \
	K(CONDENSED, "condensed") K(EXPANDED, "expanded") K(SEMI_CONDENSED, "semi-condensed") \
	K(SEMI_EXPANDED, "semi-expanded") K(ULTRA_CONDENSED, "ultra-condensed") \
	K(EXTRA_CONDENSED, "extra-condensed") K(EXTRA_EXPANDED, "extra-expanded") \
	K(ULTRA_EXPANDED, "ultra-expanded") K(WAVY, "wavy") K(FROM_FONT, "from-font") \
	K(SAFE, "safe") K(UNSAFE, "unsafe") K(FIRST, "first") K(LAST, "last") \
	K(MIN_CONTENT, "min-content") K(MAX_CONTENT, "max-content") K(FIT_CONTENT, "fit-content") \
	K(AUTO_FILL, "auto-fill") K(AUTO_FIT, "auto-fit") K(SPAN, "span") K(DENSE, "dense") \
	K(TEXT, "text") K(ALL, "all") K(FILL, "fill") K(STROKE, "stroke") K(PAINTED, "painted") \
	K(VISIBLEPAINTED, "visiblepainted") K(VISIBLEFILL, "visiblefill") \
	K(VISIBLESTROKE, "visiblestroke") K(BOUNDING_BOX, "bounding-box") \
	K(WEBKIT_LEFT, "-webkit-left") K(WEBKIT_RIGHT, "-webkit-right") \
	K(WEBKIT_CENTER, "-webkit-center") K(EC_CENTER_OR_INHERIT, "-ec-center-or-inherit") \
	K(WEBKIT_BASELINE_MIDDLE, "-webkit-baseline-middle")

/* The keyword lists of VP_KW properties: L(id, keywords...) */
#define EC_KWLISTS(L) \
	L(FONT_STYLE, KW_NORMAL, KW_ITALIC, KW_OBLIQUE) \
	L(FONT_VARIANT, KW_NORMAL, KW_SMALL_CAPS) \
	L(TEXT_ALIGN, KW_LEFT, KW_RIGHT, KW_CENTER, KW_JUSTIFY, KW_START, KW_END, KW_MATCH_PARENT, \
		KW_WEBKIT_LEFT, KW_WEBKIT_RIGHT, KW_WEBKIT_CENTER, KW_EC_CENTER_OR_INHERIT) \
	L(TEXT_TRANSFORM, KW_NONE, KW_CAPITALIZE, KW_UPPERCASE, KW_LOWERCASE) \
	L(WHITE_SPACE, KW_NORMAL, KW_PRE, KW_NOWRAP, KW_PRE_WRAP, KW_PRE_LINE, KW_BREAK_SPACES) \
	L(LIST_POS, KW_INSIDE, KW_OUTSIDE) \
	L(VISIBILITY, KW_VISIBLE, KW_HIDDEN, KW_COLLAPSE) \
	L(DIRECTION, KW_LTR, KW_RTL) \
	L(BORDER_COLLAPSE, KW_COLLAPSE, KW_SEPARATE) \
	L(OVERFLOW_WRAP, KW_NORMAL, KW_BREAK_WORD, KW_ANYWHERE) \
	L(DISPLAY, KW_INLINE, KW_BLOCK, KW_LIST_ITEM, KW_RUN_IN, KW_INLINE_BLOCK, KW_TABLE, \
		KW_INLINE_TABLE, KW_TABLE_ROW_GROUP, KW_TABLE_HEADER_GROUP, KW_TABLE_FOOTER_GROUP, \
		KW_TABLE_ROW, KW_TABLE_COLUMN_GROUP, KW_TABLE_COLUMN, KW_TABLE_CELL, \
		KW_TABLE_CAPTION, KW_NONE, KW_FLEX, KW_INLINE_FLEX, KW_GRID, KW_INLINE_GRID, \
		KW_FLOW_ROOT, KW_CONTENTS) \
	L(POSITION, KW_STATIC, KW_RELATIVE, KW_ABSOLUTE, KW_FIXED, KW_STICKY) \
	L(FLOAT, KW_LEFT, KW_RIGHT, KW_NONE, KW_INLINE_START, KW_INLINE_END) \
	L(CLEAR, KW_NONE, KW_LEFT, KW_RIGHT, KW_BOTH, KW_INLINE_START, KW_INLINE_END) \
	L(BORDER_STYLE, KW_NONE, KW_HIDDEN, KW_DOTTED, KW_DASHED, KW_SOLID, KW_DOUBLE, \
		KW_GROOVE, KW_RIDGE, KW_INSET, KW_OUTSET) \
	L(BG_REPEAT, KW_REPEAT, KW_REPEAT_X, KW_REPEAT_Y, KW_NO_REPEAT, KW_SPACE, KW_ROUND) \
	L(BG_CLIP, KW_BORDER_BOX, KW_PADDING_BOX, KW_CONTENT_BOX, KW_TEXT) \
	L(POINTER_EVENTS, KW_AUTO, KW_NONE, KW_VISIBLEPAINTED, KW_VISIBLEFILL, KW_VISIBLESTROKE, \
		KW_VISIBLE, KW_PAINTED, KW_FILL, KW_STROKE, KW_ALL, KW_BOUNDING_BOX) \
	L(OVERFLOW, KW_VISIBLE, KW_HIDDEN, KW_SCROLL, KW_AUTO, KW_CLIP) \
	L(UNICODE_BIDI, KW_NORMAL, KW_EMBED, KW_ISOLATE, KW_BIDI_OVERRIDE, KW_ISOLATE_OVERRIDE, \
		KW_PLAINTEXT) \
	L(BOX_SIZING, KW_CONTENT_BOX, KW_BORDER_BOX) \
	L(FLEX_DIRECTION, KW_ROW, KW_ROW_REVERSE, KW_COLUMN, KW_COLUMN_REVERSE) \
	L(FLEX_WRAP, KW_NOWRAP, KW_WRAP, KW_WRAP_REVERSE) \
	L(JUSTIFY, KW_NORMAL, KW_FLEX_START, KW_FLEX_END, KW_CENTER, KW_SPACE_BETWEEN, \
		KW_SPACE_AROUND, KW_SPACE_EVENLY, KW_START, KW_END, KW_LEFT, KW_RIGHT, KW_STRETCH) \
	L(ALIGN_ITEMS, KW_NORMAL, KW_STRETCH, KW_FLEX_START, KW_FLEX_END, KW_CENTER, \
		KW_BASELINE, KW_START, KW_END, KW_SELF_START, KW_SELF_END) \
	L(ALIGN_SELF, KW_AUTO, KW_NORMAL, KW_STRETCH, KW_FLEX_START, KW_FLEX_END, KW_CENTER, \
		KW_BASELINE, KW_START, KW_END, KW_SELF_START, KW_SELF_END) \
	L(ALIGN_CONTENT, KW_NORMAL, KW_STRETCH, KW_FLEX_START, KW_FLEX_END, KW_CENTER, \
		KW_SPACE_BETWEEN, KW_SPACE_AROUND, KW_SPACE_EVENLY, KW_START, KW_END, KW_BASELINE) \
	L(TABLE_LAYOUT, KW_AUTO, KW_FIXED) \
	L(JUSTIFY_ITEMS, KW_NORMAL, KW_STRETCH, KW_START, KW_END, KW_CENTER, KW_FLEX_START, \
		KW_FLEX_END, KW_SELF_START, KW_SELF_END, KW_LEFT, KW_RIGHT, KW_BASELINE) \
	L(JUSTIFY_SELF, KW_AUTO, KW_NORMAL, KW_STRETCH, KW_START, KW_END, KW_CENTER, KW_FLEX_START, \
		KW_FLEX_END, KW_SELF_START, KW_SELF_END, KW_LEFT, KW_RIGHT, KW_BASELINE) \
	L(LIST_TYPE, KW_DISC, KW_CIRCLE, KW_SQUARE, KW_DECIMAL, KW_DECIMAL_LEADING_ZERO, \
		KW_LOWER_ROMAN, KW_UPPER_ROMAN, KW_LOWER_GREEK, KW_LOWER_LATIN, KW_UPPER_LATIN, \
		KW_LOWER_ALPHA, KW_UPPER_ALPHA, KW_ARMENIAN, KW_GEORGIAN, KW_HEBREW, KW_DISCLOSURE_OPEN, \
		KW_DISCLOSURE_CLOSED, KW_NONE)
