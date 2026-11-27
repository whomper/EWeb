/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tw_ovl_head.c - the start of EWEBTW.OVL's text (atari/ovl.ld puts it
 * first): the call EWEB.PRG makes into it (tw_ovl.h).
 */
#include "tw_ovl.h"

#ifndef TW_ABI
#error "TW_ABI: the Makefile gives it"
#endif

const tw_exports tw_ovl_head __attribute__((section(".ovlhead"), used)) = {
	TW_OVL_MAGIC, TW_ABI_OF, tw_sheet
};
