/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cfmath.c - EWEBFB.PRG's (the ColdFire's) libgcc calls that common's
 * ll64.c doesn't have and EWEBJSFB.OVL takes from it: QuickJS's 64-bit
 * integers to and from the FPU's doubles, and the leading zeros of a
 * 64-bit number (the cross compiler's libgcc is 68020 code). Each from
 * the double's bits, so that none calls itself (a conversion GCC would
 * make a call of these). tools/mkfirebee.py builds it; on the host
 * tests/host/cfmathtest.c checks it against the C compiler's own, with
 * CFM() naming the functions apart.
 */
#include <stdint.h>

#ifndef CFM
#define CFM(name) name
#endif

int CFM(__clzdi2)(uint64_t a);
int64_t CFM(__fixdfdi)(double a);
double CFM(__floatdidf)(int64_t a);

/* the leading zero bits of a 32-bit number, by halving steps */
static int clz32(uint32_t x)
{
	int n = 0;
	if (!x)
		return 32;
	if (!(x & 0xffff0000UL)) { n += 16; x <<= 16; }
	if (!(x & 0xff000000UL)) { n += 8; x <<= 8; }
	if (!(x & 0xf0000000UL)) { n += 4; x <<= 4; }
	if (!(x & 0xc0000000UL)) { n += 2; x <<= 2; }
	if (!(x & 0x80000000UL))
		n += 1;
	return n;
}

int CFM(__clzdi2)(uint64_t a)
{
	uint32_t hi = (uint32_t) (a >> 32);
	return hi ? clz32(hi) : 32 + clz32((uint32_t) a);
}

/* a double to a 64-bit integer, toward zero (out of range: C leaves it
   undefined; the nearest end here) */
int64_t CFM(__fixdfdi)(double a)
{
	union {
		double d;
		uint64_t u;
	} b;
	uint64_t u, m;
	int e;
	b.d = a;
	u = b.u;
	e = (int) (u >> 52 & 0x7ff) - 1023;
	if (e < 0)
		return 0;
	if (e >= 63)
		return (u >> 63) ? INT64_MIN : INT64_MAX;
	m = (u & ((UINT64_C(1) << 52) - 1)) | UINT64_C(1) << 52;
	m = e >= 52 ? m << (e - 52) : m >> (52 - e);
	return (u >> 63) ? -(int64_t) m : (int64_t) m;
}

/* a 64-bit integer to the nearest double (ties to even): its top 32 bits
   times 2^32 is exact, and so is its low half, so their sum is rounded
   once; a negative one as its magnitude, then turned */
double CFM(__floatdidf)(int64_t a)
{
	uint64_t m = a < 0 ? 0 - (uint64_t) a : (uint64_t) a;
	uint32_t hi = (uint32_t) (m >> 32), lo = (uint32_t) m;
	/* (each half as two 16-bit pieces: an unsigned 32-bit number to a
	   double is otherwise a call too) */
	double h = ((double) (int32_t) (hi >> 16) * 65536.0 + (double) (int32_t) (hi & 0xffff)) * 4294967296.0;
	double l = (double) (int32_t) (lo >> 16) * 65536.0 + (double) (int32_t) (lo & 0xffff);
	double r = h + l;
	return a < 0 ? -r : r;
}
