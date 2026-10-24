/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* visited.c - a page's links marked visited (visited.h). The page's
   address is parsed once, and each link into the same URL, rather than
   two of them allocated a link as following one does: a page has
   hundreds */
#include <stdlib.h>
#include <string.h>

#include "visited.h"
#include "history.h"

typedef struct {
	URL base, u;
	int based;			/* base parsed (http, https) */
	int base_in;			/* and a page from its origin in the history */
	int file;			/* base is a file: address */
	const char *base_s;
	const URL_ENC *enc;
	VIS_FILE file_resolve;
} MARK;

static int is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* a scheme before ':' (as browse's: not a drive letter); its length */
static int scheme_len(const char *s)
{
	int i = 0;
	if (!is_alpha(s[0]))
		return 0;
	while (is_alpha(s[i]) || (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' || s[i] == '.')
		i++;
	return s[i] == ':' && i > 1 ? i : 0;
}

static int is_scheme(const char *s, int n, const char *scheme)
{
	int i;
	if (n != (int) strlen(scheme))
		return 0;
	for (i = 0; i < n; i++)
		if ((s[i] | 0x20) != scheme[i])
			return 0;
	return 1;
}

static int visited_href(void *ud, const char *href)
{
	MARK *m = ud;
	char o[300];
	int n;
	while (*href == ' ' || *href == '\t' || *href == '\n' || *href == '\r')
		href++;
	n = scheme_len(href);
	if ((m->file && n == 0) || (n > 0 && is_scheme(href, n, "file"))) {
		char abs[URL_MAX];
		if (m->file_resolve == NULL)
			return 0;
		m->file_resolve(m->base_s, href, abs, sizeof abs);
		return hg_visited(abs);
	}
	if (n > 0 && !is_scheme(href, n, "http") && !is_scheme(href, n, "https"))
		return hg_visited(href);	/* (as it stands, as following it takes it) */
	/* no page from its origin shown: not visited, without joining it
	   (a first visit to a site, a link to another) */
	n = url_origin_quick(m->based ? &m->base : NULL, href, o, sizeof o);
	if (n < 0 ? !m->base_in : n > 0 && !hg_origin(o, n))
		return 0;
	if (url_join_quick(m->based ? &m->base : NULL, href, m->u.href, sizeof m->u.href))
		return hg_visited(m->u.href);	/* most links: without a parse */
	return url_parse_enc(&m->u, href, m->based ? &m->base : NULL, m->enc) && hg_visited(m->u.href);
}

long vis_mark(doc *d, const char *base, const URL_ENC *enc, VIS_FILE file, int dry)
{
	MARK *m;
	long k;
	int n = scheme_len(base);
	if (hg_count() == 0)
		return doc_mark_links(d, NULL, NULL, dry);
	m = malloc(sizeof *m);
	if (m == NULL)
		return -1;
	m->file = n > 0 && is_scheme(base, n, "file");
	m->based = !m->file && url_parse(&m->base, base, NULL);
	m->base_in = m->based && hg_origin(m->base.href, m->base.path);
	m->base_s = base;
	m->enc = enc;
	m->file_resolve = file;
	k = doc_mark_links(d, visited_href, m, dry);
	free(m);
	return k;
}
