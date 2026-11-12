/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_fpu.h - EWEBJS.OVL's 68881/68882 at double precision while it runs
 * (DESIGN.md, Scripts on the 68060 and the FireBee). JavaScript's numbers
 * are IEEE doubles, and the 68881 works in 80 bits: GCC keeps what it
 * works out in the FPU's registers, so a script's sums came out other than
 * a PC's. With the FPCR's rounding precision at double each result is
 * rounded to a double's 53 bits as it is made, and musl's maths is built
 * as for a machine whose sums are doubles (-D__FLT_EVAL_METHOD__=0: its
 * floor's 2^52 trick, which the 80-bit build made 2^63). The overlay's
 * calls (js_ovl_head.c) set it and give the FPCR back as it was, so
 * nothing else that runs is changed; tests/coldfire/run.sh checks the
 * answers against the host's. JS_FPU_DOUBLE is the 68030 overlay's only
 * (the Makefile's TOSJS_FPU): the 68060's FPU rounds to doubles itself
 * (GCC's fdadd and fdmul), and the ColdFire's has doubles only.
 */
#ifndef EW_JS_FPU_H
#define EW_JS_FPU_H

#if defined(JS_FPU_DOUBLE) && defined(__HAVE_68881__) && !defined(__mcoldfire__)
/* the FPCR as it was; rounding precision double (bits 7-6: 10), the
   rounding mode and the exceptions left as they are */
static inline unsigned long js_fpu_in(void)
{
	unsigned long old;
	__asm__ volatile("fmove.l %%fpcr,%0" : "=d"(old));
	__asm__ volatile("fmove.l %0,%%fpcr" : : "d"((old & ~0xc0UL) | 0x80UL));
	return old;
}

static inline void js_fpu_out(unsigned long old)
{
	__asm__ volatile("fmove.l %0,%%fpcr" : : "d"(old));
}
#define JS_FPU_IN	unsigned long fpcr_ = js_fpu_in()
#define JS_FPU_OUT	js_fpu_out(fpcr_)
#else
#define JS_FPU_IN	do {} while (0)
#define JS_FPU_OUT	do {} while (0)
#endif

#endif
