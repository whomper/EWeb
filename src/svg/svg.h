/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * svg.h - SVG pictures: an SVG file (an <img>'s, a background's) and a
 * page's inline <svg>, read from EWeb's own document (svgread.c; a file
 * parsed by EWeb's HTML parser, as a page's inline SVG is) and drawn by
 * raster.c, as a picture of the family's kind (common's pic.h) at the
 * size it is shown.
 */
#ifndef EW_SVG_H
#define EW_SVG_H

#include "doc.h"
#include "pic.h"

/* an SVG file's own size in CSS px (its root's width and height, else its
   viewBox's proportions on 300 wide, else 300 x 150): 0 when it isn't one */
int svg_size(const char *data, long len, long *w, long *h);
/* an SVG whose <svg> comes past pic_kind's first 1024 bytes (a long
   XML prolog, an editor's comments): it starts as XML does */
int svg_late(const char *data, long len);
/* an SVG file drawn at w x h; NULL, and why (pic.h's PIC_*) */
PIC *svg_decode(const char *data, long len, int w, int h, int *why);
/* an <svg> element (top: its document, for ids) drawn at w x h, color
   its currentColor (0xRRGGBB): svgread.c */
PIC *svg_render(const dnode *svg, const dnode *top, int w, int h, uint32_t color, int *why);
/* a page's <svg> element written out as an SVG file (malloc'ed, *len
   long; NULL when out of memory): its markup, with color (0xRRGGBB) its
   currentColor, and the elements elsewhere in the page that it names
   (<use href="#icon">, url(#gradient)) in a <defs> */
char *svg_markup(const dnode *n, uint32_t color, size_t *len);
/* an <svg> element's own size, as svg_size's */
void svg_node_size(const dnode *n, long *w, long *h);

#endif
