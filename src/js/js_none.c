/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * js_none.c - no JavaScript: the ST edition (no FPU for QuickJS's maths).
 * Pages are shown without their scripts, with
 * their <noscript> parts, as when a site's scripts are off.
 */
#include <stddef.h>

#include "js.h"

const int js_built = 0;

int js_available(void)
{
	return 0;
}

const char *js_problem(void)
{
	return "isn't part of this EWeb.";
}

int js_page(doc *d, const char *url, const char *base, const js_host *h)
{
	(void) d, (void) url, (void) base, (void) h;
	return JS_NONE;
}

int js_changed(doc *d)
{
	(void) d;
	return 0;
}

const char *js_navigation(doc *d, const char **post, long *len, const char **ctype)
{
	(void) d, (void) post, (void) len, (void) ctype;
	return NULL;
}

const char *js_navigation_download(doc *d)
{
	(void) d;
	return NULL;
}

const char *js_navigation_window(doc *d, const char **html, long *len)
{
	(void) d, (void) html, (void) len;
	return NULL;
}

void js_navigation_done(doc *d)
{
	(void) d;
}

long js_due(doc *d)
{
	(void) d;
	return -1;
}

void js_run_timers(doc *d)
{
	(void) d;
}

int js_event(doc *d, dnode *n, const char *type)
{
	(void) d, (void) n, (void) type;
	return 0;
}

int js_key(doc *d, dnode *n, const char *type, const char *key, long keycode, long charcode)
{
	(void) d, (void) n, (void) type, (void) key, (void) keycode, (void) charcode;
	return 0;
}

int js_mouse(doc *d, dnode *n, const char *type, long x, long y, int button, int detail, dnode *related)
{
	(void) d, (void) n, (void) type, (void) x, (void) y, (void) button, (void) detail, (void) related;
	return 0;
}

int js_moves(doc *d)
{
	(void) d;
	return 0;
}

void js_run(doc *d, const char *code)
{
	(void) d, (void) code;
}

void js_toggled(doc *d, dnode *n)
{
	(void) d, (void) n;
}

void js_set_page(doc *d, const struct page *p)
{
	(void) d, (void) p;
}

void js_stats(doc *d, unsigned long *ms, long *bytes)
{
	(void) d;
	*ms = 0;
	*bytes = 0;
}
