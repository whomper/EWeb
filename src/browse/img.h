/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * img.h - the pictures pages show (img.c): each address once, its file
 * as it came (kept, to decode it again at another size), and the
 * picture decoded at each size a page shows it (common/src/pic.c), with
 * its pens for the screen's colours. browse.c fetches the files after a
 * page is shown; the canvas draws what is decoded.
 */
#ifndef EW_IMG_H
#define EW_IMG_H

#include <stdint.h>

#include "pic.h"

typedef struct ew_img ew_img;

enum { IMG_WAITING, IMG_HERE, IMG_FAILED };

/* the picture at that address, made (waiting) the first time; NULL when
   out of memory */
ew_img *img_get(const char *url);
const char *img_url(const ew_img *im);
int img_state(const ew_img *im);
/* its size in CSS px, from its file's header; 0 x 0 until it has come */
void img_size(const ew_img *im, int32_t *w, int32_t *h);
/* a page shows it (+1), or no longer (-1) */
void img_ref(ew_img *im, int d);

/* its file came (data is taken, malloc'ed): 1, or 0 when it isn't a
   picture EWeb shows (it is failed then, why says what it is) */
int img_set_file(ew_img *im, char *data, long len);
/* it didn't come: shown by its alt text from now on */
void img_failed(ew_img *im);
/* it was stopped while it came: waiting again */
void img_waiting(ew_img *im);

/* decoded at w x h (device px): 1 if it is (now or before), 0 if it
   can't be (out of memory, damaged: tried once a size) */
int img_decode(ew_img *im, int32_t w, int32_t h);
/* tried at that size already (decoded or not) */
int img_tried(const ew_img *im, int32_t w, int32_t h);
/* for drawing: 1 and the picture at w x h if it is decoded, with its
   pens for img_pal (made now if they aren't) */
int img_picture(ew_img *im, int32_t w, int32_t h, const PIC **pic, const uint8_t **pens);
/* what went wrong last, for the log */
const char *img_why(const ew_img *im);

/* the screen's pens (the front end's, before drawing); NULL: true colour */
extern PIC_PAL *img_pal;
/* the screen's colours changed: every picture's pens made again */
void img_pal_changed(void);

/* the memory pictures may take (their files and their decoded pixels),
   roughly; past it, those no page shows go first, then the decoded
   pixels of those only pages kept for Back show (decoded again when
   they are shown) */
extern long img_max;
extern long img_used;
/* a file of more pixels than this is not decoded (a JPEG may have 12 times as many) */
extern long img_max_px;
void img_trim(void);
/* the page shown shows this picture (NULL: none, before they are named) */
void img_current(ew_img *im);
/* everything no page shows */
void img_clear(void);

#endif
