/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* errno for the C library calls that set it (strtol, strtoul). */
#ifndef EWEB_ERRNO_H
#define EWEB_ERRNO_H

extern int errno;
#define ERANGE 34
#define EINVAL 22
#define ENOMEM 12

#endif
