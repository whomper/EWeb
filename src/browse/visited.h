/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * visited.h - a page's links marked visited or not (EC_NODE_VISITED)
 * from the history (history.h), before the page is styled: each href
 * resolved against the page's address as following it would, and looked
 * for among the pages shown.
 */
#ifndef VISITED_H
#define VISITED_H

#include "doc.h"
#include "url.h"

/* a link on a file: page (or to one) made absolute: browse's */
typedef void (*VIS_FILE)(const char *base, const char *href, char *out, int size);

/* d's links against base (the page's address, or its <base>), their
   queries in enc (NULL: UTF-8), file: ones through file (NULL: not
   visited); how many changed (dry: only counted), -1 out of memory */
long vis_mark(doc *d, const char *base, const URL_ENC *enc, VIS_FILE file, int dry);

#endif
