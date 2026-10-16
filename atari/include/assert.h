/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* assert() for EWeb's freestanding Atari build: off with NDEBUG (releases
 * and measurements), else a failure says where and quits. */
#ifndef EWEB_ASSERT_H
#define EWEB_ASSERT_H

#ifdef NDEBUG
#define assert(e) ((void) 0)
#else
void eweb_assert_fail(const char *expr, const char *file, int line) __attribute__((noreturn));
#define assert(e) ((e) ? (void) 0 : eweb_assert_fail(#e, __FILE__, __LINE__))
#endif

#endif
