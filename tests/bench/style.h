/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's CSS: LibCSS stylesheets and a selection handler on dom.h's tree. */
#ifndef NSBENCH_STYLE_H
#define NSBENCH_STYLE_H

#include <stddef.h>
#include <stdint.h>
#include "dom.h"

typedef struct style_stats {
	uint32_t sheets, sheet_bytes;	/* UA, author files and <style> */
	uint32_t inline_styles;		/* style="" attributes */
	uint32_t selected;		/* elements given a style */
	uint32_t failed;
} style_stats;

struct css_select_ctx;

/* Parses the UA sheet, the author sheets (files[], data and lengths) and
 * the page's <style> elements into a selection context. */
struct css_select_ctx *style_sheets(node *doc, const char **data, const size_t *len,
		int n, style_stats *st);
/* Selects and composes a style for every element, parents first, as
 * NetSurf's nscss_get_style does. */
void style_select(struct css_select_ctx *ctx, node *doc, style_stats *st);
/* Frees the styles, LibCSS's node data and the context with its sheets. */
void style_free(struct css_select_ctx *ctx, node *doc);

#endif
