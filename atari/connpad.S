/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Room linked after common's conn.o in EWEB.PRG, never run or read: it puts
   the code and read-only data linked after conn.o (TLS.OVL's loader, the
   network, the pictures' decoders, tcpix, deep) back at their places of
   1.11 modulo 256, as the 68030's caches see them (256 bytes each).
   TLS.OVL 1.16's conn.c (the hosts kept between sessions, 14 Nov 2026)
   grew its code by 892 bytes and its strings by 42: the sample page's
   longest stretch on the stock Falcon under NVDI went from 605 to 615 ms
   with no line of it run (the speed rig's pages are from the disk), and
   came back with this room. When conn.o's size changes again, work the
   two sizes out again from the build's symbols against the release
   before (DESIGN.md, Code layout and the 68030's caches) */
	.section .text.connpad,"ax"
	.globl	connpad_text
connpad_text:
	.space	132		/* 892 + 132 = 1024 */

	.section .rodata.connpad,"a"
	.globl	connpad_ro
connpad_ro:
	.space	214		/* 934 + 132 + 214 = 1280 */
