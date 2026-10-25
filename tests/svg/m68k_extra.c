/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * m68k_extra.c - for build/m68k/svgrender, soft float as EWEB.PRG would
 * run it: the float functions libsvgtiny calls beyond musl's
 * (third_party/libm, built soft float here), and the two of pic.c's the
 * readers use (not the decoders).
 */
#include <math.h>
#include <stdlib.h>

#include "pic.h"

long lroundf(float x)
{
	return x < 0 ? -(long) (-x + 0.5f) : (long) (x + 0.5f);
}

float ceilf(float x)
{
	return (float) ceil(x);
}

float powf(float a, float b)
{
	return (float) pow(a, b);
}

/* (through pointers: gcc would make the two calls one to sincos again) */
static double (*volatile sin_p)(double) = sin;
static double (*volatile cos_p)(double) = cos;

void sincos(double x, double *s, double *c)
{
	*s = sin_p(x);
	*c = cos_p(x);
}

void pic_free(PIC *p)
{
	if (p != NULL) {
		free(p->px);
		free(p->alpha);
		free(p);
	}
}

const char *pic_why_text(int why)
{
	return why == PIC_MEMORY ? "out of memory" : why == PIC_TOO_BIG ? "too big" : "damaged";
}
