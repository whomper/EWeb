/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* ewget: fetches a web address with EWeb's HTTP client (src/net) and says
 * how long each part took: finding the host and connecting, the TLS
 * handshake, waiting for the answer, the body, unpacking it. EWGET.TTP on
 * the Atari (main_tos.c), ewget on this computer (main_posix.c).
 *
 *   ewget [-o FILE] [-n N] [-l LOG] [-z MINUTES] [-r] [-A] URL
 *
 * -o saves the body, -n fetches N times (the second over the kept
 * connection, or with the TLS session resumed), -l writes the protocol log,
 * -z is the time zone in minutes east of UTC (for certificate dates), -r
 * leaves out the DSP (RSA on the 68030), -A shows the answer's headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewget.h"
#include "http.h"
#include "conn.h"
#include "tls.h"
#include "plat.h"
#include "ui.h"

EWOPT opt = { .falcon = 1, .dsp = 1 };

static void usage(void)
{
	out("usage: ewget [-o FILE] [-n N] [-l LOG] [-z MINUTES] [-r] [-A] URL\n");
}

static int save(const char *path, const SBUF *b)
{
	int h = pf_open(path, PF_WRITE);
	long n;
	if (h < 0)
		return 0;
	n = pf_write(h, b->s ? b->s : "", b->len);
	pf_close(h);
	return n == b->len;
}

/* tenths of a KB a second, in whole numbers (no FPU needed) */
static unsigned long kb_s(long bytes, unsigned long ms)
{
	if (!ms)
		return 0;
	return ((unsigned long)bytes / 1024 * 10000UL + (unsigned long)bytes % 1024 * 10000UL / 1024) / ms;
}

int ewget(int argc, char **argv)
{
	const char *url = 0, *file = 0;
	int i, times = 1, show_headers = 0, rc = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc)
			file = argv[++i];
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			times = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-l") && i + 1 < argc)
			str_copy(conn_logfile, argv[++i], sizeof(conn_logfile));
		else if (!strcmp(argv[i], "-z") && i + 1 < argc)
			tls_tz_minutes = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-r"))
			opt.dsp = 0;
		else if (!strcmp(argv[i], "-A"))
			show_headers = 1;
		else if (argv[i][0] == '-' || url)
			return usage(), 2;
		else
			url = argv[i];
	}
	if (!url || times < 1)
		return usage(), 2;
	if (!net_init()) {
		out("no TCP/IP: start STinG (or MiNTnet) first\n");
		return 1;
	}
	str_copy(conn_cacert, "CACERT.PEM", sizeof(conn_cacert));
	str_copy(tls_roots_cache, "ROOTS.DAT", sizeof(tls_roots_cache));
	str_copy(tls_seed_path, "SEED.DAT", sizeof(tls_seed_path));
	str_copy(tls_sigs_path, "SIGS.DAT", sizeof(tls_sigs_path));
	tls_file_stamp = pf_stamp;
	falcon_dsp_init();
	falcon_dsp_ec_init();
	out("network: %s; RSA on the %s, elliptic curves on the %s\n", net_stack(), falcon_dsp_state(),
	    falcon_dsp_ec_state());
	for (i = 0; i < times; i++) {
		HTTPREQ rq;
		HTTPRESP rs;
		SBUF body;
		char err[200];
		memset(&rq, 0, sizeof(rq));
		rq.url = url;
		sb_init(&body);
		out("\nfetch %d: %s\n", i + 1, url);
		if (!http_request(&rq, &rs, &body, err, sizeof(err))) {
			out("  failed: %s (after %lu ms)\n", err, rs.t.total_ms);
			rc = 1;
		} else {
			out("  %d %s, %s%s%s\n", rs.status, rs.reason, rs.mime[0] ? rs.mime : "no type",
			    rs.charset[0] ? ", charset " : "", rs.charset);
			if (rs.redirects)
				out("  after %d redirect%s: %s\n", rs.redirects, rs.redirects > 1 ? "s" : "", rs.url.href);
			/* a redirect may have connected before a kept connection */
			if (rs.reused && !rs.t.connect_ms)
				out("  connect      0 ms   (the connection was kept open)\n");
			else
				out("  connect %6lu ms   (finding the host and connecting)\n", rs.t.connect_ms);
			if (rs.security[0] && rs.t.tls_tries > 1)
				out("  TLS     %6lu ms   %s (%d tries)\n", rs.t.tls_ms, rs.security, rs.t.tls_tries);
			else if (rs.security[0])
				out("  TLS     %6lu ms   %s\n", rs.t.tls_ms, rs.security);
			out("  wait    %6lu ms   (from the request to the answer's first line)\n", rs.t.wait_ms);
			if (rs.t.body_ms >= 10)
				out("  body    %6lu ms   %ld bytes, %lu.%lu KB/s\n", rs.t.body_ms, rs.length,
				    kb_s(rs.length, rs.t.body_ms) / 10, kb_s(rs.length, rs.t.body_ms) % 10);
			else
				out("  body    %6lu ms   %ld bytes\n", rs.t.body_ms, rs.length);
			if (rs.encoding == 1 || rs.encoding == 2)
				out("  unpack  %6lu ms   %s, %ld to %ld bytes\n", rs.t.unpack_ms,
				    rs.encoding == 1 ? "gzip" : "deflate", rs.length, rs.size);
			else if (rs.encoding == 3)
				out("  packed in a way EWeb doesn't unpack\n");
			out("  total   %6lu ms\n", rs.t.total_ms);
			if (show_headers && rs.headers.s)
				out("%s", rs.headers.s);
			if (file && i == times - 1) {
				if (save(file, &body))
					out("saved in %s (%ld bytes)\n", file, body.len);
				else {
					out("couldn't write %s\n", file);
					rc = 1;
				}
			}
		}
		http_resp_free(&rs);
		sb_free(&body);
	}
	http_close_all();
	conn_log_flush();
	return rc;
}
