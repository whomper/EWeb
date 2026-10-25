/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * raster.h - paths filled and stroked into a picture, anti-aliased (fresh
 * code: NetSurf's front ends draw paths with the system's own calls). In
 * whole numbers for the 68030: a point is in 256ths of a pixel, a pixel's
 * coverage is the exact area a path covers in it (FreeType's way: each
 * cell an edge passes through keeps what the edge covers in it and what
 * it passes on to its right), worked out a band of rows at a time, so a
 * big picture needs little memory beyond its own pixels.
 */
#ifndef EW_RASTER_H
#define EW_RASTER_H

#include <stdint.h>

#define RS_ONE 256			/* a pixel, in a point's units */

typedef struct rs_edge {
	int32_t x0, y0, x1, y1;
} rs_edge;

/* what a path is painted with */
enum { RS_SOLID, RS_LINEAR, RS_RADIAL };
enum { RS_PAD, RS_REFLECT, RS_REPEAT };
typedef struct rs_paint {
	int kind;
	uint32_t rgb;			/* RS_SOLID: 0xRRGGBB */
	unsigned alpha;			/* 0..255, over the whole paint */
	/* a gradient: its colours (0xAARRGGBB) from its start (0) to its
	   end (255), and where a pixel's centre (x, y, 16.16 pixels) is along
	   it: RS_LINEAR t = m[0] x + m[1] y + m[2]; RS_RADIAL (u, v) =
	   (m[0] x + m[1] y + m[2], m[3] x + m[4] y + m[5]), t its distance
	   from (0, 0); t 1.0 (16.16) at the end */
	uint32_t ramp[256];
	int32_t m[6];
	int spread;
} rs_paint;

/* how a path's lines are stroked */
enum { RS_BUTT, RS_ROUND_CAP, RS_SQUARE };
enum { RS_MITER, RS_ROUND_JOIN, RS_BEVEL };
typedef struct rs_stroke_style {
	int32_t width;			/* a point's units */
	int cap, join;
	int32_t miter;			/* the miter limit, 16.16 */
} rs_stroke_style;

typedef struct rs_canvas {
	int w, h;
	/* each pixel's colour premultiplied by its alpha, 0..255 each: R, G,
	   B, A */
	uint8_t *px;
	/* the path: its points (x, y) in lines, curves flattened, and each
	   subpath's first point (bit 30 set: it was closed) */
	int32_t *pt;
	long npt, ptcap;
	long *sub;
	long nsub, subcap;
	/* what a fill is made of: the edges, and a band's cells (a cover
	   and an area for each pixel of BAND rows) */
	rs_edge *e;
	long ne, ecap;
	int32_t *cell;
	int nomem;
} rs_canvas;

/* a canvas of w x h pixels, all clear; 0 if out of memory */
int rs_init(rs_canvas *c, int w, int h);
void rs_free(rs_canvas *c);

/* the path: a subpath begun, lines and cubic curves added, closed */
void rs_move(rs_canvas *c, int32_t x, int32_t y);
void rs_line(rs_canvas *c, int32_t x, int32_t y);
void rs_cubic(rs_canvas *c, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3);
void rs_close(rs_canvas *c);
/* the path emptied */
void rs_clear(rs_canvas *c);

/* the path filled (each subpath closed), by the nonzero rule (a point any
   winding goes round is in it) or the even-odd one (evenodd 1) */
void rs_fill(rs_canvas *c, int evenodd, const rs_paint *p);
/* the path's lines stroked */
void rs_stroke(rs_canvas *c, const rs_stroke_style *s, const rs_paint *p);

#endif
