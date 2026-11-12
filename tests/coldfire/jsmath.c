/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * jsmath.c - EWEBJSFB.OVL's numbers as ColdFire code: musl's maths
 * (third_party/libm, built as the overlay builds it) and atari/cfmath.c's
 * 64-bit integers to and from doubles, under qemu-m68k -cpu cfv4e (common's
 * tests/coldfire rt.S and libc.c: no C library), against the host's build
 * of the same sources (-DHOST). Every answer's bits are folded into one sum
 * that both must print alike: QuickJS's Math as the FireBee's FPU works it
 * out, the same as on any IEEE double machine (tests/coldfire/run.sh).
 */
#include <stdint.h>
#ifdef HOST
#include <stdio.h>
#else
#include "rt.h"
#endif

double sin(double), cos(double), tan(double), asin(double), acos(double), atan(double), atan2(double, double);
double exp(double), expm1(double), log(double), log2(double), log10(double), log1p(double), pow(double, double);
double sqrt(double), cbrt(double), hypot(double, double), sinh(double), cosh(double), tanh(double);
double asinh(double), acosh(double), atanh(double), fmod(double, double), floor(double), ceil(double);
double trunc(double), round(double), rint(double), fabs(double);
int __clzdi2(uint64_t a);
int64_t __fixdfdi(double a);
double __floatdidf(int64_t a);

static uint64_t seed = 1;
static uint64_t rnd(void)
{
	seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
	return seed ^ (seed >> 29);
}

static uint32_t sum = 2166136261UL;
static void fold(uint64_t v)
{
	int i;
	for (i = 0; i < 8; i++, v >>= 8)
		sum = (sum ^ (uint32_t) (v & 255)) * 16777619UL;
}

static void foldd(double d)
{
	union {
		double d;
		uint64_t u;
	} b;
	b.d = d;
	if (d != d)
		b.u = 0x7ff8000000000000ULL;	/* (a NaN's payload is the machine's) */
	fold(b.u);
}

/* a double of every size: random bits with the exponent kept in range */
static double num(void)
{
	union {
		double d;
		uint64_t u;
	} b;
	uint64_t r = rnd();
	b.u = (r & 0x800fffffffffffffULL) | (uint64_t) (1023 - 40 + (rnd() % 80)) << 52;
	return b.d;
}

int main(void)
{
	long i;
	for (i = 0; i < 20000; i++) {
		volatile double a = num(), b = num(), s = (double) (int64_t) (rnd() % 2000001) / 1000.0 - 1000.0;
		volatile int64_t v = (int64_t) (rnd() >> (rnd() & 63)) * ((rnd() & 1) ? -1 : 1);
		foldd(sin(s)); foldd(cos(s)); foldd(tan(s)); foldd(atan(a)); foldd(atan2(a, b));
		foldd(asin(s / 1000.0)); foldd(acos(s / 1000.0));
		foldd(exp(s / 2.0)); foldd(expm1(s / 100.0)); foldd(log(fabs(a))); foldd(log2(fabs(a)));
		foldd(log10(fabs(a))); foldd(log1p(fabs(s))); foldd(pow(fabs(a), s / 100.0)); foldd(pow(s, 3.0));
		foldd(sqrt(fabs(a))); foldd(cbrt(a)); foldd(hypot(a, b)); foldd(sinh(s / 10.0));
		foldd(cosh(s / 10.0)); foldd(tanh(s / 10.0)); foldd(asinh(a)); foldd(acosh(1.0 + fabs(a)));
		foldd(atanh(s / 1001.0)); foldd(fmod(a, b)); foldd(floor(a)); foldd(ceil(a)); foldd(trunc(a));
		foldd(round(a)); foldd(rint(a)); foldd(a * b + s); foldd(a / b);
		fold((uint64_t) __fixdfdi(a * 1e6)); foldd(__floatdidf(v)); fold((uint64_t) __clzdi2((uint64_t) v | 1));
	}
#ifdef HOST
	printf("jsmath: sum %08lx\n", (unsigned long) sum);
#else
	out("jsmath: sum ");
	out_hex(sum);
	out("\n");
#endif
	return 0;
}
