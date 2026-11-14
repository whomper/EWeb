/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Room linked after common's charset.o in EWEBST.PRG, never run or read:
   it puts what is linked after it, code and data, back at its place of
   1.11 modulo 256. common's charset.c of 14 Nov 2026 (Hebrew and Latin-1
   straight from its tables) grew it by 64 bytes, and the ST's About box
   went from 155 to 165 ms with its loop the very same instructions: 1.11's
   charset.c padded to the new size was 165 ms too (DESIGN.md, Code layout
   and the 68030's caches). The rest after it had moved 206 bytes from 1.11;
   with this room 398, 142 modulo 256, as with 1.11's charset.c. When the
   code before or in charset.o changes size again, work the size out again
   from the build's symbols against the release before */
	.section .text.charsetpad,"ax"
	.globl	charsetpad_text
charsetpad_text:
	.space	192		/* 206 + 192 = 398 = 256 + 142 */
