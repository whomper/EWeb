/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* endian.h - the 68000s are big-endian (musl's libm.h asks) */
#ifndef _ENDIAN_H
#define _ENDIAN_H
#define __BIG_ENDIAN 4321
#define __LITTLE_ENDIAN 1234
#define __BYTE_ORDER __BIG_ENDIAN
#endif
