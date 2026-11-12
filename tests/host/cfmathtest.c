/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cfmathtest.c - atari/cfmath.c (EWEBFB.PRG's 64-bit integers to and from
 * doubles, and leading zeros) against the host compiler's own, on edges
 * and on a million numbers from a fixed sequence
 */
#include <stdint.h>
#include <stdio.h>

#define CFM(name) cf##name
#include "../../atari/cfmath.c"

static uint64_t seed = 0x9e3779b97f4a7c15ULL;

static uint64_t next(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 7;
	seed ^= seed << 17;
	return seed;
}

int main(void)
{
	static const int64_t ints[] = { 0, 1, -1, 2, 3, 0x7fffffffLL, -0x80000000LL, 0x100000000LL,
					(1LL << 53) - 1, 1LL << 53, (1LL << 53) + 1, (1LL << 53) + 3, (1LL << 54) + 2,
					INT64_MAX, INT64_MIN, INT64_MIN + 1, -(1LL << 53) - 1 };
	static const double dbls[] = { 0.0, -0.0, 0.5, -0.5, 0.9999999, 1.0, -1.0, 1.5, -1.5, 2147483647.5,
				       -2147483648.9, 4294967296.25, 9007199254740993.0, 1e18, -1e18,
				       9.2233720368547748e18, -9.2233720368547758e18, 1e-300 };
	long fails = 0, checks = 0, i;
	unsigned k;
	for (k = 0; k < sizeof ints / sizeof ints[0]; k++, checks++)
		if (cf__floatdidf(ints[k]) != (double) ints[k])
			printf("FAIL floatdidf %lld\n", (long long) ints[k]), fails++;
	for (k = 0; k < sizeof dbls / sizeof dbls[0]; k++, checks++)
		if (cf__fixdfdi(dbls[k]) != (int64_t) dbls[k])
			printf("FAIL fixdfdi %.17g\n", dbls[k]), fails++;
	for (k = 0; k < 64; k++, checks++)
		if (cf__clzdi2(1ULL << k) != __builtin_clzll(1ULL << k) ||
		    cf__clzdi2((1ULL << k) | 1) != __builtin_clzll((1ULL << k) | 1))
			printf("FAIL clzdi2 bit %u\n", k), fails++;
	for (i = 0; i < 1000000; i++) {
		uint64_t r = next();
		int64_t v = (int64_t) (r >> (r & 63));	/* (every size) */
		double d;
		checks += 3;
		if (cf__floatdidf(v) != (double) v)
			printf("FAIL floatdidf %lld\n", (long long) v), fails++;
		if (cf__clzdi2(r >> (r & 63) | 1) != __builtin_clzll(r >> (r & 63) | 1))
			printf("FAIL clzdi2 %llx\n", (unsigned long long) r), fails++;
		d = (double) v / (double) ((r >> 40 & 1023) + 1);
		if (cf__fixdfdi(d) != (int64_t) d)
			printf("FAIL fixdfdi %.17g\n", d), fails++;
		if (fails > 20)
			break;
	}
	printf("cfmathtest: %ld checks, %ld failed\n", checks, fails);
	return fails != 0;
}
