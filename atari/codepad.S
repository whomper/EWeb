/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Room linked after an object of EWEB.PRG that grew, never run or read
   (as atari/connpad.S after conn.o): it puts the code after it back at
   its places of the release before modulo 256, as the 68030's caches see
   them (DESIGN.md, Code layout and the 68030's caches). Made once for
   each place, PAD_NAME, PAD_TEXT, PAD_RO and PAD_BSS (its read-only data's and
   BSS's, or 0) from the Makefile, which says where
   each goes and why */
	.section .text.PAD_NAME,"ax"
	.globl	PAD_NAME
PAD_NAME:
	.space	PAD_TEXT
#if PAD_RO > 0
#define RO_(n) n ## _ro
#define RO(n) RO_(n)
	.section .rodata.PAD_NAME,"a"
	.globl	RO(PAD_NAME)
RO(PAD_NAME):
	.space	PAD_RO
#endif
#if PAD_BSS > 0
#define BSS_(n) n ## _bss
#define BSS(n) BSS_(n)
	.section .bss.PAD_NAME,"aw",@nobits
	.globl	BSS(PAD_NAME)
BSS(PAD_NAME):
	.space	PAD_BSS
#endif
