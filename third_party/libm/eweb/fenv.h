/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* fenv.h - no floating point environment: round to nearest, no flags */
#ifndef _FENV_H
#define _FENV_H
#define FE_TONEAREST 0
#define FE_ALL_EXCEPT 0
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int feclearexcept(int e) { (void) e; return 0; }
static inline int fetestexcept(int e) { (void) e; return 0; }
#endif
