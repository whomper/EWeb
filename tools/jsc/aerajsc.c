/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aerajsc - a script compiled for EWeb on the Pi (webgate): QuickJS's
 * bytecode, made by EWeb's own copy of QuickJS (third_party/quickjs, its
 * patches applied), so that EWEB.PRG reads it instead of compiling it on
 * the 68030 (docs/DESIGN.md, Scripts kept compiled).
 *
 * The request on stdin (systemd's socket, one aerajsc a connection):
 *
 *   "EWJQ", 1, the source's length (4 bytes, big-endian), the source
 *
 * (length 0: which aerajsc this is, and nothing compiled). The answer on
 * stdout: a line "aerajsc TAG" (TAG: JS_GetBytecodeTag, which EWeb asks
 * for), then the script compiled as EWeb keeps it ("EWQJS TAG", a line,
 * then the bytecode, without line numbers), or a line "error WHY".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quickjs.h"

#define SOURCE_MAX	(4L << 20)	/* the most a script may be */
#define MEMORY_MAX	(256L << 20)	/* QuickJS's heap while it compiles */
#define SECONDS		5		/* the most compiling may take */

static time_t started;

static int too_long(JSRuntime *rt, void *opaque)
{
	(void) rt, (void) opaque;
	return time(NULL) - started > SECONDS;
}

static int read_all(unsigned char *p, long n)
{
	while (n > 0) {
		size_t got = fread(p, 1, (size_t) n, stdin);
		if (got == 0)
			return 0;
		p += got;
		n -= (long) got;
	}
	return 1;
}

int main(void)
{
	unsigned char head[9];
	unsigned char *src;
	long len;
	JSRuntime *rt;
	JSContext *ctx;
	JSValue f;
	uint8_t *bc;
	size_t n;
	const char *tag = JS_GetBytecodeTag();

	printf("aerajsc %s\n", tag);
	if (!read_all(head, 9) || memcmp(head, "EWJQ\1", 5) != 0) {
		printf("error not a request\n");
		return 1;
	}
	len = (long) head[5] << 24 | (long) head[6] << 16 | (long) head[7] << 8 | head[8];
	if (len == 0)
		return 0;
	if (len < 0 || len > SOURCE_MAX) {
		printf("error too long\n");
		return 1;
	}
	if ((src = malloc((size_t) len + 1)) == NULL || !read_all(src, len)) {
		printf("error short\n");
		return 1;
	}
	src[len] = 0;
	rt = JS_NewRuntime();
	if (rt == NULL || (ctx = JS_NewContext(rt)) == NULL) {
		printf("error no memory\n");
		return 1;
	}
	JS_SetMemoryLimit(rt, MEMORY_MAX);
	JS_SetStripInfo(rt, JS_STRIP_DEBUG);	/* as EWeb keeps what it compiles */
	started = time(NULL);
	JS_SetParsePoll(rt, too_long, NULL);
	f = JS_Eval(ctx, (const char *) src, (size_t) len, "script", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
	if (JS_IsException(f)) {
		JSValue e = JS_GetException(ctx);
		const char *s = JS_ToCString(ctx, e);
		printf("error %.200s\n", s != NULL ? s : "?");
		return 1;
	}
	bc = JS_WriteObject(ctx, &n, f, JS_WRITE_OBJ_BYTECODE);
	if (bc == NULL) {
		printf("error not written\n");
		return 1;
	}
	printf("EWQJS %s\n", tag);
	fwrite(bc, 1, n, stdout);
	return fflush(stdout) != 0;
}
