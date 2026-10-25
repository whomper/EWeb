/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fixmath.h - whole-number maths for SVG (svgread.c): numbers in 16.16,
 * angles as a turn in 65536 parts, sine and cosine from a table, atan2 by
 * CORDIC, square roots by bits. The 68030 has no FPU in a stock Falcon,
 * and EWEB.PRG uses none where it has one.
 */
#ifndef EW_FIXMATH_H
#define EW_FIXMATH_H

#include <stdint.h>

typedef int32_t fx;			/* 16.16 */
#define FX_ONE 65536
#define FX(i) ((fx) (i) * FX_ONE)

/* a b, saturated */
static inline fx fx_mul(fx a, fx b)
{
	int64_t r = ((int64_t) a * b) >> 16;
	return r > 0x7fffffff ? 0x7fffffff : r < -0x7fffffff ? -0x7fffffff : (fx) r;
}

/* a / b, saturated */
fx fx_div(fx a, fx b);
/* the sine and cosine of an angle (65536 a turn) */
fx fx_sin(uint32_t ang);
fx fx_cos(uint32_t ang);
/* the angle of (x, y), 65536 a turn, 0 along +x, a quarter along +y */
uint32_t fx_atan2(fx y, fx x);
/* the square root of a 16.16 number (negative: 0) */
fx fx_sqrt(fx v);
/* of an unsigned 64-bit number, whole */
uint32_t isqrt64(uint64_t v);
/* degrees (16.16) as an angle */
uint32_t fx_deg(fx deg);

#endif
