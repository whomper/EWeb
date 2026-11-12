/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_ovl_head.c - the start of EWEBJS.OVL's text (atari/ovl.ld puts it
 * first): the calls EWEB.PRG makes into it, the overlay's only way in
 * (js_ovl.h), each with the 68881 at double precision in the 68030's
 * (js_fpu.h).
 */
#include "js_ovl.h"
#include "js_fpu.h"

#ifndef JS_ABI
#error "JS_ABI: the Makefile gives it"
#endif

#ifdef JS_FPU_DOUBLE
/* each call with the FPU at double precision, given back as it was after
   (js_fpu.h: the 68030's overlay) */
#define V(name, params, args) \
	static void o_##name params { JS_FPU_IN; js_##name args; JS_FPU_OUT; }
#define R(type, name, params, args) \
	static type o_##name params { type r; JS_FPU_IN; r = js_##name args; JS_FPU_OUT; return r; }
R(int, page, (doc *d, const char *url, const char *base, const js_host *h), (d, url, base, h))
R(int, changed, (doc *d), (d))
R(const char *, navigation, (doc *d, const char **post, long *len, const char **ctype), (d, post, len, ctype))
R(const char *, navigation_download, (doc *d), (d))
V(navigation_done, (doc *d), (d))
R(long, due, (doc *d), (d))
V(run_timers, (doc *d), (d))
R(int, event, (doc *d, dnode *n, const char *type), (d, n, type))
V(run, (doc *d, const char *code), (d, code))
V(toggled, (doc *d, dnode *n), (d, n))
V(set_page, (doc *d, const struct page *p), (d, p))
V(stats, (doc *d, unsigned long *ms, long *bytes), (d, ms, bytes))
R(int, key, (doc *d, dnode *n, const char *type, const char *key, long keycode, long charcode),
  (d, n, type, key, keycode, charcode))
R(const char *, navigation_window, (doc *d, const char **html, long *len), (d, html, len))
R(int, mouse, (doc *d, dnode *n, const char *type, long x, long y, int button, int detail, dnode *related),
  (d, n, type, x, y, button, detail, related))
R(int, moves, (doc *d), (d))
#define E(name) o_##name
#else
#define E(name) js_##name
#endif

const js_exports js_ovl_head __attribute__((section(".ovlhead"), used)) = {
	JS_OVL_MAGIC, JS_ABI,
	E(page), E(changed), E(navigation), E(navigation_download), E(navigation_done),
	E(due), E(run_timers), E(event), E(run), E(toggled), E(set_page), E(stats), E(key),
	E(navigation_window), E(mouse), E(moves)
};
