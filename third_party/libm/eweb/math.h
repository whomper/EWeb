/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * math.h - EWeb's freestanding math: the double functions of musl's libm
 * (third_party/libm) that QuickJS's Math needs, and the classification
 * macros as GCC's builtins.
 */
#ifndef _MATH_H
#define _MATH_H

#if __FLT_EVAL_METHOD__ == 2
typedef long double float_t, double_t;
#else
typedef float float_t;
typedef double double_t;
#endif

#define NAN       __builtin_nanf("")
#define INFINITY  __builtin_inff()
#define HUGE_VALF INFINITY
#define HUGE_VAL  ((double) INFINITY)
#define HUGE_VALL ((long double) INFINITY)

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)

#define M_E        2.7182818284590452354
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_SQRT2    1.41421356237309504880

double fabs(double), floor(double), ceil(double), trunc(double), round(double), rint(double);
double sqrt(double), cbrt(double), hypot(double, double), fmod(double, double), modf(double, double *);
double fmin(double, double), fmax(double, double), scalbn(double, int);
long lrint(double);
double sin(double), cos(double), tan(double), asin(double), acos(double), atan(double), atan2(double, double);
double sinh(double), cosh(double), tanh(double), asinh(double), acosh(double), atanh(double);
double exp(double), expm1(double), log(double), log1p(double), log2(double), log10(double), pow(double, double);

#endif
