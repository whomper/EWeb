/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * SVG pictures from the fuzz input (src/svg: the file parsed by EWeb's
 * HTML parser, read by svgread.c and drawn by raster.c): the first two
 * bytes are the size it is drawn at (1 to 64 each way), the rest the
 * file. Its own size must be one (svg_size), the picture that size, and
 * the page's inline <svg>s, written out again (svg_markup), drawn too.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "svg.h"

void pic_free(PIC *p)
{
	if (p != NULL) {
		free(p->px);
		free(p->alpha);
		free(p);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	int w, h, why;
	long sw, sh;
	PIC *p;
	if (n < 2)
		return 0;
	w = 1 + (d[0] & 63);
	h = 1 + (d[1] & 63);
	if (svg_size((const char *) d + 2, (long) n - 2, &sw, &sh) && (sw < 1 || sh < 1))
		abort();
	(void) svg_late((const char *) d + 2, (long) n - 2);
	p = svg_decode((const char *) d + 2, (long) n - 2, w, h, &why);
	if (p != NULL && (p->w != w || p->h != h || p->px == NULL))
		abort();
	pic_free(p);
	{
		/* as a page's: each inline <svg> written out and drawn */
		static const ec_media m = { 640, 400, 16 };
		doc *doc = doc_parse((const char *) d + 2, n - 2, "UTF-8", &m);
		const dnode *k;
		int count = 0;
		for (k = doc != NULL ? doc->root : NULL; k != NULL && count < 4; ) {
			if (k->type == DN_ELEMENT && k->ns == DN_NS_SVG && k->tag == T_SVG) {
				size_t len;
				char *mk = svg_markup(k, 0x123456, &len);
				count++;
				if (mk != NULL) {
					if (strlen(mk) != len)
						abort();
					pic_free(svg_decode(mk, (long) len, w, h, &why));
					free(mk);
				}
			}
			if (k->first != NULL) {
				k = k->first;
				continue;
			}
			while (k != NULL && k->next == NULL)
				k = k->parent;
			if (k != NULL)
				k = k->next;
		}
		doc_free(doc);
	}
	return 0;
}
