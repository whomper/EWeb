/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * codepad.h - bytes that never run: CODE_PAD(cond, CP_NAME) at the start
 * of a function that changed makes it the size that keeps the code after
 * it in its object at the last release's places modulo 256, so that the
 * 68030's caches see the code that didn't change as they did and the
 * paired speed run times the change, not where it moved the code (1.23's
 * way; DESIGN.md, Code layout and the 68030's caches). cond is never true but the
 * compiler can't know it (an argument against a value it never has); the
 * sizes are codepads.h's, for EWEB.PRG alone (EWEBST.PRG, the FireBee's
 * and the host builds have none).
 */
#ifndef EW_CODEPAD_H
#define EW_CODEPAD_H

#if (defined EW_TOS || defined NSBENCH_TOS) && !defined EW_ST && !defined __mcoldfire__ && !defined __mc68060__
#include "codepads.h"
#define CODE_PAD(cond, n) do { if (__builtin_expect((cond), 0)) __asm__ __volatile__(".skip " n); } while (0)
#else
#define CODE_PAD(cond, n) do { } while (0)
#endif

#endif
