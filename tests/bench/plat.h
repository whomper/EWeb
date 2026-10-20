/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* What nsbench needs from the machine: plat_posix.c on Linux (this
 * computer, or the 68030 under QEMU), plat_tos.c on the Atari. */
#ifndef NSBENCH_PLAT_H
#define NSBENCH_PLAT_H

#include <stddef.h>

#ifdef NSBENCH_TOS
#define TICKS_PER_SEC 200UL	/* the system timer, _hz_200 */
#else
#define TICKS_PER_SEC 1000000UL
#endif

void plat_init(void);
/* nsbench.c: the program, called by each platform's main() */
int nsbench(int argc, char **argv);
/* picbench.c: a picture decoded, each step timed (PICBENCH.TTP) */
int picbench(int argc, char **argv);
/* the whole file, with a 0 after it, or NULL */
char *load(const char *path, size_t *len);
void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
unsigned long ticks(void);

#endif
