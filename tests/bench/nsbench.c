/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* nsbench: what NetSurf's HTML parser (Hubbub) and CSS engine (LibCSS)
 * cost on this machine, page by page.
 *
 *   nsbench [-P 1|2|3] [-c CHUNK] [-r N] [-q] PAGE.HTM [SHEET.CSS ...]
 *
 * Phase 1 parses the page into a tree (in CHUNK-byte pieces, 4096 by
 * default, as from the network); phase 2 parses the stylesheets: a small
 * UA sheet, the SHEET.CSS files (standing in for the page's linked
 * sheets) and the page's <style> elements; phase 3 selects and composes
 * every element's style. -P stops after a phase, so the cost of each can
 * be counted apart (tools/count68k.sh); -r repeats the whole run N
 * times. It prints what it found and, with a clock, the time per phase. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/libcss.h>

#include "dom.h"
#include "style.h"
#include "plat.h"

static int quiet;

static void say_stats(const dom_stats *ds, const style_stats *ss, int phases, size_t bytes)
{
	out("page %lu bytes: %lu elements, %lu text nodes (%lu bytes), %lu attributes, depth %lu\n",
		(unsigned long) bytes, (unsigned long) ds->elements, (unsigned long) ds->texts,
		(unsigned long) ds->text_bytes, (unsigned long) ds->attrs,
		(unsigned long) ds->depth);
	if (phases >= 2)
		out("css: %lu sheets, %lu bytes\n", (unsigned long) ss->sheets,
			(unsigned long) ss->sheet_bytes);
	if (phases >= 3)
		out("styles: %lu elements, %lu inline styles, %lu failed\n",
			(unsigned long) ss->selected, (unsigned long) ss->inline_styles,
			(unsigned long) ss->failed);
}

int nsbench(int argc, char **argv)
{
	int phases = 3, repeat = 1, i, r, nsheets = 0;
	size_t chunk = 4096, page_len = 0;
	const char *page_name = NULL;
	char *page;
	const char *sheet_data[32];
	size_t sheet_len[32];
	unsigned long t[4] = { 0, 0, 0, 0 };

	plat_init();
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-P") == 0 && i + 1 < argc)
			phases = atoi(argv[++i]);
		else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
			chunk = atoi(argv[++i]);
		else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
			repeat = atoi(argv[++i]);
		else if (strcmp(argv[i], "-q") == 0)
			quiet = 1;
		else if (page_name == NULL)
			page_name = argv[i];
		else if (nsheets < 32) {
			sheet_data[nsheets] = load(argv[i], &sheet_len[nsheets]);
			if (sheet_data[nsheets] == NULL) {
				out("nsbench: can't read %s\n", argv[i]);
				return 1;
			}
			nsheets++;
		}
	}
	if (page_name == NULL || chunk == 0 || phases < 1 || phases > 3 || repeat < 1) {
		out("usage: nsbench [-P 1|2|3] [-c CHUNK] [-r N] [-q] PAGE.HTM [SHEET.CSS ...]\n");
		return 2;
	}
	page = load(page_name, &page_len);
	if (page == NULL) {
		out("nsbench: can't read %s\n", page_name);
		return 1;
	}

	for (r = 0; r < repeat; r++) {
		dom_stats ds;
		style_stats ss;
		struct css_select_ctx *ctx = NULL;
		unsigned long t0 = ticks(), t1, t2, t3;
		node *doc = dom_parse((const uint8_t *) page, page_len, chunk);

		t1 = ticks();
		if (doc == NULL) {
			out("nsbench: Hubbub could not parse %s\n", page_name);
			return 1;
		}
		memset(&ss, 0, sizeof ss);
		if (phases >= 2)
			ctx = style_sheets(doc, sheet_data, sheet_len, nsheets, &ss);
		t2 = ticks();
		if (phases >= 3 && ctx != NULL)
			style_select(ctx, doc, &ss);
		t3 = ticks();
		t[1] += t1 - t0;
		t[2] += t2 - t1;
		t[3] += t3 - t2;
		if (r == 0 && !quiet) {
			dom_count(doc, &ds);
			say_stats(&ds, &ss, phases, page_len);
		}
		style_free(ctx, doc);
		dom_free(doc);
	}
	if (TICKS_PER_SEC > 0 && !quiet)
		out("time per run: html %lu ms, css %lu ms, select %lu ms\n",
			t[1] * 1000 / TICKS_PER_SEC / repeat, t[2] * 1000 / TICKS_PER_SEC / repeat,
			t[3] * 1000 / TICKS_PER_SEC / repeat);
	for (i = 0; i < nsheets; i++)
		free((char *) sheet_data[i]);
	free(page);
	return 0;
}
