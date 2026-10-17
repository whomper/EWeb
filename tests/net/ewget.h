/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* ewget (ewget.c) and what each platform's main gives it. */
#ifndef EWGET_H
#define EWGET_H

int ewget(int argc, char **argv);
/* a line (or part of one) for the console and the log */
void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
