/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tw_ovl.h - EWEBTW.OVL, EWEB.PRG's Tailwind utilities in a file of their
 * own (decided 27 Nov 2026; DESIGN.md, Tailwind's utilities made by
 * EWeb): src/browse/tw.c and its tables, loaded the first time a page
 * whose head loads Tailwind's compiler needs them, so that every other
 * page, and a stock 4 MB Falcon's ST RAM, never spends the memory.
 *
 * The file is made as EWEBJS.OVL is (tools/mkovl.py --tw: a TOS
 * program's image, its fixups, then its imports, "EWTI"); its text starts
 * with tw_exports. TW_ABI (the Makefile's, from tw.h, this file and
 * VERSION) must be the same in both.
 */
#ifndef EW_TW_OVL_H
#define EW_TW_OVL_H

#include "tw.h"

#define TW_OVL_MAGIC	0x45575457L	/* "EWTW" */
#ifdef __mcoldfire__
#define TW_ABI_OF	(TW_ABI ^ 0x43460000UL)	/* "CF": the FireBee's, refused elsewhere */
#else
#define TW_ABI_OF	TW_ABI
#endif
#define TW_OVL_IMPORTS	0x45575449L	/* "EWTI", the imports' part */

typedef struct tw_exports {
	long magic;
	unsigned long abi;
	char *(*sheet)(tw_name *names, int n, size_t *len);
} tw_exports;

/* the addresses of what the overlay imports, in mkovl.py's order, and a
   CRC of their names (build/gen/tw_imports.c) */
extern const void *const tw_imports[];
extern const unsigned long tw_imports_n, tw_imports_crc;

/* where EWEBTW.OVL is (next to EWEB.PRG), set at start */
void tw_ovl_at(const char *path);

#endif
