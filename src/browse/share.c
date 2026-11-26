/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* share.c - EWeb's own page for shared files (share.h; DESIGN.md, Shared
   files). The page is the home page's look: black, AERA's text colour,
   light copper for the label and the buttons' words, copper for the
   ruler and the buttons' frames; laid out from 320 pixels wide up, and
   read in 16 colours and in black and white too. */
#include <stdio.h>
#include <string.h>

#include "share.h"

#ifndef EW_ST

static const char share_style[] =
	"<style>body{margin:0;background:#000;color:#e5e5e5;font:16px/1.35 \"DejaVu Sans\",sans-serif}"
	"a{color:#e5e5e5;text-decoration:none}"
	".page{max-width:620px;margin:0 auto;padding:22px 18px 14px}"
	".kick{font-family:monospace;font-size:13px;letter-spacing:2px;text-transform:uppercase;color:#e2a76f}"
	"h1{font-size:22px;margin:6px 0 2px;font-weight:bold}"
	".from{color:#a3a3a3;font-size:13px}"
	".ruler{height:2px;background:#b87333;margin:12px 0 10px}"
	"table{width:100%;border-collapse:collapse}"
	"td{padding:7px 4px;border-bottom:1px solid #333;vertical-align:middle}"
	"td.t{width:46px;font-family:monospace;font-size:12px;color:#e2a76f}"
	"td.s{width:80px;text-align:right;color:#a3a3a3;font-size:13px}"
	"td.g{width:96px;text-align:right}"
	".btn{display:inline-block;border:1px solid #b87333;color:#e2a76f;padding:2px 10px;font-size:13px}"
	".all{margin:14px 0 0;display:flex;justify-content:space-between;align-items:center;color:#a3a3a3;"
	"font-size:13px}"
	".note{margin:18px 0 0;color:#777;font-size:12px}"
	".addr{color:#777;font-size:12px;margin:4px 0 0;word-break:break-all}</style>";

static void esc(SBUF *s, const char *t)
{
	for (; *t; t++) {
		if (*t == '<')
			sb_adds(s, "&lt;");
		else if (*t == '>')
			sb_adds(s, "&gt;");
		else if (*t == '&')
			sb_adds(s, "&amp;");
		else if (*t == '"')
			sb_adds(s, "&quot;");
		else
			sb_addc(s, *t);
	}
}

void share_size(char *out, int size, long bytes)
{
	if (bytes < 0)
		snprintf(out, size, "%s", "");
	else if (bytes < 1024)
		snprintf(out, size, "%ld bytes", bytes);
	else if (bytes < 1024L * 1024)
		snprintf(out, size, "%ld KB", (bytes + 512) / 1024);
	else if (bytes < 100L * 1024 * 1024)
		snprintf(out, size, "%ld.%ld MB", bytes / (1024L * 1024), (bytes % (1024L * 1024)) * 10 / (1024L * 1024));
	else
		snprintf(out, size, "%ld MB", (bytes + 512L * 1024) / (1024L * 1024));
}

void share_type(char *out, int size, const char *name)
{
	const char *dot = strrchr(name, '.');
	int k = 0;
	out[0] = 0;
	if (dot == NULL || dot == name || strlen(dot + 1) > 4)
		return;
	for (dot++; *dot && k < size - 1; dot++) {
		char c = *dot;
		if (c >= 'a' && c <= 'z')
			c = (char) (c - 32);
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			return;		/* (not an extension: out stays as made so far) */
		out[k++] = c;
		out[k] = 0;
	}
}

void share_page(SBUF *s, const share *sh, int gateway)
{
	int i, files = 0, folders = 0;
	for (i = 0; i < sh->n; i++) {
		if (sh->it[i].folder)
			folders++;
		else
			files++;
	}
	sb_adds(s, "<!DOCTYPE html><html lang=en-GB><head><meta charset=utf-8><title>");
	esc(s, sh->title[0] ? sh->title : sh->service);
	sb_adds(s, " - ");
	esc(s, sh->service);
	sb_adds(s, "</title>");
	sb_adds(s, share_style);
	sb_adds(s, "</head><body><div class=page><div class=kick>// Shared ");
	sb_adds(s, files + folders == 1 && files == 1 && sh->title[0] && !strcmp(sh->title, sh->it[0].name) ? "file" :
		"files");
	sb_adds(s, " &middot; ");
	esc(s, sh->service);
	sb_adds(s, "</div><h1>");
	esc(s, sh->title[0] ? sh->title : sh->service);
	sb_adds(s, "</h1><div class=from>");
	if (files || folders) {
		char n[48];
		if (files)
			snprintf(n, sizeof n, "%d file%s", files, files == 1 ? "" : "s");
		else
			n[0] = 0;
		sb_adds(s, n);
		if (folders) {
			snprintf(n, sizeof n, "%s%d folder%s", files ? " and " : "", folders, folders == 1 ? "" : "s");
			sb_adds(s, n);
		}
		sb_adds(s, " &middot; ");
	}
	esc(s, sh->host);
	sb_adds(s, "</div><div class=ruler></div>");
	if (sh->n == 0) {
		sb_adds(s, "<p>");
		sb_adds(s, sh->empty != NULL ? sh->empty : "Nothing to show: the folder is empty, or isn't shared with everyone.");
		sb_adds(s, "</p>");
	} else {
		sb_adds(s, "<table>");
		for (i = 0; i < sh->n; i++) {
			const share_item *it = &sh->it[i];
			sb_adds(s, "<tr><td class=t>");
			esc(s, it->folder ? "DIR" : it->type);
			sb_adds(s, "</td><td><a href=\"");
			esc(s, it->url);
			sb_adds(s, "\">");
			esc(s, it->name);
			sb_adds(s, "</a></td><td class=s>");
			esc(s, it->size);
			sb_adds(s, "</td><td class=g><a class=btn href=\"");
			esc(s, it->url);
			sb_adds(s, it->folder ? "\">Open</a></td></tr>" : "\">Download</a></td></tr>");
		}
		sb_adds(s, "</table>");
	}
	sb_adds(s, "<div class=all><span>");
	sb_adds(s, files > 1 || folders ? "Each file asks where to save it." : files ? "It asks where to save it." : "");
	sb_adds(s, "</span>");
	if (sh->all[0]) {
		sb_adds(s, "<a class=btn href=\"");
		esc(s, sh->all);
		sb_adds(s, "\">Download all</a>");
	}
	sb_adds(s, "</div><div class=note>");
	esc(s, sh->service);
	sb_adds(s, sh->n ? "'s own page needs scripts far bigger than EWeb runs; EWeb shows the files itself." :
		"'s own page needs scripts far bigger than EWeb runs.");
	if (sh->note != NULL) {
		sb_adds(s, " ");
		sb_adds(s, sh->note);
	}
	if (gateway)
		sb_adds(s, " The page itself: View &gt; Through the Gateway's browser.");
	sb_adds(s, "</div><div class=addr>");
	esc(s, sh->page);
	sb_adds(s, "</div></div></body></html>");
}

#endif
