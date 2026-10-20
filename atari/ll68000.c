/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ll68000.c - 64-bit multiply, divide and remainder for the 68000 build
 * (EWEBST.PRG). GCC calls these for long long arithmetic (the layout's
 * and the CSS engine's fixed point, cookies' and the cache's dates); the
 * cross compiler's libgcc has them only as 68020 code (divu.l, bfffo),
 * an illegal instruction on an ST. common/atari/tos.c has the 32-bit ones
 * (__mulsi3, __divsi3...); these use only those, 16-bit mulu, and 64-bit
 * adds, shifts and compares, which GCC writes inline (or as __ashldi3,
 * plain 68000 code).
 */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;		/* 32 bits on the 68000 and on a test host */

u64 __udivdi3(u64 n, u64 d);
u64 __umoddi3(u64 n, u64 d);
s64 __divdi3(s64 n, s64 d);
s64 __moddi3(s64 n, s64 d);
s64 __muldi3(s64 a, s64 b);

/* a 32 x 32 -> 64 multiply from four 16 x 16 ones (mulu.w) */
static u64 mul32(u32 a, u32 b)
{
	u32 al = a & 0xFFFF, ah = a >> 16, bl = b & 0xFFFF, bh = b >> 16;
	u32 ll = (unsigned short) al * (u32) (unsigned short) bl;
	u32 lh = (unsigned short) al * (u32) (unsigned short) bh;
	u32 hl = (unsigned short) ah * (u32) (unsigned short) bl;
	u32 hh = (unsigned short) ah * (u32) (unsigned short) bh;
	u64 r = ((u64) hh << 32) + ll;
	r += (u64) lh << 16;
	r += (u64) hl << 16;
	return r;
}

s64 __muldi3(s64 a, s64 b)
{
	u64 ua = (u64) a, ub = (u64) b;
	u32 al = (u32) ua, ah = (u32) (ua >> 32), bl = (u32) ub, bh = (u32) (ub >> 32);
	u64 r = mul32(al, bl);
	/* the high halves' products: only their low 32 bits stay */
	r += (u64) ((u32) mul32(al, bh) + (u32) mul32(ah, bl)) << 32;
	return (s64) r;
}

/* n / d and n % d, shifting and subtracting; d 0 gives 0 and n (no trap) */
static u64 udivmod(u64 n, u64 d, u64 *rem)
{
	u64 q = 0, r = 0;
	int i;
	if (d == 0) {
		*rem = n;
		return 0;
	}
	if ((n >> 32) == 0 && (d >> 32) == 0) {	/* the 32-bit routines */
		u32 a = (u32) n, b = (u32) d;
		*rem = a % b;
		return a / b;
	}
	if (n < d) {
		*rem = n;
		return 0;
	}
	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		q <<= 1;
		if (r >= d) {
			r -= d;
			q |= 1;
		}
	}
	*rem = r;
	return q;
}

u64 __udivdi3(u64 n, u64 d)
{
	u64 r;
	return udivmod(n, d, &r);
}

u64 __umoddi3(u64 n, u64 d)
{
	u64 r;
	udivmod(n, d, &r);
	return r;
}

s64 __divdi3(s64 n, s64 d)
{
	u64 r, q = udivmod(n < 0 ? -(u64) n : (u64) n, d < 0 ? -(u64) d : (u64) d, &r);
	return (n < 0) != (d < 0) ? -(s64) q : (s64) q;
}

s64 __moddi3(s64 n, s64 d)
{
	u64 r;
	udivmod(n < 0 ? -(u64) n : (u64) n, d < 0 ? -(u64) d : (u64) d, &r);
	return n < 0 ? -(s64) r : (s64) r;
}
