/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * A web server's answers, from the fuzz input (the AERA guidelines, 7.1):
 * status lines, headers, lengths, chunks, gzip and deflate, redirects and
 * kept connections, read by EWeb's HTTP client (src/net/http.c). The first
 * byte picks the request: the method, a small limit, a sink, no redirects,
 * and whether a second request follows on the kept connection.
 */
#include <stdint.h>
#include <string.h>
#include "http.h"
#include "fuzz_net.h"

static int sink(void *ud, const char *d, long n)
{
	volatile long sum = 0;
	long i;
	(void)ud;
	for (i = 0; i < n; i++)
		sum += d[i];
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char *methods[] = { "GET", "HEAD", "POST", "GET" };
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF body;
	char err[200], h[100];
	int sel, round;
	if (n < 1)
		return 0;
	sel = d[0];
	fuzz_net_set(d + 1, (long)n - 1);
	http_close_all();
	for (round = 0; round < ((sel & 0x80) ? 2 : 1); round++) {
		memset(&rq, 0, sizeof(rq));
		rq.url = (sel & 0x40) ? "http://x.test/a/b?c" : "http://[::1]:8080/";
		rq.method = methods[sel & 3];
		if (!strcmp(rq.method, "POST")) {
			rq.body = "a=1&b=2";
			rq.bodylen = 7;
		}
		rq.maxbody = (sel & 4) ? 4096 : 0;
		rq.sink = (sel & 8) ? sink : 0;
		rq.no_redirect = (sel & 16) != 0;
		sb_init(&body);
		if (http_request(&rq, &rs, &body, err, sizeof(err))) {
			http_header(&rs, "Content-Type", h, sizeof(h));
			http_header(&rs, (sel & 32) ? "Location" : "X-Nothing", h, sizeof(h));
		}
		http_resp_free(&rs);
		sb_free(&body);
	}
	http_close_all();
	return 0;
}
