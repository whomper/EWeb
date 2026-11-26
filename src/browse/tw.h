/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* tw.h - Tailwind's utilities made by EWeb (DESIGN.md, Tailwind's
   utilities made by EWeb): for a page that loads Tailwind's in-browser
   compiler (cdn.tailwindcss.com), which EWeb doesn't run, the sheet it
   would have written for the page's classes, from Tailwind 3's own data
   (src/css/tw.txt, tools/mktw.py). */
#ifndef EW_TW_H
#define EW_TW_H

#include <stddef.h>

/* a class as the page writes it (len bytes at s, not 0-ended) */
typedef struct tw_name {
	const char *s;
	size_t len;
} tw_name;

/* The sheet for these classes (any order, any repeated): Tailwind's base
   sheet, then a rule for each class it knows, in Tailwind's order,
   responsive and dark ones in their @media. malloc'd, 0-ended, its
   length in *len; NULL if out of memory. Classes it doesn't know are
   passed over. */
char *tw_sheet(tw_name *names, int n, size_t *len);

/* 1 if the address (as a page writes it: https: or //) is Tailwind's
   in-browser compiler */
int tw_script(const char *url);

#endif
