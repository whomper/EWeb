/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's CSS with EWeb's own engine (src/css). */
#ifndef NSBENCH_ECSTYLE_H
#define NSBENCH_ECSTYLE_H

#include "dom.h"
#include "style.h"

struct ec_ctx;

extern unsigned ecs_opts;	/* ec_create()'s options */
extern int ecs_visited;		/* ectest's: a link to "visited:..." has been visited */
extern void (*ecs_setup)(struct ec_ctx *c);	/* if set, called on each context made */

/* the UA sheet, the author sheets and the page's <style> elements */
struct ec_ctx *ecs_sheets(node *doc, const char **data, const size_t *len, int n, style_stats *st);
/* a sheet added: ec_sheet's, or with ecs_piece > 0 an author's read as
   it comes (ec_sheet_feed), ecs_piece bytes at a time */
extern int ecs_piece;
int ecs_add(struct ec_ctx *c, const char *data, size_t len, int origin);
/* every element's style: each node's ecn is its ec_node, with the style */
void ecs_select(struct ec_ctx *c, node *doc, style_stats *st);
void ecs_free(struct ec_ctx *c, node *doc);

#endif
