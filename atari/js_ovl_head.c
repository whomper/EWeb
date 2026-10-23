/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_ovl_head.c - the start of EWEBJS.OVL's text (atari/ovl.ld puts it
 * first): the calls EWEB.PRG makes into it, the overlay's only way in
 * (js_ovl.h).
 */
#include "js_ovl.h"

#ifndef JS_ABI
#error "JS_ABI: the Makefile gives it"
#endif

const js_exports js_ovl_head __attribute__((section(".ovlhead"), used)) = {
	JS_OVL_MAGIC, JS_ABI,
	js_page, js_changed, js_navigation, js_navigation_download, js_navigation_done,
	js_due, js_run_timers, js_event, js_run, js_toggled, js_set_page, js_stats
};
