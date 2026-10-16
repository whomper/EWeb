/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The fixed-width types (GCC's stdint.h) and the print formats NetSurf's
 * libraries name, for the 68k: int is 32 bits, long is 32 bits. */
#ifndef EWEB_INTTYPES_H
#define EWEB_INTTYPES_H

#include <stdint.h>

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "ld"
#define PRIi32 "li"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "lu"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "lx"
#define PRIX32 "lX"
#define PRIxPTR "lx"

#endif
