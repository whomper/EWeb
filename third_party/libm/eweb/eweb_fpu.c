/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * eweb_fpu.c - with the FPU (-m68881) musl's hypot works in long double:
 * its square root is the FPU's own instruction
 */
long double sqrtl(long double x);

long double sqrtl(long double x)
{
	return __builtin_sqrtl(x);
}
