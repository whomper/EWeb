/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * wide.c - View > Desktop width (decided 27 Nov 2026; DESIGN.md, Decisions:
 * A wide layout): the sites whose pages are laid out 1024 CSS pixels wide,
 * as on a desktop's screen, the page scrolling sideways in the window; the
 * list is EWEB.INF's WIDE=, host names with spaces between, a name taking
 * its subdomains too. A file of its own, linked last.
 */
#include <string.h>
#include "ui.h"
#include "util.h"

/* the host of an http or https address into out; 0: none */
static int host_of(const char *url, char *out, int size)
{
	const char *h;
	size_t n;
	if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		return 0;
	h = strstr(url, "://") + 3;
	n = strcspn(h, "/?#:");
	if (n == 0 || (int) n >= size)
		return 0;
	memcpy(out, h, n);
	out[n] = 0;
	return 1;
}

int wide_site(const char *url)
{
	char host[128];
	const char *s = opt.wide;
	size_t hl;
	if (!host_of(url, host, sizeof host))
		return 0;
	hl = strlen(host);
	while (*s) {
		const char *w;
		size_t n;
		while (*s == ' ')
			s++;
		w = s;
		while (*s && *s != ' ')
			s++;
		n = (size_t) (s - w);
		if (n > 0 && (n == hl ? strncmp(host, w, n) == 0 :
			      n < hl && host[hl - n - 1] == '.' && strncmp(host + hl - n, w, n) == 0))
			return 1;
	}
	return 0;
}

int wide_can(const char *url)
{
	char host[128];
	return host_of(url, host, sizeof host);
}

void wide_set(const char *url, int on)
{
	char host[128], out[sizeof opt.wide];
	const char *s = opt.wide;
	size_t n = 0, hl;
	if (!host_of(url, host, sizeof host))
		return;
	hl = strlen(host);
	out[0] = 0;
	while (*s) {
		const char *w;
		size_t k;
		while (*s == ' ')
			s++;
		w = s;
		while (*s && *s != ' ')
			s++;
		k = (size_t) (s - w);
		/* (off: the host taken out, and any parent of it that took it in) */
		if (k == 0 || (k == hl && strncmp(w, host, k) == 0) ||
		    (!on && k < hl && host[hl - k - 1] == '.' && strncmp(host + hl - k, w, k) == 0))
			continue;
		if (n + k + 2 >= sizeof out)
			break;
		if (n)
			out[n++] = ' ';
		memcpy(out + n, w, k);
		n += k;
		out[n] = 0;
	}
	if (on && n + hl + 2 < sizeof out) {
		if (n)
			out[n++] = ' ';
		strcpy(out + n, host);
	}
	str_copy(opt.wide, out, sizeof opt.wide);
}
