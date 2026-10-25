/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tiny.c - NetSurf's libsvgtiny (third_party/netsurf/libsvgtiny, on
 * EWeb's document through tests/svg/dom.c), its diagram drawn by EWeb's
 * rasteriser: the yardstick EWeb's own SVG reader (src/svg/svgread.c)
 * was measured against (DESIGN.md, SVG), svgrender -T.
 */
#include <stdlib.h>
#include <string.h>

#include "svg.h"
#include "raster.h"
#include "svgtiny.h"

PIC *tiny_decode(const char *data, long len, int w, int h, int *why);

/* the diagram (its own units: diagram->width x height) on a w x h canvas */
static PIC *draw(struct svgtiny_diagram *dg, int w, int h, int *why)
{
	rs_canvas c;
	float sx, sy;
	unsigned i;
	PIC *p;
	long k, n = (long) w * h;
	int any_clear = 0;
	static rs_paint p0;

	p0.kind = RS_SOLID;
	p0.alpha = 255;
	if (!rs_init(&c, w, h)) {
		*why = PIC_MEMORY;
		return NULL;
	}
	sx = dg->width > 0 ? (float) w * RS_ONE / dg->width : RS_ONE;
	sy = dg->height > 0 ? (float) h * RS_ONE / dg->height : RS_ONE;
	for (i = 0; i < dg->shape_count && !c.nomem; i++) {
		const struct svgtiny_shape *s = &dg->shape[i];
		unsigned j = 0;
		if (s->path == NULL)
			continue;	/* (text: not drawn) */
		rs_clear(&c);
		while (j < s->path_length) {
			switch ((int) s->path[j]) {
			case svgtiny_PATH_MOVE:
				rs_move(&c, (int32_t) (s->path[j + 1] * sx), (int32_t) (s->path[j + 2] * sy));
				j += 3;
				break;
			case svgtiny_PATH_LINE:
				rs_line(&c, (int32_t) (s->path[j + 1] * sx), (int32_t) (s->path[j + 2] * sy));
				j += 3;
				break;
			case svgtiny_PATH_BEZIER:
				rs_cubic(&c, (int32_t) (s->path[j + 1] * sx), (int32_t) (s->path[j + 2] * sy),
					 (int32_t) (s->path[j + 3] * sx), (int32_t) (s->path[j + 4] * sy),
					 (int32_t) (s->path[j + 5] * sx), (int32_t) (s->path[j + 6] * sy));
				j += 7;
				break;
			case svgtiny_PATH_CLOSE:
			default:
				rs_close(&c);
				j++;
				break;
			}
		}
		if (s->fill != svgtiny_TRANSPARENT) {
			p0.rgb = (uint32_t) s->fill & 0xffffff;
			rs_fill(&c, 0, &p0);
		}
		if (s->stroke != svgtiny_TRANSPARENT && s->stroke_width > 0) {
			rs_stroke_style ss = { 0, RS_BUTT, RS_ROUND_JOIN, 4 * 65536 };
			ss.width = (int32_t) (s->stroke_width * (sx + sy) / 2);
			p0.rgb = (uint32_t) s->stroke & 0xffffff;
			rs_stroke(&c, &ss, &p0);
		}
	}
	if (c.nomem) {
		rs_free(&c);
		*why = PIC_MEMORY;
		return NULL;
	}
	p = calloc(1, sizeof *p);
	if (p != NULL) {
		p->w = w;
		p->h = h;
		p->how = PIC_SVG;
		p->px = malloc((size_t) n * sizeof *p->px);
		p->alpha = malloc((size_t) n);
	}
	if (p == NULL || p->px == NULL || p->alpha == NULL) {
		pic_free(p);
		rs_free(&c);
		*why = PIC_MEMORY;
		return NULL;
	}
	for (k = 0; k < n; k++) {
		const uint8_t *q = c.px + k * 4;
		unsigned a = q[3], r = 0, g = 0, b = 0;
		if (a > 0) {
			/* (premultiplied: the colour again) */
			r = (q[0] * 255u + a / 2) / a;
			g = (q[1] * 255u + a / 2) / a;
			b = (q[2] * 255u + a / 2) / a;
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (b > 255) b = 255;
		}
		if (a < 255)
			any_clear = 1;
		p->px[k] = (unsigned short) ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
		p->alpha[k] = (unsigned char) a;
	}
	if (!any_clear) {
		free(p->alpha);
		p->alpha = NULL;
	}
	rs_free(&c);
	*why = PIC_OK;
	return p;
}

PIC *tiny_decode(const char *data, long len, int w, int h, int *why)
{
	struct svgtiny_diagram *dg = svgtiny_create();
	svgtiny_code code;
	PIC *p;
	if (dg == NULL) {
		*why = PIC_MEMORY;
		return NULL;
	}
	code = svgtiny_parse(dg, data, (size_t) len, "", w, h);
	if (code != svgtiny_OK) {
		*why = code == svgtiny_OUT_OF_MEMORY ? PIC_MEMORY : PIC_DAMAGED;
		svgtiny_free(dg);
		return NULL;
	}
	p = draw(dg, w, h, why);
	svgtiny_free(dg);
	return p;
}

