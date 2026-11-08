/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pagecol.h - EWeb's own 256 colours (Settings > Display > EWeb's
 * colours: ui/screen.c), and the page's own colours on some of them
 * (Erez, 8 Nov 2026: a page's buttons, cards and gradients in their own
 * colours, not the nearest of a fixed set).
 *
 * Pixel values 16 to 254 are EWeb's (0 to 15 and 255 the system's):
 * 16 + k for k of EW_OWN (239). The first EW_FIXED of them are the same
 * on every page, and the pictures are made of those alone (with the
 * system's: pixel values 0 to EW_PIC_PENS - 1), so a picture's pens never
 * change with the page; the last EW_PAGE are the page's own colours,
 * set for the page in front.
 */
#ifndef EW_PAGECOL_H
#define EW_PAGECOL_H

#include <stdint.h>

#include "layout.h"

#define EW_OWN 239
#define EW_FIXED 203			/* a 6 x 6 x 5 cube (180) and 23 greys */
#define EW_PAGE (EW_OWN - EW_FIXED)	/* 36 */
#define EW_PIC_PENS (16 + EW_FIXED)	/* pixel values 0..218: the pictures' */

/* EWeb's colour k of EW_FIXED, 0..255 a part */
void ew_own_colour(int k, uint8_t rgb[3]);

/* the colours p shows most that pal (n pens, 0..255 a part) lacks, at
   most max into out (0xRRGGBB), the most wanted first; how many. A
   colour is wanted by how much of the page it covers and how far it is
   from the nearest pen: a fill's or a gradient's area (its stops and the
   colours between them), a text's, a control's, the page's background;
   one less than solid as it shows on the page's background. A colour
   close to a pen, or to one already chosen, isn't chosen. */
int page_colours(const page *p, const uint8_t (*pal)[3], int n, uint32_t *out, int max);

#endif
