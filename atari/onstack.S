| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| on_stack(fn, top): fn() run on the stack that ends at top (its first
| long at top - 4), the stack it was called on put back after; fn's
| result returned. EWEB.PRG's app_main runs the program so, on a stack
| taken from GEMDOS as big as the machine allows (ui/main.c, DESIGN.md's
| The stack): crt0's in the BSS is small, a stock 4 MB Falcon under NVDI
| having laid a big page out in what a 256 KB one there took.
|
|   int on_stack(int (*fn)(void), void *top);
	.text
	.globl	on_stack

on_stack:
	move.l	4(%sp),%a0		| fn
	move.l	8(%sp),%a1		| top
	move.l	%sp,%d1
	move.l	%a1,%sp
	move.l	%d1,-(%sp)		| the caller's stack, on the new one
	jsr	(%a0)			| (fn keeps d2-d7 and a2-a6, as C does)
	move.l	(%sp),%sp		| back on the caller's
	rts
