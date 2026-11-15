/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| NatFeats (emulator native features) for debug output under Hatari/ARAnyM.
| long nf_probe(void)  - supervisor only: NF id of "NF_STDERR", 0 if absent
| long nf_call2(long id, long arg)
| long nf_find(const char *name) - supervisor only: NF id of name, 0 if absent
|
| NatFeats read their argument at 4(sp), as in a C call, so the caller
| leaves room for a return address (EHypView found that the earlier copy
| missed it, so nf_debug() never printed).

#ifdef __mcoldfire__
| The ColdFire (the FireBee): no emulator, so no NatFeats; and NatFeats'
| opcodes are no illegal instructions there (0x7300 is mvs.b %d0,%d1 on a
| V4e), so nothing is tried and no vector touched: none found, and a call
| does nothing
	.text
	.globl	nf_probe, nf_call2, nf_find, nf_vec
nf_probe:
nf_find:
nf_call2:
	moveq	#0,%d0
	rts
	.data
nf_vec:
	.long	0x10
	.text
#else
	.text
	.globl	nf_probe
nf_probe:
	move.l	%a2,-(%sp)
	movea.l	nf_vec,%a2		| the illegal instruction vector (VBR + 0x10)
	move.l	(%a2),%a1
	move.l	%sp,%a0
	move.l	#1f,(%a2)
	pea	nf_name
	subq.l	#4,%sp			| NatFeats read the argument at 4(sp),
	moveq	#0,%d0			| as in a C call: leave room for the
	.dc.w	0x7300			| return address (nf_id)
	addq.l	#8,%sp
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts
1:	move.l	%a0,%sp			| no NatFeats: illegal instruction
	moveq	#0,%d0
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts

	.globl	nf_call2
nf_call2:
	.dc.w	0x7301			| nf_call(id, arg)
	rts

	.data
nf_name:
	.asciz	"NF_STDERR"
	.even

| long nf_find(const char *name) - supervisor only: NF id of `name`,
| 0 if absent (EHypView's test program finds NF_EXIT with it)
	.text
	.globl	nf_find
nf_find:
	move.l	4(%sp),%d1
	move.l	%a2,-(%sp)
	movea.l	nf_vec,%a2
	move.l	(%a2),%a1
	move.l	%sp,%a0
	move.l	#2f,(%a2)
	move.l	%d1,-(%sp)
	subq.l	#4,%sp			| argument at 4(sp), as in a C call
	moveq	#0,%d0
	.dc.w	0x7300			| nf_id
	addq.l	#8,%sp
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts
2:	move.l	%a0,%sp
	moveq	#0,%d0
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts

| where the illegal-instruction vector is: 0x10, or VBR + 0x10 as tos.c's
| nf_debug sets it on a 68010 or later before the first probe
	.data
	.globl	nf_vec
nf_vec:
	.long	0x10
	.text
#endif
