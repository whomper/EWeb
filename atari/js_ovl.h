/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_ovl.h - EWEBJS.OVL, EWEB.PRG's JavaScript in a file of its own
 * (DESIGN.md, One EWEB.PRG for every Falcon): QuickJS, musl's maths and
 * src/js/js.c, loaded the first time a site whose scripts are on needs
 * them, and only with an FPU, so a Falcon without one, and every page
 * without scripts, never spends the memory.
 *
 * The file is a TOS program's image (tools/mkovl.py): the header, text
 * and data, TOS's fixups, then EWeb's imports: each place the overlay
 * calls or reads EWEB.PRG (malloc, the document's calls, ec_stack_floor)
 * and which of js_imports[] (build/gen/js_imports.c) goes there. Its text
 * starts with js_exports, the calls EWEB.PRG makes (js.h). JS_ABI, made by
 * the Makefile from the headers both sides share and VERSION, must be the
 * same in both, as the overlay reads EWeb's structures as they are.
 */
#ifndef EW_JS_OVL_H
#define EW_JS_OVL_H

#include "js.h"

#define JS_OVL_MAGIC	0x45574A53L	/* "EWJS" */
#define JS_OVL_IMPORTS	0x45574A49L	/* "EWJI", the imports' part */

typedef struct js_exports {
	long magic;
	unsigned long abi;
	int (*page)(doc *d, const char *url, const char *base, const js_host *h);
	int (*changed)(doc *d);
	const char *(*navigation)(doc *d, const char **post, long *len, const char **ctype);
	const char *(*navigation_download)(doc *d);
	void (*navigation_done)(doc *d);
	long (*due)(doc *d);
	void (*run_timers)(doc *d);
	int (*event)(doc *d, dnode *n, const char *type);
	void (*run)(doc *d, const char *code);
	void (*toggled)(doc *d, dnode *n);
	void (*set_page)(doc *d, const struct page *p);
	void (*stats)(doc *d, unsigned long *ms, long *bytes);
} js_exports;

/* the addresses of what the overlay imports, in mkovl.py's order, and a
   CRC of their names (both build/gen/js_imports.c) */
extern const void *const js_imports[];
extern const unsigned long js_imports_n, js_imports_crc;

/* where EWEBJS.OVL is (next to EWEB.PRG), set at start */
void js_ovl_at(const char *path);

#endif
