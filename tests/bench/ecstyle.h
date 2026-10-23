/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench's CSS with EWeb's own engine (src/css). */
#ifndef NSBENCH_ECSTYLE_H
#define NSBENCH_ECSTYLE_H

#include "dom.h"
#include "style.h"

struct ec_ctx;

extern unsigned ecs_opts;	/* ec_create()'s options */
extern void (*ecs_setup)(struct ec_ctx *c);	/* if set, called on each context made */

/* the UA sheet, the author sheets and the page's <style> elements */
struct ec_ctx *ecs_sheets(node *doc, const char **data, const size_t *len, int n, style_stats *st);
/* every element's style: each node's ecn is its ec_node, with the style */
void ecs_select(struct ec_ctx *c, node *doc, style_stats *st);
void ecs_free(struct ec_ctx *c, node *doc);

#endif
